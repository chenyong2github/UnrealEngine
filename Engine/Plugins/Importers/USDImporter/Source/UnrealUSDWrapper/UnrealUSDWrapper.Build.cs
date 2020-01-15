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
				"Python"
				});

			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
			var PythonSourceTPSDir = Path.Combine(EngineDir, "Source", "ThirdParty", "Python");

			if (Target.WindowsPlatform.Compiler != WindowsCompiler.Clang && Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.None &&
				Target.LinkType != TargetLinkType.Monolithic && // If you want to use USD in a monolithic target, you'll have to use the ANSI allocator and remove this condition
				(Target.Platform != UnrealTargetPlatform.Linux || Target.bForceEnableRTTI)) // USD on Linux needs RTTI enabled for the whole editor
			{
				PublicDefinitions.Add("USE_USD_SDK=1");

                PublicIncludePaths.AddRange(
					new string[] {
					ModuleDirectory + "/../ThirdParty/USD/include",
					});

				var USDLibsDir = "";

				if (Target.Platform == UnrealTargetPlatform.Win64)
				{
                    PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=1");

                    USDLibsDir = Path.Combine(ModuleDirectory, "../ThirdParty/USD/lib/");

					var USDLibs = new string[]
					{
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

					PublicIncludePaths.Add(PythonSourceTPSDir + "/Win64/include");
                    PublicSystemLibraryPaths.Add(Path.Combine(EngineDir, "Source/ThirdParty/Python/" + Target.Platform.ToString() + "/libs"));
                }
				else if (Target.Platform == UnrealTargetPlatform.Linux)
				{
                    PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0"); // USD uses tbb malloc on Linux

                    USDLibsDir = Path.Combine(ModuleDirectory, "../../Binaries/Linux/", Target.Architecture);

					var USDLibs = new string[]
					{
							"libarch.so",
                            "libboost_python.so",
							"libgf.so",
							"libjs.so",
							"libkind.so",
							"libpcp.so",
							"libplug.so",
							"libsdf.so",
							"libtbb.so",
							"libtbbmalloc.so",
							"libtf.so",
							"libusd.so",
							"libusdGeom.so",
							"libusdLux.so",
							"libusdShade.so",
							"libusdSkel.so",
							"libusdUtils.so",
							"libvt.so",
							"libwork.so",
					};

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
