.globl setjmp
.globl longjmp

setjmp:
  xorl    %eax, %eax
  movl    0x4(%esp), %edx
  movl    %ebx, (%edx)
  movl    %esi, 0x4(%edx)
  movl    %edi, 0x8(%edx)
  leal    0x4(%esp), %ecx
#xorl    %gs:0x18, %ecx
  roll    $0x9, %ecx
  movl    %ecx, 0x10(%edx)
  movl    (%esp), %ecx
#xorl    %gs:0x18, %ecx
  roll    $0x9, %ecx
  movl    %ecx, 0x14(%edx)
  movl    %ebp, 0xc(%edx)
  movl    %eax, 0x18(%edx)
  ret


longjmp:
  movl    0x4(%esp),  %eax
  movl    0x14(%eax), %edx
  movl    0x10(%eax), %ecx
  rorl    $0x9, %edx
#xorl    %gs:0x18, %edx
  rorl    $0x9, %ecx
#xorl    %gs:0x18, %ecx
  movl    (%eax), %ebx
  movl    0x4(%eax), %esi
  movl    0x8(%eax), %edi
  movl    0xc(%eax), %ebp
  movl    0x8(%esp), %eax
  movl    %ecx, %esp
  jmpl    *%edx

