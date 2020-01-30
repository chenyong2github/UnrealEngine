// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using Tools.DotNETCommon;

namespace UnrealBuildTool.Rules
{
	public class DatasmithC4DTranslator : ModuleRules
	{
		public DatasmithC4DTranslator(ReadOnlyTargetRules Target) : base(Target)
		{
			bLegalToDistributeObjectCode = true;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Analytics",
					"Core",
					"CoreUObject",
					"DatasmithCore",
					"DatasmithExporter",
					"Engine",
					"Json",
					"MeshDescription",
					"MeshDescriptionOperations",
					"MessageLog",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UEOpenExr",
                }
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"DatasmithContent",
					"DatasmithTranslator"
				}
			);

			// Set up the C4D Melange SDK includes and libraries.
			string MelangeSDKLocation = Path.Combine(PluginDirectory, "Source", "ThirdParty", "NotForLicensees", "Melange", "20.004_RBMelange20.0_259890");

			// When C4D Melange SDK is not part of the developer's workspace, look for environment variable Melange_SDK.
			if (!Directory.Exists(MelangeSDKLocation))
			{
				MelangeSDKLocation = System.Environment.GetEnvironmentVariable("Melange_SDK");
			}

			// Make sure the C4D Melange SDK is actually installed.
			if (Directory.Exists(MelangeSDKLocation))
			{
				PublicDefinitions.Add("_MELANGE_SDK_");
				PrivateDependencyModuleNames.Add("MelangeSDK");
			}
		}
	}
}
