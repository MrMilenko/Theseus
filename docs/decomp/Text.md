# Font & Text Rendering

Infrastructure layer; no XAP node class. This is not a script-accessible node type.

## Font Loading

Loads Xbox .xtf font files. A font table is built at startup by scanning the font directory. FindFont and GetFont map face name strings to loaded font file handles.

## Text Mesh Generation

Builds text meshes from glyph outlines by triangulating Bezier curves. Glyph data is cached with LRU eviction to keep memory usage bounded.

## Rendering Features

- Vertical fade (alpha gradient top-to-bottom)
- Alpha blending
- Multi-line layout
