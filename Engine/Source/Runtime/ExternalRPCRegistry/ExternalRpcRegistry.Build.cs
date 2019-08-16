// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ExternalRpcRegistry : ModuleRules
{
    public ExternalRpcRegistry(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "InputCore",
				"Json",
				"JsonUtilities",
                "HTTPServer"
            }
        );
    }
}
