# toytui — User Guide

## Operating model

`toytui` is a stateful, element-based TUI framework.

By default, elements and their parameters are stored in the `toytui.state`
file in the startup directory. In daemon mode, the state lives in the
daemon’s memory; in that case, `toytui.state` contains only the information
required to access the daemon.

Each invocation:

1. reads the state,
2. performs the requested modification,
3. redraws the interface,
4. saves the state,
5. writes the result,
6. exits.

State saving is atomic: it is performed through a temporary file and a
`rename` operation. Concurrent invocations are not recommended; the program
does not use locking, so race conditions and unexpected behavior may occur.
If you try it, do not blame me if it breaks...

Visual content is written to the device specified by the `core` `tty`
parameter, by default `/dev/tty`. The results of `new` and `get` are written
to stdout, one `param:value` pair per line. Error messages are written to
stderr.

`get` returns the processed state, not necessarily the input value. For
example, after `bg:red`, `get bg` returns `bgcol:1`; after `border:0`, it
returns `border:off`. For *text parameters*, the raw input is available in
the `*_raw` fields.

Invocation without parameters:

```sh
./toytui
```

This performs a complete redraw of the stored state. If `toytui.state`
does not exist, it returns an error.

## Commands and identifiers

The general form of a program invocation is:

```sh
toytui <type> <method> <param:value> ...
```

| Method | Effect |
|---|---|
| `new` | Creates a new element. The result written to stdout is `type@id`. The `id` must be unique among elements of the same type. |
| `set` | Modifies an existing element. The modification is atomic; the `id` cannot be changed. |
| `get` | Queries an element or parameter. `get all` returns all parameters. |
| `del` | Deletes an element together with its subordinate elements. Only the `id:` parameter is required. |

### Identifiers

The identifier of an element has the form `type@id`, for example
`box@window`.

- The `id` may contain only `[0-9a-zA-Z_-]` characters, up to 23 characters.
- Every element reference expects a full identifier.
- Only the `id:` parameter may use a short name: `id:window`.
- `core` is the root element; it has no `new` method and can be addressed
  without an `id`: `core@terminal`.

### Unknown parameters

- For `new` and `set`, unknown parameters, or parameters intended for another
  element type, are silently discarded.
- For `get`, an unknown parameter name or a token containing a value is an
  error.

### The `auto` value

The `auto` value resets a parameter to the state it would have if it had
never been set:

- For an inheritable parameter, it takes the parent’s value.
- For a parameter with a fixed default, it takes the default value.
- For a text field, it is the literal `"auto"` string.

## Common parameters

Common parameters are meaningful for most elements. Aliases are only
shortcuts usable in commands; `get` always returns the canonical parameter
name.

| Parameter | Alias | Default | Description |
|---|---|---|---|
| `id` | — | — | The element identifier. Only a short name may be used: `id:window`. |
| `parent` | — | `core@terminal` | The full identifier of the parent element. |
| `posX` / `posY` | `x` / `y` | `0` | The element’s position relative to the parent’s content area; negative values are allowed. |
| `width` / `height` | `w` / `h` | `auto` | The element size. `auto` means inheritance. |
| `bgcol` / `fgcol` | `bg` / `fg` | `auto` | Background and foreground colors. `auto` means inheritance. |
| `attrib` | — | `auto` | Text attributes. `auto` means inheritance. |
| `z_index` | — | `1` | Z-order within the parent. The element with the higher value is drawn on top. |

### Aliases and canonical names

Short aliases can be used in commands:

```sh
./toytui label set id:label x:10 y:2 bg:red fg:iwhite
```

The same command using canonical parameter names:

```sh
./toytui label set id:label posX:10 posY:2 bgcol:1 fgcol:15
```

`get` always returns the canonical name:

```sh
./toytui label get id:label x y bg fg
# posX:10
# posY:2
# bgcol:1
# fgcol:15
```

### Colors

Colors can be specified as numbers from 0 to 255 or by name.

The names of the 16 base colors are aliases:

```text
black red green yellow blue magenta cyan white
iblack ired igreen iyellow iblue imagenta icyan iwhite
```

Their canonical values:

| Name | Value |
|---|---:|
| `black` | 0 |
| `red` | 1 |
| `green` | 2 |
| `yellow` | 3 |
| `blue` | 4 |
| `magenta` | 5 |
| `cyan` | 6 |
| `white` | 7 |
| `iblack` | 8 |
| `ired` | 9 |
| `igreen` | 10 |
| `iyellow` | 11 |
| `iblue` | 12 |
| `imagenta` | 13 |
| `icyan` | 14 |
| `iwhite` | 15 |

The `i` prefix denotes the bright variant. True-color input is rounded to
the nearest 256-color palette entry.

### Text attributes

The value of `attrib` is a comma-separated list of names, without spaces.
It is recommended to enclose it in quotation marks:

```sh
./toytui label set id:label attrib:"bold,underline"
```

Name list:

```text
bold underline strikethrough blink inverse italic dim
```

Aliases:

```text
ul = underline
strike = strikethrough
reverse = inverse
```

### Boolean values

Boolean parameters accept the following forms:

```text
on true 1
off false 0
```

### Inheritance

A size, color, or attribute with the value `auto` is inherited from the
parent element, up to the `core` element.

A color or attribute is either inherited or overridden; the two are not
mixed.

## Escaping and Unicode

### Escaping

The shell tokenizes command-line arguments. Within a value, as well as in
the free text of `text:`, the following escape sequences are resolved from
left to right:

| Input | Result |
|---|---|
| `\"` | `"` |
| `\'` | `'` |
| `\\` | `\` |
| `\n` | newline |
| `\t` | tab |

Any other backslash sequence remains unchanged.

Backslashes and quotation marks must be protected. For example, to specify
the value `C:\new\table`:

```sh
./toytui label set id:label text:'C:\\new\\table'
```

To specify ANSI control sequences, the `\\e[` form produces the literal
`\e[` text, not an escape sequence:

```sh
./toytui label set id:label text:'a \\e[1m b'
# displayed text: a \e[1m b
```

Three backslashes can be used to produce both a literal backslash and a
real escape sequence.

### Unicode

A Unicode character can be specified as `\u[HEX]` in the following text
inputs:

- `text:`
- `text_add:`
- `title:`
- the `value:` and `placeholder:` parameters of `input`
- the elements of `select` `options:`
- the `pattern:` of `input`
- the contents of the `textarea` `src:` file

`HEX` may consist of 2 to 8 hexadecimal digits, in either upper- or
lowercase. Examples:

```sh
./toytui label set id:label text:'\u[263b]'
./toytui label set id:label text:'\u[1f37a]'
./toytui label set id:label text:'\u[1b]'
```

`\u[1b]` is equivalent to the `\e` escape sequence.

Only a valid Unicode code point is interpreted: it cannot be 0, a surrogate,
or a value above `10FFFF`. Any other form — for example `\u[4]`, `\u[zz]`,
`\u263b`, or a missing bracket — remains literal.

To specify the literal `\u[3a]`:

```sh
./toytui label set id:label text:'\\u[3a]'
```

In the values of `pattern` and `options`, the resolved character is plain
data: `\u[7b]` is a literal `{`, and `\u[7c]` is a literal `|` character
within the element. `get` returns the raw form here.

For `pattern`, an invalid `\u[...]` form is an error; for other parameters,
it remains literal.

`pattern` may also contain SGR control sequences. These can color the
display of the pattern; they do not affect value processing.

```sh
./toytui input new id:mac \
  pattern:'MAC={%X2}:\e[1;33m{%X2}\e[0m:{%X2}:{%X2}:{%X2}:{%X2}'
```

### Special behavior of the `text:` parameter

Starting from the first `text:` argument, **all remaining arguments** of the
invocation become part of the text; they are joined with spaces.

Therefore, the `text:` parameter must always be specified last:

```sh
./toytui label set id:label fg:green text:'Done: \e[1m42\e[0m'
```

In the following invocation, `fg:green` is already part of the text, not a
parameter:

```sh
./toytui label set id:label text:'Done' fg:green
```

## Elements

Elements are the building blocks of the user interface. Every element has
its own type, identifier, and set of parameters.

### `core` — the terminal

`core` is the root element. It cannot be created with the `new` method and
can be addressed without an `id`. Its full identifier is `core@terminal`.

#### Terminal parameters

| Parameter | Default | Description |
|---|---|---|
| `width` / `height` | actual terminal size / 80×25 | The terminal size. At startup, it is set to the actual terminal size if it can be queried; otherwise, the fallback is 80×25. |
| `bgcol` / `fgcol` | `0` / `15` | The terminal’s base colors. |
| `attrib` | `0`, none | Base text attributes. |
| `tty` | `/dev/tty` | The target of visual output. It must be an existing, writable device or file; the program does not create it. |
| `path` | `.` | The directory in which state data is stored. Changing it moves the data storage; `toytui.state` remains only a flag and a path pointing to the data store. |
| `setfocus` | `auto` | The full identifier of the focused element. With `auto`, there is no element focus; keystrokes appear on `toytui`’s stdout. |
| `term_cursor` | `on` | The visibility of the terminal cursor after `toytui` finishes. (An unnecessary cursor can be hidden without using `tput civis`.) |

For `core`, `auto` resets to the system default.
If `tty` is not a terminal, the size fallback is 80×25.

#### Focus

Focus determines which element receives keyboard events.

```sh
./toytui core set setfocus:box@window
```

The value `setfocus:auto` sets a mode without element focus. In that case,
typed characters appear on `toytui`’s stdout.

Example:

```sh
./toytui core get keyboard setfocus:auto keys_exit:'[ESC]'
```

If you then press `asd<ENTER><ESC>`, the stdout output is:

```text
exit:[ESC] out:"asd[ENTER]"
```

#### `core get` function calls

`core get` can query not only parameters but also invoke functions. The
function name must be the first parameter after `get`.

| Function | Parameters | Effect |
|---|---|---|
| `keypress` | `timeout:<ms>`, `noblock:<bool>` | Reads one key. Without focus, it writes the key notation to stdout; if no key is available, `[NUL]`. |
| `keyboard` | `keys_exit:<keys>` | Loops until an exit key is pressed. The output is one line. |
| `param_list` | names or `all` | Lists all elements, one per line. |

Examples:

```sh
./toytui core get keypress timeout:1000
./toytui core get keyboard keys_exit:'[CTRL-Q]'
./toytui core get param_list all
```

`core set keyboard:[SEQ]` performs simulated, non-blocking key playback.
The other `set` parameters take effect first.

### `box` — container

`box` is a container element: it can be the parent of other elements. The
positions of elements inside it are interpreted relative to the `box`
content area.

#### Parameters

| Parameter | Alias | Default | Description |
|---|---|---|---|
| `border` | — | `off` | Border type: `off`/`0`, `1`, or `2`. |
| `border_fgcol` / `border_bgcol` | `brdfg` / `brdbg` | `auto` | The border colors. |
| `title` | — | — | The title displayed on the top border. |
| `title_raw` | — | — | The raw title text. |
| `titlePos` | — | `left` | The title position: `left`/`0`, `center`/`1`, `right`/`2`. |
| `scrollbarX` / `scrollbarY` | `scrollX` / `scrollY` | `off` | Visibility of the horizontal and vertical scrollbars. |
| `scrollbarX_pos` / `scrollbarY_pos` | — | `auto` | The scrollbar position; 1-based. With `auto`, it is calculated automatically. |
| `scroll_fgcol` / `scroll_bgcol` | `scfg` / `scbg` | `auto` | The scrollbar colors. |
| `offsetX` / `offsetY` | `offsX` / `offsY` | `0` | The content offset. It may be negative. |
| `minOffsetX/Y` / `maxOffsetX/Y` | `minOffsX/Y` / `maxOffsX/Y` | `auto` | The limits of the offset range. |
| `overscroll` | — | `off` | Allows content to extend beyond the area. |

#### Content area

The content area is what remains of the `box` size after subtracting the
border and scrollbars.

The stored `offset` value is raw; display clamps it to the permitted range.

#### Scrolling while focused

When focused, `box` can be scrolled with the following keys:

| Key | Effect |
|---|---|
| `[UP]` / `[DOWN]` | One line up / down |
| `[LEFT]` / `[RIGHT]` | One column left / right |
| `[PGUP]` / `[PGDN]` | One page up / down |
| `[HOME]` / `[END]` | Vertical beginning / end |
| `[SHIFT-PGUP]` / `[SHIFT-PGDN]` | One page left / right |
| `[SHIFT-HOME]` / `[SHIFT-END]` | Horizontal beginning / end |

#### Example

```sh
./toytui box new id:window x:2 y:1 w:40 h:10 \
  border:1 title:'ScrollTest' scrollY:on

./toytui label new id:line parent:box@window fg:green \
  text:'done: \e[1m42\e[0m'

./toytui core set setfocus:box@window
./toytui core get keyboard keys_exit:'[CTRL-Q]'
```

### `label` — single-line text

`label` is used to display single-line text.

#### Parameters

| Parameter | Description |
|---|---|
| `text` | The processed text; readable back with `get`. |
| `text_raw` | The raw text as received, stored unchanged. |

#### Width and padding

- Without `width:`, the length of the cleaned text becomes the width.
- When `width:` is specified, the text is truncated or padded with spaces.
- Padding inherits the attributes.

#### Control characters

- Tab, newline, CR, VT, and FF characters are converted to spaces.
- Other control characters disappear from the display and the `text` view.
- The value of `text_raw` remains unchanged.

#### SGR sequences

SGR sequences provide per-character styling:

```sh
./toytui label set id:title text:'\e[1masdf\e[0m'
```

An invalid or unknown sequence has no effect. After an unterminated
sequence, the remaining text is lost.

#### `get text`

`get text` returns the canonical form: sections with the same style are
merged. The output can be used as `text:` input.

### `textarea` — multi-line text

`textarea` is a leaf element for displaying multi-line, wrapped, and
aligned text. It stores up to 512 KiB of raw text.

It handles `\n` (hard line break) and `\t` (tab) characters. The text may
also contain ANSI SGR sequences.

#### Position and size

`posX` / `posY` are interpreted relative to the logical width of the
`textarea`. This width does not change, whether it is `auto` or explicitly
specified; wrapping and alignment are calculated for this width.

#### Parameters

| Parameter | Default | Description |
|---|---|---|
| `text` | — | The processed text; readable with `get`. |
| `text_raw` | — | The raw text as received, unchanged. |
| `text_add` | — | Write-only; appends text to the end of the existing buffer. |
| `src` | — | Write-only; loads the buffer from a file. |
| `tabsize` | `4` | Tab width. |
| `wrap` | `full` | Wrapping mode. |
| `align` | `left` | Alignment. |

#### Escapes and tabs

The following escapes are interpreted in the input:

- `\n`: newline, i.e. a hard line break
- `\t`: tab
- `\\`: backslash

A tab fills up to the next multiple of `tabsize` columns. The step is
counted from the beginning of the displayed line; padding inherits the tab
cell’s style.

#### Wrapping

- `full`: searches for break points, with forced breaks if necessary.
- `semi`: forced breaks only.
- `off`: only explicit `\n` breaks.

#### Alignment

- `left`
- `right`
- `center`
- `justify`

`justify` stretches only lines broken by wrapping. The last line of a
paragraph — the line before `\n`, or the final line of the text — is
left-aligned.

#### Example

```sh
./toytui textarea new id:text parent:box@window \
  text:'line #1\nline #2'
```

### `button` — push button

`button` is a focusable, visually indicated control element built on
`label`. All `label` parameters can be used: `text`, `text_raw`, size,
colors, `attrib`, and the other common parameters.

#### Additional parameters

| Parameter | Alias | Default | Description |
|---|---|---|---|
| `focus_bgcol` / `focus_fgcol` | `focus_bg` / `focus_fg` | `auto` | The appearance when the button is focused. |
| `pressed_bgcol` / `pressed_fgcol` | `pressed_bg` / `pressed_fg` | `auto` | The appearance during `trigger`. |
| `delay` | — | `150` | The duration of the pressed state in milliseconds; 0–10000. |
| `trigger` | — | — | Write-only action. With `set ... trigger:on`, the pressed appearance is displayed. |

`trigger` is not stored: it does not appear in the output of `get`,
`get all`, or `param_list`. In a `new` command, `trigger:on` is an error;
`trigger:off` has no effect.

#### Color rule

The color rule is shared by `button` and `select`. It applies after colors
have been resolved, including `auto` colors inherited from the parent.

- Without focus, the base `bgcol` / `fgcol` values apply.
- When focused, if both members of the `focus_bgcol` / `focus_fgcol` pair
  are unset, the base colors are swapped.
- If at least one focus color is specified, the explicit value applies; the
  other member remains the base value.
- During `trigger`, the same rule applies to the current appearance and the
  `pressed_bgcol` / `pressed_fgcol` pair.
- Thus, the flash is visible both with and without focus, even on a
  monochrome terminal.

#### `trigger:on`

After validating the other `set` parameters, `trigger:on` displays the
pressed appearance. After `delay` milliseconds, the normal appearance is
restored.

The stored state remains byte-identical: the pressed state is only a
non-persisted internal `pressed` flag that exists while the display is
being drawn.

#### Key handling

A focused button passes the notation of the received key unchanged to
`toytui`’s stdout. The button state does not change.

- `core get keypress`: returns immediately with the key notation.
- `core set keyboard:<SEQ>`: the notations are displayed concatenated.
- `core get keyboard`: the notations of typed keys are collected; the loop
  runs until a `keys_exit` / `keys_accept` key, then the complete typed
  data is written to stdout.

The shell decides what to do with the notation. For example, on `[ENTER]`
or space, it can invoke:

```sh
button set id:<id> trigger:on
```

#### Example

```sh
./toytui button new id:ok x:2 y:8 w:10 h:1 delay:200 text:'OK'

./toytui core set setfocus:button@ok
./toytui core get keypress
./toytui button set id:ok trigger:on
```

`core get keyboard` is not recommended for `button`. It works, but there is
no visual feedback; it is useful only for advancing on predefined keys
without separate inspection (`keys_exit` / `keys_accept`).

### `select` — selection list

`select` is a bordered selection list that displays one option per line. The
list is scrollable; if it has a border and at least two content lines,
scroll indicators appear at the top and bottom of the right border.

The indicators use the border colors: `△` upward, `▽` downward.

#### Parameters

| Parameter | Default | Description |
|---|---|---|
| `border` | `off` | Border type: `off`/`0`, `1`, or `2`. |
| `border_fgcol` / `border_bgcol` | `auto` | The border colors. |
| `title` | — | The title displayed on the top border. |
| `titlePos` | `left` | The title position: `left`/`0`, `center`/`1`, `right`/`2`. |
| `options` | — | The list items, separated by vertical bars. |
| `keys_exit` | — | Required exit keys. |
| `keys_accept` | — | Optional accept keys. |
| `cursor` | `1` | The line number of the marked item; 1-based. |
| `selected` | `0` | The line number of the accepted item; otherwise `0`. |
| `top` | `0` | The 0-based index of the first visible item. |
| `visualCursor` | `auto` | The cursor line within the content area; 1-based. |
| `cursor_bgcol` / `cursor_fgcol` | `auto` | The cursor line colors. |
| `sel_bgcol` / `sel_fgcol` | `auto` | The selected line colors. |

#### `options`

The value of `options` contains the list items separated by vertical bars:

```sh
options:'first|second|third'
```

- The `\|` pair produces a literal vertical bar.
- The `\\` pair produces a backslash.
- The `\u[HEX]` form is resolved into a Unicode character.
- An empty value means an empty list.
- Two consecutive separators produce an empty item.
- The maximum size is 512 KiB.
- `get` returns the raw value.

#### Exit and acceptance

`keys_exit` is required. `keys_accept` is optional; the union of the two
lists forms the element’s exit keys.

- `keys_exit`: leaves the element; the selection is not accepted.
- `keys_accept`: accepts the selection, then leaves the element.

The output format is:

```text
exit:<key> selected:<n>
```

- `n` is the cursor line number; on an empty list, `0`.
- With an `keys_accept` key, `selected := n`.
- With any other exit key, `selected := 0`.

#### Cursor and view

- `cursor`: the line number of the marked item; 1-based.
- `selected`: the line number of the accepted item; otherwise `0`.
- `top`: the 0-based index of the first visible item.
- `visualCursor`: the cursor line within the content area; 1-based.

`visualCursor` is a derived, non-stored value:

```text
visualCursor = cursor - top + 1
```

If nothing is visible, its value is `auto`.

When writing, `visualCursor` sets the visible window (`top`) so that the
cursor lands on the requested line. The value of `cursor` does not change.
If the requested position cannot be reached, the nearest possible position
remains; no error occurs.

When `cursor` and `visualCursor` are set together, the cursor is set first,
then the view.

#### Color rule

- `cursor_bgcol` / `cursor_fgcol`: the cursor line colors.
- `sel_bgcol` / `sel_fgcol`: the selected line colors.
- If both cursor colors are unset, the current `bgcol` / `fgcol` values are
  swapped.
- If one is specified, the other remains the base value.
- If both selection colors are unset, no change occurs; the cursor color
  remains.
- If one selection color is specified, the other becomes the cursor
  appearance.

The cursor line highlight is independent of focus: it remains visible after
leaving the selection.

#### Size and appearance

- With explicit `w:` / `h:`, the specified size applies, even if it
  overflows.
- Without an explicit size, the width is the widest option plus the border.
- Without an explicit size, the height is the number of lines plus the
  border.
- Automatic size is limited to the parent content area; the `posX` / `posY`
  values are subtracted.
- Every line is drawn across the full width of the content area.
- The line colors use the `select` `bgcol` / `fgcol` values, including
  those inherited from the parent.

#### Movement

When focused, the following keys move:

| Key | Effect |
|---|---|
| `[UP]` | One line up |
| `[DOWN]` | One line down |
| `[PGUP]` | One page up |
| `[PGDN]` | One page down |
| `[HOME]` | To the first item |
| `[END]` | To the last item |

- There is no wrapping.
- `[PGUP]` / `[PGDN]` move by the number of visible lines, up to the ends.
- The list scrolls minimally so that the cursor remains visible.
- A key press at a boundary neither modifies nor redraws.
- Any other key is discarded.
- There is no movement on an empty list.

#### Example

```sh
./toytui select new id:menu x:2 y:2 w:30 h:7 \
  options:'first|second|third' \
  keys_exit:'[ESC]' keys_accept:'[ENTER]'

./toytui core set setfocus:select@menu
./toytui core get keyboard
```

### `input` — input field

`input` is a single-line input field governed by a pattern (`pattern`).
The pattern defines the structure of the visible text, the characters that
can be entered, and the lengths of the blocks.

#### Pattern

The default pattern is:

```text
{%s0-}
```

At least one block is required. A pattern may consist of literals, SGR
sequences, and blocks:

```text
pattern ::= ( literal | sgr | block )*
literal ::= any character except `{`
sgr     ::= `\e[...m`
block   ::= '{' ( type-length | '%h'character | '%'option':'value )+ '}'
```

Any number of spaces may appear between the elements of a block; their
order is arbitrary.

- `\` is always an escape: `\\`, `\{`, `\}`, and `\%`.
- An SGR sequence is for display only; it is not part of the data.
- Termination is not required.
- The pattern receives double escaping: first from the command line, then
  from the pattern.
- A literal backslash in the command line is, for example, `\\\\`.

#### Blocks

| Type | Meaning |
|---|---|
| `%s` | Any printable, single-column character. |
| `%S` | Any printable character; stored uppercase. |
| `%u` | Positive integer (`unsigned int`). |
| `%i` | Signed integer (`signed int`); `-` only in the first position. |
| `%f` | Signed floating-point number (`float` / `double`); `-` only in the first position, and one `.` is also allowed. |
| `%x` | Hexadecimal digits; case is preserved. |
| `%X` | Hexadecimal digits; stored uppercase. |
| `%c[set]` | The characters of the set; case is significant. |
| `%C[set]` | The characters of the set; stored uppercase. |

The length denotes the number of characters within the block:

```text
N      exactly N characters
N-     at least N characters
-M     at most M characters
N-M    between N and M characters
0-     any number
```

The entire field can contain at most 512 typed characters.

`%h<character>` is the block placeholder. A bounded block is filled with
the placeholder up to its maximum width; an unbounded (`N-`) block has no
padding.

Block-level options override the element’s corresponding parameters:

- `%csr:value`: cursor shape
- `%mode:value`: input mode
- `%adv:value`: automatic advancement

#### Unicode and escape

The `\u[HEX]` form can be used in a literal, after `%h`, and within a
`%c[...]` set. The resolved code point is always a plain character, never a
metacharacter.

- `\u[7b]`: literal `{`
- `\u[5d]`: `]` within a set
- `\u[1b]`: a real ESC

If `[` follows ESC, it is interpreted as the SGR introducer. Any other
control character or invalid form is an error.

The stored `pattern` is the raw form. The `input` `value` and `placeholder`
parameters are resolved as data in the command line.

#### Parameters

| Parameter | Default | Description |
|---|---|---|
| `pattern` | `{%s0-}` | The input pattern. |
| `value` | `""` | The derived, pattern-formatted visible text. |
| `blocks` | — | Read-only; the raw text of the blocks. |
| `placeholder` | space | The character used for empty positions; single-cell and printable. |
| `advance` / `adv` | `on` | After a bounded block is full, moves to the beginning of the next block. |
| `cursorType` | `bBar` | The cursor shape; canonical name or alias. Overridden by the block-level `%csr:`. |
| `inputMode` | `insert` | Insert or overwrite mode. |
| `keys_accept` | — | Required; the keys that commit the input. |
| `keys_exit` | `auto` (`CTRL-C`) | Exit keys; with `auto`, `CTRL-C`. |
| `keys_navNext` | `[SHIFT-RIGHT]` | Moves to the beginning of the next block. |
| `keys_navPrev` | `[SHIFT-LEFT]` | Moves to the beginning of the previous block. |
| `keys_navHome` | `[HOME]` | Moves to the beginning of the field. |
| `keys_navEnd` | `[END]` | Moves to the end of the field. |

`value` is the pattern-formatted visible text: it contains literals, blocks,
and the placeholders of bounded blocks, without SGR.

`blocks` returns the raw text of the blocks, separated by vertical bars; it
does not contain literals or placeholders.

#### Cursor shape

| Canonical value | Aliases | Meaning |
|---|---|---|
| `bBlock` | `bB`, `1` | Blinking block |
| `Block` | `B`, `2` | Fixed block |
| `bUnderline` | `bU`, `3` | Blinking underline |
| `Underline` | `U`, `4` | Fixed underline |
| `bBar` | `bR`, `5` | Blinking bar |
| `Bar` | `R`, `6` | Fixed bar |

The block-level `%csr:` overrides the element’s `cursorType` value.

#### Keys

`keys_exit` and `keys_accept` are the element’s declared exit keys. The
`keys_nav*` lists may also be enumerations; an empty value means that no
key is assigned.

The order of handling:

1. exit or accept key
2. `keys_nav*`
3. fixed editing keys
4. typing

| Key | Effect |
|---|---|
| `[LEFT]` / `[RIGHT]` | Moves across a block boundary |
| `[BS]` | At the beginning of a block, moves to the end of the previous block and deletes its last character |
| `[DEL]` | Deletes only within the current block |
| `[INS]` | Switches between insert and overwrite |
| printable character | Enters according to the block rule |

- An unacceptable character is discarded.
- Insertion into a full block is rejected.
- `advance` moves to the next block after the current one is full.
- An unrecognized key is discarded.
- An exit or accept key takes precedence over typing; a printable character
  is not entered into the field.

#### Conflicts

- `keys_exit` and `keys_accept` must not overlap; an overlap is an error.
- The default value of `keys_nav*` is overridden by `exit` / `accept` and
  by another explicitly specified navigation list.
- A conflicting key is silently removed from the navigation list.
- `auto` restores the default; the conflicting key is removed in that case
  as well.
- A conflict between navigation keys explicitly specified in the same
  invocation is an error.
- Silent overriding of a value stored from another invocation is not an
  error.

#### Exit

After an exit key, the output is:

```text
exit:<key> accepted:on|off complete:on|off value:"<value>"
```

- `accepted:on`: accept key (`keys_accept`).
- `accepted:off`: only leaving (`keys_exit`).
- `complete:on`: every block has reached its minimum length.
- `value`: the `get value` view, in quotation marks; with `\"` and `\\`
  escaping.

The field contents remain after exit.

Because the element declares an exit key, the default `[CTRL-C]` of
`keyboard` does not apply; the same is true for `select`.

#### Terminal cursor

The terminal cursor appears only when the target is a terminal; no sequence
is written to a file or pipe.

When an input field is focused, after every redraw the program positions the
cursor at the field’s editing position and makes it visible.

The cursor shape is the current block’s `%csr:` value; if absent, the
element’s `cursorType` value.

Default state:

- after an exit or accept key
- when the focused element loses focus
- when the element is deleted
- in `mode:destroy`

In these cases, the cursor is reset, and its visibility follows the `core`
`term_cursor` value.

With focus on an element without a cursor — for example, `select` or `box`
— no cursor sequence is emitted.

#### Width

- Without `w:`, the full width of the pattern applies.
- With the cursor at the end of an unbounded block, one additional column is
  required.
- With a narrower `w:`, horizontal scrolling occurs.
- The cursor column is always visible; scrolling uses the minimum movement.
- The height is always `1`.

#### Example

```sh
./toytui input new id:inp y:2 keys_accept:[ENTER] \
    keys_exit:[TAB] \
    cursorType:bU \
    pattern:"MAC:{%X2}:\e[1;33m{%X2}\e[0m:{%X2}:{%X2}:{%X2}:{%X2}"
./toytui core set setfocus:input@inp
./toytui core get keyboard
```

### `subterm` — embedded terminal

`subterm` displays the terminal output of a subprocess — a shell or any
program. Terminal emulation is performed by `libvterm`.

It works only in daemon mode. In file mode, `new subterm` returns an error.

#### Parameters

| Parameter | Default | Description |
|---|---|---|
| `cmd` | empty | The command line to run. |
| `cwd` | startup directory | The working directory of the child process. |
| `scrollback` | `0` | The number of history lines. |
| `exec` | `off` | Explicit control of program execution. |
| `textout` | `off` | Textual collection of the program’s output. |
| `keys_exit` | `[CTRL-END]` | Optional exit keys. |
| `pid` | `0` | Read-only; the PID of the child process. |
| `exit_code` | — | Read-only; exit code or `128+signal`. |
| `send` | — | Write-only; text sent to the program’s input. |
| `signal` | — | Write-only; signal sent to the child process group. |

#### `cmd` and `cwd`

`cmd` is the command line to run. Examples:

```sh
cmd:mc
cmd:"/usr/bin/ssh 127.0.0.1 -l guest"
cmd:"tail -f log | grep ERROR"
```

`cmd` and `cwd` can be written while running, but they only parameterize;
they do not start a program.

Here, `auto` means the literal program named `auto`.

#### `exec`

A program starts only with an explicit command:

```sh
./toytui subterm new id:x cmd:mc exec:start
```

or later:

```sh
./toytui subterm set id:x exec:start
```

- `exec:on` / `start`: starts the program.
- `exec:off` / `stop`: stops the program.
- `get exec`: returns the current execution state (`on` / `off`).
- `auto` is not meaningful; it is an error.

State restoration, daemon restart, or anything else does not start a program
automatically.

`new` and `set` validate all other parameters before `exec`; the start
occurs afterward. A `new` without `exec` only prepares; it does not start.

#### Exit

The terminal’s lifetime is the program’s lifetime. There is no intermediate
shell that remains open after the program exits.

#### `textout`

From the moment `textout:on` is enabled, the daemon collects the program’s
output read from the pty, filtered into plain text.

- Control and escape sequences are omitted.
- `\r\n` is converted to `\n`.
- A lone `\r` and the other C0 control characters are discarded, except
  `\t`.
- Invalid UTF-8 is converted to `U+FFFD`.

This is the text of the byte stream, not the contents of the screen. It is
intended for line-oriented programs: prompts, error messages, and command
output. For full-screen programs — such as `mc` or `vim` — it is
meaningless.

`get textout` returns the collected text as `textout:<text>` and empties the
buffer. Here, `get` modifies the daemon’s runtime state; it does not modify
the saved state.

`textout:off` discards the buffer. The buffer limit is 1 MiB; on overflow,
the oldest text is dropped.

#### Waiting

The program’s output arrives asynchronously, so a plain `get` may be early.
`get textout` accepts two optional parameters:

- `timeout:<ms>`: waits at most this long.
- `idle:<ms>`: waits until no new data arrives for this long; default `100`.

#### Keys

`keys_exit` is optional. All other keys are sent to the program.

`keys_exit` works as a declared exit key, similar to `select` and `input`.

#### Example

```sh
t="./toytui"
$t core set mode:daemon
term=$($t subterm new id:term w:60 h:15 cmd:mc)
$t core set setfocus:$term
$t subterm set id:term exec:start
dummy=$($t core get keyboard) # exit: CTRL-END
$t subterm set id:term exec:stop
```
