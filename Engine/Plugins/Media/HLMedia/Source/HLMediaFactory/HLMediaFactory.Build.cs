// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class HLMediaFactory : ModuleRules
    {
        public HLMediaFactory(ReadOnlyTargetRules Target) : base(Target)
        {
            DynamicallyLoadedModuleNames.AddRange(
                new string[] {
                    "Media",
                });

            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
                    "CoreUObject",
                    "MediaAssets",
                });

            PrivateIncludePathModuleNames.AddRange(
                new string[] {
                    "Media",
                    "HLMedia",
                });

            DynamicallyLoadedModuleNames.Add("HLMedia");
        }
    }
}
