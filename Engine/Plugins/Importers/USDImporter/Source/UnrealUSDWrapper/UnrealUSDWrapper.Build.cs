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
				});
			
			if (EnableUsdSdk(Target))
			{
				PublicDependencyModuleNames.Add("Python3");

				PublicDefinitions.Add("USE_USD_SDK=1");
				PublicDefinitions.Add("BOOST_LIB_TOOLSET=\"vc141\"");

				PublicIncludePaths.AddRange(
					new string[] {
					ModuleDirectory + "/../ThirdParty/USD/include",
					});

				var USDLibsDir = "";

				var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
				var PythonSourceTPSDir = Path.Combine(EngineDir, "Source", "ThirdParty", "Python3");
				var PythonBinaryTPSDir = Path.Combine(EngineDir, "Binaries", "ThirdParty", "Python3");

				// Always use the official version of IntelTBB
				string IntelTBBLibs = Target.UEThirdPartySourceDirectory + "Intel/TBB/IntelTBB-2019u8/lib/";
				string IntelTBBIncludes = Target.UEThirdPartySourceDirectory + "Intel/TBB/IntelTBB-2019u8/include/";

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
					PublicSystemLibraryPaths.Add(Path.Combine(EngineDir, "Source/ThirdParty/Python3/" + Target.Platform.ToString() + "/libs"));
				}
				else if (Target.Platform == UnrealTargetPlatform.Linux)
				{
					PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0"); // USD uses tbb malloc on Linux

					USDLibsDir = Path.Combine(ModuleDirectory, "../../Binaries/Linux/", Target.Architecture);

					var USDLibs = new string[]
					{
							"libar.so",
							"libarch.so",
							"libboost_python37.so",
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

					PublicSystemIncludePaths.Add(IntelTBBIncludes);
					PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "Linux/libtbb.so"));
					RuntimeDependencies.Add("$(EngineDir)/Binaries/Linux/libtbb.so.2", Path.Combine(IntelTBBLibs, "Linux/libtbb.so.2"));
					PublicAdditionalLibraries.Add(Path.Combine(IntelTBBLibs, "Linux/libtbbmalloc.so"));

					foreach (string UsdLib in USDLibs)
					{
						PublicAdditionalLibraries.Add(Path.Combine(USDLibsDir, UsdLib));
					}

					PublicSystemLibraryPaths.Add(Path.Combine(EngineDir, "Source/ThirdParty/Python3/" + Target.Platform.ToString() + "/lib"));
				}
                else if (Target.Platform == UnrealTargetPlatform.Mac)
                {
                    PublicDefinitions.Add("USD_USES_SYSTEM_MALLOC=0");
                    
                    USDLibsDir = Path.Combine(ModuleDirectory, "../../Binaries/Mac/");

                    var USDLibs = new string[]
                    {
                        "libar",
                        "libarch",
                        "libboost_python37",
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

                    foreach (string UsdLib in USDLibs)
                    {
                        PublicAdditionalLibraries.Add(Path.Combine(USDLibsDir, UsdLib + ".dylib"));
                    }

                    PublicAdditionalLibraries.Add(Path.Combine(PythonBinaryTPSDir, "Mac", "lib", "libpython3.7.dylib"));
                    PublicIncludePaths.Add(Path.Combine(PythonSourceTPSDir, "Mac", "include"));
                    PublicSystemLibraryPaths.Add(Path.Combine(PythonBinaryTPSDir, "Mac", "lib"));
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
			// USD SDK has been built against Python 3 and won't launch if the editor is using Python 2

			bool bEnableUsdSdk = (Target.WindowsPlatform.Compiler != WindowsCompiler.Clang && Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.None &&
				Target.LinkType != TargetLinkType.Monolithic); // If you want to use USD in a monolithic target, you'll have to use the ANSI allocator and remove this condition

			// Don't enable USD when running the include tool because it has issues parsing Boost headers
			if (Target.GlobalDefinitions.Contains("UE_INCLUDE_TOOL=1"))
			{
				bEnableUsdSdk = false;
			}

			return bEnableUsdSdk;
		}
	}
}
