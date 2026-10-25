import "net" for Server, Req, Res

Server.serve { |req, res|
  if (req.path == "/stop") {
    res.send(200, "bye")
    Server.stop()
  } else {
    res.send(200, "cli\n")
  }
}
