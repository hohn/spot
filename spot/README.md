README
======

Welcome to spot
---------------

spot is a minimalistic and fast text editor.

spot:

* Is cross-platform.
* Does not use any curses library.
* Uses a double buffering method for graphics which has no flickering.
* Uses the gap buffer method for text with transactional operations.
* Uses the Quick Search algorithm with a reusable bad character table on
  repeated searches.
* Is released under the ISC License.

Install
-------

On POSIX systems:
```
$ cc -O3 -o spot lbuf.c lcurses.c spot.c
```
otherwise:
```
> cl spot.c lbuf.c lcurses.c
```
and place `spot` or `spot.exe` somewhere in your PATH.

Key Bindings
------------

Some command can take an optional command multiplier prefix,
`^U n`, where `^U` denotes `Ctrl-U` and `n` is a number.

Please note that `^[` denotes `Esc`, `Alt`, `Meta`, or `Ctrl-[`.
You may need to adjust your console settings to make `Alt` send `Esc`.

Usually `Ctrl-Spacebar` is equivalent to `^@`.
`LK` is the left arrow key and `RK` is the right arrow key.

```
^U n ^P     Previous line (up)
^U n ^N     Next line (down)
^U n ^B     Backwards line (left)
^U n ^F     Forwards line (right)
^U n ^A     Antes of line (home)
^U n ^E     End of line (end)
^U n ^D     Delete
^U n ^H     backspace
^U n ^Y     Yank (paste)
^U n ^Q 0a  Quote (insert) two digit hex value (example is newline)
^U n ^[\ t  insert C escape sequence (example is tab)
^U n ^[u    Uppercase word
^U n ^[l    Lowercase word
     ^@     set mark
     ^W     Wipe region (cut)
     ^K     Kill to end of line
     ^O     kill to Origin of line
     ^S     Search
     ^R     Replace. In the command line type:
                delimiter_char find_text delimiter_char replace_text
     ^L     Level (centre) the cursor on the screen
     ^G     Get out (deactivate the mark or exit the command line)
     ^T     Trim trailing whitespace and remove all characters that
                are not ASCII graph, space, tab, or newline.
     ^Z a   change paste buffer (a to z)
     ^X ^S  Save current buffer
     ^X i   Insert file at the cursor
     ^X r   Rename buffer (does not save automatically)
     ^X f   open File (new buffer)
     ^X ^C  Close editor without prompting to save any buffers
     ^X LK  move left one buffer
     ^X RK  move right one buffer
     ^[w    soft Wipe (copy)
     ^[n    Next (repeat) search
     ^[L    redraw screen
     ^[<    start of buffer
     ^[>    end of buffer
     ^[m    Match brace
```

Status bar
----------

The status bar always shows the buffer name, the row number and column index
(in brackets), and the active paste buffer (could be one of `a` to `z`).

```
   README.md (1, 0) a
   ^          ^  ^  ^
   |          |  |  |
Buffer name   |  |  +-- Paste buffer is a
              |  |
              |  +--- Column index (starts at 0)
              |
            Row number (starts at 1)
```
It also shows an exclamation mark if the last command failed, an asterisk
if the buffer has been modified, and a letter `m` if the mark is currently set
(there is a region between the mark and the cursor).

```
!* README.md (92, 3) am
^^                    ^
||                    |
||                    +-- Mark is set
|+-- Modified buffer
|
Last command failed
```

Enjoy,
Logan
