var total = 0
var i = 0
while (i < 10) {
  total = total + i
  i = i + 1
}
if (total != 45) Fiber.abort("bad sum")
