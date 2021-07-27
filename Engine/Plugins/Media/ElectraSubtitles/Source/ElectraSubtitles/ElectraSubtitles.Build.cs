// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ElectraSubtitles : ModuleRules
	{
		public ElectraSubtitles(ReadOnlyTargetRules Target) : base(Target)
		{
            PCHUsage = PCHUsageMode.NoPCHs;
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"ElectraBase",
					"ElectraSamples",
					"XmlParser"
				});
		}
	}
}
