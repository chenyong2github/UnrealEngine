// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
                    "MainFrame",
                    "MeshUtilities",
                    "MeshDescription",
                    "MessageLog",
                    "RawMesh",
                    "Slate",
                    "SlateCore",
                    "UnrealEd",
                    "MaterialEditor",
                }
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "DatasmithContent",
                    "DatasmithImporter",
                    "GLTFImporter",
                }
            );
        }
    }
}
