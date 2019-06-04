// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class TraceLog : ModuleRules
{
	public TraceLog(ReadOnlyTargetRules Target) : base(Target)
	{
		bRequiresImplementModule = false;
		PrivateIncludePathModuleNames.Add("Core");

        if (Target.Platform == UnrealTargetPlatform.PS4)
        {
            PublicAdditionalLibraries.Add("SceNet_stub_weak");
        }
    }
}
