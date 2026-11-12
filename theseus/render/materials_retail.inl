// materials_retail.inl: every material the 5960 dashboard builds, with its
// retail colours. Included by render/materials.cpp and
// desktop/materials_desktop.cpp after their CMatInfo classes, so both builds
// bind the same names. A skin only overrides what it lists; the rest stays retail.

enum MatKind
{
	MK_STANDARD,
	MK_SOLID,
	MK_BACKING,
	MK_MODULATE,
	MK_FALLOFF,
	MK_FALLOFFTEX,
	MK_INNERWALL,
	MK_ANISO,
	MK_EGGGLOW,
	MK_EGGGLOWPULSE,
	MK_KEY,
	MK_ICON,
	MK_MASK,
	MK_REFLECT,
	MK_FREESPACE,
};

struct RetailMat
{
	const TCHAR *szName;
	MatKind kind;
	DWORD dwFlags;
	D3DCOLOR colorA; // solid colour, or falloff side
	D3DCOLOR colorB; // falloff front
	WORD nIconSize;
	BYTE bFadeIn, bInXIP, bSelImg;
	BYTE bBright, bText;
};

#define RGBA D3DCOLOR_RGBA

// Same order as the 5960 Material_Init. EggGlow lists what its constructor is
// handed; the constructor shifts them one slot, same as retail.
static const RetailMat g_rgRetailMat[] =
{
	{ _T("InnerWall_01"), MK_INNERWALL, 0, RGBA(243, 255, 107, 255), RGBA(40, 212, 20, 20), 0, 0, 0, 0, 0, 0 },
	{ _T("InnerWall_02"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(243, 255, 107, 255), RGBA(20, 192, 0, 20), 0, 0, 0, 0, 0, 0 },
	{ _T("MetaInfo_Text"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("NamePanel_Text"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("GameNameText_01"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("GameNameText_02"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("GameNameText_03"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("GameIcon_01"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("GameIcon_03"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Material #132"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("u2 info"), MK_STANDARD, MATINFO_STANDARD_MATERIAL, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Material #1334"), MK_SOLID, 0, RGBA(11, 32, 0, 192), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("XBOXgreendark"), MK_SOLID, 0, RGBA(6, 33, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("XBOXgreen"), MK_SOLID, 0, RGBA(140, 201, 25, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("XBoxGreen2"), MK_SOLID, 0, RGBA(139, 200, 24, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("GameHilite33"), MK_SOLID, 0, RGBA(221, 208, 120, 178), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Nothing"), MK_SOLID, 0, RGBA(128, 128, 128, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("NavType"), MK_SOLID, 0, RGBA(190, 250, 94, 178), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("OrangeNavType"), MK_SOLID, 0, RGBA(249, 152, 25, 178), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("RedType"), MK_SOLID, 0, RGBA(200, 30, 30, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("XBoxGreen"), MK_SOLID, 0, RGBA(139, 200, 24, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Type"), MK_SOLID, 0, RGBA(100, 200, 25, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Typesdsafsda"), MK_SOLID, 0, RGBA(255, 255, 255, 179), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Material #133"), MK_SOLID, 0, RGBA(0, 0, 0, 192), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Material #1335"), MK_SOLID, 0, RGBA(76, 162, 0, 200), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Material #133511"), MK_SOLID, 0, RGBA(41, 87, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Material #1336"), MK_SOLID, 0, RGBA(0, 0, 0, 128), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("HilightedType"), MK_SOLID, 0, RGBA(3, 44, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("XBoxGreenq"), MK_SOLID, 0, RGBA(139, 200, 24, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Black80"), MK_SOLID, 0, RGBA(0, 0, 0, 204), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Black"), MK_SOLID, 0, RGBA(0, 0, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("CellEgg/Partsw"), MK_SOLID, MATINFO_CULL_NONE, RGBA(77, 224, 57, 166), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("CellEgg/Partsz"), MK_SOLID, MATINFO_CULL_NONE, RGBA(170, 170, 170, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Material #108"), MK_SOLID, MATINFO_CULL_NONE, RGBA(160, 252, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("ItemsType"), MK_SOLID, MATINFO_CULL_NONE, RGBA(182, 245, 96, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("GameHiliteMemory"), MK_SOLID, 0, RGBA(178, 208, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("red"), MK_SOLID, 0, RGBA(255, 0, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("white"), MK_SOLID, 0, RGBA(255, 255, 255, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("solid green 1"), MK_SOLID, 0, RGBA(17, 255, 34, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("solid green2"), MK_SOLID, 0, RGBA(18, 193, 10, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("solid green 3"), MK_SOLID, 0, RGBA(10, 117, 28, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("solid green 4"), MK_SOLID, 0, RGBA(18, 55, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("dark green panels"), MK_SOLID, 0, RGBA(9, 41, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("FlatSurfaces"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("FlatSurfacesSelected"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(30, 255, 0, 128), 0, 0, 0, 0, 0, 0 },
	{ _T("FlatSurfacesMemory"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(30, 160, 0, 128), 0, 0, 0, 0, 0, 0 },
	{ _T("DarkSurfaces"), MK_FALLOFF, 0, RGBA(203, 205, 85, 90), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("DarkSurfaces2"), MK_FALLOFF, 0, RGBA(203, 205, 85, 90), RGBA(10, 100, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("FlatSurfaces2sided"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("DetailLegSkin_Inner"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("Screen"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("Spout"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("NavType34"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("MetaFlatSurfaces"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("SC_SavedGame_Row01"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("HK_SavedGame_Row01"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("SavedEgg_Selected"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("FlatUnselected"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("Mem_InnerWall_Outer"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("SavedGameEgg"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("GameMenuEgg"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("Shell"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("Material #133sdsfdsf"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("IconParts"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("IconParts1"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("MU1Pod_HL"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("MU1Pod"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("GamePodb"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("GamePod"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("JwlSrfc01/InfoPnls"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("MenuCell"), MK_FALLOFF, 0, RGBA(243, 255, 107, 192), RGBA(20, 192, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("dark green panels falloff"), MK_FALLOFF, 0, RGBA(0, 0, 0, 0), RGBA(5, 35, 5, 255), 0, 0, 0, 0, 0, 0 },
	{ _T("grey"), MK_FALLOFF, 0, RGBA(71, 83, 69, 0), RGBA(86, 100, 82, 255), 0, 0, 0, 0, 0, 0 },
	{ _T("grill grey"), MK_FALLOFF, 0, RGBA(32, 32, 32, 0), RGBA(64, 64, 64, 255), 0, 0, 0, 0, 0, 0 },
	{ _T("Wireframe"), MK_FALLOFFTEX, 0, RGBA(125, 198, 34, 100), RGBA(125, 198, 34, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("Tubes"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(242, 250, 153, 215), RGBA(7, 104, 0, 37), 0, 0, 0, 0, 0, 0 },
	{ _T("JewelSurface02/PodMesh"), MK_FALLOFF, 0, RGBA(242, 250, 153, 215), RGBA(7, 104, 0, 37), 0, 0, 0, 0, 0, 0 },
	{ _T("TubesFade"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(242, 250, 153, 215), RGBA(7, 104, 0, 37), 0, 0, 0, 0, 0, 0 },
	{ _T("TubesQ"), MK_FALLOFF, 0, RGBA(242, 250, 153, 215), RGBA(7, 104, 0, 37), 0, 0, 0, 0, 0, 0 },
	{ _T("EmptyMU"), MK_FALLOFF, 0, RGBA(242, 250, 153, 215), RGBA(7, 104, 0, 37), 0, 0, 0, 0, 0, 0 },
	{ _T("Tube"), MK_FALLOFF, 0, RGBA(242, 250, 153, 215), RGBA(7, 104, 0, 37), 0, 0, 0, 0, 0, 0 },
	{ _T("MemoryHeader"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(60, 198, 67, 122), RGBA(7, 104, 0, 37), 0, 0, 0, 0, 0, 0 },
	{ _T("MemoryHeaderHilite"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(199, 232, 0, 240), RGBA(97, 114, 0, 130), 0, 0, 0, 0, 0, 0 },
	{ _T("EggGlow"), MK_EGGGLOW, 0, RGBA(252, 255, 0, 0), RGBA(254, 255, 188, 228), 0, 0, 0, 0, 0, 0 },
	{ _T("ButtonGlow"), MK_FALLOFF, 0, RGBA(252, 255, 0, 0), RGBA(254, 255, 188, 228), 0, 0, 0, 0, 0, 0 },
	{ _T("gradient"), MK_FALLOFF, 0, RGBA(191, 255, 107, 51), RGBA(0, 255, 18, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("CellWallStructure"), MK_FALLOFF, 0, RGBA(191, 255, 107, 51), RGBA(0, 255, 18, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("FlatSrfc/PodParts"), MK_FALLOFF, 0, RGBA(191, 255, 107, 51), RGBA(0, 255, 18, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("Cell_Light"), MK_FALLOFF, 0, RGBA(191, 255, 107, 51), RGBA(0, 255, 18, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("Cell_Light/LegSkin"), MK_FALLOFF, 0, RGBA(191, 255, 107, 51), RGBA(0, 255, 18, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("CellEgg/Parts"), MK_FALLOFF, 0, RGBA(243, 255, 107, 178), RGBA(30, 255, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("GameEgg"), MK_FALLOFF, 0, RGBA(243, 255, 107, 178), RGBA(30, 255, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("FlatSurfaces2sided3"), MK_FALLOFF, MATINFO_CULL_NONE, RGBA(253, 30, 0, 255), RGBA(242, 28, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("console_hilite"), MK_FALLOFF, 0, RGBA(255, 173, 107, 255), RGBA(246, 255, 0, 0), 0, 0, 0, 0, 0, 0 },
	{ _T("TitleIcon"), MK_ICON, 0, 0, 0, 128, 0, 0, 0, 0, 0 },
	{ _T("TitleSoundtrackIcon"), MK_ICON, 0, 0, 0, 128, 0, 1, 0, 0, 0 },
	{ _T("SavedGameIcon"), MK_ICON, 0, 0, 0, 64, 1, 0, 0, 0, 0 },
	{ _T("SoundtrackIcon"), MK_ICON, 0, 0, 0, 64, 1, 1, 0, 0, 0 },
	{ _T("MotdIcon"), MK_ICON, 0, 0, 0, 64, 1, 0, 0, 0, 0 },
	{ _T("XboxLiveGameTitleOrbIcon"), MK_ICON, 0, 0, 0, 128, 0, 1, 0, 0, 0 },
	{ _T("XboxLiveAccountIcon"), MK_ICON, 0, 0, 0, 64, 1, 1, 0, 0, 0 },
	{ _T("SelectedIcon"), MK_ICON, 0, 0, 0, 128, 0, 0, 1, 0, 0 },
	{ _T("Key"), MK_KEY, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("BrightKey"), MK_KEY, 0, 0, 0, 0, 0, 0, 0, 1, 0 },
	{ _T("KeyText"), MK_KEY, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
	{ _T("EggGlowPulse"), MK_EGGGLOWPULSE, 0, RGBA(178, 208, 0, 0), RGBA(255, 255, 128, 255), 0, 0, 0, 0, 0, 0 },
	{ _T("Metal_Chrome"), MK_ANISO, 0, RGBA(229, 229, 229, 0), RGBA(229, 229, 229, 255), 0, 0, 0, 0, 0, 0 },
	{ _T("Tvbox"), MK_ANISO, 0, RGBA(229, 229, 229, 0), RGBA(229, 229, 229, 255), 0, 0, 0, 0, 0, 0 },
	{ _T("PanelBacking_01"), MK_BACKING, 0, RGBA(4, 20, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("PanelBacking_02"), MK_BACKING, 0, RGBA(0, 0, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("PanelBacking_03"), MK_BACKING, 0, RGBA(4, 20, 0, 240), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("PanelBacking_04"), MK_BACKING, 0, RGBA(14, 46, 7, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("DarkenBacking"), MK_BACKING, MATINFO_CULL_NONE | MATINFO_RADIAL_EDGE_ALPHA, RGBA(4, 50, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("TextBacking"), MK_BACKING, MATINFO_CULL_NONE | MATINFO_RADIAL_ALPHA, RGBA(0, 0, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("button"), MK_MODULATE, MATINFO_CULL_NONE, RGBA(157, 109, 194, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("image"), MK_MODULATE, MATINFO_CULL_NONE, RGBA(4, 20, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("live header"), MK_MODULATE, MATINFO_CULL_NONE, RGBA(4, 20, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("highlight"), MK_MODULATE, MATINFO_CULL_NONE, RGBA(157, 109, 194, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("footer"), MK_MODULATE, MATINFO_CULL_NONE, RGBA(157, 109, 194, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("LiveChrome"), MK_MODULATE, MATINFO_CULL_NONE, RGBA(157, 109, 194, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("orangeEggGlow"), MK_SOLID, 0, RGBA(254, 255, 188, 255), 0, 0, 0, 0, 0, 0, 0 },	// textured in 5960, not traced yet
	{ _T("GameHilite"), MK_MODULATE, MATINFO_CULL_NONE, RGBA(255, 255, 255, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("equalizer"), MK_MASK, MATINFO_RADIAL_AV_ALPHA, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("MainMenuOrb"), MK_MASK, MATINFO_RADIAL_EDGE_MAIN_ALPHA, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("ReflectSurface"), MK_REFLECT, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ _T("PanelBacking"), MK_MODULATE, MATINFO_CULL_NONE, RGBA(4, 20, 0, 255), 0, 0, 0, 0, 0, 0, 0 },
	{ _T("Material #10822"), MK_FREESPACE, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

// The Keyboard section, shared by Key, BrightKey and KeyText: Live side, side,
// bright front, text.
static const D3DCOLOR g_rgRetailKey[4] =
{
	RGBA(178, 208, 0, 255),
	RGBA(243, 255, 107, 255),
	RGBA(25, 225, 0, 34),
	RGBA(190, 250, 94, 0),
};

#undef RGBA

#define MAT_R(c) ((BYTE)((c) >> 16))
#define MAT_G(c) ((BYTE)((c) >> 8))
#define MAT_B(c) ((BYTE)(c))
#define MAT_A(c) ((BYTE)((c) >> 24))

static CustomColor ToCustomColor(D3DCOLOR c)
{
	CustomColor cc;
	cc.r = MAT_R(c);
	cc.g = MAT_G(c);
	cc.b = MAT_B(c);
	cc.a = MAT_A(c);
	return cc;
}

// "r, g, b, a" from the skin; leaves *pColor alone if the skin doesn't have it
static bool SkinColor(CSettingsFile *pSkin, const TCHAR *szSection, const TCHAR *szKey, D3DCOLOR *pColor)
{
	TCHAR buf[MAX_PATH];
	if (pSkin == NULL || !pSkin->GetValue(szSection, szKey, buf, MAX_PATH))
		return false;

	int r, g, b, a = 0;
	if (_stscanf(buf, _T("%d, %d, %d, %d"), &r, &g, &b, &a) < 3)
		return false;

	*pColor = D3DCOLOR_RGBA(r, g, b, a);
	return true;
}

static void ResolveColors(CSettingsFile *pSkin, const RetailMat &m, D3DCOLOR *pA, D3DCOLOR *pB)
{
	*pA = m.colorA;
	*pB = m.colorB;

	switch (m.kind)
	{
	case MK_SOLID:
	case MK_BACKING:
	case MK_MODULATE:
		SkinColor(pSkin, m.szName, _T("Color"), pA);
		break;

	case MK_FALLOFF:
	case MK_FALLOFFTEX:
	case MK_INNERWALL:
	case MK_ANISO:
	case MK_EGGGLOW:
	case MK_EGGGLOWPULSE:
		SkinColor(pSkin, m.szName, _T("ColorA"), pA);
		SkinColor(pSkin, m.szName, _T("ColorB"), pB);
		break;

	default:
		break;
	}
}

static void ResolveKeyColors(CSettingsFile *pSkin, CustomColor rgColor[4])
{
	static const TCHAR *rgszKey[4] = { _T("ColorA"), _T("ColorB"), _T("ColorC"), _T("ColorD") };
	for (int i = 0; i < 4; i++)
	{
		D3DCOLOR c = g_rgRetailKey[i];
		SkinColor(pSkin, _T("Keyboard"), rgszKey[i], &c);
		rgColor[i] = ToCustomColor(c);
	}
}

static void CreateMat(const RetailMat &m, D3DCOLOR a, D3DCOLOR b, const CustomColor rgKey[4])
{
	switch (m.kind)
	{
	case MK_STANDARD:       new CMatInfo(m.szName, m.dwFlags); break;
	case MK_SOLID:          new CSolidMatInfo(m.szName, MAT_R(a), MAT_G(a), MAT_B(a), MAT_A(a), m.dwFlags); break;
	case MK_BACKING:        new CBackingMatInfo(m.szName, MAT_R(a), MAT_G(a), MAT_B(a), MAT_A(a), m.dwFlags); break;
	case MK_MODULATE:       new CModulateTextureMatInfo(m.szName, MAT_R(a), MAT_G(a), MAT_B(a), MAT_A(a), m.dwFlags); break;
	case MK_FALLOFF:        new CFalloffMatInfo(m.szName, a, b, m.dwFlags); break;
	case MK_FALLOFFTEX:     new CFalloffTexInfo(m.szName, a, b, m.dwFlags); break;
	case MK_INNERWALL:      new CInnerWallMatInfo(m.szName, a, b, m.dwFlags); break;
	case MK_ANISO:          new CAnisoMatInfo(m.szName, a, b, m.dwFlags); break;
	case MK_EGGGLOW:        new CEggGlowFadeMatInfo(m.szName, a, ToCustomColor(b), m.dwFlags); break;
	case MK_EGGGLOWPULSE:   new CEggGlowPulseMatInfo(m.szName, ToCustomColor(a), ToCustomColor(b)); break;
	case MK_KEY:            new CKeyMatInfo(m.szName, m.bBright != 0, m.bText != 0, rgKey[0], rgKey[1], rgKey[2], rgKey[3]); break;
	case MK_ICON:           new CIconMatInfo(m.szName, m.nIconSize, m.nIconSize, m.bFadeIn != 0, m.bInXIP != 0, m.bSelImg != 0); break;
	case MK_MASK:           new CMaskTextureMatInfo(m.szName, m.dwFlags); break;
	case MK_REFLECT:        new CReflectMatInfo(m.szName); break;
	case MK_FREESPACE:      new CFreeSpaceMatInfo(m.szName); break;
	}
}

// Skin reload: recolour in place, same colours CreateMat would have used
static void RecolourMat(CMatInfo *p, const RetailMat &m, D3DCOLOR a, D3DCOLOR b, const CustomColor rgKey[4])
{
	switch (m.kind)
	{
	case MK_SOLID:
		{
			CSolidMatInfo *s = static_cast<CSolidMatInfo *>(p);
			s->m_r = MAT_R(a); s->m_g = MAT_G(a); s->m_b = MAT_B(a); s->m_a = MAT_A(a);
		}
		break;

	case MK_BACKING:
		{
			CBackingMatInfo *s = static_cast<CBackingMatInfo *>(p);
			s->m_r = MAT_R(a); s->m_g = MAT_G(a); s->m_b = MAT_B(a); s->m_a = MAT_A(a);
		}
		break;

	case MK_MODULATE:
		{
			CModulateTextureMatInfo *s = static_cast<CModulateTextureMatInfo *>(p);
			s->m_r = MAT_R(a); s->m_g = MAT_G(a); s->m_b = MAT_B(a); s->m_a = MAT_A(a);
		}
		break;

	case MK_FALLOFF:
		static_cast<CFalloffMatInfo *>(p)->m_colorSide = a;
		static_cast<CFalloffMatInfo *>(p)->m_colorFront = b;
		break;

	case MK_FALLOFFTEX:
		static_cast<CFalloffTexInfo *>(p)->m_colorSide = a;
		static_cast<CFalloffTexInfo *>(p)->m_colorFront = b;
		break;

	case MK_INNERWALL:
		static_cast<CInnerWallMatInfo *>(p)->m_colorSide = a;
		static_cast<CInnerWallMatInfo *>(p)->m_colorFront = b;
		break;

	case MK_ANISO:
		static_cast<CAnisoMatInfo *>(p)->m_colorSide = a;
		static_cast<CAnisoMatInfo *>(p)->m_colorFront = b;
		break;

	case MK_EGGGLOW:
		{
			// mirrors the shifted constructor: a lands in front, b in the flags
			CEggGlowFadeMatInfo *e = static_cast<CEggGlowFadeMatInfo *>(p);
			e->m_colorSide = m.dwFlags;
			e->m_colorFront = a;
			e->m_flags = b;
			e->Color1 = ToCustomColor(b);
		}
		break;

	case MK_EGGGLOWPULSE:
		static_cast<CEggGlowPulseMatInfo *>(p)->Color1 = ToCustomColor(a);
		static_cast<CEggGlowPulseMatInfo *>(p)->Color2 = ToCustomColor(b);
		break;

	case MK_KEY:
		{
			CKeyMatInfo *k = static_cast<CKeyMatInfo *>(p);
			k->Color1 = rgKey[0];
			k->Color2 = rgKey[1];
			k->Color3 = rgKey[2];
			k->Color4 = rgKey[3];
		}
		break;

	default:
		break;
	}
}

static void Material_Reload()
{
	CSettingsFile *pSkin = TheseusGetSkinSettings();
	CustomColor rgKey[4];
	ResolveKeyColors(pSkin, rgKey);

	for (int i = 0; i < (int)(sizeof(g_rgRetailMat) / sizeof(g_rgRetailMat[0])); i++)
	{
		const RetailMat &m = g_rgRetailMat[i];
		CMatInfo *p = LookupMatInfo(m.szName);
		if (p == NULL)
			continue;

		D3DCOLOR a, b;
		ResolveColors(pSkin, m, &a, &b);
		RecolourMat(p, m, a, b, rgKey);
	}
}

void Material_Init(bool bReloadSkinXBX)
{
	if (bReloadSkinXBX)
	{
		Material_Reload();
		return;
	}

	CSettingsFile *pSkin = TheseusGetSkinSettings();
	CustomColor rgKey[4];
	ResolveKeyColors(pSkin, rgKey);

	for (int i = 0; i < (int)(sizeof(g_rgRetailMat) / sizeof(g_rgRetailMat[0])); i++)
	{
		const RetailMat &m = g_rgRetailMat[i];
		D3DCOLOR a, b;
		ResolveColors(pSkin, m, &a, &b);
		CreateMat(m, a, b, rgKey);
	}

	qsort(g_rgMatInfo, g_nMatInfoCount, sizeof(CMatInfo *), SortMatInfoCompare);
}

#undef MAT_R
#undef MAT_G
#undef MAT_B
#undef MAT_A
