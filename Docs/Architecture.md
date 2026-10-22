# Telescreen architecture

The full design is in the plan (`C:\Users\benvr\.claude\plans\i-want-a-new-mod-telescreen.md`). This file
describes what exists in the code and why it is shaped that way.

## Content (M0)

Everything is C++; the mod has no `Content` folder. Vanilla assets are referenced by path
(`Content/TSAssetPaths.h`).

- `ATSBuildableScreen` derives from `AFGBuildableSignBase`, the base of every vanilla sign, not from
  `AFGBuildableWidgetSign`. That keeps the vanilla sign hologram (`AFGStandaloneSignHologram`: wall snapping, auto
  poles, height adjust) working, because it only needs the sign base's dimensions and pole offsets, while leaving
  out the widget sign's text/icon layout, save data and interact UI.
- `ATSBuildableScreenSmall` / `ATSBuildableScreenLarge` copy the vanilla billboards' component layout, dimensions,
  pole offsets and clearance (values in `ReverseEngineering.md`). The screen face is an engine plane in the same
  place as the vanilla `ProxyMesh`, with a dynamic instance of the vanilla `MM_SignWidgetMaterial`, whose `SlateUI`
  texture shows the screen.
- Asset references are resolved at game instance start (`TSContent::ResolveVanillaAssets`, called from
  `UTSGameInstanceModule` at `INITIALIZATION`) and written into the class default objects, instead of being loaded
  in constructors: constructors run when the module loads, and loading Blueprint classes there is fragile. Because
  holograms and spawned buildables copy the class default's components, the meshes must be on the defaults before
  any world loads.
- `UTSDesc_Screen*`, `UTSRecipe_Screen*`, `UTSUnlockRecipe_Screens` and `UTSSchematic_Screens` are native
  subclasses of the game's descriptor, recipe, unlock and schematic classes. The schematic is an AWESOME Shop entry
  (25 coupons) registered by `UTSGameWorldModule`. Recipes mirror the vanilla billboard costs for now.
- Until the screen panel exists (M1), using a screen cycles through three test patterns. The pattern's coloured
  corners (red top-left, green top-right, blue bottom-left, yellow bottom-right) and the arrow pointing up show
  whether the screen plane's UVs are flipped.
