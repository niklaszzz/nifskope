# Moving an existing mesh between rooms

Open **Render > Rooms**. The sidebar shows the selected mesh's assigned room, the room volumes that fully contain its vertices, and a destination list grouped by existing NiRoomGroup.

1. Select a static mesh in the viewport or Block List.
2. Move it using the existing movement tools. This changes its placement, not its room membership.
3. Read the containment suggestion. **Choose Suggested Room** selects a destination only when exactly one room contains the entire mesh. If the mesh spans a boundary, is outside the rooms, or lies inside overlapping rooms, choose the intended room explicitly.
4. Click **Assign Selected Mesh to Room**. This is the only room-membership mutation in the sidebar. Ctrl+Z undoes the reassignment independently of the preceding movement.

**Show room bounds and portal openings** draws editor-only outlines: the assigned room is orange, other room bounds are blue, and portal openings are green. Toggling this display does not change saved visibility flags or portal flags. It is a boundary visualization, not a simulation of the game's portal visibility traversal. The destination list also shows wall, portal and fixture counts for inspection.

**Validate Room Links (Read Only)** reports basic structural issues. It does not repair them, change portal directions, regenerate planes, or test full geometric overlap/visibility correctness. A report about an optional incoming portal reference or a general non-room adjoiner needs interpretation; it is not an instruction to rewrite an existing map.

## What reassignment changes

Supported transfers move one static NiMesh or NiTriShape between rooms in the same single existing room group. A mesh can be a direct room fixture or a child of an ordinary untagged fixture subtree.

The operation removes the mesh from its previous parent's Children list, removes its direct source Fixtures entry if present, and adds it to the destination room's Children and Fixtures lists. When extracting a mesh from a fixture subtree, the old subtree and its siblings remain intact in the original room.

A new local transform preserves the mesh's existing world position, rotation and scale under the new parent, within normal floating-point precision. Inherited render-property references are added locally where necessary to preserve the mesh's material/render state. Mesh buffers and material blocks themselves are not rewritten.

The entire transfer is one document undo transaction. Saving and reloading retains the new room membership.

## Preservation rules

The room/group/portal graph is inspected, not rebuilt. Reassignment preserves all existing NiRoomGroup blocks, portal vertices and flags, room wall planes, incoming/outgoing portal lists, unrelated fixtures, collision data, and unrelated blocks. It does not create/delete rooms or portals or regenerate room boundaries.

The transfer is blocked before writing when ownership or membership is ambiguous, a room belongs to a different/multiple group, another system references the affected mesh/fixture ancestry, a hierarchy is animated or specialized/tagged, an occluder name/tag is encountered, a transform is invalid, or inherited visibility/render effects cannot be preserved. Unassigned and multi-room meshes require manual review in this first version. These restrictions avoid guessing about existing map semantics.

## Verification

The `--rooms` test suite checks inspection without mutations, world-transform preservation beneath rotated/scaled parents, material inheritance, nested fixture extraction, blocked ambiguous/external/occluder/animated cases, GUI assignment, and exact undo/redo. It also exercises both existing MicroVolts sample maps and compares serialized portal/group/occluder blocks and protected room fields before/after assignment and save/reload.

```powershell
build/face-check/release/NifSkopeTests.exe . --rooms
```
## Existing portal openings

The Portal Openings section now supports previewed rectangular fitting and numeric
in-plane movement/resizing, with paired reverse-portal updates. See
[portal-workflow.md](portal-workflow.md). Room containment planes remain separate.
