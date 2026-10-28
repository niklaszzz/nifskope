Build `NifSkope_tests.pro` with the same Qt 5 kit as the application. The test
executable expects `nif.xml` beside it and the source directory as its first
argument. Run the default document tests with an offscreen Qt platform. Add
`--gizmo` on a desktop OpenGL platform to exercise mouse input, rendering, and
the main window's Undo/Redo actions, locked-mesh vertex selection, vertex
transforms, snapping, light-marker picking, and Show Nodes visibility.

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
