// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class PhysXCookingLib : ModuleRules
{
	enum PhysXLibraryMode
	{
		Debug,
		Profile,
		Checked,
		Shipping
	}

	PhysXLibraryMode GetPhysXLibraryMode(UnrealTargetConfiguration Config)
	{
		switch (Config)
		{
			case UnrealTargetConfiguration.Debug:
				if (Target.bDebugBuildsActuallyUseDebugCRT)
				{
					return PhysXLibraryMode.Debug;
				}
				else
				{
					return PhysXLibraryMode.Checked;
				}
			case UnrealTargetConfiguration.Shipping:
				return PhysXLibraryMode.Shipping;
			case UnrealTargetConfiguration.Test:
				return PhysXLibraryMode.Profile;
			case UnrealTargetConfiguration.Development:
			case UnrealTargetConfiguration.DebugGame:
			case UnrealTargetConfiguration.Unknown:
			default:
				if (Target.bUseShippingPhysXLibraries)
				{
					return PhysXLibraryMode.Shipping;
				}
				else if (Target.bUseCheckedPhysXLibraries)
				{
					return PhysXLibraryMode.Checked;
				}
				else
				{
					return PhysXLibraryMode.Profile;
				}
		}
	}

	static string GetPhysXLibrarySuffix(PhysXLibraryMode Mode)
	{
		switch (Mode)
		{
			case PhysXLibraryMode.Debug:
				return "DEBUG";
			case PhysXLibraryMode.Checked:
				return "CHECKED";
			case PhysXLibraryMode.Profile:
				return "PROFILE";
			case PhysXLibraryMode.Shipping:
			default:
				return "";
		}
	}

	public PhysXCookingLib(ReadOnlyTargetRules Target) : base(Target)
	{
        Type = ModuleType.External;

		if(!Target.bCompilePhysX)
        {
            return;
        }

        // Determine which kind of libraries to link against
        PhysXLibraryMode LibraryMode = GetPhysXLibraryMode(Target.Configuration);
        string LibrarySuffix = GetPhysXLibrarySuffix(LibraryMode);

        string PhysXLibDir = Target.UEThirdPartySourceDirectory + "PhysX3/Lib/";

        // Libraries and DLLs for windows platform
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(String.Format("{0}/Win64/VS{1}/PhysX3Cooking{2}_x64.lib", PhysXLibDir, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), LibrarySuffix));
            PublicDelayLoadDLLs.Add(String.Format("PhysX3Cooking{0}_x64.dll", LibrarySuffix));

            string PhysXBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win64/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
            string FileName = PhysXBinariesDir + String.Format("PhysX3Cooking{0}_x64.dll", LibrarySuffix);
            RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
            RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
        }
        else if (Target.Platform == UnrealTargetPlatform.Win32)
        {
            PublicAdditionalLibraries.Add(String.Format("{0}/Win32/VS{1}/PhysX3Cooking{2}_x86.lib", PhysXLibDir, Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), LibrarySuffix));
            PublicDelayLoadDLLs.Add(String.Format("PhysX3Cooking{0}_x86.dll", LibrarySuffix));

            string PhysXBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win32/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
            string FileName = PhysXBinariesDir + String.Format("PhysX3Cooking{0}_x86.dll", LibrarySuffix);
            RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
            RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string PhysXBinariesDir = Target.UEThirdPartyBinariesDirectory + "PhysX3/Mac/";
            string LibraryPath = PhysXBinariesDir + String.Format("libPhysX3Cooking{0}.dylib", LibrarySuffix);
            
            PublicDelayLoadDLLs.Add(LibraryPath);
            RuntimeDependencies.Add(LibraryPath);
        }
        else if (Target.Platform == UnrealTargetPlatform.Android)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "ARM64", String.Format("libPhysX3Cooking{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "ARMv7", String.Format("libPhysX3Cooking{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "x64", String.Format("libPhysX3Cooking{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "x86", String.Format("libPhysX3Cooking{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            if (Target.Architecture != "arm-unknown-linux-gnueabihf")
            {
                PhysXLibDir = Path.Combine(PhysXLibDir, "Linux", Target.Architecture);
                PublicAdditionalLibraries.Add(PhysXLibDir + String.Format("/libPhysX3Cooking{0}.a", LibrarySuffix));
            }

        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PhysXLibDir = Path.Combine(PhysXLibDir, "IOS/");
            PublicAdditionalLibraries.Add(PhysXLibDir + String.Format("/libPhysX3Cooking{0}.a", LibrarySuffix));
        }
        else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            PhysXLibDir = Path.Combine(PhysXLibDir, "TVOS/");
            PublicAdditionalLibraries.Add(PhysXLibDir + String.Format("/libPhysX3Cooking{0}.a", LibrarySuffix));
        }
        else if (Target.Platform == UnrealTargetPlatform.HTML5)
        {
            PhysXLibDir = Path.Combine(PhysXLibDir, "HTML5/");
            string OpimizationSuffix = "";
            if (Target.bCompileForSize)
            {
                OpimizationSuffix = "_Oz";
            }
            else
            {
                if (Target.Configuration == UnrealTargetConfiguration.Development)
                {
                    OpimizationSuffix = "_O2";
                }
                else if (Target.Configuration == UnrealTargetConfiguration.Shipping)
                {
                    OpimizationSuffix = "_O3";
                }
            }

            PublicAdditionalLibraries.Add(PhysXLibDir + "PhysX3Cooking" + OpimizationSuffix + ".bc");

        }
        else if (Target.Platform == UnrealTargetPlatform.PS4)
        {
            PhysXLibDir = Path.Combine(PhysXLibDir, "PS4/");
            PublicAdditionalLibraries.Add(PhysXLibDir + String.Format("libPhysX3Cooking{0}.a", LibrarySuffix));
        }
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            // Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null) as string;
                PhysXLibDir = Path.Combine(PhysXLibDir, "XboxOne", "VS" + VersionName) + "/";

                PublicAdditionalLibraries.Add(PhysXLibDir + String.Format("PhysX3Cooking{0}.lib", LibrarySuffix));
            }
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PhysXLibDir = Path.Combine(PhysXLibDir, "Switch/");
            PublicAdditionalLibraries.Add(PhysXLibDir + String.Format("libPhysX3Cooking{0}.a", LibrarySuffix));
        }
	}
}
