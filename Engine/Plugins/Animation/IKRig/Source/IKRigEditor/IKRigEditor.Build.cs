// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
    public class IKRigEditor : ModuleRules
    {
        public IKRigEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                }
            );

			PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
					"UnrealEd",
					"IKRig",
					"IKRigDeveloper",
					"AssetTools",
					"SlateCore",
					"Slate",
					"InputCore",
					"PropertyEditor",
					"EditorStyle",
					"EditorFramework",
					"BlueprintGraph",
					"AnimGraph",
				}
            );

            PrivateIncludePathModuleNames.AddRange(
                new string[]
                {
				}
            );

        }
    }
}
