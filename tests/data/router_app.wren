import "router" for Router

class FakeReq {
  construct new(method, path) {
    _method = method
    _path = path
  }
  method { _method }
  path { _path }
}

class FakeRes {
  construct new() { _body = "" }
  body { _body }
  send(body) { _body = body }
}

var router = Router.new()
router.add("GET", "/users/:id") { |req, res, params| res.send("user " + params["id"]) }
router.add("GET", "/exact") { |req, res, params| res.send("exact") }

var res = FakeRes.new()
if (!router.dispatch(FakeReq.new("GET", "/users/42"), res)) Fiber.abort("router dispatch")
if (res.body != "user 42") Fiber.abort("router param")
if (!router.matches("GET", "/exact/")) Fiber.abort("router trailing slash")
if (router.dispatch(FakeReq.new("POST", "/exact"), FakeRes.new())) Fiber.abort("router method")
var duplicate = Fiber.new { router.add("GET", "/exact") { |req, res, params| } }.try()
if (duplicate == null) Fiber.abort("router duplicate")
var bad_arity = Fiber.new { router.add("GET", "/bad") { |req, res| } }.try()
if (bad_arity == null) Fiber.abort("router arity")
