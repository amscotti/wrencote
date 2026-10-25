import "sys" for Sys

if (Sys.args.count != 2) Fiber.abort("args count")
if (Sys.args[0] != "one" || Sys.args[1] != "two") Fiber.abort("args values")
