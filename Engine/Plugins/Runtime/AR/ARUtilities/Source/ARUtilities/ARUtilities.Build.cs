// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ARUtilities : ModuleRules
{
	public ARUtilities(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(new string[]
		{
		});
				
		
		PrivateIncludePaths.AddRange(new string[]
		{
		});
			
		
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"LiveLink",
		});
			
		
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"AugmentedReality",
			"MRMesh",
		});
		
		
		DynamicallyLoadedModuleNames.AddRange(new string[]
		{
		});
	}
}
