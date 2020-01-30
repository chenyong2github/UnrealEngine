// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AppleARKitPoseTrackingLiveLink : ModuleRules
{
	public AppleARKitPoseTrackingLiveLink(ReadOnlyTargetRules Target) : base(Target)
	{
        //OptimizeCode = CodeOptimization.Never;
        
		PrivateIncludePaths.AddRange(
			new string[] {
				"../../../../../../Plugins/Runtime/AR/Apple/AppleARKit/Source/AppleARKit/Private",
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
                "HeadMountedDisplay",
//                "AugmentedReality",
                "LiveLink",
                "LiveLinkInterface",
                "AppleARKit",
                "AppleImageUtils"
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			PublicFrameworks.Add( "ARKit" );
		}
	}
}
