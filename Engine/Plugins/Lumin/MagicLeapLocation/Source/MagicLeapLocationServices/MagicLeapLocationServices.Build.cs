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
                    "LocationServicesBPLibrary",
                    "MagicLeapLocationServices"
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "LuminRuntimeSettings",
                    "MLSDK",
                    "MagicLeap",
                    "LocationServicesBPLibrary",
                    "MagicLeapLocationServices"
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
