// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteSession : ModuleRules
{
	public RemoteSession(ReadOnlyTargetRules Target) : base(Target)
	{
		DefaultBuildSettings = BuildSettingsVersion.V2;

		PrivateIncludePaths.AddRange(
			new string[] {
				"../../../../Source/Runtime/Renderer/Private",
				// ... add other private include paths required here ...
			}
		);
			
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"MediaIOCore",
				"BackChannel",
				// ... add other public dependencies that you statically link with here ...
			}
		);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputDevice",
				"InputCore",
				"RHI",
				"Renderer",
				"RenderCore",
				"ImageWrapper",
				"MovieSceneCapture",
				"Sockets",
				"EngineSettings",
				"HeadMountedDisplay",
				"AugmentedReality",
				// iOS uses the Apple Image Utils plugin for GPU accellerated JPEG compression
				"AppleImageUtils"
			}
		);

		if (Target.bBuildEditor == true)
		{
			//reference the module "MyModule"
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
