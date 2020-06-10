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
				"Python",
				"USDClasses"
				});
			
			if (EnableUsdSdk(Target))
			{
				PublicDefinitions.Add("USE_USD_SDK=1");

				PublicIncludePaths.AddRange(
					new string[] {
					ModuleDirectory + "/../ThirdParty/USD/include",
					});

				var USDLibsDir = "";

				var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
				var PythonSourceTPSDir = Path.Combine(EngineDir, "Source", "ThirdParty", "Python");
				var PythonBinaryTPSDir = Path.Combine(EngineDir, "Binaries", "ThirdParty", "Python");

				// Always use the official version of IntelTBB
				string IntelTBBLibs = Target.UEThirdPartySourceDirectory + "Intel/TBB/IntelTBB-2019u8/lib/";

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
                else if (Target.Platform == UnrealTargetPlatform.Mac)
                {
                    PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0");
                    
                    USDLibsDir = Path.Combine(ModuleDirectory, "../../Binaries/Mac/");

                    var USDLibs = new string[]
                    {
                        "libar",
                        "libarch",
                        "libboost_python",
                        "libgf",
                        "libjs",
                        "libkind",
                        "libndr",
                        "libpcp",
                        "libplug",
                        "libsdf",
                        "libsdr",
                        "libtf",
                        "libusd",
                        "libusdGeom",
                        "libusdLux",
                        "libusdShade",
                        "libusdSkel",
                        "libusdUtils",
                        "libvt",
                        "libwork",
                    };

					PublicDependencyModuleNames.AddRange(
						new string[] {
						"IntelTBB",
						});

                    foreach (string UsdLib in USDLibs)
                    {
                        PublicAdditionalLibraries.Add(Path.Combine(USDLibsDir, UsdLib + ".dylib"));
                    }

                    PublicAdditionalLibraries.Add(Path.Combine(PythonBinaryTPSDir, "Mac", "libpython2.7.dylib"));
                    PublicIncludePaths.Add(PythonSourceTPSDir + "/Mac/include/");
                    PublicSystemLibraryPaths.Add(Path.Combine(PythonBinaryTPSDir, "Mac"));
                }
                
                PublicSystemLibraryPaths.Add(USDLibsDir);
			}
			else
			{
				PublicDefinitions.Add("USE_USD_SDK=0");
				PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0");
			}
		}

		bool EnableUsdSdk(ReadOnlyTargetRules Target)
		{
			bool bEnableUsdSdk = ( Target.WindowsPlatform.Compiler != WindowsCompiler.Clang && Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.None &&
				Target.CppStandard < CppStandardVersion.Cpp17 && // Not currently compatible with C++17 due to old version of Boost
				Target.LinkType != TargetLinkType.Monolithic ); // If you want to use USD in a monolithic target, you'll have to use the ANSI allocator and remove this condition

			if (Target.GlobalDefinitions.Contains("UE_INCLUDE_TOOL=1"))
			{
				bEnableUsdSdk = false;
			}

			return bEnableUsdSdk;
		}
	}
}
