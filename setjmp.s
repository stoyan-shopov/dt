/*
Copyright (c) 2014-2016 stoyan shopov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
.code32
.globl setjmp
.globl longjmp

setjmp:
	movl	4(%esp),	%eax
	movl	%ebx,	(%eax)
	movl	%ecx,	4(%eax)
	movl	%edx,	8(%eax)
	movl	%esi,	12(%eax)
	movl	%edi,	16(%eax)
	movl	%esp,	20(%eax)
	movl	%ebp,	24(%eax)
	pushl	%ebx
	/* save return address */
	movl	4(%esp),	%ebx
	movl	%ebx,	28(%eax)
	popl	%ebx
	xorl	%eax,	%eax
	ret

longjmp:
	movl	4(%esp),	%eax
	movl	8(%esp),	%ebx
	movl	20(%eax),	%esp
	/* put return address on the stack */
	addl	$4,	%esp
	pushl	28(%eax)
	/* save return value */
	pushl	%ebx
	movl	(%eax),		%ebx
	movl	4(%eax),	%ecx
	movl	8(%eax),	%edx
	movl	12(%eax),	%esi
	movl	16(%eax),	%edi
	movl	24(%eax),	%ebp
	/* retrieve return value and make sure it is nonzero */
	popl	%eax
	orl	%eax,	%eax
	jnz	1f
	movl	$1,	%eax
1:
	ret

