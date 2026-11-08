# UI

A compact, modern flat widget toolkit written entirely in GBX. It draws its
own widgets with `DRAW_RECT`, `DRAW_LINE` and `TEXT_DRAW`, reads the mouse and
keyboard, and needs no images, fonts or C++ code.

The module is the folder `Modules/ui`; `UI.gbx` in it is the entry file.
The IDE only adds an `INCLUDE` line to your project; it never copies these
files. Add the module with the Modules dock or write:

```gbx
INCLUDE "modules/ui/UI.gbx"
```

## Quick start

```gbx
INCLUDE "modules/ui/UI.gbx"

SCREEN_SET 960, 640, 0
WINDOW_TITLE "Settings"

GLOBAL app AS UI
app.init()

GLOBAL win = app.window("Settings", 40, 40, 420, 360)
GLOBAL nameF = app.field("Player", 20, 56, 260, "Player one")
GLOBAL volume = app.slider("Volume", 20, 116, 260, 0, 100, 1, 70)
GLOBAL mute = app.toggle("Mute", 20, 172)
GLOBAL start = app.button("Start", 20, 300, 120, 36, 1)
app.endWindow()

WHILE TRUE
 SCREEN_CLEAR app.bg
 app.run()
 IF app.clicked(start) THEN STDOUT "start" + CHR$(10)
 IF KEY_PRESSED(KEY_ESC) THEN END
 SCREEN_SHOW
WEND
```

## Widgets

| Factory | Purpose |
| --- | --- |
| `window(title$, x, y, w, h)` | Draggable window with close button and client origin |
| `panel(title$, x, y, w, h)` | Flat group box |
| `label(text$, x, y, w = 0)` | Single line label; `w = 0` fits the text |
| `heading(text$, x, y, w = 0)` | Accent heading with underline |
| `separator(x, y, w)` | One pixel divider |
| `button(text$, x, y, w, h, primary = 0)` | Flat button, primary uses the accent |
| `toggle(text$, x, y, checked = 0)` | Checkbox-style toggle |
| `slider(title$, x, y, w, lo, hi, step, value)` | Numeric slider with value label |
| `progress(title$, x, y, w, value)` | 0..1 progress bar |
| `field(title$, x, y, w, text$ = "")` | Editable single-line text field |
| `dropdown(title$, x, y, w, options$, index = 0)` | Popup selection; options are `a|b|c` |
| `listbox(title$, x, y, w, h, options$, index = 0)` | Scrollable list |

Widgets created after a window belong to that window until `endWindow()`.
Coordinates are relative to the window's client area.

## Reading and changing state

```gbx
value = app.value(sliderId)     ' number
text$ = app.text$(fieldId)      ' text or selected option
IF app.clicked(buttonId) THEN ...
IF app.changed(dropdownId) THEN ...
app.setValue(sliderId, 50)
app.setText(labelId, "Ready")
app.setOptions(dropdownId, "Low|Normal|High")
app.enable(buttonId, 0)
app.openWin(windowId)
app.closeWin(windowId)
app.focusOn(fieldId)
```

## Keyboard and mouse

- `TAB` and `SHIFT+TAB` move the focus.
- `RETURN` or `SPACE` activates the focused button or toggle.
- Arrow keys change sliders and select list/dropdown entries.
- `HOME` and `END` jump to the first or last value.
- Typing edits the focused field; `RETURN` leaves it.
- Click a window title to drag it, click its X to close it.
- The wheel scrolls a hovered list.
- `ESC` is deliberately left to the game.

For scripted demos and headless tests, `app.vmouse(x, y, down)`,
`app.vkey(code)` and `app.vtext(s$)` feed virtual input. Set
`app.vm% = 0` to return to real input.

## Theme

`app.theme("dark")` and `app.theme("light")` switch the built-in palettes.
For a custom palette assign the fields directly:

```gbx
app.bg = RGB(10, 14, 20)
app.surface = RGB(20, 27, 38)
app.accent = RGB(56, 189, 248)
app.accentInk = RGB(4, 18, 28)
```

Also available: `surface2`, `titleBar`, `line`, `ink`, `inkDim`, `danger` and
`field`. Metrics `titleH`, `pad` and `rowH` control the geometry.

## Practical notes

- Create widgets once, before the frame loop.
- Call `app.run()` once per frame, after drawing your own background.
- One widget store exists per program.
- `app.clear()` removes every widget and starts a new screen.
- Floating dropdowns are drawn above all windows.
- The UI is pure GBX and works in the interpreter, a built EXE and HTML5.

---

Generated from `@help` blocks in the module's GBX sources.
