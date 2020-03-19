// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class UnrealUSDWrapper : ModuleRules
	{
		public UnrealUSDWrapper(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableExceptions = false;
			bUseRTTI = true;

			PublicDependencyModuleNames.AddRange(
				new string[] {
				"Core",
				"CoreUObject",
				"Python"
				});

			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
			var PythonSourceTPSDir = Path.Combine(EngineDir, "Source", "ThirdParty", "Python");

			if (Target.WindowsPlatform.Compiler != WindowsCompiler.Clang && Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.None &&
				Target.CppStandard < CppStandardVersion.Cpp17 && // Not currently compatible with C++17 due to old version of Boost
				Target.LinkType != TargetLinkType.Monolithic && // If you want to use USD in a monolithic target, you'll have to use the ANSI allocator and remove this condition
				(Target.Platform != UnrealTargetPlatform.Linux || Target.bForceEnableRTTI)) // USD on Linux needs RTTI enabled for the whole editor
			{
				PublicDefinitions.Add("USE_USD_SDK=1");

				PublicIncludePaths.AddRange(
					new string[] {
					ModuleDirectory + "/../ThirdParty/USD/include",
					});

				var USDLibsDir = "";

				// Always use the official version of IntelTBB
				string IntelTBBLibs = Target.UEThirdPartySourceDirectory + "IntelTBB/IntelTBB-2019u8/lib/";

				if (Target.Platform == UnrealTargetPlatform.Win64)
				{
					PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=1");

					USDLibsDir = Path.Combine(ModuleDirectory, "../ThirdParty/USD/lib/");

					var USDLibs = new string[]
					{
						"ar",
						"arch",
						"gf",
						"js",
						"kind",
						"pcp",
						"plug",
						"sdf",
						"tf",
						"usd",
						"usdGeom",
						"usdLux",
						"usdShade",
						"usdSkel",
						"usdUtils",
						"vt",
						"work",
					};

					foreach (string UsdLib in USDLibs)
					{
						PublicAdditionalLibraries.Add(Path.Combine(USDLibsDir, UsdLib + ".lib"));
					}
					PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "Win64/vc14/tbb.lib"));

					PublicIncludePaths.Add(PythonSourceTPSDir + "/Win64/include");
					PublicSystemLibraryPaths.Add(Path.Combine(EngineDir, "Source/ThirdParty/Python/" + Target.Platform.ToString() + "/libs"));
				}
				else if (Target.Platform == UnrealTargetPlatform.Linux)
				{
					PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0"); // USD uses tbb malloc on Linux

					USDLibsDir = Path.Combine(ModuleDirectory, "../../Binaries/Linux/", Target.Architecture);

					var USDLibs = new string[]
					{
							"libar.so",
							"libarch.so",
							"libboost_python.so",
							"libgf.so",
							"libjs.so",
							"libkind.so",
							"libndr.so",
							"libpcp.so",
							"libplug.so",
							"libsdf.so",
							"libsdr.so",
							"libtf.so",
							"libtrace.so",
							"libusd.so",
							"libusdGeom.so",
							"libusdLux.so",
							"libusdShade.so",
							"libusdSkel.so",
							"libusdUtils.so",
							"libusdVol.so",
							"libvt.so",
							"libwork.so",
					};

					PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "Linux/libtbb.so"));
					RuntimeDependencies.Add("$(EngineDir)/Binaries/Linux/libtbb.so.2", Path.Combine(IntelTBBLibs, "Linux/libtbb.so.2"));
					PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "Linux/libtbbmalloc.so"));

					foreach (string UsdLib in USDLibs)
					{
						PublicAdditionalLibraries.Add(Path.Combine(USDLibsDir, UsdLib));
					}

					PublicIncludePaths.Add(PythonSourceTPSDir + "/Linux/include/" + Target.Architecture);
					PublicSystemLibraryPaths.Add(Path.Combine(EngineDir, "Source/ThirdParty/Python/" + Target.Platform.ToString() + "/lib"));
				}

				if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux)
				{
					PublicSystemLibraryPaths.Add(USDLibsDir);
				}
				else
				{
					System.Console.WriteLine("UnrealUSDWrapper does not support this platform");
				}
			}
			else
			{
				PublicDefinitions.Add("USE_USD_SDK=0");
				PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0"); // USD uses tbb malloc on Linux
			}
		}
	}
}
