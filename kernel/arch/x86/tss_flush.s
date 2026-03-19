.section .text
.global tss_flush
.type tss_flush, @function

tss_flush:
    mov 4(%esp), %ax
    ltr %ax
    ret
