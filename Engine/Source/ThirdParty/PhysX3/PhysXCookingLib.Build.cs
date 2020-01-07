// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class PhysXCookingLib : ModuleRules
{
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string PhysXLibDir { get { return Path.Combine(LibRootDirectory, "PhysX3", "Lib"); } }

	protected virtual PhysXLibraryMode LibraryMode { get { return Target.GetPhysXLibraryMode(); } }
	protected virtual string LibrarySuffix { get { return LibraryMode.AsSuffix(); } }

	public PhysXCookingLib(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

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
                PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Linux", Target.Architecture, String.Format("libPhysX3Cooking{0}.a", LibrarySuffix)));
            }
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "IOS", String.Format("libPhysX3Cooking{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "TVOS", String.Format("libPhysX3Cooking{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            // Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null) as string;
                PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "XboxOne", "VS" + VersionName, String.Format("PhysX3Cooking{0}.lib", LibrarySuffix)));
            }
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Switch", String.Format("libPhysX3Cooking{0}.a", LibrarySuffix)));
        }
	}
}
