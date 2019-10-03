// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ClothingSystemEditor : ModuleRules
{
	public ClothingSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicIncludePathModuleNames.Add("UnrealEd");
        PublicIncludePathModuleNames.Add("ClothingSystemRuntimeInterface");
        PublicIncludePathModuleNames.Add("ClothingSystemRuntimeNv");
        PublicIncludePathModuleNames.Add("ClothingSystemEditorInterface");

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Engine",
                "RenderCore"
            }
        );

        PublicDependencyModuleNames.AddRange(
			new string[] { 
				"Core",
				"CoreUObject",
				"ClothingSystemRuntimeInterface",
                "ClothingSystemRuntimeCommon",
                "ClothingSystemRuntimeNv",
                "ContentBrowser",
                "UnrealEd",
                "SlateCore",
                "Slate",
                "Persona",
                "ClothingSystemEditorInterface"
            }
		);
        
        SetupModulePhysicsSupport(Target);
	}
}
