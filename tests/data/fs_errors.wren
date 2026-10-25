import "fs" for Fs

var e = Fiber.new { Fs.read("") }.try()
if (e == null) Fiber.abort("empty path")

e = Fiber.new { Fs.read("missing") }.try()
if (e == null) Fiber.abort("missing read")

e = Fiber.new { Fs.list("missing") }.try()
if (e == null) Fiber.abort("missing list")

if (Fs.exists("missing")) Fiber.abort("exists missing")

e = Fiber.new { Fs.list("hello.txt") }.try()
if (e == null) Fiber.abort("list file")

e = Fiber.new { Fs.read("dir") }.try()
if (e == null) Fiber.abort("read directory")

e = Fiber.new { Fs.read("big.txt") }.try()
if (e == null) Fiber.abort("read big")

e = Fiber.new { Fs.write("dir", "x") }.try()
if (e == null) Fiber.abort("write directory")

e = Fiber.new { Fs.write("link", "x") }.try()
if (e == null) Fiber.abort("write symlink")

var long = ""
for (i in 0...4097) long = long + "a"
e = Fiber.new { Fs.read(long) }.try()
if (e == null) Fiber.abort("long path")

var nul = "a" + String.fromByte(0) + "b"
e = Fiber.new { Fs.read(nul) }.try()
if (e == null) Fiber.abort("nul path")

e = Fiber.new { Fs.write("../escape", "x") }.try()
if (e == null) Fiber.abort("write dotdot")

e = Fiber.new { Fs.list("..") }.try()
if (e == null) Fiber.abort("list dotdot")

e = Fiber.new { Fs.exists("/etc/passwd") }.try()
if (e == null) Fiber.abort("exists absolute")

e = Fiber.new { Fs.read("dir/../../etc/passwd") }.try()
if (e == null) Fiber.abort("nested dotdot")

e = Fiber.new { Fs.read("link") }.try()
if (e == null) Fiber.abort("read symlink")

e = Fiber.new { Fs.list("link") }.try()
if (e == null) Fiber.abort("list symlink")

var big = "x"
while (big.count < 1048576) big = big + big
big = big[0...1048576]
Fs.write("exact.txt", big)
if (Fs.read("exact.txt").count != 1048576) Fiber.abort("1 MiB roundtrip")
var too = big + "x"
e = Fiber.new { Fs.write("too.txt", too) }.try()
if (e == null) Fiber.abort("1 MiB + 1 write")
