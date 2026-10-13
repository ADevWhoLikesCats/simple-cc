# cfe — a C frontend for mini-asm

A C compiler frontend written in C++20 that targets the
[mini-asm](https://github.com/ADevWhoLikesCats/mini-asm) compiler backend.

cfe handles lexing, parsing, semantic analysis, and IR construction.
Everything from IR onward — optimization, register allocation, code
emission, assembling, linking — is done by mini-asm.

## Supported C subset

- `int` functions and parameters
- Local variables (`int x = ...;` and `int x;`)
- Assignment and re-assignment (with SSA + phi insertion for loops)
- Arithmetic: `+ - * / %`
- Comparisons: `== != < <= > >=`
- `if` / `else`
- `while` loops, including nested loops
- Function calls (up to 16 arguments, per mini-asm's ABI support)
- `return`

Not yet supported:

- `for` loops
- Pointers, arrays, structs
- `char`, `long`, `short`, `float`, `double`
- Function declarations (only definitions)
- Globals
- Merge-block phi insertion for `if`/`else` (see "Status" below)

## Getting mini-asm

cfe links against a prebuilt `libmini-asm.a`. Build it from the
mini-asm checkout first:

    git clone https://github.com/ADevWhoLikesCats/mini-asm
    cd mini-asm
    make lib
    # produces libmini-asm.a in the repo root

Then copy the archive into this repo's `lib/` directory:

    mkdir -p lib
    cp /path/to/mini-asm/libmini-asm.a lib/

That's a one-time step. `lib/` is where the Makefile looks for the
archive. The archive is not committed — build it from mini-asm
yourself, so you always get the version that matches your headers.

## Building cfe

    make

If your mini-asm checkout isn't at `../cc-backend`, point the
Makefile at it:

    make MINI_ASM_DIR=$HOME/src/mini-asm

The Makefile needs mini-asm's **headers** (for `-I`) and its
**archive** (for linking). The headers come from `MINI_ASM_DIR`,
the archive from `lib/`.

## Usage

    cfe [options] <file.c>

Options:

- `-S`               — emit assembly to `/tmp/cfe_out.s`, stop before assembling
- `-dump-ast`        — print the parsed AST to stderr
- `-t <target>`      — select backend target (default `x86_64`)

Examples:

    cfe -dump-ast test.c        # see the AST
    cfe -S test.c               # see the generated assembly
    cfe test.c                  # compile, link, run; prints exit code

The exit code of the compiled program is printed to stderr and
returned as cfe's own exit status.

## Example

    $ cat hello.c
    int main() {
        int i = 0;
        int sum = 0;
        while (i < 10) {
            sum = sum + i;
            i = i + 1;
        }
        return sum;
    }

    $ ./cfe hello.c
    wrote /tmp/cfe_out.s for target x86_64
    program exited with 45

## Project layout

    include/
      token.h       token kinds
      lexer.h       lexer interface
      ast.h         AST node definitions
      parser.h      parser interface
      codegen.h     codegen interface
    src/
      lexer.cpp
      parser.cpp
      ast_dump.cpp  AST pretty-printer (for -dump-ast)
      codegen.cpp   AST -> mini-asm IR
      main.cpp      CLI driver

## Design notes

**Lexer.** Hand-written, one token of lookahead. Tokens store
`std::string_view` into the source buffer, so no per-token
allocation. Keywords live in a static map.

**AST.** A `std::variant` of plain structs rather than a class
hierarchy with virtual methods. Children are `std::unique_ptr`.
Visitors use the standard `overloaded` helper. Compact, no
vtable, and `std::visit` gives exhaustiveness warnings when a
case is forgotten.

**Codegen.** Direct SSA construction. Each local variable maps
to an SSA value tracked in a scope stack; assignment rebinds,
reads look up. There is no alloca/load/store detour and no
separate mem2reg pass — codegen emits SSA directly.

For `while` loops, loop-carried variables (assigned in the body
and declared before the loop) get phi nodes at the loop head.
mini-asm's `ir_value_placeholder` + `ir_value_define` API handles
the forward reference from the head block to the body's
end-of-loop value.

For `if`/`else`, merge-block phi insertion is not yet
implemented. Assigning to a variable inside one branch and
reading it after the `if` will see the pre-`if` value
(incorrectly), because no phi is emitted at `if.end`.

## Status

Verified working:

| test                                         | exit code |
|----------------------------------------------|-----------|
| `int main() { return 42; }`                  | 42        |
| `int add(int a, int b) { return a + b; }`    | —         |
| `int main() { return add(1, 2) + 10; }`      | 13        |
| if/else with comparison                      | 20        |
| while loop summing 0..9                      | 45        |
| nested while loops                           | 9         |

Known incorrect: assignments inside an `if` branch that are read
after the `if`. Will be fixed when merge-block phi insertion lands.

## Requirements

- C++20 compiler (g++ 10+ or clang 12+)
- mini-asm built as `libmini-asm.a` (see above)
- GNU `as` and `ld` on the target architecture

## License

Apache 2.0, matching mini-asm.
