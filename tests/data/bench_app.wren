import "net" for Server, Req, Res

Server.serve { |req, res| res.send(200, "ok\n") }
