// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RemoteControl : ModuleRules
{
	public RemoteControl(ReadOnlyTargetRules Target) : base(Target)
    {
		PublicDependencyModuleNames.AddRange(
			new string[] {
                "Core",
                "CoreUObject",
            }
        );

		PrivateDependencyModuleNames.AddRange(
            new string[] {
				"Engine",
				"Serialization",
            }
        );

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnrealEd",
				}
			);
		}
	}
}
