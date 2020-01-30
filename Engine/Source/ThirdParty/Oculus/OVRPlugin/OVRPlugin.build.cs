// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OVRPlugin : ModuleRules
{
	public OVRPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string SourceDirectory = Target.UEThirdPartySourceDirectory + "Oculus/OVRPlugin/OVRPlugin/";

		PublicIncludePaths.Add(SourceDirectory + "Include");

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicAdditionalLibraries.Add(SourceDirectory + "Lib/armeabi-v7a/" + "libOVRPlugin.so");
			PublicAdditionalLibraries.Add(SourceDirectory + "Lib/arm64-v8a/" + "libOVRPlugin.so");

			PublicAdditionalLibraries.Add(SourceDirectory + "ExtLibs/armeabi-v7a/" + "libvrapi.so");
			PublicAdditionalLibraries.Add(SourceDirectory + "ExtLibs/arm64-v8a/" + "libvrapi.so");
		}
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(SourceDirectory + "Lib/Win64/OVRPlugin.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32 )
		{
			PublicAdditionalLibraries.Add(SourceDirectory + "Lib/Win32/OVRPlugin.lib");
		}
	}
}