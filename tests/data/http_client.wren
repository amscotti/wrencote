import "http" for Http
import "sys" for Sys

var full = Http.request("GET", "http://127.0.0.1:18311/full", "", null)
if (full != "hello") Fiber.abort("full body: " + full)
var lines = Http.streamLines("GET", "http://127.0.0.1:18311/stream", "", null)
if (lines.count != 3) Fiber.abort("line count: " + lines.count.toString)
if (lines[0] != "event: progress") Fiber.abort("line 0: " + lines[0])
if (lines[1] != "data: 42") Fiber.abort("line 1: " + lines[1])
if (lines[2] != "") Fiber.abort("line 2: " + lines[2])
System.print("http client: full and stream ok")
Sys.exit()
