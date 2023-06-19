// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RigVMEditor : ModuleRules
{
    public RigVMEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "Engine",
                "RigVM",
                "RigVMDeveloper",
                "SlateCore",
                "BlueprintGraph",
                "GraphEditor",
                "UnrealEd",
            }
        );
    }
}
