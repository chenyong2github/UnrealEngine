// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Collections.Generic;
using Tools.DotNETCommon;


namespace UnrealBuildTool.Rules
{
	public class PixelStreaming : ModuleRules
	{
		const string PixelStreamingProgramsDirectory = "../../Samples/PixelStreaming";

		private void AddSignallingServer()
		{
			string SignallingServerDir = new DirectoryInfo(PixelStreamingProgramsDirectory + "/WebServers/SignallingWebServer").FullName;

			if (!Directory.Exists(SignallingServerDir))
			{
				return;
			}

			List<string> DependenciesToAdd = new List<string>();
			DependenciesToAdd.AddRange(Directory.GetFiles(SignallingServerDir, "*.*", SearchOption.AllDirectories));

			string NodeModulesDirPath = new DirectoryInfo(SignallingServerDir + "/node_modules").FullName;
			string LogsDirPath = new DirectoryInfo(SignallingServerDir + "/logs").FullName;
			foreach (string Dependency in DependenciesToAdd)
			{
				if (!Dependency.StartsWith(NodeModulesDirPath) &&
					!Dependency.StartsWith(LogsDirPath))
				{
					RuntimeDependencies.Add(Dependency, StagedFileType.NonUFS);
				}
			}
		}

		private void AddMatchmakingServer()
		{
			string MatchmakingServerDir = new DirectoryInfo(PixelStreamingProgramsDirectory + "/WebServers/Matchmaker").FullName;

			if (!Directory.Exists(MatchmakingServerDir))
			{
				return;
			}

			List<string> DependenciesToAdd = new List<string>();
			DependenciesToAdd.AddRange(Directory.GetFiles(MatchmakingServerDir, "*.*", SearchOption.AllDirectories));

			string NodeModulesDirPath = new DirectoryInfo(MatchmakingServerDir + "/node_modules").FullName;
			string LogsDirPath = new DirectoryInfo(MatchmakingServerDir + "/logs").FullName;
			foreach (string Dependency in DependenciesToAdd)
			{
				if (!Dependency.StartsWith(NodeModulesDirPath) &&
					!Dependency.StartsWith(LogsDirPath))
				{
					RuntimeDependencies.Add(Dependency, StagedFileType.NonUFS);
				}
			}
		}

		public PixelStreaming(ReadOnlyTargetRules Target) : base(Target)
		{
			// use private PCH to include lots of WebRTC headers
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			PrivatePCHHeaderFile = "Private/PCH.h";

			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

			// NOTE: General rule is not to access the private folder of another module
			PrivateIncludePaths.AddRange(new string[]
				{
					Path.Combine(EngineDir, "Source/Runtime/AudioMixer/Private"),
					Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private"),
				});

			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Json",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"AudioMixer",
				"WebRTC",
				"WebSockets",
				"Sockets",
				"MediaUtils",
				"DeveloperSettings",
				"AVEncoder"
			});

			// This is so for game projects using our public headers don't have to include extra modules they might not know about.
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"InputDevice"
			});

			AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");

			// required for casting UE4 BackBuffer to Vulkan Texture2D for NvEnc
			PrivateDependencyModuleNames.AddRange(new string[] { "CUDA", "VulkanRHI", "nvEncode" });

			AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");
			
			PrivateIncludePathModuleNames.Add("VulkanRHI");
			PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private"));
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");

			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
			{
				PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Windows"));
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
			{
				PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Linux"));
			}

			AddSignallingServer();
			AddMatchmakingServer();
		}
	}
}
