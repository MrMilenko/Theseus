# Scene Groups (scene_groups.cpp)

Container and grouping nodes in the XAP scene graph. These handle
hierarchy, transforms, level transitions, and viewport management.

## Node Classes

All node type strings confirmed in the 4920-5960 retail XBE:

| Node | String Address | Class String Address | Parent |
|------|---------------|----------------------|--------|
| Group | 0x00027f04 | 0x00027ef4 "CGroup" | CNode |
| Transform | 0x00027ecc | 0x00027eb4 "CTransform" | CGroup |
| Inline | 0x00027d54 | 0x00027d44 "CInline" | CGroup |
| Spinner | 0x00027ce0 | 0x00027ccc "CSpinner" | CGroup |
| Waver | 0x00027ca0 | 0x00027c90 "CWaver" | CSpinner |
| Layout | 0x00027c74 | 0x00027c64 "CLayout" | CGroup |
| Switch | 0x00027c40 | 0x00027c30 "CSwitch" | CNode |
| Billboard | 0x00027bf4 | 0x00027bdc "CBillboard" | CGroup |
| Layer | 0x00029a10 | 0x00029a00 "CLayer" | CGroup |
| Background | 0x000281d4 | 0x000281bc "CBackground" | CNode |
| Level | 0x00029f00 | 0x00029ef0 "CLevel" | CGroup |

## Class Registration Data

FND table entry confirmed for CTransform:
- `_c_rgfnd_CTransform` at 0x00014b68

PRD table entry confirmed for CInline:
- `_m_rgprd_CInline` at 0x00021a94

Class registration objects:
- `_classCTransform` at 0x0001eb64
- `_classCInline` at 0x00013dac
- `_classCSpinner` at 0x000145cc
- `_classCSwitch` at 0x00013db4
- `_classCBillboard` at 0x00014b3c
- `_classCLayout` at 0x00014b38
- `_classCLayer` at 0x0001f33c
- `_classCLevel` at 0x000213d8 (vtable `___7CLevel__6B_`)

## Level Transition Globals

Level transition state found in .data section:
- `g_timeToNextLevel` at 0x0001f558 (labeled `_g_timeToNextLevel__3NA`)

## Properties (from FND/PRD tables)

### CGroup
- `children` (pt_children)

### CTransform
- `center` (pt_vec3), `scaleOrientation` (pt_vec4), `scale` (pt_vec3)
- `rotation` (pt_vec4), `translation` (pt_vec3)
- `fade` (pt_number), `moving` (pt_boolean), `alpha` (pt_number)
- Functions: SetScale, SetScaleOrientation, SetTranslation, SetCenter,
  SetRotation, SetAlpha, DisappearAfter

### CInline
- `url` (pt_string), `preload` (pt_boolean), `fadeInDelayLoad` (pt_boolean)

### CSpinner
- `rpm` (pt_number), `axis` (pt_vec3), `angle` (pt_number)

### CWaver (extends CSpinner)
- `field` (pt_number)

### CLayout
- `direction` (pt_vec3), `spacing` (pt_number)

### CSwitch
- `whichChoice` (pt_integer), `choice` (pt_nodearray)

### CBillboard
- `axisOfRotation` (pt_vec3)

### CLayer
- `viewpoint` (pt_node), `navigationInfo` (pt_node)
- `fade` (pt_number), `transparency` (pt_number)

### CBackground
- `skyColor` (pt_vec3), `backdrop` (pt_node), `isBound` (pt_boolean)

### CLevel
- `control` (pt_node), `tunnel` (pt_node), `path` (pt_node), `shell` (pt_node)
- `archive` (pt_string), `unloadable` (pt_boolean), `fade` (pt_boolean)
- Functions: GoTo, GoBackTo
- Script callbacks: OnActivate, OnArrival, OnDeactivate

## Notes

- CWaver inherits CSpinner but its IMPLEMENT_NODE parent is CGroup, not
  CSpinner.
- CInline has a thread handle member for background class loading, but
  the retail binary does not spawn a thread for this.
- CBackground has no Render() override in the retail binary. Only
  RenderBackdrop() is used.
- SetLight() methods are present in all retail builds.
