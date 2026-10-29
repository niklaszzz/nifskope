Build `NifSkope_tests.pro` with the same Qt 5 kit as the application. The test
executable expects `nif.xml` beside it and the source directory as its first
argument. Run the default document tests with an offscreen Qt platform. Add
`--gizmo` on a desktop OpenGL platform to exercise mouse input, rendering, and
the main window's Undo/Redo actions, locked-mesh vertex selection, vertex
transforms, snapping, light-marker picking, and Show Nodes visibility.
Each run uses temporary INI preferences, leaving installed application settings untouched.

`--color-hex` exercises single-click property-table color editing, clipboard
copy/paste, Enter/Escape, exact float/HDR preservation on unchanged values,
undo/redo, invalid/incomplete input, RGBA/byte-color channel ordering, alpha
preservation for RGB-only paste and the existing color-wheel button. It writes
`color-hex-preview.png` beside the test executable.

Desktop checks also cover selecting coincident XYZ vertices without grouping
screen overlaps at different depths, and temporary mesh-center pivots for object
rotation/scaling under a transformed parent, including exact undo restoration.

`--light-indicators` checks the Gamebryo +X light direction under parent/light
rotations, fixed display length under scaled parents, spotlight cone angles,
omnidirectional point lights, rendered arrows, picking and Show Nodes visibility
in perspective and orthographic views. It verifies unchanged NIF bytes and undo
history, and writes `light-indicators-preview.png` beside the test executable.

`--light-falloff` verifies the documented attenuation formula against known
distances, combined and linear-only coefficients, constant-only/invalid values,
world-space distances under scaled parents, cone boundaries, rendered selection
visibility, live property updates, undo restoration and unchanged NIF data from
visualization. It writes `light-falloff-preview.png` beside the test executable.

Move-plane checks exercise XY/XZ/YZ dragging in perspective and orthographic
views under a transformed parent, plus selected-vertex movement and cancellation.
Walk checks cover camera continuity on mode changes, WASD movement with a visible
gizmo, and editing objects and vertices without moving the camera during a drag.

Workspace checks exercise reference compositing and opacity, click-through
selection, switching active models and their editors/Spells menu, preserving
camera and vertex selection, independent undo, saving/reloading separate files,
save cancellation and staging failures, duplicate destinations, and returning to
single-file Save. With both `Working.nif` and `World_colp.nif` fixtures, the suite
also loads real MicroVolts visuals/collision together, edits both, saves temporary
copies, and verifies the cooked PhysX payloads remain unchanged without Rebuild.
It writes `workspace-preview.png` and `workspace-microvolts-preview.png` beside
the test executable; the original fixtures are never written.

Reference-refresh checks warm the preview cache, repeatedly switch away and back,
move the original object, save through the ordinary Save action, and verify the
rendered reference position. They also check unsaved edits and inactive undo/redo.
Unshaded checks compare rendered pixels to the combined base, lightmap, translucent
decal and glow textures while the mesh has varying normals, dark vertex colors
and emissive color. They verify independent lightmap UVs, vertex alpha, unchanged
color under different editor lights, and unchanged NIF bytes when toggling the mode.
Black diffuse materials are tested on textured geometry and transparent decal
meshes, including material opacity without vertex colors. Repeated toggles must
restore the exact normal image and preserve the other preview modes. Untextured
meshes retain their own material diffuse color.
Detail/lightmap brightness checks render a white Detail texture with the original
2x multiplier, then adjust the slider through 100%, 150%, 50% and 0%. They verify
the remaining texture layers, alpha testing, normal lit rendering, exact restoration
at 200%, legacy preference migration, and unchanged NIF bytes and undo history.
Alpha-test checks cover ignored, emissive and ambient/diffuse vertex-color rules,
zero vertex alpha, and a texture with opaque and transparent cutout regions.
`NifSkopeTests.exe <source-directory> --alpha-check <World.nif>` compares visible
texture-alpha coverage against unshaded rendering on each alpha-tested map mesh
without changing the NIF. Meshes with no visible reference pixels are reported.

Drop checks send Qt drag-enter, drag-move and drop events through the real
viewport in its nested workspace layout. They verify actual loading, switching
to an already-open NIF without losing edits, cancellation, invalid directories,
and forwarding texture drags.

Viewport regression checks use the real startup/layout event path without
manually resizing the GL widget. They verify first-load coverage, recovery from
stale viewport/scissor state, consistent perspective pixel scale, window resizing
and maximize/restore, dock visibility, and adding/removing/switching workspace
tabs. A render preview is written as `viewport-resize-preview.png`.

Vertex storage checks cover selected-only edits, stored bounds, save/reload,
undo/redo, and rejecting shared geometry or destinations from another document.
When the MicroVolts fixtures below are present, they also verify that editing
positions leaves all unrelated buffer bytes intact in separate and interleaved
datastream layouts.

Optional MicroVolts regression fixtures belong in `build/qt-check/fixtures/`:

| File | SHA-256 of supplied fixture |
| --- | --- |
| Working.nif | a57bca92da7285c7699f4a99ea4b98fb97c40e94d1e3a2b7ca902f5b036b95e6 |
| NotWorking.nif | 680a448f0431d64a75f8e1d42e5812c3f8fb617a317d3dc6649ac1a49cd76d81 |

With those fixtures present, `--gizmo` also checks every NiMesh's decoded
geometry against its source datastreams, compares the working sample against
a geometry fingerprint captured before the compatibility change, and renders
both full scenes without external textures. Render previews are written beside
the test executable. The fixtures themselves are not modified or distributed.

`--inspect <file>...` prints datastream layout information without rendering.
`--mesh-check <file>...` checks mesh decoding using a desktop OpenGL context.

UV checks enumerate every UV-bearing mesh in both MicroVolts fixtures, verify
selected-coordinate writes preserve all other bytes of interleaved streams,
and test save/reload and exact global undo/redo. Desktop checks additionally
exercise actual UV mouse drags, rotation/scaling dialogs, texture backgrounds,
coordinate-set menus, and retaining the editor through undo/redo. A classic UV
array regression ensures the existing editor still works without a texture.
The desktop suite writes `uv-editor-preview.png` beside the test executable.

`--physx-check <World_colp.nif> <game/Bin/PhysXCooking.dll>` tests the supplied
nine-mesh player-collision sample against the game's PhysX 2.8.4 libraries.
It checks original collider/proxy alignment, explicit rebuild after a vertex edit,
runtime validation of the preview decoder and every new cooked mesh, preservation
of all other document fields, unchanged geometry and winding, failure atomicity,
save/reload, single-step undo/redo, and refusal to modify ordinary visual NIFs.
The DLLs stay in the user's installation and the original NIF is never written.
With `World_colp.nif` in the fixture directory (SHA-256
`fb28a298448ab84808d327791176becc0b96da76f96582bd11cb93dcf7c8f2de`),
`--gizmo` also exercises collision rendering, picking, vertex editing availability,
and the main-window action's enable state. It writes a collision render preview.

Run `--face-check` for classic/NiMesh face deletion, retained winding, deleting all
faces, save/reload, exact undo/redo, material independence, suffix naming and new
material creation. Run `--face-ui` with the Windows desktop platform for triangle
picking, moving its vertices with the gizmo, Delete, Ctrl+A and undo retention.
It writes `vertex-wireframe-preview.png` beside the test executable.

Editing regressions also cover duplicated NiMesh/classic geometry detachment,
retained original buffers, actual vertex removal with matching UV/normal/color
compaction, all-vertex removal, save/reload and exact undo/redo. The desktop face
checks test Tab cycling through the viewport and its container, action state,
X-ray depth rejection for vertices/faces, Shift+X toggling and menu synchronization, and keyboard deletion on a duplicate.
Real-file material checks verify attachment in the live hierarchy and save/reload
for New Material and Make Material Unique. The visible-only preview is written as
`vertex-visible-only-preview.png` beside the test executable.

`--controls-ui` checks W/E/R tool synchronization, RMB navigation and camera
continuity, Shift speed, wheel speed adjustment, focus loss, Ctrl+L with multiple
islands and coincident disconnected vertices, and axis/plane snapping under
rotated/scaled parents for vertices and faces. `--uv-ui` checks the existing UV
editor plus direct cage moving, resizing, uniform scaling, rotation, cancellation,
unselected-coordinate preservation, exact undo/redo and preview rendering.
It writes `uv-cage-preview.png` beside the test executable.

`--toolbar-ui` checks compact mode/transform cycling, active-icon synchronization
with Tab and V/W/E/R, grouping, icon resources and toolbar sizing. It exercises
swift RMB clicks, long holds, out-and-back pointer movement, navigation keys,
wheel changes and focus loss, plus native context events through both the viewport
and its container. The actual popup must open once for a swift click and never
for navigation. A cropped toolbar preview is written as
`compact-toolbar-preview.png` beside the test executable.

--editor-features checks numerical transforms under transformed parents, isolation,
UV/viewport selection synchronization, and material creation/assignment/editing
with exact undo, save/reload and dock integration.

--uv-ui also checks cube projection, seam attributes, partial-face projection,
active UV channels, exact undo/redo, and real MicroVolts NiMesh streams.
--editor-features checks owned UV window lifetime, mouse face synchronization,
mesh following, geometry relinking, and undo across geometry detachment.

--portal-inspect <nif-path> prints room/portal counts, sample wall planes and links,
portal flag values, and outgoing/incoming/child-reference consistency. Read-only;
used for the NiRoom editor research in docs/niroom-editor-research.md.

--rooms verifies conservative room reassignment, containment suggestions, overlays
and validation without mutations, protected map structures, world placement and
material inheritance, nested fixtures, blocked ambiguous/animated/occluder/external
references, exact undo/redo, and save/reload on both existing MicroVolts maps.

--physx-groups <collision-nif> <PhysXCooking.dll> validates an existing multi-mesh
NiNode collision source. It edits only an in-memory copy, tests parent/child
movement and a child vertex edit, compares merged positions/topology/winding,
verifies payload-only changes and exact undo/redo, saves/reopens temporary NIFs
with and without normal sanitization, and checks refusal of ambiguous/unsupported
sources. The input NIF is never overwritten.

--portals checks paired portal fitting and numeric edits, rotated/scaled/mirrored
transforms, parallel-plane projection, stale/ambiguous/invalid preview rejection,
UI selection handoff and cancellation, exact undo/redo and save/reload, and
preservation of unrelated structures on existing MicroVolts portal maps.
