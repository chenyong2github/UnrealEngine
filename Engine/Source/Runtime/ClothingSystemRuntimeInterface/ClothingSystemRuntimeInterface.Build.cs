// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ClothingSystemRuntimeInterface : ModuleRules
{
	public ClothingSystemRuntimeInterface(ReadOnlyTargetRules Target) : base(Target)
	{
		ShortName = "ClothSysRuntimeIntrfc";

        PrivateIncludePaths.Add("Runtime/ClothingSystemRuntimeInterface/Private");
        SetupModulePhysicsSupport(Target);

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject"
            }
        );
    }
}
