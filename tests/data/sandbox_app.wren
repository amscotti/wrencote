import "net" for Server, Req, Res
import "fs" for Fs

Server.serve { |req, res|
  if (req.path == "/health") {
    res.send(200, "ok\n")
  } else if (req.path == "/file") {
    res.send(200, Fs.read("ok.txt"))
  } else if (req.path == "/escape") {
    var error = Fiber.new { Fs.read("../../etc/passwd") }.try()
    res.send(200, error == null ? "ESCAPED" : "denied")
  } else {
    res.send(404, "not found\n")
  }
}
