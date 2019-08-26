// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class WebRTCProxy : ModuleRules
{
	public WebRTCProxy(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core"
			});

        var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

        PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/ThirdParty/WebRTC/rev.23789/include/Win64/VS2017"));
		PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/ThirdParty/WebRTC/rev.23789/include/Win64/VS2017/third_party/jsoncpp/source/include"));

        string LibDir = Path.Combine(EngineDir, "Source/ThirdParty/WebRTC/rev.23789/lib/Win64/VS2017/release/");

        PublicAdditionalLibraries.Add(Path.Combine(LibDir, "json.lib"));
        PublicAdditionalLibraries.Add(Path.Combine(LibDir, "webrtc.lib"));
        PublicAdditionalLibraries.Add(Path.Combine(LibDir, "webrtc_opus.lib"));
        PublicAdditionalLibraries.Add(Path.Combine(LibDir, "audio_decoder_opus.lib"));

        PublicSystemLibraries.Add("Msdmo.lib");
        PublicSystemLibraries.Add("Dmoguids.lib");
        PublicSystemLibraries.Add("wmcodecdspuuid.lib");
        PublicSystemLibraries.Add("Secur32.lib");

        bEnableExceptions = true;
    }
}
