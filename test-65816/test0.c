#include <stddef.h>
#include <stdint.h>

int test1;

int test = 0;

void f(void) {

}

int f2_a(int a, int b) {
    /* should be:
     * pha
     * lda $ 1 + 3 + 2,S -> lda $6,S
     * ply
     */
    return a;
}

int f2_b(int a, int b) {
    /* should be:
     * pha
     * lda $1,S
     * ply
     * rtl
     * */
    return b;
}

int f3(void) {
    /* expected:
     * lda #$0003
     * rtl
     */
    return 3;
}

int f4(void) {
    /* expected:
     * phx
     * phx
     * phx
     * lda $3,S
     * rtl
     * ...
     * */
    int a;
    int b;
    int c;

    return b;
}

int f5(int a) {
    /* expected:
     * pha
     * lda $0x1,S
     * clc
     * adc #$0001
     * ply
     * rtl
     * ... */
    return a + 1;
}

int f6(int a, int b) {
    return (a + b) + 2;
}

static int t7;

int f7(int a) {
    /* expected:
     * pha
     * lda $0x1,S
     * clc
     * adc #$0001
     * sta t7
     * lda t7
     * ply
     * rtl
     * */
    t7 = a + 1;
    return t7;
}

void f8(void) {
    f();
}

int f9(int a) {
    /* expected:
     * pha
     * lda $0x1,S
     * clc
     * adc #$0002
     * pha
     * lda #$0003
     * jsr f6
     * ply
     * ply
     * rtl
     * ...
     * */
    return f6(a + 2, 3);
}

void f10(char c) {
    /* expected:
     * pha
     * lda $0x1,S
     * pha
     * lda #$BEEF
     * pha
     * sta $1,S
     * */
    *((volatile uint16_t *) 0xBEEF) = c;
}

int f11(char c1) {
    return (10 + 12) + c1;
}

int f12(int b) {
    return 2 - b;
}

/* FIXME: wrong */
int f13(void *p) {
    uint16_t *p2 = p;
    *p2 = 0;
}

int f14(unsigned long a) {
    return a;
}

int f15(int a, int b, int c) {
    return c + b + a;
}

int f16(int a) {
    /* expected:
     * pha
     * lda $1,S
     * ply
     * rtl
     * ...
     * */
    return a;
}

int f17(int a, int b) {
    /* expected:
     * pha
     * lda $1 + 3 + 2,S
     * ply
     * rtl
     * ...
     * */
    return a;
}

/* expected offset:
 * a:
 * b:
 * c: -1 */
int f18(int a, int b, int c) {
    /* expected:
     * pha
     * lda $1 + 3 + 2 + 2,S -> lda $8,S
     * ply
     * rtl
     * lda $1 + 3 + 2,S     -> lda $6,S
     * ply
     * rtl
     * lda $1,S
     * ply
     * rtl
     * ply
     * rtl
     * */
    return a;
    return b;
    return c;
}

static int t19 = 0xBEEF;
