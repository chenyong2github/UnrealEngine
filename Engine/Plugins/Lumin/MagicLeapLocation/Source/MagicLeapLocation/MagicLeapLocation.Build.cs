// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapLocation : ModuleRules
	{
        public MagicLeapLocation(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "LuminRuntimeSettings",
                    "MLSDK",
                    "MagicLeap",
                }
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
                }
            );
        }
	}
}
