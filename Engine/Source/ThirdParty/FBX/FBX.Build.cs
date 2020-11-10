// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class FBX : ModuleRules
{
	public FBX(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string FBXSDKDir = Target.UEThirdPartySourceDirectory + "FBX/2020.1.1/";
		PublicSystemIncludePaths.AddRange(
			new string[] {
					FBXSDKDir + "include",
					FBXSDKDir + "include/fbxsdk",
				}
			);


		if ( Target.Platform == UnrealTargetPlatform.Win64 )
		{
			string FBxLibPath = FBXSDKDir + "lib/vs2017/";

			FBxLibPath += "x64/release/";

			if (Target.LinkType != TargetLinkType.Monolithic)
			{
				PublicAdditionalLibraries.Add(FBxLibPath + "libfbxsdk.lib");

				// We are using DLL versions of the FBX libraries
				PublicDefinitions.Add("FBXSDK_SHARED");

				RuntimeDependencies.Add("$(TargetOutputDir)/libfbxsdk.dll", FBxLibPath + "libfbxsdk.dll");
			}
			else
			{
				if (Target.bUseStaticCRT)
				{
					PublicAdditionalLibraries.Add(FBxLibPath + "libfbxsdk-mt.lib");
					PublicAdditionalLibraries.Add(FBxLibPath + "libxml2-mt.lib");
					PublicAdditionalLibraries.Add(FBxLibPath + "zlib-mt.lib");
				}
				else
				{
					PublicAdditionalLibraries.Add(FBxLibPath + "libfbxsdk-md.lib");
					PublicAdditionalLibraries.Add(FBxLibPath + "libxml2-md.lib");
					PublicAdditionalLibraries.Add(FBxLibPath + "zlib-md.lib");
				}
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string FBxLibPath = FBXSDKDir + "lib/clang/release/";
			PublicAdditionalLibraries.Add(FBxLibPath + "libfbxsdk.dylib");
			RuntimeDependencies.Add("$(TargetOutputDir)/libfbxsdk.dylib", FBxLibPath + "libfbxsdk.dylib");
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string LibDir = FBXSDKDir + "lib/gcc/" + Target.Architecture + "/release/";
			if (!Target.bIsEngineInstalled && !Directory.Exists(LibDir))
			{
				string Err = string.Format("FBX SDK not found in {0}", LibDir);
				System.Console.WriteLine(Err);
				throw new BuildException(Err);
			}

			/* There seems to be an issue with static linking since FBXSDK 2019 on Linux,
			 * we use the shared library */
			PublicDefinitions.Add("FBXSDK_SHARED");

			PublicRuntimeLibraryPaths.Add(LibDir);
			PublicAdditionalLibraries.Add(LibDir + "libfbxsdk.so");
			RuntimeDependencies.Add(LibDir + "libfbxsdk.so");
			
			/* There is a bug in fbxarch.h where is doesn't do the check
			 * for clang under linux */
			PublicDefinitions.Add("FBXSDK_COMPILER_CLANG");

			// libfbxsdk has been built against libstdc++ and as such needs this library
			PublicSystemLibraries.Add("stdc++");

			AddEngineThirdPartyPrivateStaticDependencies(Target, new string[]
			{
				"libxml2",
				"zlib"
			});
		}
	}
}

