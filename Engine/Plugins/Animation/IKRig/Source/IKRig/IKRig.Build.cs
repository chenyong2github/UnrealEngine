// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IKRig : ModuleRules
{
	public IKRig(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"AnimationCore",
				"PBIK"
			}
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"ControlRig",
				"Core",
				
				"PBIK"
			}
			);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
