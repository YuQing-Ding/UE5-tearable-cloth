# 3ds Max Cloth Pin Weight Exporter

File: `ClothPinWeightExporter.ms`

## What it does
- Reads vertex color from **Map Channel 0**, using **Red** channel as pin weight.
- Exports `vertex_index,weight` text file for Unreal `AClothSimActor` external pin weight loader.
- Vertex index in file is **0-based**.

## Install in 3ds Max
1. Open `Scripting > Run Script...`
2. Pick `ClothPinWeightExporter.ms`
3. UI window `Cloth Pin Weight Exporter` appears.

Optional permanent install:
1. Put the script under your Max scripts folder.
2. Use `Customize > Customize User Interface` and find macro category `Physics_SandBox`.
3. Assign to menu/toolbar.

## Artist workflow
1. Select cloth mesh.
2. Add `VertexPaint` modifier.
3. Paint **Red channel** (`R=0..1`) as pin weight.
   - `R=1` means stronger pin influence.
   - `R=0` means free.
4. In exporter UI:
   - `Pick Cloth Mesh`
   - set `Weight Scale` / `Invert` if needed
   - click `Export CSV/TXT for Unreal`
5. In Unreal `ClothSimActor`:
   - enable `bUseExternalPinWeights`
   - set `ExternalPinWeightFile`
   - click `ReloadExternalPinWeights` (or `RebuildCloth`)

## Format example
```text
# vertex_index,weight
0,1.000000
1,0.850000
2,0.000000
```

## Important notes
- Mesh topology and vertex order must match Unreal mesh exactly.
- If Unreal logs `count does not match cloth point count`, missing entries are treated as `0`.
- For custom static mesh cloth, use the same source mesh/FBX pipeline to keep indices stable.
