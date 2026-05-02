# File Operations & Copy System

File copy and filesystem nodes in the 4920-5960 retail binary.

## Nodes

| Class | Address | FND String | Address |
|-------|---------|------------|---------|
| CCopyDestination | 0x00027878 | "CopyDestination" | 0x0002789c |
| CFile | 0x00022260 | "File" | (none) |
| CFolder | 0x000221fc | "Folder" | 0x0002220c |

CGameCopier is NOT a node (no class registration). It is a utility class used by the copy system, running file copy operations in a background thread.

## CCopyDestination

Memory unit / HDD destination picker for game save copying.

**Properties:** pod, podIcon, panelMU, panelMUHilite, panelText, panelTextHilite, console, memoryUnit, curDevUnit, selDevUnit, sourceDevUnit, spacing, isActive, select.

**Functions:** selectUp, selectDown.

## CFile

File metadata node.

**Properties:** name, type, path, length, date.

**Functions:** readText.

## CFolder

Directory listing node.

**Properties:** path, name, files, subFolders.

**Functions:** sortByName, sortByType, sortByDate, sortByLength.
