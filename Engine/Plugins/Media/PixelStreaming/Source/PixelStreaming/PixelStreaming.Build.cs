// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Collections.Generic;
using EpicGames.Core;


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
				Log.TraceInformation(string.Format("Signalling Server path '{0}' does not exist", SignallingServerDir));
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
				Log.TraceInformation(string.Format("Matchmaking Server path '{0}' does not exist", MatchmakingServerDir));
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

		private void AddWebRTCServers()
		{
			string webRTCRevision = "31262";
			string webRTCRevisionDirectory = "./ThirdParty/WebRTC/rev." + webRTCRevision;
			string webRTCProgramsDirectory = Path.Combine(webRTCRevisionDirectory, "programs/Win64/VS2015/Release");

			List<string> DependenciesToAdd = new List<string>();
			DependenciesToAdd.AddRange(Directory.GetFiles(webRTCProgramsDirectory, "*.exe"));
			DependenciesToAdd.AddRange(Directory.GetFiles(webRTCProgramsDirectory, "*.pdb"));
			DependenciesToAdd.AddRange(Directory.GetFiles(webRTCProgramsDirectory, "*.bat"));
			DependenciesToAdd.AddRange(Directory.GetFiles(webRTCProgramsDirectory, "*.ps1"));

			foreach (string Dependency in DependenciesToAdd)
			{
				RuntimeDependencies.Add(Dependency, StagedFileType.NonUFS);
			}
		}

		public PixelStreaming(ReadOnlyTargetRules Target) : base(Target)
		{
			// use private PCH to include lots of WebRTC headers
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			PrivatePCHHeaderFile = "Private/PCH.h";
			//PCHUsage = PCHUsageMode.NoPCHs;

			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

			// NOTE: General rule is not to access the private folder of another module
			PrivateIncludePaths.AddRange(new string[]
				{
					Path.Combine(EngineDir, "Source/Runtime/AudioMixer/Private"),
					Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private"),
					Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Linux"),
				});

			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"InputDevice",
				"Json",
				"RenderCore",
				"RHI",
				"RHICore",
				"Slate",
				"SlateCore",
				"AudioMixer",
				"WebRTC",
				"WebSockets",
				"Sockets",
				"MediaUtils",
				"AVEncoder",
				"DeveloperSettings"
			});
			
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
			}

			DynamicallyLoadedModuleNames.AddRange(new string[]
			{
				"Media",
			});

			PrivateIncludePathModuleNames.AddRange(new string[]
			{
				"Media"
			});

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.Add("Engine");
				PrivateDependencyModuleNames.Add("HeadMountedDisplay");
			}

			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
			{
				// required for casting UnrealEngine BackBuffer to Vulkan Texture2D for NvEnc
				PrivateDependencyModuleNames.AddRange(new string[] { "CUDA", "VulkanRHI", "nvEncode"});
				PrivateIncludePaths.AddRange(new string[]
				{
					Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private"),
					Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Linux"),
				});


			}
			
			AddSignallingServer();
			AddMatchmakingServer();
			
			// We have not been able to build these yet for M84 (these are the WebRTC bundled sample STUN/TURN servers).
			// For future I think we should advise use of COTURN instead of the WebRTC sample servers.
			// if (Target.Platform == UnrealTargetPlatform.Win64) {
			//     AddWebRTCServers();
			// }
		}
	}
}
