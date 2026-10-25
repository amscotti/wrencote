import "net" for Server, Req, Res

Server.serve { |req, res|
  if (req.path == "/health") {
    res.header("X-Test", "yes")
    res.send(200, "ok\n")
  } else if (req.path == "/echo") {
    res.send(200, req.body)
  } else if (req.path == "/id") {
    res.send(200, req.query["n"])
  } else if (req.path == "/json") {
    res.header("Content-Type", "application/json")
    res.send(200, "{}")
  } else if (req.path == "/headers") {
    res.send(200, req.headers["x-probe"])
  } else if (req.path == "/inspect") {
    res.send(200, req.method + "|" + req.httpVersion + "|" + res.sent.toString + "|" +
                  req.query["a"] + "|" + req.query["b"] + "|" + req.headers["x-probe"])
  } else if (req.path == "/created") {
    res.send(201, "made")
  } else if (req.path == "/large") {
    var body = "x"
    for (i in 0...20) body = body + body
    res.send(200, body)
  } else if (req.path == "/huge") {
    var body = "x"
    for (i in 0...23) body = body + body
    res.send(200, body)
  } else if (req.path == "/no-content") {
    res.send(204, "")
  } else if (req.path == "/running") {
    res.send(200, Server.running.toString)
  } else if (req.path == "/stop") {
    res.send(200, "bye")
    Server.stop()
  } else {
    res.send(404, "not found\n")
  }
}
