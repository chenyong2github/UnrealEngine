// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HTTPServer : ModuleRules
{
    public HTTPServer(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicIncludePaths.AddRange(
			new string[] {
				"Runtime/Online/HTTPServer/Public",
            }
        );

        PrivateIncludePaths.AddRange(
            new string[] {
                "Runtime/Online/HTTPServer/Private",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "Sockets",
            }
        );
    }
}
