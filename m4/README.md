m4 macro processor
==================

Welcome. This is a from scratch, cross platform, implementation of the `m4`
macro processor, released under the ISC License.

Why m4 rocks
------------

It works on text and can be used with any type of text document. It can be used
to add power to another programming language. An enormous amount of power is
gained from a small amount of learning.

Install
-------

To build `m4` simply run:
```
$ cc -O3 -o m4 m4.c
```
or
```
> cl m4.c
```
and place `m4` or `m4.exe` somewhere in your `PATH`.


Synopsis
--------

To use `m4` the synopsis is:

```
m4 [file ...]
```
If no files are specified, then it will read from `stdin`.

How (this version of) m4 works
------------------------------

When `m4' starts all the files listed on the command line are loaded into the
input buffer in a concatenated manner. If no files are listed, then all of
`stdin` is read and placed into the input buffer at once.

`m4' reads from the input one token at a time. A token is a series of
alphanumerical or underscore characters that must commence with an
alpha or underscore character.

`m4' maintains a doubly linked list of macro definitions. The token read is
compared to the defined macros and if there is no match, then the token is
simply written to the output buffer, unchanged.

If there is a match then the macro is processed. Some macros are built-in
and may change some aspect of the program. For user defined macros, if the
macro has no arguments (is called without parentheses) then the replacement
text is prepended into the input.

If the user defined macro is called with arguments (the macro name is followed
by an immediate left parenthesis) then the arguments are collected. When the
argument collection is finished (known by the matched right parenthesis), then
the arguments are inserted into the replacement text at the designated
locations (`$1` for argument one, up to `$9` for argument nine) and the result
is prepended into the input.

The process will continue as normal, that is, these injected tokens will be
read and tested to see if they match a macro definition (the input is treated
as the input regardless of its origin).

There is a stack (implemented as a doubly linked list) to keep track of nested
macro calls. Each time a token matches a macro a new node is added on top of
the stack, and when that macro has finshed being processed, the node is
removed. This gives `m4` a powerful recursive nature.

Quotes can be used to prevent macro expansion. When the first left quote is
encountered the quote mode is entered (this left quote will be eaten, it will
not be passed to the output). When quote mode is on all tokens are written to
the output, there is no macro expansion or interpretation of other characters
such as commas and parentheses (with the exception of the left and right
quotes). When the matched right quote is reached, the quote mode is turned off
(and this right quote is eaten).

The output is a shortcut to either an argument buffer of the head stack node
(during argument collection) or one of 11 diversion buffers. The diversion
buffers are identified by the `divnum` which can be 0 to 9 (inclusive) or -1.

When the program terminates without error, the diversions 0 to 9, in order, are
written to `stdout` (there is no progressive writing to `stdout` throughout the
execution of the program). Diversion -1 is not written to `stdout', it is
simply discarded, however, it can be undiverted to another diversion before the
program terminates.

Excess arguments supplied to a built-in macro call are ignored. Since only
arguments 1 to 9 can be referenced using the `$1` to `$9` notation, all macros
cannot take more than 9 arguments. Other than this, there are no limits placed
on this version of `m4', for example, there are no limits (except for random
access memory) on the buffer sizes, or the depth of the stack, or the number
of defined macros.


Built-in macros
---------------

The following built-in macros have been implemented:

```
define(macro_name, replace_text)
```
Enables users to define their own macros. `macro_name` will be expanded
during argument collection (before the definition is added to the definition
list), so if you are redefining an existing macro you will probably want to
protect `macro_name` with quotes. The `replace_text` can contain up to nine
arguments denoted as `$1` to `$9` (the same argument can be referenced
multiple times).

```
undefine(`macro_name_A', `macro_name_B', ...)`
```
Undefines the macro names listed as arguments. Since macros will be
expanded during argument collection, you almost always want to use
quotes to prevent the macro name from being expanded before the undefine
opertation is performed. Built-in macros can be undefined too, but they
cannot be redefined as a built-in macro.

```
divert or divert(divnum)
```
This changes the current diversion. If called without arguments
(no parentheses) then this defaults to diversion 0. Only diversions 0 to 9 and
-1 are valid.

```
undivert or undivert(divnum_A, divnum_B, ...)
```
When called with no arguments it appends all diversions except for the current
diversion and diversion -1 into the current diversion. When arguments are given,
only the listed diversions are appended into the current diversion (-1 can be
undiverted too). Undiverted buffers are emptied afterwards. This does not change
the input buffer.

```
divnum
```
Prints the current diversion. Valid diversions are 0 to 9 and -1.

```
changequote or changequote(left_quote, right_quote)
```
When called with no arguments it sets the left and right quotes to the default
settings which are the backtick and single quote, respectively. With arguments
it sets the quotes to the requested characters. Please note that the left and
right quotes must be different single characters and they must be graph
characters (printable).

```
include(filename)
```
Prepends the file specified into the input buffer.

```
dnl
```
Deletes all characters up to and including the next newline (`\n`) character.
This is used for single line comments, or to avoid the newline from being
written to the output after a macro call. `divert(-1)` followed by `divert`
can also be use to stop the trailing newlines from passing to the output
when defining macros.

```
esyscmd(shell_command)
```
Executes the shell command and prepends the `stdout` from the process
into the input buffer.

```
ifdef(`macro_name', text_if_defined, text_if_not_defined)
```
Collects the arguments (processing any macros possible) then checks if
argument 1 is in the macro definition list. If so, then argument 2 is prepended
into the input, otherwise argument 3 is prepended into the input. You will almost
always want to protect `macro_name' with quotes.

```
ifelse(A, B, text_if_A_equals_B, text_if_A_not_equal_to_B)
```
Compares `A` and `B` and prepends argument 3 into the input if they are equal,
or prepends argument 4 if they are not equal. Of course, the arguments are
processed for macros during argument collection, and the "if else" logic is
applied afterwards.

```
dumpdef or dumpdef(`macro_name_A', `macro_name_B', ...)
```
With no arguments this prints the entire macro definition list to `stderr`.
With arguments, it just prints the macro definitions for the given arguments
to `stderr`. You will normally want to protect the macro names with quotes to
prevent them from being expanded during the argument collection phase.
If there is no match, then "Undefined" is printed.

```
errprint(message_to_stderr)
```
Prints an error message directly to `stderr'. The argument is still subject
to expansion during the argument collection phase before the print is
performed.

```
traceon
traceoff
```
Turns on and off the trace functionality. When on, trace prints all tokens to
`stderr` as they are read, and prints the full nested macro stack to `stderr`
before a macro is processed (after argument collection, but before argument
substitution and input prepending).


Enjoy,
Logan
