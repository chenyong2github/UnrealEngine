// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64")]
	public class DatasmithFacade : ModuleRules
	{
		public DatasmithFacade(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
                    "DatasmithCore"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"DatasmithExporter",
					"UEOpenExr",
				}
			);
		}
	}
}
