# toytui

## What is it?

`toytui` is a small, simple TUI framework that lets you create interactive,
structured terminal interfaces from shell scripts.

It does not follow the traditional operating model of `dialog`, `tput`, or
`ncurses`. Instead, it uses a stateful, element-based approach: elements and
their parameters live in a store; each call reads the state, modifies it,
renders it, writes it to the terminal, and exits.

## What is it for?

For building more complex interactive shell tools:

- Menus, lists, input fields, and scrollable text boxes
- Status or log interfaces displayed in the terminal
- Running and displaying external programs with the `subterm` element
- Controlled overlapping elements, relative element positions, and
  inheritable parameters

## Who is it for?

For people who write shell scripts and want a simple, structured user
interface in the terminal.

## Why does it exist?

It was created because I did not like the traditional
`dialog`/`tput`/`ncurses` ecosystem. Therefore, it does not attempt to
replace them.

This is a hobby project: beyond its usefulness to me, it does not aim for
profit, recognition, or an audience.

## Installation and build

### Requirements

- GNU/Linux
- `g++` 12.2+ (tested: 14.2.0, 13.3.0, 12.2.0; `-Wno-psabi` may be needed on 12.2.0)
- GNU `make`
- A terminal

C++17, no external C++ libraries.

Vendored C dependency: `libvterm` (MIT, C99, unmodified), in `third_party/vterm/`.

### From a downloaded ZIP

```sh
wget https://github.com/AXELab-personal/toytui/archive/refs/heads/main.zip
unzip toytui-main.zip
cd toytui-main
make clean
make
```

### With Git clone

```sh
git clone https://github.com/AXELab-personal/toytui.git
cd toytui
make clean
make
```

After building, the `toytui` executable is located in the
`build/release/` directory.

## Quick example

```sh
./toytui box new id:mainbox x:2 y:1 w:40 h:10 border:1 title:'Example'
./toytui label new id:label1 parent:box@mainbox fg:green \
  text:'test: \e[1m42\e[0m'
./toytui core set setfocus:box@mainbox
./toytui core get keyboard keys_exit:'[CTRL-Q]'
```

## Platform notes

`toytui` is Linux-only and writes directly to the TTY. It is intended for
users who are comfortable with the Linux command line and shell scripting.

It has not been designed or tested for screen-reader use.

## Documentation

The full user guide — commands, elements, parameters, key bindings, and
the daemon mode — is available here:

- [User guide](docs/User_Guide.md)

## License

`toytui` is available under the GNU Affero General Public License v3.0 (AGPL-3.0).

The bundled `libvterm` library is MIT-licensed. Both the code and the
license are included in the repository in their original, unmodified form.
See [THIRD_PARTY.md](THIRD_PARTY.md) for details.

## Maintenance

This is a hobby project. Updates and bug fixes are occasional; there is no
committed support, release schedule, or warranty.

The project is provided “as is”. If it does not meet your needs, feel free
to fork it and modify it to suit your own requirements :)

## A note on how it was made

Most of this project was created with free tools and the assistance of
several LLMs. Thanks to the Claude, Perplexity, and DeepSeek models.

Special “thanks” to Anthropic for the unpredictable usage limits that
strongly hindered development.
