glTF exporter plugin
====================

An Unreal Editor plugin for exporting to web-optimized glTF 2.0 (incl custom and ratified extensions).


Implementation status
---------------------

Feature                   | Status |
------------------------- | :----: |
Web-based Viewer          |   👷   |
Models                    |   👷   |
└ Static Mesh             |   ✔️   |
└ Skeletal Mesh           |        |
└ Animation Sequence      |        |
└ Mesh Quantization       |   👷   |
Materials                 |   👷   |
└ Default Lit             |   ✔️   |
└ Unlit                   |   👷   |
└ Clear Coat              |        |
└ Blend Modes             |   👷   |
└ Constant Input          |   ✔️   |
└ Parameter Input         |   ✔️   |
└ Texture Input           |   👷   |
└ Material Instance       |   ✔️   |
└ Material Baking         |        |
Levels                    |   👷   |
└ Actor/Blueprint         |   ✔️   |
└ Static Mesh             |   ✔️   |
└ Skeletal Mesh           |        |
└ Light                   |        |
└ Camera                  |        |
└ Orbital Camera          |        |
└ Reflection Capture      |        |
└ HDRI Backdrop           |   👷   |
└ Interaction Hotspot     |   👷   |
└ Level Variant Sets      |        |
└ Baked Lightmaps         |        |
Textures                  |        |
└ 2D                      |        |
└ HDR                     |        |
└ Cubemap                 |        |
└ Lightmap                |        |
Exporter Options          |   👷   |


Installation
------------

* Alt 1: Install to project:
  1. Go to project folder which contains `[ProjectName].uproject`
  1. Create a folder called `Plugins` (if it doesn't already exist).
  1. Copy `GLTFExporter` folder into the `Plugins` folder.

* Alt 2: Install to Unreal Engine:
  1. Go to the plugin folder of Unreal Engine which is `Engine/Plugins`
  1. Copy `GLTFExporter` folder into the `Plugins` folder.

In Unreal Editor, open `Menu -> Edit -> Plugins` and make sure `glTF Exporter` is installed and enabled.


Usage
-----

* Alt 1: Export asset via Content Browser
  1. Right-click on a `StaticMesh`, `Level`, or `Material` asset in the Content Browser.
  1. Select `Asset Actions -> Export...`
  1. Change `Save as type` to `.gltf` and click `Save`
  1. When `Export Options` window is shown, click `Export`

* Alt 2: Export current level via File Menu
  1. Select any number of actors in the current level
  1. In the top menu, select `File -> Export Selected...`
  1. Change `Save as type` to `.gltf` and click `Save`
  1. When `Export Options` window is shown, click `Export`

The exported file can be opened in any glTF viewer including those readily available on the web.
