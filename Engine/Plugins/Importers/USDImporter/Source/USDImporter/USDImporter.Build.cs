// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDImporter : ModuleRules
	{
		public USDImporter(ReadOnlyTargetRules Target) : base(Target)
		{
			// We require the whole editor to be RTTI enabled on Linux for now
			if (Target.Platform != UnrealTargetPlatform.Linux)
			{
				bUseRTTI = true;
			}

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"JsonUtilities",
					"UnrealEd",
					"InputCore",
					"SlateCore",
					"PropertyEditor",
					"Slate",
				"EditorStyle",
                    "RawMesh",
                    "GeometryCache",
					"MeshDescription",
					"MeshUtilities",
					"MessageLog",
					"PythonScriptPlugin",
                    "RenderCore",
                    "RHI",
					"StaticMeshDescription",
					"UnrealUSDWrapper",
					"USDUtilities",
				}
				);

			// Always use the official version of IntelTBB
			string IntelTBBLibs = Target.UEThirdPartySourceDirectory + "IntelTBB/IntelTBB-2019u8/lib/";

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				PrivateDependencyModuleNames.Add("UnrealUSDWrapper");

				foreach (string FilePath in Directory.EnumerateFiles(Path.Combine(ModuleDirectory, "../../Binaries/Win64/"), "*.dll", SearchOption.AllDirectories))
				{
					RuntimeDependencies.Add(FilePath);
				}

				RuntimeDependencies.Add(IntelTBBLibs + "Win64/vc14/tbb.dll");
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux && Target.Architecture.StartsWith("x86_64"))
			{
				PrivateDependencyModuleNames.Add("UnrealUSDWrapper");

				// link directly to runtime libs on Linux, as this also puts them into rpath
				string RuntimeLibraryPath = Path.Combine(ModuleDirectory, "../../Binaries", Target.Platform.ToString(), Target.Architecture.ToString());
				PrivateRuntimeLibraryPaths.Add(RuntimeLibraryPath);

				RuntimeDependencies.Add(IntelTBBLibs + "Linux/libtbb.so");
				RuntimeDependencies.Add(IntelTBBLibs + "Linux/libtbb.so.2");
				RuntimeDependencies.Add(IntelTBBLibs + "Linux/libtbbmalloc.so");
				RuntimeDependencies.Add(IntelTBBLibs + "Linux/libtbbmalloc.so.2");

				foreach (string FilePath in Directory.EnumerateFiles(RuntimeLibraryPath, "*.so*", SearchOption.AllDirectories))
				{
					RuntimeDependencies.Add(FilePath);
				}
			}
		}
	}
}
