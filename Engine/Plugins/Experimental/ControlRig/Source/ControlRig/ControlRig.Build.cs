// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class ControlRig : ModuleRules
    {
        public ControlRig(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.Add("ControlRig/Private");
            PrivateIncludePaths.Add("ControlRig/Private/Sequencer");
            PrivateIncludePaths.Add("ControlRig/Private/Units");
            PrivateIncludePaths.Add("ControlRig/ThirdParty/AHEasing");

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "AnimGraphRuntime",
                    "MovieScene",
                    "MovieSceneTracks",
                    "PropertyPath",
					"TimeManagement",
					"DeveloperSettings"
				}
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "AnimationCore",
                    "LevelSequence",
                    "RigVM",
                    "RHI",
                }
            );

            if (Target.bBuildEditor == true)
            {
                PublicDependencyModuleNames.AddRange(
				    new string[]
					{
						"Slate",
						"SlateCore",
						"EditorStyle",
						"RigVMDeveloper",
                        "AnimGraph",
                    }
                );

                PrivateDependencyModuleNames.AddRange(
                    new string[]
                    {
                        "UnrealEd",
                        "BlueprintGraph",
                        "PropertyEditor",
                        "RigVMDeveloper",
                    }
                );

                PrivateIncludePathModuleNames.Add("ControlRigEditor");
                DynamicallyLoadedModuleNames.Add("ControlRigEditor");
            }
        }
    }
}
