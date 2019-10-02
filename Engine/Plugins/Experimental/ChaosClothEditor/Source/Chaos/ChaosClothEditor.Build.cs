// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosClothEditor : ModuleRules
{
    public ChaosClothEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.Add("Chaos/Private");

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "ClothingSystemEditorInterface",
                "SlateCore",
                "Slate",
                "Persona",
                "ChaosCloth",
                "UnrealEd",
                "Engine"
            }
        );

        SetupModulePhysicsSupport(Target);
    }
}
