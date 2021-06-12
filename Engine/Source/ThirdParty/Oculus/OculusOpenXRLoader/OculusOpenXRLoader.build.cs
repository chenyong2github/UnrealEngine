// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OculusOpenXRLoader : ModuleRules
{
	public OculusOpenXRLoader(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string SourceDirectory = Target.UEThirdPartySourceDirectory + "Oculus/OculusOpenXRLoader/OculusOpenXRLoader/";

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			RuntimeDependencies.Add(SourceDirectory + "Lib/armeabi-v7a/libopenxr_loader.so");
			RuntimeDependencies.Add(SourceDirectory + "Lib/arm64-v8a/libopenxr_loader.so");
		}
	}
}