// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GLTFExporter : ModuleRules
{
	public GLTFExporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bTreatAsEngineModule = true; // Only necessary when plugin installed in project

		{
			// NOTE: ugly hack to access plugin info (should propose change to engine)
			var PluginField = typeof(ModuleRules).GetField("Plugin", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
			if (PluginField == null)
			{
				throw new System.Exception("Missing internal member Plugin");
			}

			var Plugin = PluginField.GetValue(this) as PluginInfo;
			if (Plugin == null)
			{
				throw new System.Exception("Missing plugin information");
			}

			var PluginDescriptor = Plugin.Descriptor;
			if (PluginDescriptor == null)
			{
				throw new System.Exception("Missing plugin descriptor");
			}

			PrivateDefinitions.Add("GLTFEXPORTER_FRIENDLY_NAME=TEXT(\"" + PluginDescriptor.FriendlyName + "\")");
			PrivateDefinitions.Add("GLTFEXPORTER_VERSION_NAME=TEXT(\"" + PluginDescriptor.VersionName + "\")");
		}

		PublicDependencyModuleNames .AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Json",
				"RenderCore",
				"RHI",
				"ImageWrapper",
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
					"PropertyEditor",
					"Projects",
					"MeshMergeUtilities",
					"MeshDescription",
					"StaticMeshDescription",
					"GLTFMaterialBaking",
					"GLTFMaterialAnalyzer",
				}
			);
		}
	}
}
