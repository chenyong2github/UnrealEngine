// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapLocationServices : ModuleRules
	{
        public MagicLeapLocationServices(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

            PublicIncludePathModuleNames.AddRange
            (
                new string[]
                {
                    "LocationServicesBPLibrary"
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "LuminRuntimeSettings",
                    "MLSDK",
                    "MagicLeap",
                    "LocationServicesBPLibrary"
                }
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "LocationServicesBPLibrary"
                }
            );
        }
	}
}
