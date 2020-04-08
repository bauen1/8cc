.p816
.a16
.i16
; .segment "CODE2" : far

.export _strlen
_strlen:
    pha
    ldy #$0000
@loop:
    lda ($1,S),Y
    and #$00FF
    beq @done
    iny
    bne @loop
@done:
    pla
    tya
    rtl
