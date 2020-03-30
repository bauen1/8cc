# 8cc 65816

A port of the 8cc compiler to 65816 native mode.

## What works

```C
int f(int a, int b, int c) {
    return a + b + c;
}
```

## Build

Run make to build:

    make

8cc comes with unit tests. To run the tests, give "test" as an argument:

    make test

The following target builds 8cc three times to verify that
stage1 compiler can build stage2, and stage2 can build stage3.
It then compares stage2 and stage3 binaries byte-by-byte to verify
that we reach a fixed point.

    make fulltest

## Author

Rui Ueyama <rui314@gmail.com>
bauen


Links for C compiler development
--------------------------------

Besides popular books about compiler, such as the Dragon Book,
I found the following books/documents are very useful
to develop a C compiler.
Note that the standard draft versions are very close to the ratified versions.
You can practically use them as the standard documents.

-   LCC: A Retargetable C Compiler: Design and Implementation
    http://www.amazon.com/dp/0805316701,
    https://github.com/drh/lcc

-   TCC: Tiny C Compiler
    http://bellard.org/tcc/,
    http://repo.or.cz/w/tinycc.git/tree

-   C99 standard final draft
    http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf

-   C11 standard final draft
    http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf

-   Dave Prosser's C Preprocessing Algorithm
    http://www.spinellis.gr/blog/20060626/

-   The x86-64 ABI
    http://www.x86-64.org/documentation/abi.pdf
