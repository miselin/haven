# High-Level TODOs

- [ ] UTF8 tokens, not chars
- [x] sret setup for returning structs/enums from functions
- [x] probably need a plan for passing structs/enums TO functions too
- [x] semantic analysis pass to identify semantic errors (e.g. match arm issues)
- [x] semantic analysis to detect enum values >4G
- [x] semantic analysis to detect enum that should have a value but doesn't
- [ ] figure out a better way to handle attribute valeus like "argmem: readwrite" which are now just a hardcoded constant
- [x] ranges stop before the terminal value (e.g. 10..0..-1's last iteration is 1)
- [ ] finish on generic enums (for option types and such), some initial code is in there
