// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class DatasmithRuntime : ModuleRules
{
	public DatasmithRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.InShippingBuildsOnly;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.Add("Private");

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"DatasmithContent",
				"DatasmithCore",
				"DatasmithNativeTranslator",
				"DatasmithTranslator",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CinematicCamera",
				"Core",
				"CoreUObject",
				"DirectLink",
				"Engine",
				"IESFile",
				"FreeImage",
				"Landscape",
				"LevelSequence",
				"MeshDescription",
				"MeshUtilitiesCommon",
				"RawMesh",
				"RHI",
				"PhysicsCore",
				"RuntimePhysXCooking",
				"RenderCore",
				"SlateCore",
				"StaticMeshDescription",
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"DesktopPlatform",
					"MessageLog",
					"UnrealEd",
				}
			);
		}

		// Set environment variable DIRECTLINK_LOG to get DirectLink logging
		string DirectLog = System.Environment.GetEnvironmentVariable("DIRECTLINK_LOG");
		if (DirectLog != null)
		{
			PublicDefinitions.Add("DIRECTLINK_LOG");
		}

		// Add dependency to CoreTech to enable load of CAD files on Windows
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string CADRuntimeDllPath = EngineDirectory + "/Plugins/Enterprise/DatasmithCADImporter/Binaries/Win64";

			if (Target.Type == TargetType.Game)
            {
				PublicDefinitions.Add("USE_CAD_RUNTIME_DLL");
			}

			PublicDefinitions.Add("DATASMITH_CAD_IGNORE_CACHE");

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CADInterfaces",
					"DatasmithCADTranslator",
					// Temporarily remove dependency to Rhino and Wire translators
					//"DatasmithWireTranslator",
					//"DatasmithOpenNurbsTranslator",
					"DatasmithDispatcher",
				}
			);

			if (System.Type.GetType("CoreTech") != null)
			{
				PublicDependencyModuleNames.Add("CoreTech");
				
				CADRuntimeDllPath = Path.Combine(CADRuntimeDllPath, "DatasmithCADRuntime.dll");
				if (File.Exists(CADRuntimeDllPath))
                {
					RuntimeDependencies.Add(CADRuntimeDllPath);
				}
			}
			else
            {
				if (File.Exists(Path.Combine(CADRuntimeDllPath, "DatasmithCADRuntime.dll")))
				{
					string[] CADRuntimeDlls = new string[] { "kernel_io.dll", "mimalloc-override.dll", "mimalloc-redirect.dll", "oda_translator.exe", "DatasmithCADRuntime.dll" };

					foreach (string DllName in CADRuntimeDlls)
					{
						RuntimeDependencies.Add(Path.Combine(CADRuntimeDllPath, DllName));
					}
				}
			}
		}
	}
}