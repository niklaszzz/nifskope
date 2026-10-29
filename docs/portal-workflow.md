# Editing an existing window or doorway portal

Open **Render > Rooms**, then scroll to **Portal Openings**. This edits existing NiPortal polygons; visible wall geometry and collision remain separate.

## Fit an opening to mesh selection

1. Edit the visible wall/opening using the existing mesh tools.
2. In Vertex or Face mode, select the boundary vertices or planar faces defining the desired opening. Select the opening boundary, not the entire surrounding wall. The sidebar shows the selected vertex count.
3. Choose the existing portal in the dropdown. Entries identify source room, destination room, portal name and block number. Its current outline is yellow in the viewport, even if the general room overlay is off.
4. Leave **Update matching reverse portal too** checked for a normal two-way opening.
5. Click **Preview Fit Rectangle to Selection**. A magenta outline shows a rectangle enclosing the selected points in the portal's own planar axes. This is an editor-only preview; it does not modify the NIF.
6. Check that the outline matches the intended opening and click **Apply Portal Change**, or **Cancel Preview**.
7. Save the NIF. Ctrl+Z undoes the complete paired update in one step.

Selection points must define a nonzero-area planar region parallel to the chosen portal. They are projected onto the portal's existing divider plane; if the selected wall face is offset because of wall thickness, the preview reports the projection distance. Fitting does not move the room divider plane or change NiRoom containment planes.

## Numeric movement and resizing

Choose a portal and enter **Move U**, **Move V**, **Scale U**, and **Scale V**, then click **Preview Move / Resize**. U follows the existing polygon's first usable edge; V is perpendicular to it within the portal plane. Movement is in world units. Positive scale factors resize about the polygon's vertex-average center. Numeric adjustment preserves the existing polygon's shape and vertex count rather than replacing it with a rectangle.

The numeric fields reset after a successful apply. Each preview is relative to the currently stored opening. Nothing is written until Apply is clicked.

## Reverse portals and safety

A two-way opening normally has two directed NiPortal objects. Paired editing requires one reverse portal that points back to the source room and occupies the same opening with opposite facing. This uses world-space geometry, not names alone, so multiple doorways between the same rooms are distinguished. Compatible extra collinear boundary vertices can be matched.

Both polygons are converted into their own local coordinates and written together. Room/group membership, incoming/outgoing lists, Adjoiner references, transforms, portal flags and unrelated portal fields are preserved. No room bounds, meshes, materials, occluders or collision data are rebuilt by this operation.

If the reverse portal is missing or ambiguous, the preview reports it and does not guess. Uncheck paired editing only for an intentional one-way update. Unsupported ownership, cross-group targets, animation, nonplanar polygons and legacy explicit bound/collision bindings are refused with an explanation.

Preview snapshots guard the relevant source geometry, transforms and portal/room state. Changes to that state invalidate application of an old preview. The sidebar also cancels previews when selection/settings/document state changes or when it is hidden.

## Validation

`--portals` tests transformed and mirrored parents, rectangular fitting, parallel-face projection, numeric adjustments, paired facing, missing/ambiguous reverse portals, stale previews, cancel/apply from the sidebar, preservation of every unrelated block, exact undo/redo and save/reload. It also updates existing portal pairs in the two MicroVolts sample maps using temporary outputs only.

```powershell
build/face-check/release/NifSkopeTests.exe . --portals
```