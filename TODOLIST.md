# High-Level TODOs

- [ ] UTF8 tokens, not chars
- [x] sret setup for returning structs/enums from functions
- [ ] probably need a plan for passing structs/enums TO functions too
- [ ] semantic analysis pass to identify semantic errors (e.g. match arm issues)
- [ ] semantic analysis to detect enum values >4G
- [ ] semantic analysis to detect enum that should have a value but doesn't
- [ ] figure out a better way to handle attribute valeus like "argmem: readwrite" which are now just a hardcoded constant
- [ ] ranges stop before the terminal value (e.g. 10..0..-1's last iteration is 1)
