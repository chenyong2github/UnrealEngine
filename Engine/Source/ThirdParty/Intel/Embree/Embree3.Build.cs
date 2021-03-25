// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Embree3 : ModuleRules
{
	public Embree3(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string SDKDir = Target.UEThirdPartySourceDirectory + "Intel/Embree/Embree3122/";
		string EmbreeRuntimeLibsDir = Target.UEThirdPartyBinariesDirectory + "Intel/Embree/Embree3122/";

		// EMBREE
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicIncludePaths.Add(SDKDir + "Win64/include"); 
			PublicAdditionalLibraries.Add(SDKDir + "Win64/lib/embree3.lib");
			PublicAdditionalLibraries.Add(SDKDir + "Win64/lib/tbb.lib");
			RuntimeDependencies.Add(EmbreeRuntimeLibsDir + "Win64/lib/embree3.dll");
			RuntimeDependencies.Add(EmbreeRuntimeLibsDir + "Win64/lib/tbb12.dll");
			PublicDefinitions.Add("USE_EMBREE=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			//@todo - fix for Mac
			PublicDefinitions.Add("USE_EMBREE=0");
			/*

			PublicIncludePaths.Add(SDKDir + "MacOSX/include");
			PublicAdditionalLibraries.Add(SDKDir + "MacOSX/lib/libembree3.3.dylib");
			PublicAdditionalLibraries.Add(SDKDir + "MacOSX/lib/libtbb.12.1.dylib");
			//@todo - get rid of copy, put runtime dependency in Binaries/Thirdparty/Intel/Embree3122
			RuntimeDependencies.Add("$(TargetOutputDir)/libembree3.3.dylib", SDKDir + "MacOSX/lib/libembree3.3.dylib");
			RuntimeDependencies.Add("$(TargetOutputDir)/libtbb.12.1.dylib", SDKDir + "MacOSX/lib/libtbb.12.1.dylib");
			PublicDefinitions.Add("USE_EMBREE=1");
*/

		}
		else
		{
			PublicDefinitions.Add("USE_EMBREE=0");
		}
	}
}
