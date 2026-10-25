class Json {
  static encode(value) {
    return encodeValue(value, [])
  }

  static encodeValue(value, stack) {
    if (value == null) return "null"
    if (value is Bool) return value ? "true" : "false"
    if (value is Num) {
      if (value.isNan || value == Num.infinity || value == -Num.infinity) {
        Fiber.abort("Json.encode: non-finite number")
      }
      return value.toString
    }
    if (value is String) return quote(value)
    if (value is List) {
      if (stack.contains(value)) Fiber.abort("Json.encode: cycle")
      stack.add(value)
      var parts = []
      for (item in value) parts.add(encodeValue(item, stack))
      stack.removeAt(stack.count - 1)
      return "[" + parts.join(",") + "]"
    }
    if (value is Map) {
      if (stack.contains(value)) Fiber.abort("Json.encode: cycle")
      stack.add(value)
      var parts = []
      for (key in value.keys) {
        if (!(key is String)) Fiber.abort("Json.encode: Map keys must be String")
        parts.add(quote(key) + ":" + encodeValue(value[key], stack))
      }
      stack.removeAt(stack.count - 1)
      return "{" + parts.join(",") + "}"
    }
    Fiber.abort("Json.encode: unsupported type")
  }

  static quote(text) {
    var parts = ["\""]
    for (b in text.bytes) {
      if (b == 34) {
        parts.add("\\\"")
      } else if (b == 92) {
        parts.add("\\\\")
      } else if (b == 8) {
        parts.add("\\b")
      } else if (b == 12) {
        parts.add("\\f")
      } else if (b == 10) {
        parts.add("\\n")
      } else if (b == 13) {
        parts.add("\\r")
      } else if (b == 9) {
        parts.add("\\t")
      } else if (b < 32) {
        parts.add("\\u00" + hex(b))
      } else {
        parts.add(String.fromByte(b))
      }
    }
    parts.add("\"")
    return parts.join()
  }

  static hex(value) {
    var digits = "0123456789abcdef"
    var high = digits.bytes[value >> 4]
    var low = digits.bytes[value & 15]
    return String.fromByte(high) + String.fromByte(low)
  }

  static decode(text) {
    return JsonParser.new(text).parse()
  }
}

class JsonParser {
  construct new(text) {
    _text = text
    _bytes = text.bytes
    _pos = 0
    _length = _bytes.count
  }

  parse() {
    if (_length > 1048576) Fiber.abort("Json.decode: input exceeds 1 MiB")
    skipWhitespace()
    var value = parseValue(0)
    skipWhitespace()
    if (_pos != _length) fail("trailing data")
    return value
  }

  parseValue(depth) {
    if (depth > 64) fail("depth exceeds 64")
    if (_pos >= _length) fail("unexpected end")
    var b = _bytes[_pos]
    if (b == 123) return parseObject(depth)
    if (b == 91) return parseArray(depth)
    if (b == 34) return parseString()
    if (b == 116) return parseLiteral("true", true)
    if (b == 102) return parseLiteral("false", false)
    if (b == 110) return parseLiteral("null", null)
    if (b == 45 || (b >= 48 && b <= 57)) return parseNumber()
    fail("unexpected byte")
  }

  skipWhitespace() {
    while (_pos < _length) {
      var b = _bytes[_pos]
      if (b == 32 || b == 9 || b == 10 || b == 13) {
        _pos = _pos + 1
      } else {
        return
      }
    }
  }

  parseLiteral(word, value) {
    if (_pos + word.bytes.count > _length) fail("truncated literal")
    for (i in 0...word.bytes.count) {
      if (_bytes[_pos + i] != word.bytes[i]) fail("bad literal")
    }
    _pos = _pos + word.bytes.count
    return value
  }

  parseString() {
    _pos = _pos + 1
    var parts = []
    while (true) {
      if (_pos >= _length) fail("unterminated string")
      var b = _bytes[_pos]
      if (b == 34) {
        _pos = _pos + 1
        return parts.join()
      }
      if (b == 92) {
        _pos = _pos + 1
        if (_pos >= _length) fail("unterminated escape")
        var e = _bytes[_pos]
        _pos = _pos + 1
        if (e == 34) {
          parts.add("\"")
        } else if (e == 92) {
          parts.add("\\")
        } else if (e == 47) {
          parts.add("/")
        } else if (e == 98) {
          parts.add(String.fromByte(8))
        } else if (e == 102) {
          parts.add(String.fromByte(12))
        } else if (e == 110) {
          parts.add(String.fromByte(10))
        } else if (e == 114) {
          parts.add(String.fromByte(13))
        } else if (e == 116) {
          parts.add(String.fromByte(9))
        } else if (e == 117) {
          var code = parseHex4()
          if (code >= 55296 && code <= 56319) {
            if (_pos + 6 > _length || _bytes[_pos] != 92 || _bytes[_pos + 1] != 117) {
              fail("unpaired surrogate")
            }
            _pos = _pos + 2
            var low = parseHex4()
            if (low < 56320 || low > 57343) fail("bad surrogate pair")
            code = 65536 + ((code - 55296) << 10) + (low - 56320)
          } else if (code >= 56320 && code <= 57343) {
            fail("unpaired surrogate")
          }
          parts.add(String.fromCodePoint(code))
        } else {
          fail("bad escape")
        }
      } else {
        parts.add(String.fromByte(b))
        _pos = _pos + 1
      }
    }
  }

  parseHex4() {
    if (_pos + 4 > _length) fail("truncated unicode escape")
    var value = 0
    for (i in 0...4) {
      var b = _bytes[_pos + i]
      var digit = -1
      if (b >= 48 && b <= 57) {
        digit = b - 48
      } else if (b >= 97 && b <= 102) {
        digit = b - 87
      } else if (b >= 65 && b <= 70) {
        digit = b - 55
      } else {
        fail("bad unicode escape")
      }
      value = (value << 4) + digit
    }
    _pos = _pos + 4
    return value
  }

  parseNumber() {
    var start = _pos
    var integer = true
    if (_bytes[_pos] == 45) _pos = _pos + 1
    if (_pos >= _length) fail("truncated number")
    if (_bytes[_pos] == 48) {
      _pos = _pos + 1
    } else if (_bytes[_pos] >= 49 && _bytes[_pos] <= 57) {
      while (_pos < _length && _bytes[_pos] >= 48 && _bytes[_pos] <= 57) _pos = _pos + 1
    } else {
      fail("bad number")
    }
    if (_pos < _length && _bytes[_pos] == 46) {
      integer = false
      _pos = _pos + 1
      if (_pos >= _length || _bytes[_pos] < 48 || _bytes[_pos] > 57) fail("bad fraction")
      while (_pos < _length && _bytes[_pos] >= 48 && _bytes[_pos] <= 57) _pos = _pos + 1
    }
    if (_pos < _length && (_bytes[_pos] == 101 || _bytes[_pos] == 69)) {
      integer = false
      _pos = _pos + 1
      if (_pos < _length && (_bytes[_pos] == 43 || _bytes[_pos] == 45)) _pos = _pos + 1
      if (_pos >= _length || _bytes[_pos] < 48 || _bytes[_pos] > 57) fail("bad exponent")
      while (_pos < _length && _bytes[_pos] >= 48 && _bytes[_pos] <= 57) _pos = _pos + 1
    }
    var value = Num.fromString(_text[start..._pos])
    if (integer && value.abs > Num.maxSafeInteger) fail("integer outside safe range")
    return value
  }

  parseArray(depth) {
    _pos = _pos + 1
    var result = []
    skipWhitespace()
    if (_pos < _length && _bytes[_pos] == 93) {
      _pos = _pos + 1
      return result
    }
    while (true) {
      result.add(parseValue(depth + 1))
      skipWhitespace()
      if (_pos >= _length) fail("unterminated array")
      if (_bytes[_pos] == 44) {
        _pos = _pos + 1
        skipWhitespace()
      } else if (_bytes[_pos] == 93) {
        _pos = _pos + 1
        return result
      } else {
        fail("expected comma or ]")
      }
    }
  }

  parseObject(depth) {
    _pos = _pos + 1
    var result = {}
    skipWhitespace()
    if (_pos < _length && _bytes[_pos] == 125) {
      _pos = _pos + 1
      return result
    }
    while (true) {
      skipWhitespace()
      if (_pos >= _length || _bytes[_pos] != 34) fail("expected string key")
      var key = parseString()
      skipWhitespace()
      if (_pos >= _length || _bytes[_pos] != 58) fail("expected colon")
      _pos = _pos + 1
      result[key] = parseValue(depth + 1)
      skipWhitespace()
      if (_pos >= _length) fail("unterminated object")
      if (_bytes[_pos] == 44) {
        _pos = _pos + 1
      } else if (_bytes[_pos] == 125) {
        _pos = _pos + 1
        return result
      } else {
        fail("expected comma or }")
      }
    }
  }

  fail(message) {
    Fiber.abort("Json.decode at byte %(_pos): " + message)
  }
}
