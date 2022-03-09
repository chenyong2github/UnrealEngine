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

		private void AddFolder(string Folder)
		{
			string DirectoryToAdd = new DirectoryInfo(PixelStreamingProgramsDirectory + "/WebServers/" + Folder).FullName;

			if (!Directory.Exists(DirectoryToAdd))
			{
				return;
			}

			List<string> DependenciesToAdd = new List<string>();
			DependenciesToAdd.AddRange(Directory.GetFiles(DirectoryToAdd, "*.*", SearchOption.AllDirectories));

			string NodeModulesDirPath = new DirectoryInfo(DirectoryToAdd + "/node_modules").FullName;
			string LogsDirPath = new DirectoryInfo(DirectoryToAdd + "/logs").FullName;
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

			// This is so for game projects using our public headers don't have to include extra modules they might not know about.
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"InputDevice",
				"WebRTC"
			});

			// NOTE: General rule is not to access the private folder of another module
			PrivateIncludePaths.AddRange(new string[]
				{
					Path.Combine(EngineDir, "Source/Runtime/AudioMixer/Private"),
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

			PrivateDependencyModuleNames.Add("VulkanRHI");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan", "CUDA");

			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
			{
				PrivateDependencyModuleNames.Add("D3D11RHI");
				PrivateDependencyModuleNames.Add("D3D12RHI");

				AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11", "DX12");
			}

			AddFolder("SignallingWebServer");
			AddFolder("Matchmaker");
			AddFolder("SFU");
		}
	}
}
