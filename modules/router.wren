class Router {
  construct new() {
    _routes = []
  }

  add(method, pattern, handler) {
    if (handler.arity != 3) {
      Fiber.abort("Router.add: handler must take |req, res, params|")
    }
    var normalized = normalizePath(pattern)
    for (route in _routes) {
      if (route["method"] == method && route["pattern"] == normalized) {
        Fiber.abort("Router.add: duplicate route %(method) %(normalized)")
      }
    }
    _routes.add({"method": method, "pattern": normalized, "handler": handler})
    return this
  }

  dispatch(req, res) {
    var route = findRoute(req.method, req.path)
    if (route == null) return false
    route["handler"].call(req, res, paramsFor(route["pattern"], req.path))
    return true
  }

  matches(method, path) {
    return findRoute(method, path) != null
  }

  findRoute(method, path) {
    var normalized = normalizePath(path)
    for (route in _routes) {
      if (route["method"] != method) continue
      if (matchPattern(route["pattern"], normalized)) return route
    }
    return null
  }

  normalizePath(path) {
    if (path.count > 1 && path.endsWith("/")) return path[0...path.count - 1]
    return path
  }

  matchPattern(pattern, path) {
    var left = pattern.split("/")
    var right = path.split("/")
    if (left.count != right.count) return false
    for (i in 0...left.count) {
      var part = left[i]
      if (part.count > 0 && part[0] == ":") {
        if (right[i].count == 0) return false
      } else if (part != right[i]) {
        return false
      }
    }
    return true
  }

  paramsFor(pattern, path) {
    var left = pattern.split("/")
    var right = path.split("/")
    var params = {}
    for (i in 0...left.count) {
      var part = left[i]
      if (part.count > 0 && part[0] == ":") {
        params[part[1...part.count]] = right[i]
      }
    }
    return params
  }
}
