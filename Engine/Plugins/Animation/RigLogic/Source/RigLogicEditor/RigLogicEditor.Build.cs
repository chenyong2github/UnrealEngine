// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
    public class RigLogicEditor : ModuleRules
    {
        private string ModulePath
        {
            get { return ModuleDirectory; }
        }

        private string ThirdPartyPath
        {
            get { return Path.GetFullPath(Path.Combine(ModulePath, "../Source/ThirdParty/")); }
        }

        public RigLogicEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicDefinitions.Add("RL_SHARED=1");

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "ControlRig",
                    "UnrealEd",
                    "MainFrame",
                    "RigLogicModule",
                    "RigLogicLib",
                    "PropertyEditor",
                    "SlateCore",
                    "ApplicationCore",
                    "Slate",
                    "EditorStyle",
                    "InputCore"
                }
            );

            PrivateIncludePathModuleNames.AddRange(
                new string[]
                {
                    "PropertyEditor",
                    "AssetTools"
                }
            );

        }
    }
}
