// Copyright Epic Games, Inc. All Rights Reserved.

using System.Linq;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class UnrealUSDWrapper : ModuleRules
	{
		public UnrealUSDWrapper(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseRTTI = true;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"IntelTBB",
					"USDClasses"
				}
			);

			if (EnableUsdSdk(Target))
			{
				PublicDependencyModuleNames.Add("Python3");

				PublicDefinitions.Add("USE_USD_SDK=1");
				PublicDefinitions.Add("BOOST_LIB_TOOLSET=\"vc141\"");

				var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
				var PythonSourceTPSDir = Path.Combine(EngineDir, "Source", "ThirdParty", "Python3", Target.Platform.ToString());
				var PythonBinaryTPSDir = Path.Combine(EngineDir, "Binaries", "ThirdParty", "Python3", Target.Platform.ToString());
				string IntelTBBLibs = Path.Combine(Target.UEThirdPartySourceDirectory, "Intel", "TBB", "IntelTBB-2019u8", "lib", Target.Platform.ToString());
				string IntelTBBIncludes = Path.Combine(Target.UEThirdPartySourceDirectory, "Intel", "TBB", "IntelTBB-2019u8", "include");

				if (Target.Platform == UnrealTargetPlatform.Win64)
				{
					PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=1");

					// TBB
					PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "vc14", "tbb.lib"));
					RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "tbb.dll"), Path.Combine(IntelTBBLibs, "vc14", "tbb.dll"));

					// Python3
					PublicIncludePaths.Add(Path.Combine(PythonSourceTPSDir, "include"));
					PublicSystemLibraryPaths.Add(Path.Combine(PythonSourceTPSDir, "libs"));
					RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", "python37.dll"), Path.Combine(PythonBinaryTPSDir, "python37.dll"));

					// USD
					PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "USD", "include"));
					var USDLibsDir = Path.Combine(ModuleDirectory, "..", "ThirdParty", "USD", "lib");
					PublicSystemLibraryPaths.Add(USDLibsDir);
					foreach (string UsdLib in Directory.EnumerateFiles(USDLibsDir, "*.lib", SearchOption.AllDirectories))
					{
						if(Path.GetFileName(UsdLib).StartsWith("boost"))
						{
							continue;
						}

						PublicAdditionalLibraries.Add(UsdLib);
					}
					var USDBinDir = Path.Combine(ModuleDirectory, "..", "ThirdParty", "USD", "bin");
					foreach (string UsdDll in Directory.EnumerateFiles(USDBinDir, "*.dll", SearchOption.AllDirectories))
					{
						// We can't delay-load the USD dlls as they contain data and vtables: They need to be next to the executable and implicitly linked
						RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", Path.GetFileName(UsdDll)), UsdDll);
					}
				}
				else if (Target.Platform == UnrealTargetPlatform.Linux)
				{
					PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0"); // USD uses tbb malloc on Linux

					// TBB
					PublicSystemIncludePaths.Add(IntelTBBIncludes);
					PrivateRuntimeLibraryPaths.Add(IntelTBBLibs);
					PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "libtbb.so"));
					PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "libtbbmalloc.so"));
					RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "libtbb.so"));
					RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "libtbb.so.2"));
					RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "libtbbmalloc.so"));
					RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "libtbbmalloc.so.2"));

					// Python3
					PublicIncludePaths.Add(Path.Combine(PythonSourceTPSDir, "include"));
					PublicSystemLibraryPaths.Add(Path.Combine(PythonSourceTPSDir, "lib"));
					PrivateRuntimeLibraryPaths.Add(Path.Combine(PythonBinaryTPSDir, "bin"));
					RuntimeDependencies.Add(Path.Combine(PythonBinaryTPSDir, "bin", "python3.7m"));

					// USD
					PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "USD", "include"));
					var USDBinDir = Path.Combine(ModuleDirectory, "..", "ThirdParty", "Linux", "bin", Target.Architecture);
					PrivateRuntimeLibraryPaths.Add(USDBinDir);
					foreach (string LibPath in Directory.EnumerateFiles(USDBinDir, "*.so*", SearchOption.AllDirectories))
					{
						if(LibPath.EndsWith(".so")) // Don't add all versions of libboost_python37.so as they're duplicates
						{
							PublicAdditionalLibraries.Add(LibPath);
						}

						RuntimeDependencies.Add(LibPath);
					}
				}
				else if (Target.Platform == UnrealTargetPlatform.Mac)
				{
					PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0");

					// TBB
					RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "libtbb.dylib"));
					RuntimeDependencies.Add(Path.Combine(IntelTBBLibs, "libtbbmalloc.dylib"));

					// Python3
					PublicIncludePaths.Add(Path.Combine(PythonSourceTPSDir, "include"));
					PublicSystemLibraryPaths.Add(Path.Combine(PythonSourceTPSDir, "lib"));
					PrivateRuntimeLibraryPaths.Add(Path.Combine(PythonBinaryTPSDir, "bin"));
					PublicAdditionalLibraries.Add(Path.Combine(PythonBinaryTPSDir, "bin", "libpython3.7.dylib"));

					// USD
					PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "USD", "include"));
					var USDBinDir = Path.Combine(ModuleDirectory, "..", "ThirdParty", "Mac", "bin");
					foreach (string LibPath in Directory.EnumerateFiles(USDBinDir, "*.dylib", SearchOption.AllDirectories))
					{
						PublicAdditionalLibraries.Add(LibPath);
						RuntimeDependencies.Add(LibPath);
					}
				}

				// When packaging we also need to move our USD plugins to the packaged executable
				if (Target.Type == TargetType.Game)
				{
					// The multiple nested folders make sure that the USD plugin plugInfo.json files can reference their library dlls in the engine folder or in the packaged game folder with the same relative path
					RuntimeDependencies.Add(
						Path.Combine("$(ProjectDir)", "Resources", "RequiredNestedFolder", "RequiredNestedFolder", "RequiredNestedFolder", "UsdResources", Target.Platform.ToString()),
						Path.Combine("$(PluginDir)", "Resources", "UsdResources", Target.Platform.ToString(), "...") // Moves everything that's inside the Windows folder
					);
				}
			}
			else
			{
				PublicDefinitions.Add("USE_USD_SDK=0");
				PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0");
			}
		}

		bool EnableUsdSdk(ReadOnlyTargetRules Target)
		{
			// USD SDK has been built against Python 3 and won't launch if the editor is using Python 2

			bool bEnableUsdSdk = (
				Target.WindowsPlatform.Compiler != WindowsCompiler.Clang &&
				Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.None
			);

			// Don't enable USD when running the include tool because it has issues parsing Boost headers
			if (Target.GlobalDefinitions.Contains("UE_INCLUDE_TOOL=1"))
			{
				bEnableUsdSdk = false;
			}

			// If you want to use USD in a monolithic target, you'll have to use the ANSI allocator.
			// USD always uses the ANSI C allocators directly. In a DLL UE build (so not monolithic) we can just override the operators new and delete
			// on each module with versions that use either the ANSI (so USD-compatible) allocators or the UE allocators (ModuleBoilerplate.h) when appropriate.
			// In a monolithic build we can't do that, as the primary game module will already define overrides for operator new and delete with
			// the standard UE allocators: Since we can only have one operator new/delete override on the entire monolithic executable, we can't define our own overrides.
			// The only way around it is by forcing the ansi allocator in your project's target file (YourProject/Source/YourProject.Target.cs) file like this:
			//
			//		public class YourProject : TargetRules
			//		{
			//			public YourProject(TargetInfo Target) : base(Target)
			//			{
			//				...
			//				GlobalDefinitions.Add("FORCE_ANSI_ALLOCATOR=1");
			//			}
			//		}
			//
			// This will force the entire built executable to use the ANSI C allocators for everything (by disabling the UE overrides in ModuleBoilerplate.h), and so UE and USD allocations will be compatible.
			// Note that by that point everything will be using the USD-compatible ANSI allocators anyway, so our overrides in USDMemory.h are also disabled, as they're unnecessary.
			// Also note that we're forced to use dynamic linking for monolithic targets mainly because static linking the USD libraries disables support for user USD plugins, and secondly
			// because those static libraries would need to be linked with the --whole-archive argument, and there is currently no standard way of doing that in UE.
			if (bEnableUsdSdk && Target.LinkType == TargetLinkType.Monolithic && !Target.GlobalDefinitions.Contains("FORCE_ANSI_ALLOCATOR=1"))
			{
				System.Console.WriteLine("Warning: To build a monolithic target (e.g. a game) using the USD SDK you need to set 'FORCE_ANSI_ALLOCATOR=1' as a Global Definition on your project's Target.cs file. See the explanation on UnrealUSDWrapper.build.cs for more details. The USD SDK will be force disabled otherwise.");
				PublicDefinitions.Add("USD_FORCE_DISABLED=1");
				bEnableUsdSdk = false;
			}
			else
			{
				PublicDefinitions.Add("USD_FORCE_DISABLED=0");
			}

			return bEnableUsdSdk;
		}
	}
}
