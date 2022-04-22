// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MediaIOCore : ModuleRules
	{
		public MediaIOCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"AudioExtensions",
					"Core",
					"CoreUObject",
					"Engine",
					"ImageWriteQueue",
					"Media",
					"MediaAssets",
					"MediaUtils",
					"MovieSceneCapture",
					"Projects",
					"RenderCore",
					"RHI",
					"Slate",
					"SlateCore",
					"TimeManagement"
				});
			
			PrivateIncludePaths.AddRange(
				new string[] {
				});

			if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux ||
			    Target.Platform == UnrealTargetPlatform.Mac)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"GPUTextureTransfer"
					});
			}

			PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "AudioMixer",
	                "AudioMixerCore",
                    "RenderCore",
                    "SignalProcessing",
	                "SoundFieldRendering"
				});

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("EditorFramework");
				PrivateDependencyModuleNames.Add("UnrealEd");
				PrivateDependencyModuleNames.Add("LevelEditor");
			}
		}
	}
}
