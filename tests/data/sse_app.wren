import "http" for Http
import "json" for Json
import "net" for Server, Req, Res
import "sys" for Sys

Server.serve { |req, res|
  if (req.path == "/stop") {
    Server.stop()
    return
  }
  if (req.path == "/details") {
    res.header("Content-Type", "text/html; charset=utf-8")
    res.send(200, "<span class=\"live\">SSE is connected</span>")
    return
  }
  if (req.path == "/bad-http-scheme") {
    Http.request("GET", "file:///etc/hostname", "", null)
    return
  }
  if (req.path == "/bad-sse-start") {
    res.sseStart("")
    return
  }
  if (req.path == "/bad-sse-header") {
    res.sseStart("text/event-stream\r\nX-Injected: yes")
    return
  }
  if (req.path == "/bad-sse-send") {
    var event = "e"
    for (i in 1..300) event = event + "x"
    res.sseStart("text/event-stream")
    res.sseSend(event, "data")
    return
  }
  if (req.path == "/bad-sse-close") {
    res.sseClose()
    return
  }
  if (req.path == "/bad-sleep") {
    Sys.sleep(-1)
    return
  }
  if (req.path == "/events") {
    res.sseStart("text/event-stream")
    for (i in 1..20) {
      var percent = i * 5
      res.sseSend("progress", Json.encode({"percent": percent, "step": i}))
      Sys.sleep(120)
    }
    res.sseSend("reset", "{\"message\":\"complete\"}")
    res.sseClose()
    return
  }
  res.send(404, "not found")
}
