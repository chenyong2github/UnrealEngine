// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ClothingSystemRuntimeNv : ModuleRules
{
	public ClothingSystemRuntimeNv(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Runtime/ClothingSystemRuntimeNv/Private");
        
        SetupModulePhysicsSupport(Target);

        PublicIncludePathModuleNames.Add("ClothingSystemRuntimeInterface");

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "ClothingSystemRuntimeInterface",
                "ClothingSystemRuntimeCommon"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Engine",
                "RenderCore",
                "SlateCore",
                "Slate"
            }
        );
    }
}
