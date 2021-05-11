// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ElectraCDM : ModuleRules
	{
		public ElectraCDM(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.NoPCHs;     // to get around the "xzy must be first header included" error in 3rdParty lib
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Json"
				});
		}
	}
}
