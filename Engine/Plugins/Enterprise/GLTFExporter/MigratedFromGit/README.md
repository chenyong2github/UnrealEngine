glTF exporter plugin
====================

An Unreal Editor plugin for exporting assets to web-optimized glTF 2.0 (incl custom and ratified extensions).


Implementation status
---------------------

Feature                   | Status |
------------------------- | :----: |
Web-based Viewer          |   👷   |
Models                    |   👷   |
└ Static Mesh             |   ✔️   |
└ Skeletal Mesh           |        |
└ Animation Sequence      |        |
Materials                 |   👷   |
└ Default Lit             |   ✔️   |
└ Unlit                   |   👷   |
└ Clear Coat              |        |
└ Blend Modes             |   ✔️   |
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
