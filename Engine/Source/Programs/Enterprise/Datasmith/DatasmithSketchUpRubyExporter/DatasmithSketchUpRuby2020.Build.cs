// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public abstract class DatasmithSketchUpRubyBase : ModuleRules
	{
		public DatasmithSketchUpRubyBase(ReadOnlyTargetRules Target)
			: base(Target)
		{
			bUseRTTI = true;

			// XXX
			OptimizeCode = CodeOptimization.Never;
			bUseUnity = false;
			PCHUsage = PCHUsageMode.NoPCHs;


			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					// For DirectLink
					"MessagingCommon",
					"Messaging",
					"RemoteImportMessaging",
					"UdpMessaging",

					"DatasmithExporter",

					"UEOpenExr",
				}
			);

			// Set up the SketchUp SDK paths and libraries.
			{
				string SketchUpSDKLocation = System.Environment.GetEnvironmentVariable(GetSketchUpEnvVar());

				if (!Directory.Exists(SketchUpSDKLocation))
				{
					// Try with build machine setup
					string SDKRootEnvVar = System.Environment.GetEnvironmentVariable("UE_SDKS_ROOT");
					if (SDKRootEnvVar != null && SDKRootEnvVar != "")
					{
						SketchUpSDKLocation = Path.Combine(SDKRootEnvVar, "HostWin64", "Win64", "SketchUp", GetSketchUpSDKFolder());
					}
				}

				// Make sure this version of the SketchUp SDK is actually installed.
				if (Directory.Exists(SketchUpSDKLocation))
				{
					PrivateIncludePaths.Add(Path.Combine(SketchUpSDKLocation, "headers"));
					PublicAdditionalLibraries.Add(Path.Combine(SketchUpSDKLocation, "binaries", "sketchup", "x64", "sketchup.lib"));
					PublicDelayLoadDLLs.Add("SketchUpAPI.dll");

					PrivateIncludePaths.Add(Path.Combine(SketchUpSDKLocation, "samples", "common", "ThirdParty", "ruby", "include", "win32_x64"));
					PublicAdditionalLibraries.Add(Path.Combine(SketchUpSDKLocation, "samples", "common", "ThirdParty", "ruby", "lib", "win", "x64", "x64-msvcrt-ruby250.lib"));
				}

				if (!Directory.Exists(SketchUpSDKLocation))
                {
					// System.Console.WriteLine("SketchUp SDK directory doesn't exist: '" + SketchUpSDKLocation + "'");
				}
			}
		}

		public abstract string GetSketchUpSDKFolder();
		public abstract string GetSketchUpEnvVar();
	}

	[SupportedPlatforms("Win64")]
	public class DatasmithSketchUpRuby2020 : DatasmithSketchUpRubyBase
	{
		public DatasmithSketchUpRuby2020(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PrivateDefinitions.Add("SKP_SDK_2020");
		}

		public override string GetSketchUpSDKFolder()
		{
			return "SDK_WIN_x64_2020-2-172";
		}

		public override string GetSketchUpEnvVar()
		{
			return "SKP_SDK_2020";
		}
	}
}
