// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GLTFExporter : ModuleRules
	{
		public GLTFExporter(ReadOnlyTargetRules Target) : base(Target)
		{
		    // NOTE: ugly hack to access plugin info (should propose change to engine)
			var BindingFlags = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;
			var FieldInfo = typeof(ModuleRules).GetField("Plugin", BindingFlags);
			var Plugin = FieldInfo != null ? FieldInfo.GetValue(this) as PluginInfo : null;

			if (Plugin != null && Plugin.Descriptor != null)
			{
				PrivateDefinitions.Add("GLTFEXPORTER_FRIENDLY_NAME=TEXT(\"" + Plugin.Descriptor.FriendlyName + "\")");
				PrivateDefinitions.Add("GLTFEXPORTER_VERSION_NAME=TEXT(\"" + Plugin.Descriptor.VersionName + "\")");
			}

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"Json",
					"RenderCore",
					"RHI",
					"LevelSequence",
					"MovieScene",
					"MovieSceneTracks",
					"VariantManagerContent",
				}
				);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"MessageLog",
						"Slate",
						"SlateCore",
						"Mainframe",
						"InputCore",
						"EditorStyle",
						"Projects",
						"MeshMergeUtilities",
						"MeshDescription",
						"StaticMeshDescription",
						"GLTFMaterialBaking",
						"GLTFMaterialAnalyzer"
					}
					);
			}
		}
	}
}
