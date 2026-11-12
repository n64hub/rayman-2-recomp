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
  one before, so a dropped one was lost for good. The scene transition, which blacks out the last frame from the CPU,
  showed that frame still and cut to black, or stopped its blotches partway.
