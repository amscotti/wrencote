import "json" for Json

var value = {
  "name": "Ada",
  "numbers": [1, 2, 3],
  "ok": true,
  "none": null,
  "text": "line\n\"quote\" \u00e9"
}
var encoded = Json.encode(value)
var decoded = Json.decode(encoded)
if (decoded["name"] != "Ada") Fiber.abort("json name")
if (decoded["numbers"][2] != 3) Fiber.abort("json number")
if (decoded["ok"] != true) Fiber.abort("json bool")
if (decoded["none"] != null) Fiber.abort("json null")
if (decoded["text"] != "line\n\"quote\" \u00e9") Fiber.abort("json text")
if (Json.decode("[1,2,3]").count != 3) Fiber.abort("json array")
var error = Fiber.new { Json.decode("{") }.try()
if (error == null) Fiber.abort("json malformed should abort")
var escape = Fiber.new { Json.encode(Num.infinity) }.try()
if (escape == null) Fiber.abort("json infinity should abort")
