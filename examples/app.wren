import "net" for Server, Req, Res
import "router" for Router
import "json" for Json

var router = Router.new()
router.add("GET", "/") { |req, res, params| res.send(200, "hello\n") }
router.add("GET", "/health") { |req, res, params| res.send(200, "ok\n") }
router.add("GET", "/json") { |req, res, params|
  res.header("Content-Type", "application/json")
  res.send(200, Json.encode({"ok": true}))
}

Server.serve { |req, res|
  if (!router.dispatch(req, res)) res.send(404, "not found\n")
}
