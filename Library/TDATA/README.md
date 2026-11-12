# TDATA

Title data. Each Xbox title gets a folder here named by its eight character title ID, holding the persistent state it writes through `T:\` (settings, caches and the like). On Xbox hardware the kernel maps `T:\` to `E:\TDATA\<current title ID>\` when a title launches. Saved games live next door in `UDATA`.

The folder is mostly empty in the repo. The dashboard's own state lives under `fffe0000`, the dashboard's title ID; see that subfolder's README. On desktop the live copy is in your user data folder's `Library/TDATA/`. On Xbox hardware your real `E:\TDATA\` is what gets read.
