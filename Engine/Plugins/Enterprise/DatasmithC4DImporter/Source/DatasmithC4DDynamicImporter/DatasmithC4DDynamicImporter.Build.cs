// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64", "Mac")]
	public class DatasmithC4DDynamicImporter : ModuleRules
	{
		public DatasmithC4DDynamicImporter(ReadOnlyTargetRules Target) : base(Target)
		{
			bLegalToDistributeObjectCode = true;
			CppStandard = CppStandardVersion.Cpp17;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Analytics",
					"Core",
					"CoreUObject",
					"DatasmithCore",
					"Engine",
					"InputCore",
					"Json",
					"MeshDescription",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UEOpenExr",
                }
			);

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"DatasmithExporter",
					}
				);
			}

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"DatasmithContent",
					"DatasmithTranslator"
				}
			);

			// Temporary 'soft' reference to DatasmithC4DTranslator to access IDatasmithC4DImporter.h
			// We cannot use PublicDependencyModuleNames because DatasmithC4DTranslator actually depends on this module.
			// TODO: Revisit modules so IDatasmithC4DImporter.h has its own module
			string DsC4DImportLocation = Path.Combine(PluginDirectory, "Source", "DatasmithC4DTranslator");
			PublicIncludePaths.Add(Path.Combine(DsC4DImportLocation, "Public"));

			// Set up the C4D Cineware SDK includes and libraries.
			string CinewareSDKLocation = "";
			string SDKRootEnvVar = System.Environment.GetEnvironmentVariable("UE_SDKS_ROOT");
			if (SDKRootEnvVar != null && SDKRootEnvVar != "")
            {
				CinewareSDKLocation = Target.Platform == UnrealTargetPlatform.Mac ? Path.Combine(SDKRootEnvVar, "HostMac", "Mac", "CinewareSDK") : Path.Combine(SDKRootEnvVar, "HostWin64", "Win64", "CinewareSDK");
			}

			// When C4D Cineware SDK is not part of the developer's workspace, look for environment variable Cineware_SDK.
			if (!Directory.Exists(CinewareSDKLocation))
			{
				CinewareSDKLocation = System.Environment.GetEnvironmentVariable("Cineware_SDK");
			}

			// Make sure the C4D Cineware SDK can be used to compile
			bool bCanUseCinewareSDK = Directory.Exists(CinewareSDKLocation);
			// Temporary: Debug configuration does not link yet
			bCanUseCinewareSDK &= Target.Configuration == UnrealTargetConfiguration.Development || Target.Configuration == UnrealTargetConfiguration.Shipping;
			// Temporary: Do not enable Cineware SDK code if static analysis is requested
			if (Target.Platform == UnrealTargetPlatform.Win64)
            {
				bCanUseCinewareSDK &= Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.None;
			}

			if (bCanUseCinewareSDK)
			{
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "Public"));

				PublicDefinitions.Add("_CINEWARE_SDK_");
				PublicDefinitions.Add("MAXON_MODULE_ID=\"net.maxon.core.framework\"");

				PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS");
				PublicDefinitions.Add("PSAPI_VERSION=1");
				PublicDefinitions.Add("MAXON_TARGET_64BIT");
				PublicDefinitions.Add("__64BIT");
				PublicDefinitions.Add("_UNICODE");
				PublicDefinitions.Add("UNICODE");

				// Set up the C4D Melange SDK includes and libraries.
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes"));
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "c4d_customgui"));
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "c4d_gv"));
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "c4d_libs"));
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "c4d_misc"));
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "description"));

				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "generated", "core.framework", "hxx"));
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "generated", "cinema.framework", "hxx"));
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "generated", "image.framework", "hxx"));
				PublicIncludePaths.Add(Path.Combine(CinewareSDKLocation, "includes", "generated", "exchange.framework", "hxx"));

				if (Target.Platform == UnrealTargetPlatform.Win64)
				{
					PublicDefinitions.Add("__PC");
					PublicDefinitions.Add("_WIN64");
					PublicDefinitions.Add("_WINDOWS");
					PublicDefinitions.Add("MAXON_TARGET_WINDOWS");


					PublicDefinitions.Add("CINEWARE_LOCATION=\"C:/Program Files/Maxon Cinema 4D R24/cineware.dll\"");

					PublicAdditionalLibraries.Add(Path.Combine(CinewareSDKLocation, "libraries", "cinema.framework_Release_64bit.lib"));
					PublicAdditionalLibraries.Add(Path.Combine(CinewareSDKLocation, "libraries", "core.framework_Release_64bit.lib"));
					PublicAdditionalLibraries.Add(Path.Combine(CinewareSDKLocation, "libraries", "image.framework_Release_64bit.lib"));
					PublicAdditionalLibraries.Add(Path.Combine(CinewareSDKLocation, "libraries", "exchange.framework_Release_64bit.lib"));
				}
				else if (Target.Platform == UnrealTargetPlatform.Mac)
				{
					PublicDefinitions.Add("__MAC");
					PublicDefinitions.Add("MAXON_TARGET_MACOS");
					PublicDefinitions.Add("MAXON_TARGET_OSX");

					PublicDefinitions.Add("CINEWARE_LOCATION=\"/Applications/Maxon Cinema 4D R24/cineware.bundle/Contents/MacOS/cineware\"");

					PublicAdditionalLibraries.Add(Path.Combine(CinewareSDKLocation, "libraries", "libcinema.framework.a"));
					PublicAdditionalLibraries.Add(Path.Combine(CinewareSDKLocation, "libraries", "libcore.framework.a"));
					PublicAdditionalLibraries.Add(Path.Combine(CinewareSDKLocation, "libraries", "libimage.framework.a"));
					PublicAdditionalLibraries.Add(Path.Combine(CinewareSDKLocation, "libraries", "libexchange.framework.a"));
					PublicFrameworks.Add("CoreAudio"); // MDL uses some multimedia timers from Core.Audio
				}
			}
		}
	}
}
