# NifSkope 2.0.dev7

NifSkope is a tool for opening and editing the NetImmerse file format (NIF). NIF is used by video games such as Morrowind, Oblivion, Skyrim, Fallout 3, Fallout: New Vegas, Civilization IV, and more. 

### Download

You can download the latest official release from [GitHub](https://github.com/niftools/nifskope/releases). More frequent development builds are posted in the [NifTools Discord](https://discord.gg/ZFjdN4x), pinned to #software.


### Discussion & Help

In the Block List, Ctrl-click to select or deselect individual blocks, or Shift-click to select a range. The current block controls the Block Details pane and viewport; block actions apply to the current block.

Use **Move**, **Rotate**, or **Scale** on the Render toolbar (or Render > Transform) to edit the current node or shape in the viewport. Drag a red, green, or blue arrow to move along the local X, Y, or Z axis; drag a ring to rotate around that axis. Scale uses the yellow center handle: drag right/up to enlarge or left/down to shrink, uniformly on all axes. Choose **Select** to hide the gizmo. Drag outside the handles to orbit as usual; Escape cancels an active drag. Pause animation playback before transforming objects or vertices.

For vertex editing, select a mesh in **Object Select**, then choose **Vertex Select**. LMB drag-box selects vertices through the locked mesh; Shift adds, Ctrl toggles, and Ctrl+A selects all. Use Move, Rotate, or Scale to transform the selection around its center. Enable **Snap Vertices** to snap the nearest selected anchor to another vertex under the pointer during Move; the target can belong to another visible mesh. Space+LMB pans and Alt+LMB orbits. Escape cancels a selection box or transform. Return to Object Select to choose another mesh.

This first vertex editor supports static, unskinned meshes: classic geometry, supported Bethesda vertex arrays, and NiMesh float32/float16 position streams with one contiguous region. Shared vertex buffers, skinned meshes, and morph/deformation modifiers show an explanatory message. UVs, colors, indices, and stored normals are preserved; normals are not recalculated automatically. Saved bounds update after a completed vertex transform.

With **Show Nodes** enabled, lights have bright, fixed-size sun markers that remain visible through geometry. Click a marker in Object Select to select the light, then move it with the gizmo or edit its properties. Show Nodes off hides the markers and disables their picking.

Select a mesh and click **UV Editor** on the render toolbar or Render menu (or right-click the mesh and choose **Texture > Edit UV**). NiMesh float32/float16 UV streams are supported alongside the existing classic/Bethesda mesh paths. Left-click selects and drags UVs; drag a box from empty space to select several, use Shift to add, Ctrl+A to select all, middle-drag to pan, the wheel to zoom, and F to frame the mapping. Right-click inside the editor for scale/translate, rotation, coordinate-set selection, or loading a DDS/TGA/BMP background texture when the original cannot be found. Changes appear in the main document and save normally. Ctrl+Z/Ctrl+Y use document undo/redo; each UV drag is one undo step and the editor remains open when undoing a UV edit. NiMesh support currently requires one triangle-list submesh and contiguous UV/index regions; shared UV buffers are refused to avoid changing another object's mapping. UV writes preserve the other components of interleaved streams.

For MicroVolts static PhysX triangle collision, **Show Collision** displays the stored collision as a cyan wireframe, with the selected source highlighted in orange. Click a collision edge in Object Select to select its matching source mesh. Enable **Show Hidden** if the source meshes are hidden, then use Vertex Select and the transform tools to edit them. Object transforms also work. Click **Rebuild Collision** on the render toolbar (or Render menu), select the game's `Bin/PhysXCooking.dll` on first use, and save the NIF. Each collider needs exactly one source NiMesh with the same name. Rebuilding uses PhysX 2.8.4 and validates every mesh with the game runtime before replacing any collision data. The entire rebuild is one undo step. Neither vertex edits nor saving automatically rebuild collision; the button is disabled for files without NiPhysXProp.

This initial collision editor supports static triangle meshes with one state. Convex shapes, dynamic/parented actors, joints, cloth, mixed per-face materials, and shared collision mesh descriptions are rejected without changing the document. Other collision formats remain on their existing code paths. On Windows, keep `PhysxCooker.exe` beside NifSkope; qmake builds this small x86 helper with the .NET Framework compiler. The game's PhysX DLLs are not bundled or changed. The supplied player-collision sample passes cooking, runtime loading, coordinate/winding preservation, save/reload and undo tests; gameplay still needs testing in MicroVolts.

**Ctrl+Z** undoes document edits, including block deletion, block/branch paste, field and array edits, and gizmo transforms. **Ctrl+Y** redoes them. Each gizmo drag or block action is one undo step; history retains the most recent 50 steps and resets when another document is loaded.

- Visit the [NifTools Discord](https://discord.gg/ZFjdN4x). To receive support use the #software channel.
- Visit the [NifTools.org](https://forum.niftools.org/) forum. To receive support for NifSkope please use the [Support subforum](https://forum.niftools.org/24-nifskope/).

### Issues

Anyone can [report issues at GitHub](https://github.com/niftools/nifskope/issues) or in the NifTools.org [Support subforum](https://forum.niftools.org/24-nifskope/).


### Contribute

You can fork the latest source from [GitHub](https://github.com/niftools/nifskope). See [Fork A Repo](https://help.github.com/articles/fork-a-repo) on how to send your contributions upstream. To grab all submodules, make sure to use `--recursive` like so:

```
git clone --recursive git://github.com/<YOUR_USERNAME>/nifskope.git
```

For information about development:

- Visit our [Discord #dev channel](https://discord.gg/zvWZrrJ).
- Visit the NifTools.org [development subforum](https://forum.niftools.org/6-nifskope-development/).
- Refer to our [GitHub wiki](https://github.com/niftools/nifskope/wiki#wiki-development) for information on compilation.  


### Miscellaneous

Refer to these other documents in your installation folder or at the links provided:

## [TROUBLESHOOTING](https://github.com/niftools/nifskope/blob/develop/TROUBLESHOOTING.md)

## [CHANGELOG](https://github.com/niftools/nifskope/blob/develop/CHANGELOG.md)

## [CONTRIBUTORS](https://github.com/niftools/nifskope/blob/develop/CONTRIBUTORS.md)
 
## [LICENSE](https://github.com/niftools/nifskope/blob/develop/LICENSE.md)

