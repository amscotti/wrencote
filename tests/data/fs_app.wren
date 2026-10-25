import "fs" for Fs
import "sys" for Sys

if (Fs.read("hello.txt") != "hello\n") Fiber.abort("fs read")
Fs.write("out.txt", "world")
if (!Fs.exists("out.txt")) Fiber.abort("fs exists")
var names = Fs.list(".")
if (!names.contains("hello.txt")) Fiber.abort("fs list")
var escape = Fiber.new { Fs.read("../etc/passwd") }.try()
if (escape == null) Fiber.abort("fs dotdot escape")
var absolute = Fiber.new { Fs.read("/etc/passwd") }.try()
if (absolute == null) Fiber.abort("fs absolute escape")
var symlink = Fiber.new { Fs.exists("link") }.try()
if (symlink == null) Fiber.abort("fs symlink escape")
if (Sys.env("PATH") == null) Fiber.abort("sys env")
if (Sys.clock < 0) Fiber.abort("sys clock")
if (Sys.args.count != 0) Fiber.abort("sys args")
if (Sys.env("WRENCOTE_MISSING_ENV") != null) Fiber.abort("sys missing env")
var longname = ""
for (i in 0...256) longname = longname + "a"
var env_error = Fiber.new { Sys.env(longname) }.try()
if (env_error == null) Fiber.abort("sys long env")
