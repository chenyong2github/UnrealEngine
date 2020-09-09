// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GoogleARCoreRendering : ModuleRules
{
	public GoogleARCoreRendering(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(new string[]
		{
			"GoogleARCoreRendering/Private",
			"GoogleARCoreBase/Private",
			"../../../../../../Source/Runtime/Renderer/Private",
		});
			
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Json",
			"CoreUObject",
			"Engine",
			"RHI",
			"Engine",
			"Renderer",
			"RenderCore",
			"ARUtilities",
		});
	}
}
