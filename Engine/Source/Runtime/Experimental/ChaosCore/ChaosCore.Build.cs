// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class ChaosCore : ModuleRules
    {
        public ChaosCore(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePaths.Add("Runtime/Experimental/ChaosCore/Public");

            PublicDependencyModuleNames.AddRange(
                new string[] {
                "Core",
                "IntelISPC"
                }
            );

            PublicDefinitions.Add("COMPILE_WITHOUT_UNREAL_SUPPORT=0");

            if (Target.bCompileChaos == true || Target.bUseChaos == true)
            {
                PublicDefinitions.Add("INCLUDE_CHAOS=1");
            }
            else
            {
                PublicDefinitions.Add("INCLUDE_CHAOS=0");
            }

            if (Target.bUseChaosChecked == true)
            {
                PublicDefinitions.Add("CHAOS_CHECKED=1");
            }
            else
            {
                PublicDefinitions.Add("CHAOS_CHECKED=0");
            }
        }
    }
}
