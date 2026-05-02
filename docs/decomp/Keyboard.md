# On-Screen Keyboard

CKeyboard at 0x00029d38, "Keyboard" at 0x00029d4c, FND at 0x00013da4.

## Properties

| Name | Type |
|------|------|
| keys | nodearray |
| frame | node |
| text | node |
| control | node |
| mode | integer |
| jmode | integer |
| shift | boolean |
| caps | boolean |
| string | string |

## Functions

selectKey, selectUp, selectDown, selectLeft, selectRight, activate, Backspace, Delete, CursorLeft, CursorRight, Shift, CycleMode, Insert.

## Script Callbacks

OnDone, OnError.

## Layout

Three western modes: alpha, symbol, accent, arranged in a 5x11 grid.

Three Japanese modes: hiragana, katakana, English, arranged in a 9x11 grid.

Maximum input length is 31 characters.
