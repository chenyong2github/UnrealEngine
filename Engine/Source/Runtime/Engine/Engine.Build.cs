// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Engine : ModuleRules
{
	public Engine(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("../Shaders/Shared");

		PrivatePCHHeaderFile = "Private/EnginePrivatePCH.h";

		SharedPCHHeaderFile = "Public/EngineSharedPCH.h";

		PublicIncludePathModuleNames.AddRange(new string[] { "Renderer", "PacketHandler", "AudioMixer", "AudioMixerCore", "AnimationCore" });

		PrivateIncludePaths.AddRange(
			new string[] {
				"Developer/DerivedDataCache/Public",
				"Runtime/SynthBenchmark/Public",
				"Runtime/Engine/Private",
				"Runtime/Net/Core/Private/Net/Core/PushModel/Types"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"TargetPlatform",
				"ImageWrapper",
				"ImageWriteQueue",
				"HeadMountedDisplay",
				"EyeTracker",
				"MRMesh",
				"Advertising",
				"MovieSceneCapture",
				"AutomationWorker",
				"MovieSceneCapture",
				"DesktopPlatform"
			}
		);

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"TaskGraph",
					"SlateReflector",
				}
			);

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"SlateReflector",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"EditorAnalyticsSession",
				}
			);
		}

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"NetCore",
				"ApplicationCore",
				"Json",
				"SlateCore",
				"Slate",
				"InputCore",
				"Messaging",
				"MessagingCommon",
				"RenderCore",
				"AnalyticsET",
				"RHI",
				"Sockets",
				"AssetRegistry", // Here until we update all modules using AssetRegistry to add a dependency on it
				"EngineMessages",
				"EngineSettings",
				"SynthBenchmark",
				"GameplayTags",
				"PacketHandler",
				"AudioPlatformConfiguration",
				"MeshDescription",
				"StaticMeshDescription",
				"PakFile",
				"NetworkReplayStreaming",
				"PhysicsCore",
                "SignalProcessing",
                "AudioExtensions",
				"DeveloperSettings",
				"PropertyAccess",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework",
				"Networking",
				"Landscape",
				"UMG",
				"Projects",
				"MaterialShaderQualitySettings",
				"CinematicCamera",
				"Analytics",
				"AudioMixer",
				"AudioMixerCore",
				"SignalProcessing",
				"CrunchCompression",
				"IntelISPC",
				"TraceLog",
			}
		);

		// Cross platform Audio Codecs:
		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"UEOgg",
			"Vorbis",
			"VorbisFile",
			"libOpus"
			);

		DynamicallyLoadedModuleNames.Add("EyeTracker");

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateIncludePathModuleNames.Add("Localization");
			DynamicallyLoadedModuleNames.Add("Localization");
		}

		// to prevent "causes WARNING: Non-editor build cannot depend on non-redistributable modules."
		if (Target.Type == TargetType.Editor)
		{
			// for now we depend on this
			PrivateDependencyModuleNames.Add("RawMesh");
		}

		bool bVariadicTemplatesSupported = true;
		if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			// Use reflection to allow type not to exist if console code is not present
			System.Type XboxOnePlatformType = System.Type.GetType("UnrealBuildTool.XboxOnePlatform,UnrealBuildTool");
			if (XboxOnePlatformType != null)
			{
				System.Object VersionName = XboxOnePlatformType.GetMethod("GetVisualStudioCompilerVersionName").Invoke(null, null);
				if (VersionName.ToString().Equals("2012"))
				{
					bVariadicTemplatesSupported = false;
				}
			}
		}

		if (bVariadicTemplatesSupported)
		{
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"MessagingRpc",
					"PortalRpc",
					"PortalServices",
				}
			);

			if (Target.Type == TargetType.Editor)
			{
				// these modules require variadic templates
				PrivateDependencyModuleNames.AddRange(
					new string[] {
						"MessagingRpc",
						"PortalRpc",
						"PortalServices",
					}
				);
			}
		}

		CircularlyReferencedDependentModules.Add("GameplayTags");
		CircularlyReferencedDependentModules.Add("Landscape");
		CircularlyReferencedDependentModules.Add("UMG");
		CircularlyReferencedDependentModules.Add("MaterialShaderQualitySettings");
		CircularlyReferencedDependentModules.Add("CinematicCamera");
		CircularlyReferencedDependentModules.Add("AudioMixer");

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("EditorStyle");
			PrivateIncludePathModuleNames.Add("Foliage");
		}

		// The AnimGraphRuntime module is not needed by Engine proper, but it is loaded in LaunchEngineLoop.cpp,
		// and needs to be listed in an always-included module in order to be compiled into standalone games
		DynamicallyLoadedModuleNames.Add("AnimGraphRuntime");

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				"MovieScene",
				"MovieSceneCapture",
				"MovieSceneTracks",
				"LevelSequence",
				"HeadMountedDisplay",
				"MRMesh",
				"StreamingPauseRendering",
			}
		);

		if (Target.Type != TargetType.Server)
		{
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Media",
					"SlateNullRenderer",
					"SlateRHIRenderer"
				}
			);

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Media",
					"SlateNullRenderer",
					"SlateRHIRenderer"
				}
			);
		}

		if (Target.Type == TargetType.Server || Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.Add("PerfCounters");
		}

		if (Target.Type == TargetType.Editor)
		{
			PrivateIncludePathModuleNames.Add("MeshUtilities");
			PrivateIncludePathModuleNames.Add("MeshUtilitiesCommon");

			DynamicallyLoadedModuleNames.Add("MeshUtilities");

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ImageCore",
					"RawMesh"
				}
			);

			PrivateDependencyModuleNames.Add("CollisionAnalyzer");
			CircularlyReferencedDependentModules.Add("CollisionAnalyzer");

			PrivateDependencyModuleNames.Add("LogVisualizer");
			CircularlyReferencedDependentModules.Add("LogVisualizer");

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				DynamicallyLoadedModuleNames.AddRange(
					new string[] {
						"WindowsTargetPlatform",
						"WindowsNoEditorTargetPlatform",
						"WindowsServerTargetPlatform",
						"WindowsClientTargetPlatform",
						"AllDesktopTargetPlatform",
						"WindowsPlatformEditor",
					}
				);
			}
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				DynamicallyLoadedModuleNames.AddRange(
					new string[] {
						"MacTargetPlatform",
						"MacNoEditorTargetPlatform",
						"MacServerTargetPlatform",
						"MacClientTargetPlatform",
						"AllDesktopTargetPlatform",
						"MacPlatformEditor",
					}
				);
			}
			else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
			{
				DynamicallyLoadedModuleNames.AddRange(
					new string[] {
						"LinuxTargetPlatform",
						"LinuxNoEditorTargetPlatform",
						"LinuxAArch64NoEditorTargetPlatform",
						"LinuxServerTargetPlatform",
						"LinuxAArch64ServerTargetPlatform",
						"LinuxClientTargetPlatform",
						"LinuxAArch64ClientTargetPlatform",
						"AllDesktopTargetPlatform",
						"LinuxPlatformEditor",
					}
				);
			}
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"NullNetworkReplayStreaming",
				"LocalFileNetworkReplayStreaming",
				"HttpNetworkReplayStreaming",
				"Advertising"
			}
		);

		if (Target.bWithLiveCoding)
		{
			DynamicallyLoadedModuleNames.Add("LiveCoding");
		}

		if (Target.Type != TargetType.Server)
		{
			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"ImageWrapper"
				}
			);
		}

		WhitelistRestrictedFolders.Add("Private/NotForLicensees");

		if (!Target.bBuildRequiresCookedData && Target.bCompileAgainstEngine)
		{
			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"DerivedDataCache",
					"TargetPlatform",
					"DesktopPlatform"
				}
			);
		}

		if (Target.bBuildEditor == true)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"UnrealEd",
					"Kismet"
				}
			);  // @todo api: Only public because of WITH_EDITOR and UNREALED_API

			CircularlyReferencedDependentModules.AddRange(
				new string[] {
					"UnrealEd",
					"Kismet"
				}
			);

			PrivateIncludePathModuleNames.Add("TextureCompressor");
			PrivateIncludePaths.Add("Developer/TextureCompressor/Public");

			PrivateIncludePathModuleNames.Add("HierarchicalLODUtilities");
			DynamicallyLoadedModuleNames.Add("HierarchicalLODUtilities");

			DynamicallyLoadedModuleNames.Add("AnimationModifiers");

			PrivateIncludePathModuleNames.Add("AssetTools");
			DynamicallyLoadedModuleNames.Add("AssetTools");

			PrivateIncludePathModuleNames.Add("PIEPreviewDeviceProfileSelector");
		}

		SetupModulePhysicsSupport(Target);

		if (Target.bCompilePhysX && (Target.bBuildEditor || Target.bCompileAPEX))
		{
			DynamicallyLoadedModuleNames.Add("PhysXCooking");
		}

		// Engine public headers need to know about some types (enums etc.)
		PublicIncludePathModuleNames.Add("ClothingSystemRuntimeInterface");
		PublicDependencyModuleNames.Add("ClothingSystemRuntimeInterface");

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("ClothingSystemEditorInterface");
			PrivateIncludePathModuleNames.Add("ClothingSystemEditorInterface");
		}

		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32))
		{
			// Head Mounted Display support
			//			PrivateIncludePathModuleNames.AddRange(new string[] { "HeadMountedDisplay" });
			//			DynamicallyLoadedModuleNames.AddRange(new string[] { "HeadMountedDisplay" });
		}

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.AddRange(new string[] { "AVFoundation", "CoreVideo", "CoreMedia" });
		}

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"UEOgg",
				"Vorbis",
				"VorbisFile"
				);

			PrivateIncludePathModuleNames.Add("AndroidRuntimeSettings");
		}

		if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS)
		{
			PublicIncludePaths.AddRange(
            	new string[] {
               		"Runtime/IOS/IOSPlatformFeatures/Public"
                });

			PrivateIncludePathModuleNames.Add("IOSRuntimeSettings");
		}

		if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			PrivateIncludePathModuleNames.Add("SwitchRuntimeSettings");
		}

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"UEOgg",
				"Vorbis",
				"VorbisFile",
				"libOpus"
				);
		}

		PublicDefinitions.Add("GPUPARTICLE_LOCAL_VF_ONLY=0");

		// Add a reference to the stats HTML files referenced by UEngine::DumpFPSChartToHTML. Previously staged by CopyBuildToStagingDirectory.
		if (Target.bBuildEditor || Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			RuntimeDependencies.Add("$(EngineDir)/Content/Stats/...", StagedFileType.UFS);
		}

		if (Target.bBuildEditor == false && Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PublicDefinitions.Add("WITH_ODSC=1");
		}
        else
        {
			PublicDefinitions.Add("WITH_ODSC=0");
		}
	}
}
