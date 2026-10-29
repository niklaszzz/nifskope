# NiRoom / NiPortal editor research

Analyzed against the supplied Gamebryo 2.6 CHM, MicroSkope's current NIF schema and renderer, and the existing MicroVolts sample NIFs. This is a design analysis for a later editor.

## Findings that matter for an editor

A room editor is feasible. The serialized data already contains room volumes, fixture membership, and directed visibility connections. No visibility compilation step is required by the documented runtime. Editing the data correctly requires keeping the scene hierarchy and the separate visibility graph consistent.

### Room volumes

`NiRoom` inherits `NiNode`. `Wall Planes` defines the intersection of inward-facing half-spaces in **room model space**. These planes identify the camera's cell; they are independent of the rendered wall meshes.

The documentation's NiPlane equation is `dot(N, X) = C`. A point belongs to a room when every wall satisfies:

```
dot(N, cameraInRoomModelSpace) - C >= -epsilon
```

Transform the world-space camera into the room's model space before testing. This avoids mixing local plane constants with world coordinates. The runtime derives world-space planes during scene updates; those derived planes are not additional editor-authored wall data.

Normals need not be unit length. If normalizing a plane, divide both its normal and its constant by the normal's original length. Otherwise the plane moves. Tolerances should account for normal length.

The volume is convex. An L-shaped physical room needs multiple cells connected through openings. The documentation permits deliberately unbounded cells, such as rooms defined only by vertical planes in an interior-only level. Therefore, a validator should distinguish an intentionally unbounded cell from an invalid/empty cell rather than require six planes universally.

For a bounded room, its visible editor cage can be reconstructed by intersecting triples of wall planes, retaining intersection points inside every half-space, then assembling each plane's face. The cage is an editor visualization, not a mesh to serialize. An initial box-room tool can write six inward-facing planes directly; arbitrary convex-plane editing can follow.

### Portals and orientation

`NiPortal` inherits `NiAVObject`. Its vertices are a planar polygon in **portal model space**. The portal's own transform and ancestor transforms position it in the scene. `Adjoiner` references the destination scene, normally a `NiRoom`.

The destination does **not** inherit the portal's transforms, materials or effects. `Adjoiner` is a visibility link, not a parent-child relationship.

A portal is one-way. A two-way doorway normally contains:

```
Room A -> outgoing portal P_AB -> Adjoiner = Room B
Room B -> outgoing portal P_BA -> Adjoiner = Room A
```

The converter documentation requires the portal polygon to face into its source room. The return portal uses the reversed polygon. If the rooms/portals have different transforms, creating the return polygon requires converting the shared world-space opening into the return portal's local space; reversing the same local-coordinate array is insufficient.

Unlike room volumes, portal polygons are **not required to be convex**. The docs also allow repeated or collinear vertices. A first editor can offer rectangular portals while preserving imported general polygons. It should not reject every concave imported portal as invalid.

A portal does not cut a doorway in a mesh. The visible wall geometry must already have the opening. Portal editing and wall-mesh editing are separate operations.

### Ownership and link lists

The schema stores all of the following, and the samples use them consistently:

| Object | Serialized fields | Editor responsibility |
| --- | --- | --- |
| `NiRoomGroup` | inherited `Children`, `Shell`, `Num Rooms`, `Rooms` | Maintain room membership and ordinary scene ownership together. |
| `NiRoom` | inherited `Children`, `Num Walls`, `Wall Planes`, incoming/outgoing portal lists, fixture list | Keep counts, arrays and child links consistent. |
| `NiPortal` | inherited transform, `Portal Flags`, `Plane Count`, `Num Vertices`, `Vertices`, `Adjoiner` | Preserve transform conventions and the directed target. |

For a connection A -> B, the source room's outgoing list references the portal, the portal's `Adjoiner` points to B, and B's incoming list should reference that same portal. In the samples the outgoing portal is also an ordinary child of A.

Incoming lists are documented as an optional runtime optimization: the room temporarily disables its incoming portals while traversing its outgoing portals. Maintaining them is still the sensible editor default. Missing incoming entries should be reported as an optimization/consistency issue, not proof that a file is fundamentally unreadable.

Fixtures are rendered objects or subtrees: wall surfaces, floor, ceiling, furniture, and similar geometry. They are not a special mesh class. Fixture membership should be displayed independently from the room's bounding planes. Assigning or moving fixtures must preserve their world transforms and account for any existing shared references.

The SDK permits a general `NiAVObject` adjoiner, even though the local schema's template is `NiNode`. A room destination is the standard workflow; preserve existing non-room destinations and describe their effect rather than silently replacing them. An adjoiner pointing at one mesh only exposes that mesh and does not continue traversal through the destination room's other portals.

### Runtime traversal and preview

`NiRoomGroup` finds the cell containing the camera, checking the previous cell first as an optimization. If the camera is outside all cells:

- With a shell: use the exterior shell.
- Without a shell: reuse the most recent valid room, avoiding flicker through small gaps.
- With no previously valid room and no shell: the reviewed documentation does not fully specify the initial fallback; verify against the target runtime rather than inventing an exact behavior.

Room overlap should be flagged. If several rooms contain a point, the SDK does not promise which one is chosen, although the cached previous room can affect the result.

The portal culler projects an opening into screen space, computes its screen-aligned bounding rectangle, intersects that rectangle with the current view rectangle, and recursively visits the adjoiner through the reduced frustum. It does not accumulate an arbitrary new polygon clipping plane for every portal. A preview claiming to match Gamebryo should use that rectangle-based method.

The room traversal uses a **current recursion-path** guard. A room may need to be visited again through another opening with a different visible rectangle. A single global 'visited room' set can incorrectly hide geometry. Separately, `NiFixtureCuller` deduplicates leaf render objects so that fixtures exposed through multiple windows are drawn once.

Room rendering timestamps, cached last-room values, derived world planes, and traversal state are runtime mechanisms. They are not additional authoring fields to invent in the NIF.

## What the samples establish

Read-only inspection using MicroSkope's own NIF model produced:

| Sample | NIF version | Room groups | Rooms | Portals |
| --- | --- | ---: | ---: | ---: |
| `Working.nif` | 20.6.0.0 | 1 | 16 | 54 |
| `NotWorking.nif` | 20.6.0.0 | 1 | 16 | 52 |
| `World_colp.nif` | 20.6.0.0 | 0 | 0 | 0 |

Both room-containing files have `PORTAL_NDLRG`, no shell (`-1`), and 16 room children matching group membership. Sampled rooms have six wall planes. Sampled portals have four vertices, `Portal Flags = 1`, and `Plane Count = 0`.

Across both room graphs, every outgoing portal is a child of its source room, every outgoing target is a room, and every outgoing portal has a corresponding entry in its destination's incoming list. These checks do not prove geometric correctness, winding correctness, room non-overlap, or complete reverse-pair coverage. The difference of two portals alone does not diagnose why the files were named Working and NotWorking.

The API documents an active/open flag but does not establish its serialized bit encoding. The observed value of 1 is evidence, not a sufficient specification for every bit. Confirm the active bit against serializer/SDK definitions or a controlled active/inactive sample before implementing an open/closed toggle. Preserve other flag bits. The schema describes `Plane Count` as unused for 20.x; it is not the room's wall count and should not be filled with six.

## Current MicroSkope support

The XML schema can load and edit these fields through the existing block model. The current renderer has no room-specific culler or room/portal editing overlay. `Scene::getNode` treats `NiRoomGroup` and `NiRoom` as generic `NiNode` instances, while `NiPortal` is not instantiated as a dedicated visible/pickable portal node. Normal viewport rendering therefore does not currently reproduce Gamebryo's room traversal.

A dedicated room preview is needed to validate the game's visibility behavior; ordinary mesh visibility in MicroSkope is not enough. The existing editor-only isolation/overlay pattern is a useful integration point, provided preview state remains separate from saved NIF visibility flags.

Room planes are not collision geometry. Gamebryo describes optional application-level use of room membership to organize collision groups, but that does not establish automatic PhysX rebuilding in MicroVolts. Keep collision updates explicit unless game-specific evidence establishes a link.

## Proposed editor workflow

1. **Inspect existing rooms first.** Show colored convex room cages, inward-normal indicators, portal polygons, direction arrows, fixture membership, and a camera-inside-room readout. Picking a room or portal selects its block. Provide 'show all geometry' and a separate portal-visibility preview.
2. **Create/edit box rooms.** Let the user place and resize a box that generates six model-space wall planes. Keep the volume separate from fixture meshes; moving furniture should not silently resize a room. Add arbitrary convex-plane editing later.
3. **Connect two rooms.** Select a rectangular opening, choose rooms A/B, and create a logical portal pair. Generate two directed NIF objects with reversed facing, correct local transforms, both outgoing/incoming lists, and source child ownership. Editing the logical opening updates both polygons atomically.
4. **Assign fixtures.** Show meshes/subtrees referenced by each room, support explicit assignment, and highlight objects spanning multiple cells for review. Preserve world transforms when reparenting.
5. **Validate before export.** Check empty/contradictory volumes, overlaps/gaps, missing/duplicate links, stale counts, nonplanar/degenerate polygons, portal/source orientation, portal placement near the intended shared boundary, missing return directions when a two-way connection was requested, fixture membership, and dangling targets. Distinguish hard errors from optional optimizations and intentional one-way portals.
6. **Preview traversal.** Emulate camera-cell lookup, shell/last-room fallback, active portals, rectangle clipping, recursion-path guards and fixture deduplication. Useful diagnostics include 'why is this room visible?', 'why was this portal rejected?', and room/fixture visibility colors.

Each edit should be one document undo transaction. Deleting or duplicating a room requires graph-aware link repair; the SDK explicitly warns that its ordinary clone operation does not support portal graphs. MicroSkope should use a block-map/deep-copy approach that preserves internal cycles and makes treatment of external links explicit.

Automatic convex decomposition and automatic portal discovery are later features. The first useful version is visual inspection, validation, box-room editing and paired rectangular portals.

## Remaining verification before implementation

- Confirm serialized portal active-bit semantics and the target game's treatment of inactive or hidden portal nodes.
- Check portal orientation against a known visible direction in-game; do not assume the viewport's triangle-winding convention matches every SDK convention.
- Validate room and portal transforms on samples with translated/rotated/scaled room parents, not only identity transforms.
- Reconstruct and test the actual room volumes and reverse-pair coverage; the current inspection checks references and field structure.
- Compare the proposed traversal preview with MicroVolts at doorway crossings, overlapping rooms, multiple visible openings, and outside-room positions.

## Sources and reproduction

Primary source: `C:/Users/nikty/Downloads/Gamebryo.chm`, supplied by the user. Extracted documentation pages used:

- [NiRoom class reference](../build/gamebryo-docs/Reference/CoreLibs/NiPortal/NiPortals_Class_Reference/NiRoom.htm)
- [NiPortal class reference](../build/gamebryo-docs/Reference/CoreLibs/NiPortal/NiPortals_Class_Reference/NiPortal.htm)
- [NiRoomGroup class reference](../build/gamebryo-docs/Reference/CoreLibs/NiPortal/NiPortals_Class_Reference/NiRoomGroup.htm)
- [NiFixtureCuller class reference](../build/gamebryo-docs/Reference/CoreLibs/NiPortal/NiPortals_Class_Reference/NiFixtureCuller.htm)
- [NiPlane class reference](../build/gamebryo-docs/Reference/CoreLibs/NiMain/NiMain_Class_Reference/NiPlane.htm), including the equation image
- [Portal Converter authoring guide](../build/gamebryo-docs/Reference/ToolLibs/Tool_Plug_ins/Portal_Converter.htm)
- [Portal scene strategies](../build/gamebryo-docs/Reference/CoreLibs/NiPortal/NiPortals_Scene_Strategies_and_Performance/Designing_Portal_Enabled_Levels.htm)
- [Basic portal mechanics](../build/gamebryo-docs/Reference/CoreLibs/NiPortal/NiPortals_Basics/Portals.htm)

The extracted pages are local build artifacts, not vendored SDK documentation. The report paraphrases those references; the proposed editor workflow is an engineering recommendation.

Code cross-checks: [NIF schema](../build/docsys/nifxml/nif.xml), [scene node creation](../src/gl/glscene.cpp), and [ordinary child hierarchy handling](../src/gl/glnode.cpp).

Read-only diagnostic:

```powershell
build/face-check/release/NifSkopeTests.exe . --portal-inspect build/qt-check/fixtures/Working.nif
```

Diagnostic source: [tests/portal_inspect.h](../tests/portal_inspect.h). It loads the NIF and prints structural facts; it does not save or modify the source file.