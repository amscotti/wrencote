import "sys" for Sys

// Sys.sleep is bounded to 10 s: it parks the single-threaded loop, so the cap
// keeps one handler from stalling every connection for a minute (GAPS G18).
var too_long = Fiber.new { Sys.sleep(20000) }.try()
if (too_long == null) Fiber.abort("sleep over the cap must be rejected")
if (too_long.toString.contains("0..10000") == false) {
  Fiber.abort("sleep cap message: " + too_long.toString)
}
// A sleep inside the cap must run to completion (it aborts on its own if the
// syscall path is broken).
Sys.sleep(1)
