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
					"Engine",
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

			string DsC4DDynamicLocatation = Path.Combine(PluginDirectory, "Source", "DatasmithC4DDynamicImporter");
			if (Directory.Exists(DsC4DDynamicLocatation))
			{
				PublicIncludePaths.Add(Path.Combine(DsC4DDynamicLocatation, "Public"));
				PublicDefinitions.Add("_CHECK_DYNAMIC_IMPORTER_");
			}

			// Set up the C4D Melange SDK includes and libraries.
			string MelangeSDKLocation = Path.Combine(EngineDirectory, "Restricted/NotForLicensees/Source/ThirdParty/Enterprise/Melange/20.004_RBMelange20.0_259890");

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
