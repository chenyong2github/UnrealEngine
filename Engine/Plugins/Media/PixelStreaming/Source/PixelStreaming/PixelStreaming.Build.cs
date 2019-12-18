// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Collections.Generic;
using Tools.DotNETCommon;

namespace UnrealBuildTool.Rules
{
    public class PixelStreaming : ModuleRules
    {
        private void AddSignallingServer()
        {
            string PixelStreamingProgramsDirectory = "./Programs/PixelStreaming";
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
			string PixelStreamingProgramsDirectory = "./Programs/PixelStreaming";
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
            string webRTCRevision = "23789";
            string webRTCRevisionDirectory = "./ThirdParty/WebRTC/rev." + webRTCRevision;
			string webRTCProgramsDirectory = Path.Combine(webRTCRevisionDirectory, "programs/Win64/VS2017/release");

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

            // NOTE: General rule is not to access the private folder of another module,
            // but to use the ISubmixBufferListener interface, we  need to include some private headers
            PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/AudioMixer/Private"));

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
                "D3D11RHI",
                "Slate",
				"SlateCore",
                "AudioMixer",
				"WebRTC",
				"WebSockets",
				"Sockets",
                "MediaUtils",
				"AVEncoder",
            });

            DynamicallyLoadedModuleNames.AddRange(new string[]
           {
                "Media",
           });

            PrivateIncludePathModuleNames.AddRange(new string[]
            {
                "Media",
            });

            if (Target.bCompileAgainstEngine)
            {
                PrivateDependencyModuleNames.Add("Engine");
                PrivateDependencyModuleNames.Add("HeadMountedDisplay");
            }

            // required for casting UE4 BackBuffer to D3D11 Texture2D for NvEnc
            PrivateDependencyModuleNames.AddRange(new string[] { "D3D11RHI" });
            PrivateIncludePaths.AddRange(new string[]
            {
                    Path.Combine(EngineDir, "Source/Runtime/Windows/D3D11RHI/Private"),
                    Path.Combine(EngineDir, "Source/Runtime/Windows/D3D11RHI/Private/Windows"),
            });
            // required by D3D11RHI
            AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelMetricsDiscovery");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "IntelExtensionsFramework");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");

            {   // WebRTC stuff
                PublicSystemLibraries.Add("Msdmo.lib");
                PublicSystemLibraries.Add("Dmoguids.lib");
                PublicSystemLibraries.Add("wmcodecdspuuid.lib");
                PublicSystemLibraries.Add("winmm.lib");
            }

            // for `FWmfMediaHardwareVideoDecodingTextureSample`
            // this really needs to be refactored to break dependency on WmfMedia plugin
            PrivateDependencyModuleNames.Add("WmfMedia");
            PrivateIncludePaths.Add("../../../Media/WmfMedia/Source/WmfMedia/Private/");

            AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11");
            AddEngineThirdPartyPrivateStaticDependencies(Target, "DX9");

            PublicAdditionalLibraries.Add(Target.UEThirdPartySourceDirectory + "Windows/DirectX/Lib/x64/dxerr.lib");

            PublicSystemLibraries.Add("Dxva2.lib");
            PublicSystemLibraries.Add("strmiids.lib");
            PublicSystemLibraries.Add("legacy_stdio_definitions.lib");

            //delay - load all MF DLLs to be able to check Windows version for compatibility in `StartupModule` before
            //  loading them manually
            PublicSystemLibraries.Add("mfplat.lib");
            PublicDelayLoadDLLs.Add("mfplat.dll");
            PublicSystemLibraries.Add("mfuuid.lib");

            AddSignallingServer();
    	    AddMatchmakingServer();
        	AddWebRTCServers();

        }
    }
}
