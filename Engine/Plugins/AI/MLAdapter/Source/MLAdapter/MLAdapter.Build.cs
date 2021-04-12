// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
    public class MLAdapter : ModuleRules
    {
        public MLAdapter(ReadOnlyTargetRules Target) : base(Target)
        {
            // rcplib is using exceptions so we have to enable that
            bEnableExceptions = true;
            PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

            PublicIncludePaths.AddRange(
                new string[] {
                "Runtime/AIModule/Public",
                ModuleDirectory + "/Public",
                }
            );

            PrivateIncludePaths.Add("MLAdapter/Private");

            PublicDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "GameplayTags",
                    "AIModule",
                    "InputCore",
                    "Json",
                    "JsonUtilities",
                    "GameplayAbilities",
                }
            );

            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "DeveloperSettings"
                }
            );

            // RPCLib disabled on other platforms at the moment
            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                PublicDefinitions.Add("WITH_RPCLIB=1");
                AddEngineThirdPartyPrivateStaticDependencies(Target, "RPCLib");
            
				string RPClibDir = Path.Combine(Target.UEThirdPartySourceDirectory, "rpclib");
				PublicIncludePaths.Add(Path.Combine(RPClibDir, "Source", "include"));
            }
            else
            {
                PublicDefinitions.Add("WITH_RPCLIB=0");
            }

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.Add("EditorFramework");
                PrivateDependencyModuleNames.Add("UnrealEd");
            }

            if (Target.bBuildDeveloperTools || (Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Configuration != UnrealTargetConfiguration.Test))
            {
                PrivateDependencyModuleNames.Add("GameplayDebugger");
                PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=1");
            }
            else
            {
                PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=0");
            }
        }
    }
}
