// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class HTNPlanner : ModuleRules
    {
        public HTNPlanner(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePaths.AddRange(
                    new string[] {
                    }
                    );

            PrivateIncludePaths.AddRange(
                new string[] {
                }
                );

            PublicDependencyModuleNames.AddRange(
                new string[] {
                        "Core",
                        "CoreUObject",
                        "Engine",
                        "GameplayTags",
                        "GameplayTasks",
                        "AIModule"
                }
                );

            DynamicallyLoadedModuleNames.AddRange(
                new string[] {
                    // ... add any modules that your module loads dynamically here ...
                }
                );

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("EditorFramework");
                PrivateDependencyModuleNames.Add("UnrealEd");
            }

            SetupGameplayDebuggerSupport(Target);
        }
    }
}
