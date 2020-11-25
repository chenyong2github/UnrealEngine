// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class IKRigDeveloper : ModuleRules
    {
        public IKRigDeveloper(ReadOnlyTargetRules Target) : base(Target)
        {
            // Copying some these from ControlRig.build.cs, our deps are likely leaner
            // and therefore these could be pruned if needed:
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"Core",
					"CoreUObject",
					"Engine",
					"IKRig",
				}
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                }
            );

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.AddRange(
                    new string[]
                    {
						"AnimGraph",
                        "BlueprintGraph",
                    }
                );
            }
        }
    }
}
