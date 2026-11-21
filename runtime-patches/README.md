# runtime-patches

This game's own changes to the submodules, applied on top of N64Recomp-browser's patches, in order of name, by
`apply.cmake` here when `CMakeLists.txt` configures and when `tools/build_n64recomp.sh` runs. A new change is a new
file per submodule, `<submodule>-<n>-<what>.patch`, numbered after that submodule's highest, with a line here.

- `N64ModernRuntime-1-sp-done-after-display-list.patch`: ultramodern tells the game a graphics task's RSP work is
  done once RT64 has read its display list, not as the task starts. The game changes what a list reads as soon as
  the RSP is done, so with the early message a slow renderer (lavapipe, a page on a busy host) read a list being
  rewritten: RT64 crashed reading it, or found no GBI for the task.
- `rt64-1-cpu-writes-into-a-shown-framebuffer.patch`: what the CPU writes into the framebuffer the VI shows reaches
  the screen. A present carrying those writes shows that framebuffer, not the frames RT64 interpolated from the list that
  drew it, which don't hold them: with MSAA (2x by default) every frame shown at the display's rate came from those. And
  a present dropped for a newer one still writes its changes into the framebuffer: each is RAM's difference from the
  one before, so a dropped one was lost for good. And a framebuffer at the VI's address that isn't as wide as the VI's or
  of its pixel size is shown from RAM, as when RT64 has none there: the VI gives it none of the CPU's changes, and it
  held a picture from before. The scene transition, which blacks out the last frame from the CPU, showed that frame
  still and cut to black, or stopped its blotches partway; and the title screen, at the address of the 3D frames at
  another width, showed the last 3D frame until the next level's transition cut it to black.
- `rt64-3-a-body-whose-translation-jumped-is-drawn-whole.patch`: under RT64's automatic interpolation, a transform
  whose translation isn't interpolated (it moved too far, or turned back) isn't rotated halfway either, but drawn where
  it is. Its rotation was always interpolated, so in the Woods of Light's opening shake, where every world matrix turns
  back and forth each frame, the level's mesh, its origin far from its ground, swung about that origin in the frames
  between and most of the ground left the picture.
