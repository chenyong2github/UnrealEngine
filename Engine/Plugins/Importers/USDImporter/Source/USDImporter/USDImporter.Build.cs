// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class USDImporter : ModuleRules
	{
		public USDImporter(ReadOnlyTargetRules Target) : base(Target)
		{
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
					"GeometryCache",
					"MeshDescription",
					"MeshUtilities",
					"MessageLog",
					"PythonScriptPlugin",
					"StaticMeshDescription",
					"UnrealUSDWrapper",
					"USDClasses",
					"USDUtilities",
					"DeveloperSettings"
				}
				);

			// Always use the official version of IntelTBB
			string IntelTBBLibs = Target.UEThirdPartySourceDirectory + "Intel/TBB/IntelTBB-2019u8/lib/";

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				foreach (string FilePath in Directory.EnumerateFiles(Path.Combine(ModuleDirectory, "../../Binaries/Win64/"), "*.dll", SearchOption.AllDirectories))
				{
					RuntimeDependencies.Add(FilePath);
				}

				RuntimeDependencies.Add(IntelTBBLibs + "Win64/vc14/tbb.dll");
			}
			else if (Target.Platform == UnrealTargetPlatform.Linux && Target.Architecture.StartsWith("x86_64"))
			{

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
			else if (Target.Platform == UnrealTargetPlatform.Mac)
			{
				foreach (string FilePath in Directory.EnumerateFiles(Path.Combine(ModuleDirectory, "../../Binaries/Mac/"), "*.dylib", SearchOption.AllDirectories))
				{
					RuntimeDependencies.Add(FilePath);
				}

				RuntimeDependencies.Add(IntelTBBLibs + "Mac/libtbb.dylib");
				RuntimeDependencies.Add(IntelTBBLibs + "Mac/libtbbmalloc.dylib");
			}
		}
	}
}
