// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioEditor : ModuleRules
{
	public AudioEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange
		(
			new string[]
			{
				"Editor/AudioEditor/Private",
				"Editor/AudioEditor/Private/Factories",
				"Editor/AudioEditor/Private/AssetTypeActions"
			}
		);

		PrivateDependencyModuleNames.AddRange
		(
			new string[]
			{
				"AudioMixer",
				"EditorSubsystem",
				"GameProjectGeneration",
				"ToolMenus",
				"UMG",
				"UMGEditor",
				"AudioExtensions"
			}
		);

		PublicDependencyModuleNames.AddRange
		(
			new string[]
			{
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"AudioMixer",
				"InputCore",
				"Engine",
				"UnrealEd",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"RenderCore",
				"LevelEditor",
				"Landscape",
				"PropertyEditor",
				"DetailCustomizations",
				"ClassViewer",
				"GraphEditor",
				"ContentBrowser",
			}
		);

		PrivateIncludePathModuleNames.AddRange
		(
			new string[]
			{
				"AssetTools"
			}
		);

		// Circular references that need to be cleaned up
		CircularlyReferencedDependentModules.AddRange
		(
			new string[]
			{
				"DetailCustomizations",
			}
		);

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			string PlatformName = Target.Platform == UnrealTargetPlatform.Win32 ? "Win32" : "Win64";

			string LibSndFilePath = Target.UEThirdPartyBinariesDirectory + "libsndfile/";
			LibSndFilePath += PlatformName;


			PublicAdditionalLibraries.Add(LibSndFilePath + "/libsndfile-1.lib");
			PublicDelayLoadDLLs.Add("libsndfile-1.dll");
			PublicIncludePathModuleNames.Add("UELibSampleRate");

			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/libsndfile/" + PlatformName + "/libsndfile-1.dll");

			PublicDefinitions.Add("WITH_SNDFILE_IO=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_SNDFILE_IO=0");
		}
	}
}
