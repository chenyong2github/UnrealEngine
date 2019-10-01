// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapTablet : ModuleRules
	{
		public MagicLeapTablet( ReadOnlyTargetRules Target ) : base(Target)
		{
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

            PublicDependencyModuleNames.Add("InputDevice");

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "ApplicationCore",
                    "Engine",
                    "InputCore",
                    "MagicLeap",
                    "MLSDK"
                }
            );
        }
	}
}
