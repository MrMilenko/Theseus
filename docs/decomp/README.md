# Decomp Index

This directory holds Theseus's reverse-engineering notes for the 5960 retail Xbox dashboard. The table below is the master cross-reference between the binary and the Theseus reconstruction; every row maps a function in the loaded XBE (by Ghidra label and address) to its corresponding implementation in the Theseus source tree (or marks it as reference-only for subsystems we have not yet implemented). Per-subsystem narrative documents linked in the Coverage Status table at the bottom carry the prose explanations of behavior.

The intent is auditability. Anyone with the same XBE loaded into Ghidra can search by address, compare what we call something against what they would call it, read a one-line description of the behavior, and then click through to the subsystem narrative for the longer story.

## How this table was built

- **Address**: hex offset in the 5960 retail dashboard XBE. Ghidra's default image base is at 0x00010000; addresses below that are XBE headers, addresses above are .text / .rdata / .data per the section table.
- **Ghidra name**: the symbol as labeled in the loaded project. Names were recovered the way binary RE always recovers names: by pulling apart every dashboard binary we could get our hands on and cherry-picking what each one volunteered. The 5960 retail XBE is the target and the last dashboard build Microsoft shipped; earlier revisions in the dashboard's history are the sources we cross-reference against. The principal techniques on the 5960 side are FND/PRD table extraction (the property and function descriptor blocks in the dashboard contain plaintext method names, parsed via custom Ghidra scripts) and XAP script verification (every name reachable from script lands in a known FND row). Roughly half of the binary's 4559 functions are named today.
- **Theseus name**: the corresponding implementation in the Theseus source tree. Empty when the binary function is reference-only. Marked `(reference only)` when we have decompiled the function for documentation but do not build it (for example, the original Microsoft DVD playback chain).
- **Subsystem**: per-area grouping. Links to the narrative doc for that subsystem.
- **Description**: one line, binary analysis only. No source-level commentary.

When in doubt about an entry, run the address through Ghidra and compare. The point of this table is to be checkable.

## Conventions

- Rows are sorted by address ascending (binary memory layout).
- The Ghidra column reflects the live label in the loaded project. If a Ghidra label changes (rename, refinement), that is a row update here.
- Theseus name uses `Class::method` form when the active source tree exposes that name. For free functions, the bare function name. For helpers that were folded into a larger method during reconstruction, the host method.
- Subsystem column links to the relevant narrative doc; if no doc exists yet, the cell is plain text and a doc is on the TODO list.
- XDK library code that is statically linked into the dashboard (D3DX, DirectSound mixer, MCpx audio drivers, AC97, kernel video drivers, CRT helpers) is intentionally outside the scope of this table. Those rows would describe Microsoft library code, not dashboard code.

## Functions

| Address | Ghidra name | Theseus name | Subsystem | Description |
|---------|-------------|--------------|-----------|-------------|
| 0x00013dac | `_classCInline` | `CInline::class` (scene_groups.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for `CInline`. Anchors the FND/PRD tables that script binds to. |
| 0x00013db4 | `_classCSwitch` | `CSwitch::class` (scene_groups.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for `CSwitch` (active-child selector). |
| 0x00013db8 | `_classCTextNode` | `CTextNode::class` (text.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the text-rendering node. |
| 0x000145cc | `_classCSpinner` | `CSpinner::class` (scene_groups.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the continuous-rotation node. |
| 0x000147c8 | `_classCMemoryMonitor` | `CMemoryMonitor::class` (memory_monitor.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the storage-device watcher node. |
| 0x00014b38 | `_classCLayout` | `CLayout::class` (scene_groups.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the radial layout container (used by the dashboard's main menu wheel). |
| 0x00014b3c | `_classCBillboard` | `CBillboard::class` (scene_groups.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the camera-facing quad node. |
| 0x0001a06c | `_classCXAppNode` | `CXAppNode::class` (main.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the application root node type. |
| 0x0001a098 | `_classCTimeDepNode` | `CTimeDepNode::class` (animation_nodes.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for time-dependent node base. |
| 0x0001a0f0 | `_classCArrayObject` | `CArrayObject::class` (xap_vm.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the script-visible Array type. |
| 0x0001a1b0 | `_classCDateObject` | `CDateObject::class` (date_node.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the script-visible Date type. |
| 0x0001a408 | `_classCFile` | `CFile::class` (file_ops.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the File node. |
| 0x0001a94c | `_classCVec3Object` | `CVec3Object::class` (xap_vm.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the script-visible Vec3 type. |
| 0x0001a990 | `_classCDiscDrive` | `CDiscDrive::class` (disc_management.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the disc-drive node. |
| 0x0001aa40 | `_classCConfig` | `CConfig::class` (config.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the dashboard config singleton. |
| 0x0001aef0 | `_classCRecovery` | (reference only) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the original CRecovery factory-recovery node. Theseus replaced it with `CReboot`. |
| 0x0001da7c | `_classCScreenSaver` | `CScreenSaver::class` (settings_nodes.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the screensaver node. |
| 0x0001db0c | `_classCJoystick` | `CJoystick::class` (input.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the joystick / controller input node. |
| 0x0001dbe0 | `_classCTimeSensor` | `CTimeSensor::class` (animation_nodes.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the VRML97 TimeSensor (animation clock). |
| 0x0001e198 | `_classCMusicCollection` | `CMusicCollection::class` (music_collection.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the music-collection node. |
| 0x0001e664 | `_classCSavedGameGrid` | `CSavedGameGrid::class` (savegame_grid.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the memory-panel save grid. |
| 0x0001eac0 | `_classCCopyDestination` | `CCopyDestination::class` (file_ops.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the copy-target picker node. |
| 0x0001eb64 | `_classCTransform` | `CTransform::class` (scene_groups.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the VRML97 Transform group node. |
| 0x0001ed58 | `_classCViewpoint` | `CViewpoint::class` (animation_nodes.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the VRML97 Viewpoint (camera) node. |
| 0x0001ed84 | `_classCScreen` | `CScreen::class` (screen.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the screen-management node. |
| 0x0001eddc | `_classCNavigationInfo` | `CNavigationInfo::class` (animation_nodes.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for VRML97 NavigationInfo (camera control mode). |
| 0x0001eed0 | `_classCCameraPath` | `CCameraPath::class` (camera.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the camera-spline node. |
| 0x0001efd8 | `_classCHud` | `CHud::class` (hud_node.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the heads-up overlay node. |
| 0x0001f004 | `_classCPicture` | `CPicture::class` (hud_node.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the textured-quad picture node. |
| 0x0001f300 | `_classCMaxMaterial` | `CMaxMaterial::class` (materials.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the 3ds Max material translator node. |
| 0x0001f33c | `_classCLayer` | `CLayer::class` (scene_groups.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the 2D-overlay layer node. |
| 0x0001f368 | `_classCKeyboard` | `CKeyboard::class` (keyboard_node.cpp) | [Scene Graph](Node.md) | Static `CNodeClass` registration for the on-screen keyboard node. |
| 0x000223e4 | `CMathClass_floor` | `CMathClass::floor` (math_node.cpp) | [Math](Math.md) | Script `Math.floor(x)`. Wraps the C runtime `floor`. |
| 0x00022408 | `CMathClass_pow` | `CMathClass::pow` (math_node.cpp) | [Math](Math.md) | Script `Math.pow(x, y)`. Wraps `pow`. |
| 0x00022410 | `CMathClass_random` | `CMathClass::random` (math_node.cpp) | [Math](Math.md) | Script `Math.random()`. Returns a `[0, 1)` float from the CRT PRNG. |
| 0x00022434 | `CMathClass_sqrt` | `CMathClass::sqrt` (math_node.cpp) | [Math](Math.md) | Script `Math.sqrt(x)`. Wraps `sqrt`. |
| 0x0002c570 | `CXApp::CXApp` | globals init in `main.cpp` | [App Shell](#app-shell) | Constructor for the original `CXApp` singleton. Theseus replaced this with module-scope globals and free functions; the binary class is preserved for reference. |
| 0x0002cc10 | `Xbox_Init` | `Xbox_Init` (main.cpp) | [App Shell](#app-shell) | Early-boot init: `XOnlineStartup`, drive-letter symlinks, `XSetFileCacheSize`, `XInitDevices`, region detection, network bring-up. Runs before scene graph creation. |
| 0x0002d0e0 | `CXApp::Draw` | main loop in `main.cpp` | [App Shell](#app-shell) | Per-frame render entry. Calls scene graph traversal then `Present`. |
| 0x0002d7e0 | `CXApp::InitApp` | `TheseusInit` in `main.cpp` | [App Shell](#app-shell) | Direct3D device creation, scene graph bootstrap, initial XIP load. |
| 0x0002d930 | `CXApp::CleanupApp` | `TheseusShutdown` in `main.cpp` | [App Shell](#app-shell) | Tear-down: scene graph destruction, D3D device release. |
| 0x0002da00 | `XApp` | (vtable / class registration) | [App Shell](#app-shell) | CXApp class-table thunk emitted by the compiler. Theseus's CTheseus equivalent was eliminated; the binary still has it. |
| 0x0002dbc0 | `CXApp::GetStartupClassFile` | startup logic in `main.cpp` | [App Shell](#app-shell) | Resolves the boot XAP file from config. The dashboard's first scene is loaded by string lookup against this path. |
| 0x0002dc50 | `CRecovery::StartRecovery` | `CReboot::Reboot` in `reboot_node.cpp` | [App Shell](#app-shell) | Original entry to factory recovery via `DashRecovery()`. Theseus replaced with `CReboot::Reboot()` returning to firmware; recovery as a concept does not apply on modded hardware. |
| 0x0002dc80 | `main` | `main` (main.cpp) | [App Shell](#app-shell) | C entry point. Calls `Xbox_Init`, constructs the global `CXApp` (`theApp`), enters the per-frame `Draw` loop. |
| 0x0002def0 | `XAppCreateFile` | `TheseusCreateFile` in `main.cpp` | [App Shell](#app-shell) | Wrapper around `CreateFileA` with dashboard-specific share/disposition defaults. |
| 0x0002ea20 | `CNodeClass::FindByName` | `CNodeClass::FindByName` in `node_system.cpp` | [Scene Graph](Node.md) | Walks the static class registration list (built from `_classCXxxx_CXxxx__2VCNodeClass__A` entries in .data) looking for a UTF-16 class name match. The XAP compiler uses this when binding `DEF` declarations. |
| 0x0002ee60 | `CXAppNode::Advance` | tick loop in `main.cpp` | [Scene Graph](Node.md) | Per-frame tick on the application root node. Drives the time-dependent node graph. |
| 0x00030570 | `CArrayElementReference::Deref` | `CArrayElementReference::Deref` (xap_vm.cpp) | [Script VM](VM.md) | Dereferences an `array[index]` LHS. Allocates the slot if missing for assignment paths. |
| 0x00030e70 | `CArrayObject::concat` | `CArrayObject::concat` (xap_vm.cpp) | [Script VM](VM.md) | Array concatenation builtin invoked from script via `Array.concat(...)`. |
| 0x00031040 | `CDateObject::Construct` | `CDateObject::Construct` (date_node.cpp) | [Script VM](VM.md) | Date object constructor. Initializes from current system time when called with no args, or parses string/numeric input. |
| 0x00031450 | `CDateObject::getDate` | `CDateObject::getDate` (date_node.cpp) | [Script VM](VM.md) | Returns the day-of-month component of the bound date. |
| 0x00031510 | `CDateObject::toGMTString` | `CDateObject::toGMTString` (date_node.cpp) | [Script VM](VM.md) | Formats date as RFC 1123 UTC string. |
| 0x000315c0 | `CDateObject::toLocaleString` | `CDateObject::toLocaleString` (date_node.cpp) | [Script VM](VM.md) | Formats date using the dashboard's current locale. |
| 0x00031660 | `CDateObject::toUTCString` | `CDateObject::toUTCString` (date_node.cpp) | [Script VM](VM.md) | Formats date in ISO 8601 UTC form. |
| 0x00031710 | `CDateObject::SetSystemClock` | `CDateObject::SetSystemClock` (date_node.cpp) | [Script VM](VM.md) | Writes the bound date back to the kernel via `NtSetSystemTime`. |
| 0x00031b50 | `CFolder::Refresh` | `CFolder::Refresh` (file_ops.cpp) | [File Ops](FileOps.md) | Rescans a Folder node's directory and rebuilds its child File / Folder array. Driven by FindFirstFile / FindNextFile. |
| 0x00032010 | `CFolder_sortByName` | `CFolder::sortByName` (file_ops.cpp) | [File Ops](FileOps.md) | FND-bound script method: re-sorts the folder's contents alphabetically by filename. |
| 0x00032040 | `CStrObject::CStrObject` | `CStrObject::CStrObject` (xap_vm.cpp) | [Script VM](VM.md) | String object default constructor. |
| 0x00032090 | `CStrObject::CStrObject` | `CStrObject::CStrObject` (xap_vm.cpp) | [Script VM](VM.md) | String object constructor from `wchar_t*` literal. |
| 0x00032130 | `CStrObject::CStrObject` | `CStrObject::CStrObject` (xap_vm.cpp) | [Script VM](VM.md) | String object copy constructor. |
| 0x000321f0 | `CNumObject::ToNum` | `CNumObject::ToNum` (xap_vm.cpp) | [Script VM](VM.md) | Number-object self coercion. Returns the bound `float` directly without conversion. |
| 0x00032850 | `CStrObject::toLowerCase` | `CStrObject::toLowerCase` (xap_vm.cpp) | [Script VM](VM.md) | Lowercases the bound UTF-16 string in place using `_wcslwr`. |
| 0x000328c0 | `CStrObject::toUpperCase` | `CStrObject::toUpperCase` (xap_vm.cpp) | [Script VM](VM.md) | Uppercases the bound UTF-16 string in place using `_wcsupr`. |
| 0x00032af0 | `CMathClass_abs` | `CMathClass::abs` (math_node.cpp) | [Math](Math.md) | Script `Math.abs(x)`. Returns the magnitude of a float. |
| 0x00033690 | `ExpandCString` | `ExpandCString` (string_util.cpp) | [Util](Util.md) | Resizes a `CString` buffer (the dashboard's growable wide-string container) to hold at least N additional characters. |
| 0x00034d80 | `CFunctionCompiler::ParseExpression` | `CFunctionCompiler::ParseExpression` (xap_compile.cpp) | [Script VM](VM.md) | Recursive-descent expression parser. Drives the operator-precedence DOPER table to emit operator opcodes. |
| 0x00034ea0 | `CFunctionCompiler::ParseIF` | `CFunctionCompiler::ParseIF` (xap_compile.cpp) | [Script VM](VM.md) | Compiles `if` / `if/else` to `opCond` + jump fixups. |
| 0x00035170 | `CFunctionCompiler::ParseDO` | `CFunctionCompiler::ParseDO` (xap_compile.cpp) | [Script VM](VM.md) | Compiles loop constructs (`while`, `do/while`, `for`) to `opCond` + back-edge `opJump`. |
| 0x00035540 | `CFunctionCompiler::ParseBREAK` | `CFunctionCompiler::ParseBREAK` (xap_compile.cpp) | [Script VM](VM.md) | Emits a forward `opJump` patched to the loop exit by the enclosing parse-loop. |
| 0x00035610 | `CFunctionCompiler::ParseCONTINUE` | `CFunctionCompiler::ParseCONTINUE` (xap_compile.cpp) | [Script VM](VM.md) | Emits a back-edge `opJump` to the loop condition. |
| 0x00035800 | `CFunctionCompiler::ParseBlock` | `CFunctionCompiler::ParseBlock` (xap_compile.cpp) | [Script VM](VM.md) | Wraps a brace-delimited statement list in `opFrame` / `opEndFrame` for local scope. |
| 0x00035960 | `CFunctionCompiler::ParseStatement` | `CFunctionCompiler::ParseStatement` (xap_compile.cpp) | [Script VM](VM.md) | Statement-level dispatcher. Routes by leading token to ParseIF / ParseDO / ParseBlock / etc. Emits `opStatement` line markers. |
| 0x00035ec0 | `CClassCompiler::CompileNode` | `CClassCompiler::CompileNode` (xap_compile.cpp) | [Script VM](VM.md) | VRML97 node body compiler. Walks `DEF` / `USE` / property assignments and emits scene graph opcodes. |
| 0x00036440 | `CClassCompiler::CompileDot` | `CClassCompiler::CompileDot` (xap_compile.cpp) | [Script VM](VM.md) | Compiles dotted property access (`a.b.c`). Emits `opDot` chain. |
| 0x00036f70 | `CRunner::FetchString` | `CRunner::FetchString` (xap_vm.cpp) | [Script VM](VM.md) | Reads an inline UTF-16 string from the bytecode stream. Used by `opStr`, `opVar`, and node-name opcodes. |
| 0x000373a0 | `CLocalVariable::Assign` | `CLocalVariable::Assign` (xap_vm.cpp) | [Script VM](VM.md) | Assignment handler for stack-frame locals. Stores the right-hand value into the local slot and updates refcounts. |
| 0x00037510 | `CRunner::Push` | `CRunner::Push` (xap_vm.cpp) | [Script VM](VM.md) | Pushes a `CObject*` onto the VM evaluation stack. Refcounts the pushed object. |
| 0x00037800 | `CObject::Call` | `CObject::Call` (xap_vm.cpp) | [Script VM](VM.md) | Method dispatch. Looks up the FND entry by name on the receiver's class, dereferences arguments off the script stack, and invokes the native handler via signature-typed switch (`sig_vv`, `sig_iv`, etc.). |
| 0x00038620 | `CRunner::ExecuteBuiltIn` | `CRunner::ExecuteBuiltIn` (xap_vm.cpp) | [Script VM](VM.md) | Global builtin dispatcher. Switches by argument-name length and `wcsncmp`s against `EnableInput`, `eval`, `launch`, `alert`, `log`, `DebugBreak`. |
| 0x000390f0 | `CVec3Object::ToStr` | `CVec3Object::ToStr` (xap_vm.cpp) | [Script VM](VM.md) | Formats a 3-component vector as `"x y z"` for script string coercion. |
| 0x0003a260 | `CDiscDrive_getTrackCount` | `CDiscDrive::getTrackCount` (disc_management.cpp) | [Disc](#disc) | Returns the number of audio tracks on the inserted CD. Used by the soundtrack import flow. |
| 0x0003a680 | `CConfig::GetAVPackType` | `CConfig::GetAVPackType` (config.cpp) | [Settings](Settings.md) | Reads the AV pack type (composite, S-Video, component, HDMI-via-pack) from the kernel via `XGetAVPack`. |
| 0x0003a740 | `CConfig::GetAVRegion` | `CConfig::GetAVRegion` (config.cpp) | [Settings](Settings.md) | Returns the configured analog video region (NTSC, PAL, etc.). |
| 0x0003a7e0 | `CConfig::GetGameRegion` | `CConfig::GetGameRegion` (config.cpp) | [Settings](Settings.md) | Returns the console's region code (NA / JP / EU). Used to gate region-locked game launches in scripts. |
| 0x0003a860 | `CConfig_GetPAL60Support` | `CConfig::GetPAL60Support` (config.cpp) | [Settings](Settings.md) | Returns whether the console / TV supports PAL60 mode. |
| 0x0003ab30 | `CConfig_GetLaunchParameter1` | `CConfig::GetLaunchParameter1` (config.cpp) | [Settings](Settings.md) | Returns the first parameter passed by the previous title to the dashboard via `XSetLaunchData`. |
| 0x0003af80 | `CConfig_GetVideoMode` | `CConfig::GetVideoMode` (config.cpp) | [Settings](Settings.md) | Returns the active video mode (480i/p, 720p, 1080i) selected in dashboard settings. |
| 0x0003b2d0 | `CConfig::GetLaunchReason` | `CConfig::GetLaunchReason` (config.cpp) | [Settings](Settings.md) | Reports why the dashboard was booted (cold boot, returned-to-dash, eject, error). |
| 0x0003b420 | `CConfig_GetFontVersion` | `CConfig::GetFontVersion` (config.cpp) | [Settings](Settings.md) | Returns the version of the loaded XTF font set. |
| 0x0003b860 | `CConfig::GetXdashVersion` | `CConfig::GetXdashVersion` (config.cpp) | [Settings](Settings.md) | Returns the dashboard build version string. |
| 0x0003b9d0 | `CConfig_GetDSTAllowed` | `CConfig::GetDSTAllowed` (config.cpp) | [Settings](Settings.md) | Returns whether daylight savings observance is enabled in the eeprom. |
| 0x0003b9e0 | `CConfig_GetEthernetLinkStatus` | `CConfig::GetEthernetLinkStatus` (config.cpp) | [Settings](Settings.md) | Polls the ethernet PHY for link state. Drives the network-connected indicator on the settings panel. |
| 0x0003baf0 | `CRecovery::RecoveryThread` | (reference only) | [App Shell](#app-shell) | Worker thread that performs the original factory-recovery wipe sequence. Theseus does not exercise this on modded hardware. |
| 0x0003bf00 | `CSettingsFile::Save` | `CSettingsFile::Save` (settingsfile.cpp) | [Settings](Settings.md) | Serializes the in-memory settings tree to disk. Driven by the settings panel's "Save" action. |
| 0x0003ca10 | `CTranslator::TranslateStripColon` | `CTranslator::TranslateStripColon` (locale_node.cpp) | [Locale](Locale.md) | Looks up a localized string by key, then strips a trailing `:` if present. Used for menu labels that need both punctuated and bare forms. |
| 0x0003d390 | `FUN_0003d390` | `CXipFile::ReadIndexBuffer` (xip_archive.cpp) | [XIP Archives](XIP.md) | Builds an index buffer resource by hand from `XMemAlloc`, retrying through the texture and mesh caches when memory runs out. |
| 0x0003d410 | `FUN_0003d410` | `CXipFile::ReadVertexBuffer` (xip_archive.cpp) | [XIP Archives](XIP.md) | Same for a vertex buffer: a 12 byte resource header plus contiguous memory for the vertices. |
| 0x0003d4a0 | `CFileBuffer_FreeBuffer` | `CFileBuffer::SetFile` (xip_archive.cpp) | [XIP Archives](XIP.md) | Takes the file handle and finds the archive's entry in the signed `XIPS` hash section. Theseus keeps the handle only. |
| 0x0003d570 | `CFileBuffer_VerifyXipSignature` | (not built) | [XIP Archives](XIP.md) | Hashes one 64 KB block and compares it with the signed table; a mismatch reboots. Theseus loads unsigned archives. |
| 0x0003d5e0 | `CFileBuffer_Seek` | `CFileBuffer::Seek` (xip_archive.cpp) | [XIP Archives](XIP.md) | Loads the 64 KB block holding a file offset and points the read index into it. |
| 0x0003d710 | `CFileBuffer_Read` | `CFileBuffer::Read` (xip_archive.cpp) | [XIP Archives](XIP.md) | Copies out of the block buffer and refills it a block at a time. A failed `ReadFile` reboots. |
| 0x0003d900 | `LoadXIP` | `LoadXIP` (xip_archive.cpp) | [XIP Archives](XIP.md) | Finds an archive in the 20 slot pool by path or opens the next slot, then loads it on a thread or inline. |
| 0x0003dab0 | `FindInXIPAndDetach` | `FindInXIPAndDetach` (xip_archive.cpp) | [XIP Archives](XIP.md) | Finds a raw entry, newest archive first, and hands its bytes to the caller. |
| 0x0003dba0 | `FindObjectInXIP` | `FindObjectInXIP` (xip_archive.cpp) | [XIP Archives](XIP.md) | Searches every loaded archive, newest first; textures asked of the default archive are looked up as `.xbx`. |
| 0x0003dd20 | `CXipFile_dtor` | `CXipFile::~CXipFile` (xip_archive.cpp) | [XIP Archives](XIP.md) | Frees the tables and names, releases the mesh buffers, frees the block buffer. |
| 0x0003ddf0 | `CXipFile_Open` | `CXipFile::Open` (xip_archive.cpp) | [XIP Archives](XIP.md) | Stores the file name and the lookup key: the path without its extension, with `default` dropped. |
| 0x0003dec0 | `CXipFile_StartLoadThread` | `CXipFile::StartLoadThread` (xip_archive.cpp) | [XIP Archives](XIP.md) | Thread entry: reloads the mesh buffers if a reload is pending, otherwise loads the archive. |
| 0x0003def0 | `CXipFile_Load` | `CXipFile::Load` (xip_archive.cpp) | [XIP Archives](XIP.md) | Opens the file unbuffered, reads the header, tables and name pool, then every entry. |
| 0x0003e030 | `FUN_0003e030` | `ReadTexture` (xip_archive.cpp) | [XIP Archives](XIP.md) | Turns an XPR0 entry into a texture: copies the resource header and moves the pixels to contiguous memory. |
| 0x0003e110 | `CXipFile_CreateObjects` | `CXipFile::CreateObjects` (xip_archive.cpp) | [XIP Archives](XIP.md) | Reads every entry in file order into raw memory, a texture, a mesh reference or a mesh buffer. |
| 0x0003e270 | `LAB_0003e270` | `XipCompareName` (xip_archive.cpp) | [XIP Archives](XIP.md) | `bsearch` compare: `_stricmp` of the wanted name against a directory entry's name. |
| 0x0003e290 | `CXipFile_Find` | `CXipFile::Find` (xip_archive.cpp) | [XIP Archives](XIP.md) | Checks the path starts with this archive's key, then binary searches the sorted directory. |
| 0x0003e320 | `CXipFile_FindObject` | `CXipFile::FindObject` (xip_archive.cpp) | [XIP Archives](XIP.md) | `Find` plus an optional type check; a texture gets an `AddRef` for the caller. |
| 0x0003e390 | `CXipFile_DeleteMeshBuffers` | `CXipFile::DeleteMeshBuffers` (xip_archive.cpp) | [XIP Archives](XIP.md) | Releases all ten vertex and index buffers. |
| 0x0003e440 | `CleanupMeshCache` | `CleanupMeshCache` (xip_archive.cpp) | [XIP Archives](XIP.md) | Under memory pressure, frees the mesh buffers of the archive drawn longest ago. |
| 0x0003e5f0 | `CXipFile_ReadIndexBuffer` | `CXipFile::ReadIndexBuffer` (xip_archive.cpp) | [XIP Archives](XIP.md) | Reads an index buffer entry into a new buffer. |
| 0x0003e650 | `CXipFile_ReadVertexBuffer` | `CXipFile::ReadVertexBuffer` (xip_archive.cpp) | [XIP Archives](XIP.md) | Reads the vertex count and FVF, then the vertices into a new buffer. |
| 0x0003e710 | `CXipFile_ReloadMeshBuffers` | `CXipFile::ReloadMeshBuffers` (xip_archive.cpp) | [XIP Archives](XIP.md) | Seeks back to every mesh buffer entry and reads it again after an eviction. |
| 0x0003e800 | `CMeshRef_Render` | `CXipFile::Reload` (xip_archive.cpp) | [XIP Archives](XIP.md) | Mislabeled in the project: flags a reload and starts the loader thread, running it inline if no thread starts. |
| 0x0003e850 | (no function) | `CMeshRef::Render` (xip_archive.cpp) | [XIP Archives](XIP.md) | CMeshRef vtable slot 0: reloads evicted buffers, sets the shader, draws the slice. |
| 0x0003ead0 | (no function) | `CMeshRef::GetFVF` (xip_archive.cpp) | [XIP Archives](XIP.md) | CMeshRef vtable slot 1: the FVF of the buffer the reference draws from. |
| 0x0003f420 | `CGameCopier::CreateDirectoryA` | `CGameCopier::CreateDirectory` (file_ops.cpp) | [File Ops](FileOps.md) | Wrapper around the kernel's CreateDirectory with progress-reporting hooks for the copy UI. |
| 0x0003f700 | `CGameCopier::CopyFileA` | `CGameCopier::CopyFile` (file_ops.cpp) | [File Ops](FileOps.md) | Buffered file copy with progress callback. Used by save-to-MU and game-copy flows. |
| 0x0003f830 | `CGameCopier::DeleteFileA` | `CGameCopier::DeleteFile` (file_ops.cpp) | [File Ops](FileOps.md) | DeleteFile wrapper. |
| 0x0003f8a0 | `CGameCopier::DeleteDirectory` | `CGameCopier::DeleteDirectory` (file_ops.cpp) | [File Ops](FileOps.md) | Recursive directory removal with progress callback. |
| 0x000401b0 | `CScreenSaver::Advance` | `CScreenSaver::Advance` (settings_nodes.cpp) | [Settings](Settings.md) | Per-frame tick on the screensaver node. Counts down the idle timer and toggles the saver layer when threshold is reached. |
| 0x000402a0 | `CScreenSaver::reset` | `CScreenSaver::reset` (settings_nodes.cpp) | [Settings](Settings.md) | Resets the idle timer to zero. Called on any input event. |
| 0x00040320 | `CObject::ToNum` | `CObject::ToNum` (xap_vm.cpp) | [Script VM](VM.md) | Default numeric coercion for `CObject` subclasses. Subclasses override; base returns 0. |
| 0x00040380 | `CObject::OnSetProperty` | `CObject::OnSetProperty` (xap_vm.cpp) | [Script VM](VM.md) | Property-write hook. Called after `opAssign` lands a value on a property; subclasses override to react to property changes. |
| 0x00040990 | `CTimeDepNode::~CTimeDepNode` | `CTimeDepNode::~CTimeDepNode` (animation_nodes.cpp) | [Animation](AnimationNodes.md) | Destructor for time-dependent nodes (TimeSensor and friends). Unlinks from the time-dep list. |
| 0x00040b70 | `CJoystick_EnableGlobalInput` | `CJoystick::EnableGlobalInput` (input.cpp) | [Input](#input) | Toggles dashboard-wide gamepad input. Used by modal scenes (alerts, panic) to suspend or restore controller events. |
| 0x0004147c | `CObject::Deref` | `CObject::Deref` (xap_vm.cpp) | [Script VM](VM.md) | Dereferences a script value. For `CVarObject` resolves to the bound target; for primitive objects returns `this`. |
| 0x000417f8 | `CTimeDepNode::OnSetProperty` | `CTimeDepNode::OnSetProperty` (animation_nodes.cpp) | [Animation](AnimationNodes.md) | Reacts to property writes on time-dep nodes. Re-registers the node with the time-dep list when its `enabled` toggles. |
| 0x00041950 | `CObject::Assign` | `CObject::Assign` (xap_vm.cpp) | [Script VM](VM.md) | Default assignment handler. Called by `opAssign` when LHS is a script-visible property. |
| 0x000419e0 | `CSettings_GetValue` | `CSettings::GetValue` (settings_nodes.cpp) | [Settings](Settings.md) | FND-bound script accessor: returns the value of a settings entry by name. |
| 0x00041f20 | `FUN_00041f20` | `CTitleArray::AddTitle` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Reads `<udata>\<titleID>\TitleMeta.xbx`, extracts `TitleName` for the active language code (or falls back to a translated `"Broken Game"`), then grows the title array by 50 entries if at capacity (`m_titleCount == m_titleCapacity`), and copies the title ID + name into the new slot. Identified by the path-build + INI read + `+= 50` capacity grow + array memcpy pattern. |
| 0x00041c90 | `CTimeSensor::Advance` | `CTimeSensor::Advance` (animation_nodes.cpp) | [Animation](AnimationNodes.md) | Per-frame tick. Computes fractional progress through the sensor's cycle interval and routes that into the bound interpolators. |
| 0x00041d80 | `CObject::ToStr` | `CObject::ToStr` (xap_vm.cpp) | [Script VM](VM.md) | Default string coercion. Returns class name; subclasses override for meaningful output. |
| 0x00041d90 | `CObject::_vtable_default` | `CObject::_vtable_default` (xap_vm.cpp) | [Script VM](VM.md) | The default vtable instance shared by base `CObject` instances. |
| 0x00042230 | `FUN_00042230` | `CTitleArray::Update` (title_collection.cpp) | [Title Collection](TitleCollection.md) | The boot-time title enumerator. Enters `m_RootLock`, walks `<udata>\*.*` via FindFirstFile/FindNextFile, calls AddTitle for each non-`fffe0000` directory, qsorts by name, then ScanSavedGames per title. Identified by the signature match (critical section + FindFirstFile loop + qsort). |
| 0x00042740 | `CSavedGameGrid::GetSavedGameCount` | `CSavedGameGrid::GetSavedGameCount` (savegame_grid.cpp) | [Save Games](#save-games) | Returns the total count of saves visible across all attached storage devices, summed from the title array. |
| 0x000428d0 | `FUN_000428d0` |  | [Title Collection](TitleCollection.md) | Loads / counts DLC entries for a title. Allocates a 0x1000-entry array of stride-0x34 records and iterates `C:\tdata\<titleid>\$c\` via the FUN_00043730 enumerator. 5960-only DLC content management; no Theseus equivalent. |
| 0x00042b40 | `GetXboxLiveAccountsCount_internal` | `GetXboxLiveAccountsCount_internal` (savegame_grid.cpp) | [Save Games](#save-games) | Helper that counts Xbox Live accounts visible on the current device. Backs the Live tab on the memory grid. |
| 0x00042c20 | `FUN_00042c20` | `CTitleArray::FindTitle` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Returns the index of a title by ID, or -1. Linear `__wcsicmp` scan over the title array (stride 0x28). Identified by signature match. |
| 0x00042cb0 | `FUN_00042cb0` | `CTitleArray::AddSavedGame` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Adds a save record `(szTitleID, szDirName, FILETIME saveTime)` to the title array. Calls `FindTitle`; if missing calls `AddTitle` + qsort + re-FindTitle. Then resizes the save array to count+1, calls `InitSave`, qsorts, and invalidates the title's block caches. |
| 0x00042e40 | `CTitleArray::RemoveTitle` | `CTitleArray::RemoveTitle` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Removes a title entry from the global title array (called when a title is uninstalled or the storage holding it is removed). |
| 0x00042ef0 | `FUN_00042ef0` | `CTitleArray::RemoveSavedGame` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Removes the Nth save (stride 0x30) from the Mth title via memmove + count decrement, then invalidates the title's block cache (offsets +0x10, +0x20). First overload, taking `(int nTitle, int nSavedGame)`. Identified by signature match. |
| 0x00042f40 | `FUN_00042f40` | `CTitleArray::RemoveSavedGame` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Second overload taking `(const TCHAR* szTitleID, const TCHAR* szDirName)`. Calls FindTitle, iterates saves by directory name, then memmove-removes. Identified by signature match. |
| 0x00042ff0 | `FUN_00042ff0` |  | [Title Collection](TitleCollection.md) | Parallel of `RemoveSavedGame` but operates on a stride-0x34 array (DLC) instead of stride-0x30 (saves). 5960 DLC content management; no Theseus equivalent. |
| 0x00043150 | `GetSavedGameID_internal` | `CTitleArray::GetSavedGameID` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Returns the directory-name string for the Nth save under the Mth title. The grid-side `CSavedGameGrid::GetSavedGameID2` wrapper trampolines into this on the active device. |
| 0x00043390 | `FUN_00043390` | `CTitleArray::GetTitleTotalBlocks` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Computes total block count for a title (cached at title-struct offset +0x20). Calls a path-walking enumerator and aggregates results. Identified by the `wcscmp(... L"fffe0000")` early-out and the +0x20 cache invalidation matching `RemoveSavedGame`. |
| 0x00043460 | `FUN_00043460` |  | [Title Collection](TitleCollection.md) | Internal path-build helper used by `GetTitleTotalBlocks`: appends a name to a wstring path with a backslash separator, then calls a directory walker. No direct Theseus equivalent. |
| 0x000434f0 | `FUN_000434f0` |  | [Title Collection](TitleCollection.md) | Save-game directory enumerator. Walks a `<titlepath>\$s\` directory tallying entry sizes. Parallel to FUN_00043730 (DLC enumerator). Inlined / folded into Theseus's `CTitleArray::Update`. |
| 0x00043730 | `FUN_00043730` |  | [Title Collection](TitleCollection.md) | DLC directory enumerator. Walks `<titlepath>\$c\` to count and size DLC entries. 5960-only DLC content management; folded into Theseus's `CSavedGameGrid::ResolveDLCFolder` flow. |
| 0x00043ae0 | `FUN_00043ae0` |  | [Title Collection](TitleCollection.md) | Drive-letter to device-unit translator (`'F'`/`'f'`->0, ... `'C'`/`'c'`->8). Used by every CTitleArray method that branches on the active drive. Inlined in Theseus. |
| 0x00043c50 | `FUN_00043c50` | `CTitleArray::IsDirty` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Returns 1 if both `m_root[0]` is set AND `m_dirty` is set; 0 otherwise. Two-condition test matches `IsDirty`'s body, not `IsValid`'s single-condition test. |
| 0x00044290 | `FUN_00044290` |  | [Live Accounts](#live-accounts) | Removes ALL Xbox Live accounts from HDD by looping `_XOnlineRemoveUserFromHD` over each loaded user, then invalidating the cache (`this+0x924 = -1`). Not exposed in Theseus; kept here as a binary anchor for the unported bulk-delete flow. |
| 0x000442f0 | `FUN_000442f0` |  | [Live Accounts](#live-accounts) | Removes a single Xbox Live account from HDD. Per-user inner of `FUN_00044290`. Calls `_XOnlineRemoveUserFromHD` once and invalidates the cache. |
| 0x000432f0 | `CFont::Open` | `CFont::Open` (text.cpp) | [Text](Text.md) | Loads an XTF font file from a XIP and constructs the glyph cache. |
| 0x00044240 | `CTitleArray::IsPublisherExists` | `CTitleArray::IsPublisherExists` (title_collection.cpp) | [Title Collection](TitleCollection.md) | Tests whether any title in the array shares a given publisher ID. Used by save-game grouping. |
| 0x000463f0 | `CMusicCollection_GetSoundtrackCount` | `CMusicCollection::DeleteSoundtrack` (music_collection.cpp, stubbed) | [Music](#music) | Despite the Ghidra label, this trampolines into a delete-shaped helper at 0x00046320 (bounds check, close handle, memmove + count decrement). The Ghidra label appears to be a Version Tracking misalignment. Theseus stubs `DeleteSoundtrack` to no-op. |
| 0x00046020 | `FUN_00046020` | `CMusicCollection::FormatSoundtrackSongTime` (music_collection.cpp) | [Music](#music) | Returns a `MM:SS` formatted song-length string. Calls `XGetSoundtrackSongInfo`, divides ms-length by 1000, formats minutes / seconds, wraps in a new `CStrObject`. |
| 0x00046120 | `FUN_00046120` | `CMusicCollection::AddSoundtrack` (music_collection.cpp, stubbed) | [Music](#music) | Computes the 20-byte XC digest of the soundtrack name, compares it against a 40-byte two-entry key table for system-soundtrack admission control, then dispatches to `DashAddSoundtrack`. Theseus stubs this to `return -1`. Identified by the XCCalcDigest call shape and the digest-table compare. |
| 0x00046320 | `FUN_00046320` |  | [Music](#music) | Free-function `::DeleteSoundtrack(int nSoundtrack)` worker. Bounds-checks against `c_nSoundtrackCount`, calls `DashBeginUsingSoundtracks` and `DashDeleteSoundtrack`, then on success closes the dash handle, decrements the count, and memmoves the soundtrack array (stride 0x4c). The wrapper at 0x000463f0 is what FND-binds this. |
| 0x00046390 | `FUN_00046390` |  | [Music](#music) | Free-function `::DeleteAllSoundtracks()`. Calls `DashBeginUsingSoundtracks`, loops `DashDeleteSoundtrack` over every entry in `c_rgsoundtrack`, closes the dash handle, zeroes `c_nSoundtrackCount`. |
| 0x00046400 | `FUN_00046400` | `CMusicCollection::ClearCopyList` (music_collection.cpp, stubbed) | [Music](#music) | Frees the existing `m_rgCopySong` buffer and reallocates `nSongCount * sizeof(CCopySong)` (stride 0x18). Resets `m_nCopySongAlloc` and zeroes `m_nCopySongCount`. Theseus stubs to no-op. |
| 0x00046830 | `FUN_00046830` |  | [Music](#music) | `CMusicCollection::ThreadProc` worker. Iterates the copy-song queue (`m_rgCopySong`), reads each song's name via `XGetSoundtrackSongInfo`, hands off to FUN_000468e0 (per-song copy). Theseus does not implement the music copy thread. |
| 0x00046660 | `CMusicCollection::GetUpdateString` | `CMusicCollection::GetUpdateString` (music_collection.cpp) | [Music](#music) | Builds the localized "Updating soundtracks..." progress string for the music panel. |
| 0x00047520 | `~CSavedGameGrid` | `CSavedGameGrid::~CSavedGameGrid` (savegame_grid.cpp) | [Save Games](#save-games) | Destructor. Frees per-title cached data and unhooks the device-change watcher. |
| 0x000476f0 | `CSavedGameGrid_GetTitleCount` | `CSavedGameGrid::GetTitleCount` (savegame_grid.cpp) | [Save Games](#save-games) | Returns the count of distinct titles that have at least one save / DLC / Live record on the current device. |
| 0x000478d0 | `CSavedGameGrid_GetTitleID` | `CSavedGameGrid::GetTitleID` (savegame_grid.cpp) | [Save Games](#save-games) | Returns the 8-character hex title ID at a given grid index. |
| 0x00047970 | `CSavedGameGrid::GetTitleName2` | `CSavedGameGrid::GetTitleName2` (savegame_grid.cpp) | [Save Games](#save-games) | Returns the display name for a title at a given grid index, falling back to the cached `TitleNames.ini` lookup if the live cert is unavailable. |
| 0x00047ab0 | `CSavedGameGrid_GetTitleName` | `CSavedGameGrid::GetTitleName` (savegame_grid.cpp) | [Save Games](#save-games) | FND-bound title-name accessor exposed to scripts. Wraps `GetTitleName2`. |
| 0x00048450 | `CSavedGameGrid::GetUpdateString` | `CSavedGameGrid::GetUpdateString` (savegame_grid.cpp) | [Save Games](#save-games) | Builds the "Updating saved games..." progress string for the memory panel. |
| 0x00048690 | `CSavedGameGrid::RenderIconRow` | `CSavedGameGrid::RenderIconRow` (savegame_grid.cpp) | [Save Games](#save-games) | Renders one row of save tiles. Resolves icons from each title's UDATA `TitleImage.xbx` or DLC's `ContentMeta.xbx`. |
| 0x00049590 | `CSavedGameGrid_selectUp` | `CSavedGameGrid::selectUp` (savegame_grid.cpp) | [Save Games](#save-games) | Up-arrow handler on the grid. Walks selection upward with row wrap. |
| 0x00049650 | `CSavedGameGrid_selectDown` | `CSavedGameGrid::selectDown` (savegame_grid.cpp) | [Save Games](#save-games) | Down-arrow handler on the grid. |
| 0x000497b0 | `CSavedGameGrid_selectRight` | `CSavedGameGrid::selectRight` (savegame_grid.cpp) | [Save Games](#save-games) | Right-arrow handler on the grid. Page-flips when at column boundary. |
| 0x00049840 | `CSavedGameGrid_setSelImage` | `CSavedGameGrid::setSelImage` (savegame_grid.cpp) | [Save Games](#save-games) | Updates the selection-highlight texture for the currently focused tile. |
| 0x00049940 | `CSavedGameGrid_FormatGridItemName` | `CSavedGameGrid::FormatGridItemName` (savegame_grid.cpp) | [Save Games](#save-games) | Formats the display name for any grid item (save / DLC / Live account). |
| 0x00049ca0 | `CSavedGameGrid::FormatSavedGameTime` | `CSavedGameGrid::FormatSavedGameTime` (savegame_grid.cpp) | [Save Games](#save-games) | Formats a save's last-modified `FILETIME` into a localized date string for the tile subtitle. |
| 0x00049eb0 | `CSavedGameGrid_GetXboxLiveAccountsCount` | `CSavedGameGrid::GetXboxLiveAccountsCount` (savegame_grid.cpp) | [Save Games](#save-games) | FND-bound version of the Live accounts count, exposed to scripts as a property on the grid. |
| 0x00049ed0 | `CSavedGameGrid_GetGridItemCount` | `CSavedGameGrid::GetGridItemCount` (savegame_grid.cpp) | [Save Games](#save-games) | Total count of grid tiles for the current title (saves + DLC + Live accounts). Drives layout math. |
| 0x00049f70 | `CSavedGameGrid_FormatGridItemSize` | `CSavedGameGrid::FormatGridItemSize` (savegame_grid.cpp) | [Save Games](#save-games) | Formats a grid item's storage footprint as a localized "X blocks" string. |
| 0x0004a3e0 | `CSavedGameGrid_FormatTitleSize` | `CSavedGameGrid::FormatTitleSize` (savegame_grid.cpp) | [Save Games](#save-games) | Formats a title's total footprint (sum across all of its saves and DLC). |
| 0x0004a700 | `CSavedGameGrid_GetSavedGamePath` | `CSavedGameGrid::GetSavedGamePath` (savegame_grid.cpp) | [Save Games](#save-games) | Returns the absolute path on the current device for the save at index N. |
| 0x0004a820 | `CSavedGameGrid_FormatFreeBlocks` | `CSavedGameGrid::FormatFreeBlocks` (savegame_grid.cpp) | [Save Games](#save-games) | Formats the device's free-block count for the memory panel header. |
| 0x0004a8d0 | `CSavedGameGrid_FormatTotalBlocks` | `CSavedGameGrid::FormatTotalBlocks` (savegame_grid.cpp) | [Save Games](#save-games) | Formats the device's total-block count for the memory panel header. |
| 0x0004a9c0 | `CHardDrive_GetTotalBlocks` |  | [File Ops](FileOps.md) | Returns the total block count of the HDD partition currently selected by the memory panel. |
| 0x0004b1d0 | `CSavedGameGrid::DeleteThread` | `CSavedGameGrid::DeleteThread` (savegame_grid.cpp) | [Save Games](#save-games) | Worker thread that performs the actual save-game delete (file removal + UDATA cleanup) off the UI thread. |
| 0x0004b630 | `CSavedGameGrid::StartDelete` | `CSavedGameGrid::StartDelete` (savegame_grid.cpp) | [Save Games](#save-games) | Kicks off a save-game delete operation. The confirm submenu calls this; UI bug noted in known issues prevents the action from actually firing. |
| 0x0004b6b0 | `CSavedGameGrid_CanCopy` | `CSavedGameGrid::CanCopy` (savegame_grid.cpp) | [Save Games](#save-games) | Returns true if the focused grid item is copyable (saves yes, DLC no, Live account stubbed). |
| 0x0004b830 | `CSavedGameGrid_IsSavedGameSelected` | `CSavedGameGrid::IsSavedGameSelected` (savegame_grid.cpp) | [Save Games](#save-games) | Tests whether the focused tile is a save (versus DLC or Live account). Drives save-only menu actions. |
| 0x0004ba90 | `CSavedGameGrid_IsXboxLiveAccountSelected` | `CSavedGameGrid::IsXboxLiveAccountSelected` (savegame_grid.cpp) | [Save Games](#save-games) | Tests whether the currently focused tile is a Live account row (versus a save or DLC row). Drives Live-specific menu actions. |
| 0x0004bab0 | `CSavedGameGrid_StartXboxLiveAccountCopy` | `CSavedGameGrid::StartXboxLiveAccountCopy` (savegame_grid.cpp) | [Save Games](#save-games) | Original entry to the move-Live-account-between-MUs flow. Theseus stubs this to fire `OnCopyError` immediately; XAP UI is preserved. |
| 0x0004bf50 | `CMemoryMonitor::UpdateValidDeviceFlags` | `CMemoryMonitor::UpdateValidDeviceFlags` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Polls `XGetDevices` and refreshes the bitmask of attached storage (HDD, MU0, MU1, MU2, MU3). |
| 0x0004c3b0 | `CMemoryMonitor::GetTotalAndFreeBlocks` | `CMemoryMonitor::GetTotalAndFreeBlocks` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Returns total + free blocks on a given device for the bar-chart display. |
| 0x0004c480 | `CMemoryMonitor::Advance` | `CMemoryMonitor::Advance` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Per-frame tick. Triggers re-enumeration on insert / eject events. |
| 0x0004c840 | `CMemoryMonitor_FormatDeviceName` | `CMemoryMonitor::FormatDeviceName` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | First format helper for a device's display label. Builds the bare-bones name without the user-set MU label. |
| 0x0004c8c0 | `CMemoryMonitor::FormatDeviceName2` | `CMemoryMonitor::FormatDeviceName2` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Builds a device's display label including the user-set MU name when available. |
| 0x0004c930 | `CMemoryMonitor_FormatTotalSlots` | `CMemoryMonitor::FormatTotalSlots` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Formats the device's total save-slot capacity (titles can claim per-save slots, distinct from raw blocks). |
| 0x0004c9d0 | `CMemoryMonitor_FormatFreeSlots` | `CMemoryMonitor::FormatFreeSlots` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Formats the device's remaining save-slot count. |
| 0x0004ca90 | `CMemoryMonitor_FormatFreeBlocks` | `CMemoryMonitor::FormatFreeBlocks` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Formats remaining free blocks for a single device. |
| 0x0004cb90 | `CMemoryMonitor_GetTotalFreeBlocks` | `CMemoryMonitor::GetTotalFreeBlocks` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | FND-bound: total free blocks across all attached devices. Drives the global "free space" readout. |
| 0x0004cbc0 | `CMemoryMonitor_FormatTotalBlocks` | `CMemoryMonitor::FormatTotalBlocks` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Formats total blocks for a single device. |
| 0x0004ccd0 | `CMemoryMonitor::SetMUName` | `CMemoryMonitor::SetMUName` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Renames a memory unit. Writes the new name to the unit's metadata block via the kernel. |
| 0x0004cd10 | `CMemoryMonitor_FormatMemoryUnit` | `CMemoryMonitor::FormatMemoryUnit` (memory_monitor.cpp) | [Memory Monitor](#memory-monitor) | Formats an MU's display label with its user-set name. |
| 0x0004dbb0 | `CTransform::CalcMatrix` | `CTransform::CalcMatrix` (scene_groups.cpp) | [Scene Groups](SceneGroups.md) | Composes translate / rotate / scale into the node's world matrix. Called when any TRS field changes. |
| 0x0004e0f0 | `CTransform::SetRotation` | `CTransform::SetRotation` (scene_groups.cpp) | [Scene Groups](SceneGroups.md) | Sets the rotation as axis-angle. Marks the matrix dirty for next CalcMatrix. |
| 0x0004e4d0 | `CInline::Init` | `CInline::Init` (scene_groups.cpp) | [Scene Groups](SceneGroups.md) | Inline-node init: opens the referenced XIP / XAP and inserts its root into the local scene graph. |
| 0x0004e740 | `CInline::Advance` | `CInline::Advance` (scene_groups.cpp) | [Scene Groups](SceneGroups.md) | Per-frame tick. If the load thread completed, swaps the placeholder for the loaded sub-tree. |
| 0x00053ed0 | `CFont::LoadGlyph` | `CFont::LoadGlyph` (text.cpp) | [Text](Text.md) | Decompresses a single glyph bitmap from the XTF cache table. |
| 0x0005ab80 | `CKeyboard_CKeyboard` | `CKeyboard::CKeyboard` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Constructor. Zeroes every field except the cursor, which starts at -1 (whole string selected). |
| 0x0005ac20 | `CKeyboard_dtor` | `CKeyboard::~CKeyboard` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Releases the text, frame and control nodes, frees `string`, releases the key nodearray. |
| 0x0005acb0 | `CKeyboard_GetAsteriskFormattingState` | `CKeyboard::GetAsteriskFormattingState` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Returns the global *heading* text flag at 0x0017e74c. |
| 0x0005acc0 | `CKeyboard_SetAsteriskFormattingState` | `CKeyboard::SetAsteriskFormattingState` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Sets the global *heading* text flag at 0x0017e74c. |
| 0x0005ace0 | `CKeyboard_selectKey` | `CKeyboard::selectKey` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Moves the highlight to (row, column), falling back to 0 for either one out of range. |
| 0x0005ad40 | `CKeyboard_selectUp` | `CKeyboard::selectUp` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Highlight up one row, wrapping to the bottom and skipping blank cells. |
| 0x0005adb0 | `CKeyboard_selectDown` | `CKeyboard::selectDown` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Highlight down one row, wrapping to the top and skipping blank cells. |
| 0x0005ae60 | `CKeyboard_selectLeft` | `CKeyboard::selectLeft` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Highlight left, wrapping, skipping blanks; on western grids hops across the wide bottom keys. |
| 0x0005af50 | `CKeyboard_selectRight` | `CKeyboard::selectRight` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Highlight right, the mirror of selectLeft. |
| 0x0005b040 | `CKeyboard_TIDactivate` | `CKeyboard::TIDactivate` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Turns the last USB keyboard keystroke into a key press and reports it through TIDKeyUsed / PlaySound / CloseKeyboard. |
| 0x0005b340 | `CKeyboard_activate` | `CKeyboard::activate` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Presses the highlighted key: page and case keys, cursor keys, BACKSPACE, SPACE, DONE (trim, copy to `string`, OnDone) or a character insert. |
| 0x0005b630 | `CKeyboard_Backspace` | `CKeyboard::Backspace` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Removes the character left of the cursor, OnError at the start. |
| 0x0005b6a0 | `CKeyboard_Delete` | `CKeyboard::Delete` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Removes the character under the cursor, OnError at the end. |
| 0x0005b700 | `CKeyboard_CursorLeft` | `CKeyboard::CursorLeft` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Cursor left one, OnError at the start. From a full selection it lands at 0. |
| 0x0005b740 | `CKeyboard_CursorRight` | `CKeyboard::CursorRight` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Cursor right one, OnError at the end. From a full selection it lands at the end. |
| 0x0005b780 | `CKeyboard_Shift` | `CKeyboard::Shift` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Toggles shift on the letter and accent pages, OnError on symbols. |
| 0x0005b7b0 | `CKeyboard_CycleMode` | `CKeyboard::CycleMode` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Steps mode and jmode together through 0, 1, 2. |
| 0x0005b7e0 | `CKeyboard_Insert` | `CKeyboard::Insert` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Script fn. Inserts a string at the cursor, OnError if it would pass 31 characters. |
| 0x0005b890 | `FUN_0005b890` | `IsGridChar` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Whether a typed character appears (as is or uppercased) on any page of the current grid. Used by TIDactivate. |
| 0x0005b970 | `CKeyboard_OnSetProperty` | `CKeyboard::OnSetProperty` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Setting `string` loads the edit buffer (up to 31 characters) with the whole thing selected. |
| 0x0005b9d0 | `CKeyboard_Render` | `CKeyboard::Render` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Draws the frame, the text node with the edit string and cursor, then one node per key with its label. |
| 0x0005bca0 | `CKeyboard_Advance` | `CKeyboard::Advance` (keyboard_node.cpp) | [Keyboard](Keyboard.md) | Advances the node, then the control node. |
| 0x0005c250 | `CLevel_load` |  | [Scene Groups](SceneGroups.md) | FND-bound script method on Level: triggers asynchronous load of the Level's referenced sub-scene. |
| 0x0005d200 | `CLevel::Deactivate` | `CLevel::Deactivate` (scene_groups.cpp) | [Scene Groups](SceneGroups.md) | Marks a Level node inactive. Pauses its time-dep children and skips it during render traversal. |
| 0x0005d300 | `CPositionInterpolator::Interpolate` | `CPositionInterpolator::Interpolate` (animation_nodes.cpp) | [Animation](AnimationNodes.md) | Linear interpolation between key positions based on the bound TimeSensor's fraction. |
| 0x0005d5a0 | `COrientationInterpolator::Interpolate` | `COrientationInterpolator::Interpolate` (animation_nodes.cpp) | [Animation](AnimationNodes.md) | Spherical-linear interpolation between key axis-angle rotations. |
| 0x00061090 | `CBackgroundLoader::Fetch` | `CBackgroundLoader::Fetch` (asset_loader.cpp) | [Asset Loader](AssetLoader.md) | Pulls the next pending request off the background loader queue. The loader thread spins on this. |
| 0x00061760 | `CImageTexture::Load` | `CImageTexture::Load` (asset_loader.cpp) | [Asset Loader](AssetLoader.md) | Loads an XBX (Xbox swizzled) texture from a XIP entry into a D3D texture. |
| 0x00061b40 | `CAudioClip::Cleanup` | `CAudioClip::Cleanup` (audio_system.cpp) | [Audio](AudioSystem.md) | Releases an audio clip's DirectSound buffer and waveform data. |
| 0x00061ba0 | `CAudioClip::Init` | `CAudioClip::Init` (audio_system.cpp) | [Audio](AudioSystem.md) | Loads a clip from a XIP entry, parses the WAV header, allocates a DSound buffer, and uploads the PCM payload. |
| 0x000629a0 | `CAudioBuf::Lock` | `CAudioBuf::Lock` (audio_system.cpp) | [Audio](AudioSystem.md) | DSound `IDirectSoundBuffer::Lock` wrapper. Used by the audio pump to write streamed samples. |
| 0x000629d0 | `CAudioBuf::Unlock` | `CAudioBuf::Unlock` (audio_system.cpp) | [Audio](AudioSystem.md) | DSound `Unlock` wrapper. |
| 0x00062a30 | `CAudioBuf::Stop` | `CAudioBuf::Stop` (audio_system.cpp) | [Audio](AudioSystem.md) | Stops playback of a CAudioBuf and resets its play cursor. |
| 0x00062ad0 | `CAudioBuf::SetFrequency` | `CAudioBuf::SetFrequency` (audio_system.cpp) | [Audio](AudioSystem.md) | Sets the buffer's playback rate. Used for pitch shifts and seek-rate effects. |
| 0x00062fe0 | `CAudioPump::Stop` | `CAudioPump::Stop` (audio_system.cpp) | [Audio](AudioSystem.md) | Stops the audio pump's streaming worker. The dashboard's original WMA pump path; Theseus's MP3 path is `CMP3Pump`. |
| 0x000630d0 | `CAudioPump::FillBuffer` | `CAudioPump::FillBuffer` (audio_system.cpp) | [Audio](AudioSystem.md) | Pump tick. Reads decoded samples from the source decoder and writes them into the playback buffer's free region. |
| 0x000632b0 | `CFilePump::Stop` | `CFilePump::Stop` (audio_system.cpp) | [Audio](AudioSystem.md) | Stops a file-backed audio pump. Closes the source file handle. |
| 0x00063320 | `CFilePump::Initialize` | `CFilePump::Initialize` (audio_system.cpp) | [Audio](AudioSystem.md) | Opens an audio file for streaming, parses its header, and binds a decoder. |
| 0x00063520 | `CWMAPump::Initialize` | (reference only) | [Audio](AudioSystem.md) | Original WMA-streaming pump. Cut from Theseus; replaced with `CMP3Pump` (minimp3-based). Preserved here for binary documentation. |
| 0x000638a0 | `CAudioVisualizer::CalcSpectrum` | `CAudioVisualizer::CalcSpectrum` (audio_system.cpp) | [Audio](AudioSystem.md) | Computes a frequency spectrum from the playback buffer's recent samples for visualizer scenes (FFT-driven dotfields, TMAP modulation). |
| 0x00064ec0 | `CNtIoctlCdromService::DeviceIoControl` | `CNtIoctlCdromService::DeviceIoControl` (ntiosvc.cpp) | [NtIoSvc](NtIoSvc.md) | Routes IOCTLs to the CD-ROM device driver. The dashboard talks to the disc through this; Theseus's reconstruction reuses it for ISO/CCI mount via Hermes. |
| 0x00065150 | `CCDDAStreamer::CCDDAStreamer` | (reference only) | [Audio](AudioSystem.md) | CD digital audio streamer constructor. Used by the original soundtrack rip flow; the rip-CD-to-disk feature is not yet implemented in Theseus. |
| 0x000676c0 | `CDVDPlayer::CheckUserOp` | (reference only) | DVD Playback | Checks whether a given DVD user operation (next chapter, scan, menu) is permitted by the disc's prohibited-operations mask. The full DVD playback chain was cut from Theseus; the desktop build uses libmpv instead. |
| 0x00068d60 | `XOnlineGetUsers_wrapper` | `XOnlineGetUsers_wrapper` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Thin wrapper around the kernel `XOnlineGetUsers` import. Centralizes argument marshalling for the dashboard's Live-user enumeration paths. |
| 0x00069470 | `CLiveAccounts_LoadFromHD_internal` | `CLiveAccounts::LoadFromHD` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Loads all Xbox Live accounts stored on the HDD via `_XOnlineGetUsersFromHD`. Populates the global `g_Users[]` array. |
| 0x00069730 | `CLiveAccounts_GetNumberOfAccounts` | `CLiveAccounts::GetNumberOfAccounts` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Returns the count of accounts loaded by `LoadFromHD`. |
| 0x00069750 | `CLiveAccounts_GetAccountName` | `CLiveAccounts::GetAccountName` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Returns gamertag for the Nth loaded account. |
| 0x000697f0 | `CLiveAccounts_GetAccountName` | `CLiveAccounts::GetAccountName` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Overload of `GetAccountName`. Differs by signature; both write the same gamertag string. |
| 0x000698b0 | `CLiveAccounts_IsPasswordEnabled` | `CLiveAccounts::IsPasswordEnabled` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Tests whether the bound account requires a passcode at logon. |
| 0x000698e0 | `CLiveAccounts_VerifyPassword` | `CLiveAccounts::VerifyPassword` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Compares an entered passcode against the account's stored hash. |
| 0x00069ae0 | `CLiveAccounts_Logoff` | `CLiveAccounts::Logoff` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Logs out the currently signed-in Live user and clears in-memory credentials. |
| 0x0006a000 | `CLiveAccounts_IsBackFromEntryPoint` | `CLiveAccounts::IsBackFromEntryPoint` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Returns whether the dashboard re-entered the Live flow from a title (post-game return) versus a fresh boot. |
| 0x0006a030 | `CLiveAccounts_GetLastLogonUser` | `CLiveAccounts::GetLastLogonUser` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Returns the index of the account that last logged on. Used to default the user selector. |
| 0x0006a0d0 | `CLiveAccounts_IsPasswordVerified` | `CLiveAccounts::IsPasswordVerified` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Returns whether the bound account has a verified passcode in this session (gates re-verification). |
| 0x0006a530 | `CLiveAccounts_GetResult` | `CLiveAccounts::GetResult` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Returns the numeric result code from the most recent Live operation (logon, friends fetch, etc.). |
| 0x0006a590 | `CLiveAccounts_ClearMOTDCache` | `CLiveAccounts::ClearMOTDCache` (live_accounts.cpp) | [Live Accounts](#live-accounts) | Invalidates the cached "message of the day" so the next Live update re-fetches it. |
| 0x0006e6cb | `CFilePump::GetData` | `CFilePump::GetData` (audio_system.cpp) | [Audio](AudioSystem.md) | Reads the next chunk of compressed data from the file pump's source file. Called by the decoder when its input is empty. |
| 0x0007967f | `CXAppNode::GetTextureSurface` | `CXAppNode::GetTextureSurface` (main.cpp) | [Render](#render) | Returns the underlying D3D `IDirect3DSurface8` for a node's bound texture. Used by render-to-texture nodes. |
| 0x000cfe09 | `CLayout::GetRadius` | `CLayout::GetRadius` (scene_groups.cpp) | [Scene Groups](SceneGroups.md) | Returns the layout's circular radius. Inlined in many .text use sites; this is the FND-bound wrapper. |
| 0x0011bec2 | `CXo_XOnlineLogon` | `CXo::XOnlineLogon` (xbox_live.cpp) | [Live Accounts](#live-accounts) | Theseus-side facade over `XOnlineLogon`. Marshals the dashboard's account record into the kernel call's parameter shape. |

## Coverage status

The seed and first expansion cover the application shell, script VM, scene graph backbone, XIP archives, animation, scene groups, file ops, save games, memory monitor, audio core, font / text, keyboard, settings / locale / config, and reference-only entries for cut subsystems (DVD playback, original WMA pump). Per-narrative-doc:

| Subsystem | Doc | Coverage |
|-----------|-----|----------|
| Script VM | [VM.md](VM.md) | 35 functions |
| Scene Graph | [Node.md](Node.md) | 2 functions + 32 class-table registrations |
| XIP Archives | [XIP.md](XIP.md) | 26 functions |
| Animation Nodes | [AnimationNodes.md](AnimationNodes.md) | 4 functions |
| Audio System | [AudioSystem.md](AudioSystem.md) | 12 functions |
| Camera | [Camera.md](Camera.md) | not yet started |
| Date | [Date.md](Date.md) | 7 functions |
| Math | [Math.md](Math.md) | 5 functions |
| File Operations | [FileOps.md](FileOps.md) | 7 functions |
| Disc | (no doc yet, narrative TODO) | 1 function |
| HUD & Picture | [HudNode.md](HudNode.md) | 9 functions |
| Keyboard | [Keyboard.md](Keyboard.md) | 22 functions |
| Locale | (no doc yet, narrative TODO) | 1 function |
| NtIoSvc | [NtIoSvc.md](NtIoSvc.md) | 1 function |
| Scene Groups | [SceneGroups.md](SceneGroups.md) | 6 functions |
| Settings | [Settings.md](Settings.md) | 14 functions |
| Shape Render | [ShapeRender.md](ShapeRender.md) | not yet started |
| Text | [Text.md](Text.md) | 2 functions |
| Title Collection | [TitleCollection.md](TitleCollection.md) | 9 verified Theseus mappings (AddTitle, Update, FindTitle, RemoveTitle, RemoveSavedGame x2, GetTitleTotalBlocks, IsDirty, IsPublisherExists) + 6 binary-only anchors (DLC enumerator, save dir walker, drive-letter translator, etc.); more labeling pending |
| TMAP System | [TmapSystem.md](TmapSystem.md) | not yet started |
| Util | [Util.md](Util.md) | 1 function |
| Save Games | (no doc yet, narrative TODO) | 29 functions |
| Live Accounts | (no doc yet, narrative TODO) | 13 verified Theseus mappings + 2 binary-only anchors (bulk delete, single-user delete) |
| Input | (no doc yet, narrative TODO) | 1 function |
| Memory Monitor | (no doc yet, narrative TODO) | 12 functions |
| Music Collection | (no doc yet, narrative TODO) | 5 verified Theseus mappings (FormatSoundtrackSongTime, GetUpdateString + 3 stubs: AddSoundtrack, ClearCopyList, DeleteSoundtrack) + 3 binary-only anchors (global ::DeleteSoundtrack, ::DeleteAllSoundtracks, ThreadProc); more labeling pending |
| Render | (no doc yet, narrative TODO) | 1 function |
| App Shell | (no doc yet, narrative TODO) | 9 functions |
| Asset Loader | [AssetLoader.md](AssetLoader.md) (narrative TODO) | 2 functions |
| DVD Playback | (reference-only, no doc) | 1 function |
| Original WMA Pump | (reference-only, folded into AudioSystem.md) | 1 function |
| Original CDDA Streamer | (reference-only, folded into AudioSystem.md) | 1 function |

Reference-only entries describe binary functions in the dashboard whose Theseus replacement is intentionally minimal or absent (DVD playback was cut, the legacy WMA pump was cut, the CDDA streamer was cut). They exist in this table for the historical record, not because they ship.

This page grows as more subsystems get audited. Open issues / corrections welcome.
