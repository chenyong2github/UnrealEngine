// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ElectraCDM : ModuleRules
	{
		public ElectraCDM(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseSharedPCHs;
			PrivatePCHHeaderFile = "Public/ElectraCDM.h";
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Json"
				});
		}
	}
}
