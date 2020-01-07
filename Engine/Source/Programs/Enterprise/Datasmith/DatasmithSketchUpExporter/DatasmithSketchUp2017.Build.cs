// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public abstract class DatasmithSketchUpBase : ModuleRules
	{
		public DatasmithSketchUpBase(ReadOnlyTargetRules Target)
			: base(Target)
		{
			bUseRTTI = true;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"DatasmithExporter",
					"Projects",
					"UEOpenExr",
				}
			);

			PrivateIncludePaths.AddRange(
				new string[]
				{
					"Runtime/Launch/Public",
					"Runtime/Launch/Private",
					ModuleDirectory
				}
			);

			PrivateDefinitions.Add("DATASMITH_SKETCHUP_EXPORTER_DLL");

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
					PublicAdditionalLibraries.Add(Path.Combine(SketchUpSDKLocation, "binaries", "sketchup", "x64", "SketchUpAPI.lib"));
					PublicDelayLoadDLLs.Add("SketchUpAPI.dll");
				}
			}
		}

		public abstract string GetSketchUpSDKFolder();
		public abstract string GetSketchUpEnvVar();
	}

	[SupportedPlatforms("Win64")]
	public class DatasmithSketchUp2017 : DatasmithSketchUpBase
	{
		public DatasmithSketchUp2017(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PrivateDefinitions.Add("SKP_SDK_2017");
		}

		public override string GetSketchUpSDKFolder()
		{
			return "SDK_Win_x64_17-2-2555";
		}

		public override string GetSketchUpEnvVar()
		{
			return "SKP_SDK_2017";
		}
	}
}
