// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class UnrealEd : ModuleRules
{
	public UnrealEd(ReadOnlyTargetRules Target) : base(Target)
	{
		if(Target.Type != TargetType.Editor)
		{
			throw new BuildException("Unable to instantiate UnrealEd module for non-editor targets.");
		}

		PrivatePCHHeaderFile = "Private/UnrealEdPrivatePCH.h";

		SharedPCHHeaderFile = "Public/UnrealEdSharedPCH.h";

		PrivateIncludePaths.AddRange(
			new string[]
			{
				"Editor/UnrealEd/Private",
				"Editor/UnrealEd/Private/Settings",
				"Editor/PackagesDialog/Public",
				"Developer/DerivedDataCache/Public",
				"Developer/TargetPlatform/Public",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"BehaviorTreeEditor",
				"ClassViewer",
				"StructViewer",
				"ContentBrowser",
				"DerivedDataCache",
				"DesktopPlatform",
				"LauncherPlatform",
				"GameProjectGeneration",
				"ProjectTargetPlatformEditor",
				"ImageWrapper",
				"MainFrame",
				"MaterialEditor",
				"MergeActors",
				"MeshUtilities",
				"MessagingCommon",
				"MovieSceneCapture",
				"PlacementMode",
				"Settings",
				"SettingsEditor",
				"AudioEditor",
				"ViewportSnapping",
				"SourceCodeAccess",
				"IntroTutorials",
				"OutputLog",
				"Landscape",
				"LocalizationService",
				"HierarchicalLODUtilities",
				"MessagingRpc",
				"PortalRpc",
				"PortalServices",
				"BlueprintNativeCodeGen",
				"ViewportInteraction",
				"VREditor",
				"Persona",
				"PhysicsAssetEditor",
				"ClothingSystemEditorInterface",
				"NavigationSystem",
				"Media",
				"VirtualTexturingEditor",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"DirectoryWatcher",
				"Documentation",
				"Engine",
				"Json",
				"Projects",
				"SandboxFile",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"SourceControl",
				"UnrealEdMessages",
				"GameplayDebugger",
				"BlueprintGraph",
				"Http",
				"UnrealAudio",
				"FunctionalTesting",
				"AutomationController",
				"Localization",
				"AudioEditor",
				"NetworkFileSystem",
				"UMG",
				"NavigationSystem",
				"MeshDescription",
                "StaticMeshDescription",
                "MeshBuilder",
                "MaterialShaderQualitySettings",
                "EditorSubsystem",
                "InteractiveToolsFramework",
				"ToolMenusEditor",
            }
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"AssetTagsEditor",
				"LevelSequence",
				"AnimGraph",
				"AppFramework",
				"BlueprintGraph",
				"CinematicCamera",
				"CurveEditor",
				"DesktopPlatform",
				"LauncherPlatform",
				"EditorStyle",
				"EngineSettings",
				"IESFile",
				"ImageWriteQueue",
				"InputCore",
				"InputBindingEditor",
				"LauncherServices",
				"MaterialEditor",
				"MessageLog",
				"PakFile",
				"PropertyEditor",
				"Projects",
				"RawMesh",
				"MeshUtilitiesCommon",
                "SkeletalMeshUtilitiesCommon",
                "RenderCore",
				"RHI",
				"Sockets",
				"SourceControlWindows",
				"StatsViewer",
				"SwarmInterface",
				"TargetPlatform",
				"TargetDeviceServices",
				"EditorWidgets",
				"GraphEditor",
				"Kismet",
				"InternationalizationSettings",
				"JsonUtilities",
				"Landscape",
				"MeshPaint",
				"MeshPaintMode",
				"Foliage",
				"VectorVM",
				"MaterialUtilities",
				"Localization",
				"LocalizationService",
				"AddContentDialog",
				"GameProjectGeneration",
				"HierarchicalLODUtilities",
				"Analytics",
				"AnalyticsET",
				"PluginWarden",
				"PixelInspectorModule",
				"MovieScene",
				"MovieSceneTracks",
				"Sequencer",
				"ViewportInteraction",
				"VREditor",
				"ClothingSystemEditor",
                "ClothingSystemRuntimeInterface",
                "ClothingSystemRuntimeCommon",
                "ClothingSystemRuntimeNv",
				"PIEPreviewDeviceProfileSelector",
				"PakFileUtilities",
				"TimeManagement",
                "LandscapeEditorUtilities",
                "DerivedDataCache",
				"ScriptDisassembler",
				"ToolMenus",
				"FreeImage",
				"IoStoreUtilities",
				"EditorInteractiveToolsFramework",
				"TraceLog",
				"DeveloperSettings"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				"FontEditor",
				"StaticMeshEditor",
				"TextureEditor",
				"Cascade",
				"UMGEditor",
				"Matinee",
				"AssetTools",
				"ClassViewer",
				"StructViewer",
				"CollectionManager",
				"ContentBrowser",
				"CurveTableEditor",
				"DataTableEditor",
				"EditorSettingsViewer",
				"LandscapeEditor",
				"KismetCompiler",
				"DetailCustomizations",
				"ComponentVisualizers",
				"MainFrame",
				"LevelEditor",
				"PackagesDialog",
				"Persona",
				"PhysicsAssetEditor",
				"ProjectLauncher",
				"DeviceManager",
				"SettingsEditor",
				"SessionFrontend",
				"StringTableEditor",
				"FoliageEdit",
				"ImageWrapper",
				"Blutility",
				"IntroTutorials",
				"WorkspaceMenuStructure",
				"PlacementMode",
				"MeshUtilities",
				"MergeActors",
				"ProjectSettingsViewer",
				"ProjectTargetPlatformEditor",
				"PListEditor",
				"BehaviorTreeEditor",
				"ViewportSnapping",
				"GameplayTasksEditor",
				"UndoHistory",
				"SourceCodeAccess",
				"HotReload",
				"PortalProxies",
				"PortalServices",
				"BlueprintNativeCodeGen",
				"OverlayEditor",
				"AnimationModifiers",
				"ClothPainter",
				"Media",
				"TimeManagementEditor",
				"VirtualTexturingEditor",
				"TraceInsights",
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Mac)
		{
			DynamicallyLoadedModuleNames.Add("IOSPlatformEditor");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Mac || Target.Platform == UnrealTargetPlatform.Linux)
		{
			DynamicallyLoadedModuleNames.Add("AndroidPlatformEditor");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Mac)
		{
			DynamicallyLoadedModuleNames.Add("LuminPlatformEditor");
		}

		CircularlyReferencedDependentModules.AddRange(
			new string[] {
				"Documentation",
				"GraphEditor",
				"Kismet",
				"AudioEditor",
				"ViewportInteraction",
				"VREditor",
				"MeshPaint",
				"MeshPaintMode",
				"PropertyEditor",
				"ToolMenusEditor",
				"InputBindingEditor",
				"ClothingSystemEditor",
				"PluginWarden",
				//"PIEPreviewDeviceProfileSelector",
				"EditorInteractiveToolsFramework"
			}
		);


		// Add include directory for Lightmass
		PublicIncludePaths.Add("Programs/UnrealLightmass/Public");

		PublicIncludePaths.Add("Developer/Android/AndroidDeviceDetection/Public/Interfaces");

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"AssetTagsEditor",
				"CollectionManager",
				"BlueprintGraph",
				"AddContentDialog",
				"MeshUtilities",
				"AssetTools",
				"KismetCompiler",
				"NavigationSystem",
				"GameplayTasks",
				"AIModule",
				"Engine",
				"SourceControl",
			}
		);


		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32))
		{
			PublicDependencyModuleNames.Add("XAudio2");
			PublicDependencyModuleNames.Add("AudioMixerXAudio2");

			PrivateDependencyModuleNames.Add("WindowsPlatformFeatures");
			PrivateDependencyModuleNames.Add("GameplayMediaEncoder");

			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"UEOgg",
				"Vorbis",
				"VorbisFile",
				"DX11Audio"
			);
		}

		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"VHACD",
			"FBX",
			"FreeType2"
		);

		SetupModulePhysicsSupport(Target);


		if (Target.bCompileRecast)
		{
			PrivateDependencyModuleNames.Add("Navmesh");
			PublicDefinitions.Add("WITH_RECAST=1");
			if (Target.bCompileNavmeshSegmentLinks)
			{
				PublicDefinitions.Add("WITH_NAVMESH_SEGMENT_LINKS=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_NAVMESH_SEGMENT_LINKS=0");
			}

			if (Target.bCompileNavmeshClusterLinks)
			{
				PublicDefinitions.Add("WITH_NAVMESH_CLUSTER_LINKS=1");
			}
			else
			{
				PublicDefinitions.Add("WITH_NAVMESH_CLUSTER_LINKS=0");
			}
		}
		else
		{
			PublicDefinitions.Add("WITH_RECAST=0");
			PublicDefinitions.Add("WITH_NAVMESH_CLUSTER_LINKS=0");
			PublicDefinitions.Add("WITH_NAVMESH_SEGMENT_LINKS=0");
		}

		if (Target.bWithLiveCoding)
		{
			PrivateIncludePathModuleNames.Add("LiveCoding");
		}
    }
}
