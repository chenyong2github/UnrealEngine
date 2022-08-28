# Export options reference

Option                         | Description
-------------------------------| ----------------------------------------------------------------------------------------------------------------------------
`Export Uniform Scale`         | Scale factor used for exporting all assets (0.01 by default) for conversion from centimeters (Unreal default) to meters (glTF).
`Export Preview Mesh`          | If enabled, the preview mesh for a standalone animation or material asset will also be exported.
`Bundle Web Viewer`            | If enabled, Unreal's glTF viewer (including an executable launch helper) will be bundled with the exported files. It supports all extensions used.
`Show Files When Done`         | If enabled, the target folder (with all exported files) will be shown in explorer once the export has been completed successfully.
`Export Unlit Materials`       | If enabled, materials with shading model unlit will be properly exported. Uses extension KHR_materials_unlit.
`Export Clear Coat Materials`  | If enabled, materials with shading model clear coat will be properly exported. Uses extension KHR_materials_clearcoat, which is not supported by all glTF viewers.
`Export Extra Blend Modes`     | If enabled, materials with blend modes additive, modulate, and alpha composite will be properly exported. Uses extension EPIC_blend_modes, which is supported by Unreal's glTF viewer.
`Bake Material Inputs`         | Bake mode determining if and how a material input is baked out to a texture. Can be overriden by material- and input-specific bake settings, see GLTFMaterialExportOptions.
`Default Material Bake Size`   | Default size of the baked out texture (containing the material input). Can be overriden by material- and input-specific bake settings, see GLTFMaterialExportOptions.
`Default Material Bake Filter` | Default filtering mode used when sampling the baked out texture. Can be overriden by material- and input-specific bake settings, see GLTFMaterialExportOptions.
`Default Material Bake Tiling` | Default addressing mode used when sampling the baked out texture. Can be overriden by material- and input-specific bake settings, see GLTFMaterialExportOptions.
`Default Input Bake Settings`  | Input-specific default bake settings that override the general defaults above.
`Default Level Of Detail`      | Default LOD level used for exporting a mesh. Can be overriden by component or asset settings (e.g. minimum or forced LOD level).
`Export Vertex Colors`         | If enabled, export vertex color. Not recommended due to vertex colors always being used as a base color multiplier in glTF, regardless of material. Often producing undesirable results.
`Export Vertex Skin Weights`   | If enabled, export vertex bone weights and indices in skeletal meshes. Necessary for animation sequences.
`Export Mesh Quantization`     | If enabled, export Unreal-configured quantization for vertex tangents and normals, reducing size. Requires extension KHR_mesh_quantization, which is not supported by all glTF viewers.
`Export Level Sequences`       | If enabled, export level sequences. Only transform tracks are currently supported. The level sequence will be played at the assigned display rate.
`Export Animation Sequences`   | If enabled, export single animation asset used by a skeletal mesh component or hotspot actor. Export of vertex skin weights must be enabled.
`Retarget Bone Transforms`     | If enabled, apply animation retargeting to skeleton bones when exporting an animation sequence.
`Export Playback Settings`     | If enabled, export play rate, start time, looping, and auto play for an animation or level sequence. Uses extension EPIC_animation_playback, which is supported by Unreal's glTF viewer.
`Texture Image Format`         | Desired image format used for exported textures.
`Texture Image Quality`        | Level of compression used for exported textures, between 1 (worst quality, best compression) and 100 (best quality, worst compression). Does not apply to lossless formats (e.g. PNG).
`No Lossy Image Format For`    | Texture types that will always use lossless formats (e.g. PNG) because of sensitivity to compression artifacts.
`Export Texture Transforms`    | If enabled, export UV tiling and un-mirroring settings in a texture coordinate expression node for simple material input expressions. Uses extension KHR_texture_transform.
`Export Lightmaps`             | If enabled, export lightmaps (created by Lightmass) when exporting a level. Uses extension EPIC_lightmap_textures, which is supported by Unreal's glTF viewer.
`Texture HDR Encoding`         | Encoding used to store textures that have pixel colors with more than 8-bit per channel. Uses extension EPIC_texture_hdr_encoding, which is supported by Unreal's glTF viewer.
`Export Hidden In Game`        | If enabled, export components that are flagged as hidden in-game.
`Export Lights`                | Mobility of directional, point, and spot light components that will be exported. Uses extension KHR_lights_punctual.
`Export Cameras`               | If enabled, export camera components.
`Export Camera Controls`       | If enabled, export GLTFCameraActors. Uses extension EPIC_camera_controls, which is supported by Unreal's glTF viewer.
`Export Animation Hotspots`    | If enabled, export GLTFHotspotActors. Uses extension EPIC_animation_hotspots, which is supported by Unreal's glTF viewer.
`Export HDRI Backdrops`        | If enabled, export HDRIBackdrop blueprints. Uses extension EPIC_hdri_backdrops, which is supported by Unreal's glTF viewer.
`Export Sky Spheres`           | If enabled, export SkySphere blueprints. Uses extension EPIC_sky_spheres, which is supported by Unreal's glTF viewer.
`Export Variant Sets`          | If enabled, export LevelVariantSetsActors. Uses extension EPIC_level_variant_sets, which is supported by Unreal's glTF viewer.
`Export Material Variants`      | Mode determining if and how to export material variants that change the materials property on a static or skeletal mesh component.
`Export Mesh Variants`         | If enabled, export variants that change the mesh property on a static or skeletal mesh component.
`Export Visibility Variants`   | If enabled, export variants that change the visible property on a scene component.
