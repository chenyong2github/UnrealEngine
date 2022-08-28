# glTF Exporter plugin

The glTF Exporter lets UE4 content creators showcase any scene or model on the web in full interactive real-time 3D with minimal effort.


## Compatibility

Currently, the plugin works with Unreal Engine **4.25**, **4.26**, and **4.27**. It has primarily been tested on Windows but is designed to be platform-independent.


## Usage

- Alt 1: Export asset via Content Browser
  1. Right-click on a `StaticMesh`, `SkeletalMesh`, `AnimSequence`, `Level`, or `Material` asset in the Content Browser.
  1. Select `Asset Actions -> Export...`
  1. Change `Save as type` to `.gltf` (or `.glb`) and click `Save`
  1. When `glTF Export Options` window is shown, click `Export`

- Alt 2: Export current level via File Menu
  1. Select any number of actors in the current level
  1. In the top menu, select `File -> Export Selected...`
  1. Change `Save as type` to `.gltf` (or `.glb`) and click `Save`
  1. When `glTF Export Options` window is shown, click `Export`


## Documentation

- [What is glTF?](Docs/what-is-gltf.md)
- [What can be exported?](Docs/what-can-be-exported.md)
- [Export options reference](Docs/export-options-reference.md)
