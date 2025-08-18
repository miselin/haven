This directory contains examples using the newer syntax I'm experimenting with
as of the second half of 2025.

The main changes are:

- Somewhat less ambiguous syntax
- Simplified syntax for initializers (inferred from context)
- Improved function return type annotation

I'm spinning up a Lark grammar first to try and land on a syntax I'm happy with, and then I'll
update the C version of the compiler with the syntax modifications.
