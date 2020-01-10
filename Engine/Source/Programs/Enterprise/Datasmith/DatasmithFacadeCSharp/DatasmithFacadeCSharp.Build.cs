// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64")]
	public class DatasmithFacadeCSharp : ModuleRules
	{
		public DatasmithFacadeCSharp(ReadOnlyTargetRules Target)
			: base(Target)
		{
			bUseRTTI = true;

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"DatasmithCore",
					"DatasmithExporter",
					"DatasmithFacade"
				}
			);

			bRequiresImplementModule = false;
		}
	}
}
