// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
    public class DatasmithGLTFTranslator : ModuleRules
    {
        public DatasmithGLTFTranslator(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Analytics",
                    "Core",
                    "CoreUObject",
                    "DatasmithCore",
                    "Engine",
                    "Json",
                    "MeshDescription",
                    "MessageLog",
                    "RawMesh",
                    "Slate",
                    "SlateCore",
                }
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "DatasmithContent",
                    "DatasmithTranslator",
                    "GLTFCore",
                }
            );
        }
    }
}
