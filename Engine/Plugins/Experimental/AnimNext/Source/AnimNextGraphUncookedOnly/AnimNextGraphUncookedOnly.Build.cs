// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextGraphUncookedOnly : ModuleRules
	{
		public AnimNextGraphUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
		{
			
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"RigVM",
					"RigVMDeveloper",
					"ControlRig",
					"ControlRigDeveloper",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"AnimNext",
					"AnimNextGraph",
					"BlueprintGraph",	// For K2 schema
					"AnimationCore",
					"AnimGraph",
					"Kismet",
					"Slate",
					"SlateCore",
				}
			);


			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
					}
				);
			}
		}
	}
}