// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;
using System.Collections.Generic;

namespace UnrealBuildTool.Rules
{
    public class MDLImporter : ModuleRules
    {
        private string ThirdPartyPath
        {
            get { return Path.GetFullPath(Path.Combine(ModuleDirectory, "../ThirdParty/NotForLicensees/")); }
        }

        public MDLImporter(ReadOnlyTargetRules Target) : base(Target)
        {
			bLegalToDistributeObjectCode = true;

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Analytics",
                    "Core",
                    "RenderCore",
                    "ImageCore",
                    "CoreUObject",
                    "Engine",
                    "MessageLog",
                    "UnrealEd",
                    "Slate",
                    "SlateCore",
                    "Mainframe",
                    "InputCore",
                    "EditorStyle",
                    "MaterialEditor",
                    "Projects"
                }
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                }
            );

            List<string> RuntimeModuleNames = new List<string>();
            string BinaryLibraryFolder = Path.Combine(EngineDirectory, "Plugins/Enterprise/MDLImporter/Binaries/ThirdParty/MDL", Target.Platform.ToString());

            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
				RuntimeModuleNames.Add("libmdl_sdk.dll");
				RuntimeModuleNames.Add("nv_freeimage.dll");
                    
                foreach (string RuntimeModuleName in RuntimeModuleNames)
                {
                    string ModulePath = Path.Combine(BinaryLibraryFolder, RuntimeModuleName);
                    if (!File.Exists(ModulePath))
                    {
                        string Err = string.Format("MDL SDK module '{0}' not found.", ModulePath);
                        System.Console.WriteLine(Err);
                        throw new BuildException(Err);
                    }

                    PublicDelayLoadDLLs.Add(RuntimeModuleName);
                    RuntimeDependencies.Add(ModulePath);
                }
            }
            else if (Target.Platform == UnrealTargetPlatform.Mac)
            {
                RuntimeModuleNames.Add("libmdl_sdk.so");
                RuntimeModuleNames.Add("nv_freeimage.so");
                RuntimeModuleNames.Add("dds.so");

                foreach (string RuntimeModuleName in RuntimeModuleNames)
                {
                    string ModulePath = Path.Combine(BinaryLibraryFolder, RuntimeModuleName);
                    if (!File.Exists(ModulePath))
                    {
                        string Err = string.Format("MDL SDK module '{0}' not found.", ModulePath);
                        System.Console.WriteLine(Err);
                        throw new BuildException(Err);
                    }

                    PublicDelayLoadDLLs.Add(ModulePath);
                    RuntimeDependencies.Add(ModulePath);
                }
            }

            if (Directory.Exists(ThirdPartyPath))
            {
                //third party libraries
                string[] Libs = { "mdl-sdk-314800.830"};
                foreach (string Lib in Libs)
                {
                    string IncludePath = Path.Combine(ThirdPartyPath, Lib, "include");
                    if (Directory.Exists(IncludePath))
                    {
                        PublicSystemIncludePaths.Add(IncludePath);
                    }
                    else
                    {
                        return;
                    }
                }

                PrivateDefinitions.Add("USE_MDLSDK");

                if ((Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Win32))
                {
                    PublicDefinitions.Add("MI_PLATFORM_WINDOWS");
                }
                else if (Target.Platform == UnrealTargetPlatform.Linux)
                {
                    PublicDefinitions.Add("MI_PLATFORM_LINUX");
                }
                else if (Target.Platform == UnrealTargetPlatform.Mac)
                {
                    PublicDefinitions.Add("MI_PLATFORM_MACOSX");
                }
            }
        }
    }
}
