# Quorum LLVM backend (proof of concept)

`quorum -llvm Main.quorum Counter.quorum` writes `Run/<name>.ll`, textual LLVM IR
(opaque pointers, LLVM 15 or newer). Turn it into a native executable with:

    Library/Standard/Plugins/llvm/build_native.sh Run/Default.ll myprogram
    ./myprogram

## What compiles

- `integer` (i32, wrapping like Java), `number` (double), `boolean` (i1), `text` (UTF-8 C strings)
- local variables, parameters, fields accessed from inside their own class
- arithmetic, comparisons, `and`/`or` (short circuit), `not`, unary minus, `mod`
- text concatenation and all primitive casts, including `cast(integer, someText)`
- `if`/`elseif`/`else`, `repeat N times`, `repeat while`, `repeat until`
- `output`, `say` (printed to stderr), `return`
- user classes with no parents other than Object: fields with initializers (readable from other objects),
  `on create`, actions, `Counter c` / `c:Add(5)` style object use and chained calls (`a:b:C()`)

Number formatting, integer division, `mod`, overflow and number-to-integer casts
match the JVM backend's output.

## What does not compile yet

Any standard library class (Array, List, HashTable, Math, ...), inheritance,
generics, `check`/`detect`/`alert`, `is`, calls on
primitives (`text:GetSize()`), and boxing.

These are listed as warnings when compiling, and each one compiles into a call
that stops the program with a message (exit code 70) if it is reached, so a
program using them still produces IR.

## Layout

- `Translate/LLVMBuilder.quorum` holds module and function state, type mapping, conversions, and constants
- `WriteLLVM(LLVMBuilder)` on each opcode, defaulting to "unsupported" in `QuorumOpcode`
- `Compiler:CompileToLLVM` and `WriteLLVM`, plus the `-llvm` flag in `SourceCode/Main.quorum`
- `quorum_runtime.c` is the C runtime the IR links against
