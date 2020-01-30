// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UnrealAudio : ModuleRules
{
	public UnrealAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/UnrealAudio/Private",
				"Runtime/UnrealAudio/Private/Tests",
			}
		);

		PublicIncludePaths.AddRange(
			new string[] {
				"Runtime/UnrealAudio/Public",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject", 
			}
		);

		// Libsndfile DLL
		if(!bUsePrecompiled)
		{
			string LibSndFilePath = Target.UEThirdPartyBinariesDirectory + "libsndfile/";
			if (Target.Platform == UnrealTargetPlatform.Win32)
			{
				LibSndFilePath += "Win32";
				PublicAdditionalLibraries.Add(LibSndFilePath + "/libsndfile-1.lib");
				PublicDelayLoadDLLs.Add("libsndfile-1.dll");
			}
			else if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				LibSndFilePath += "Win64";
				PublicAdditionalLibraries.Add(LibSndFilePath + "/libsndfile-1.lib");
				PublicDelayLoadDLLs.Add("libsndfile-1.dll");
			}
// 			else if (Target.Platform == UnrealTargetPlatform.Mac)
// 			{
// 				LibSndFilePath += "Mac/libsndfile.1.dylib";
// 				PublicAdditionalLibraries.Add(LibSndFilePath);
// 			}
		}

		PrecompileForTargets = PrecompileTargetsType.Editor;
	}
}
