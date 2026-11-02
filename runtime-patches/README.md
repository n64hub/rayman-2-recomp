# runtime-patches

This game's own changes to the submodules, applied on top of N64Recomp-browser's patches, in order of name, by
`apply.cmake` here when `CMakeLists.txt` configures and when `tools/build_n64recomp.sh` runs. A new change is a new
file per submodule, `<submodule>-<n>-<what>.patch`, numbered after that submodule's highest, with a line here.

- `N64ModernRuntime-1-sp-done-after-display-list.patch`: ultramodern tells the game a graphics task's RSP work is
  done once RT64 has read its display list, not as the task starts. The game changes what a list reads as soon as
  the RSP is done, so with the early message a slow renderer (lavapipe, a page on a busy host) read a list being
  rewritten: RT64 crashed reading it, or found no GBI for the task.
