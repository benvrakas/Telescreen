# Reverse-engineering notes (game CL 513885, engine 5.6.1-CSS)

Sources: the starter project's placeholder assets dumped with `Tools/dump_assets.py` (editor Python commandlet;
component trees and Blueprint-visible properties are real, meshes and textures are placeholders) and the game's
`CommunityResources/Docs/en-US.json` (read only; recipes, descriptors, schematics, buildable defaults).

## Billboards

| | Small Billboard | Large Billboard |
|---|---|---|
| Buildable | `Build_StandaloneWidgetSign_Large_C` | `Build_StandaloneWidgetSign_Huge_C` |
| Native class | `AFGBuildableWidgetSign` : `AFGBuildableSignBase` : `AFGBuildable` | same |
| Hologram | `Holo_StandaloneSign_Large_C` (`AFGStandaloneSignHologram`) | `Holo_StandaloneSign_Huge_C` |
| Descriptor | `Desc_StandaloneWidgetSign_Large_C`, menu priority 9 | `Desc_StandaloneWidgetSign_Huge_C`, menu priority 10 |
| Icons | `IconDesc_SignLarge_512` / `_256` | `IconDesc_SignHuge_512` / `_256` |
| `mWorldDimensions` | 800 x 400 | 1600 x 800 |
| `mPoleOffset` / `mPoleScale` | (0, 38, 0) / (1, 1) | (0, 25, 0) / (2, 2) |
| `mSignToSignOffset` | 0 | 60 |
| `mGainSignificanceDistance` | 10000 | 15000 |
| Clearance (soft) | box ±400 x ±10 x ±200, offset (0, -2, 0) | box ±800 x ±20 x ±400, offset (0, 46, 0) |
| Recipe (build gun) | 3 Reinforced Iron Plate, 4 Copper Sheet, 1 Crystal Oscillator | 12 / 20 / 5 |

Component trees (both):
- `SignMeshProxy` (`UFGColoredInstanceMeshProxy`, created by `AFGBuildableSignBase` as `mMeshComponentProxy`):
  `SM_DigitalBillboard_01`, yaw 90, material overrides `[None, MI_SignBackground]`. Small: location (0, -18, 0),
  scale 1.143. Large: (0, 15, 0), scale 2.29.
- `ProxyMesh` (`UStaticMeshComponent`, created by `AFGBuildableWidgetSign` as `mSignProxyPlane`): the sign face,
  `/Engine/BasicShapes/Plane` (100 x 100), roll 90. Small: (0, 10, 0), scale (7.95, 3.95, 8). Large:
  (0, 66.719, 0), scale (16, 8, 1).

Both are unlocked by the AWESOME Shop schematic `ResourceSink_BillboardSigns_C` ("Billboard Set", 10 coupons, shop
category `SC_RSS_Signs_C`, menu priority 2, tech tier 1, icon `IconDesc_SignHuge_256`). Build menu: category
`BC_Organization_C`, sub-category `SC_Signs_C`.

## Sign materials

- `MM_SignWidgetMaterial`: texture `SlateUI`; scalars `Glossiness`, `OpacityFromTexture`, `Emission`; vectors
  `TintColorAndOpacity`, `Background`. Opaque, default lit. Telescreen uses it for the screen face.
- `MM_SignRT`: textures `BackgroundTexture`, `Texture`; many layout scalars (fill mode, tiling, blur).
- `MI_SignBackground`: the sign body's background material.

## Engine facts used

- `AFGBuildable::mHologramClass`, `mDisplayName`, `mDescription` are public; `mClearanceData` and
  `mAllowColoring` are protected; `mIsUseable` and `mInteractWidgetSoftClass` are private, so Telescreen
  overrides `IsUseable_Implementation` / `OnUse_Implementation` instead.
- Descriptor, recipe, schematic and unlock fields are protected; Telescreen's own subclasses set them.
- SML registers C++ schematics from `UGameWorldModule::mSchematics`; AWESOME Shop entries are `EST_ResourceSink`
  schematics.
