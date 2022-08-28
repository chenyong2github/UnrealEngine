glTF exporter plugin
====================

Unreal Editor plugin for exporting UE4 assets to glTF 2.0 (with custom extensions).


Installation
------------

* Alt 1: Install to project:
  1. Go to project folder which contains `[ProjectName].uproject`
  1. Create a folder called `Plugins` (if it doesn't already exist)
  1. Copy this `GLTFExporter` folder into the `Plugins` folder

* Alt 2: Install to Unreal Engine:
  1. Go to the plugin folder of Unreal Engine which is `Engine/Plugins`
  1. Copy this `GLTFExporter` folder into the `Plugins` folder

In Unreal Editor, open `Menu -> Edit -> Plugins` and make sure `glTF Exporter` is installed and enabled.


Usage
-----

* Alt 1: Export asset via Content Browser
  1. Right-click on a `StaticMesh`, `SkeletalMesh`, `AnimSequence`, `Level`, or `Material` asset in the Content Browser.
  1. Select `Asset Actions -> Export...`
  1. Change `Save as type` to `.gltf` (or `.glb`) and click `Save`
  1. When `glTF Export Options` window is shown, click `Export`

* Alt 2: Export current level via File Menu
  1. Select any number of actors in the current level
  1. In the top menu, select `File -> Export Selected...`
  1. Change `Save as type` to `.gltf` (or `.glb`) and click `Save`
  1. When `glTF Export Options` window is shown, click `Export`

If the Web Viewer is bundled, run `GLTFLaunchHelper.exe` in the target folder to view the export asset in the Unreal glTF Viewer.
