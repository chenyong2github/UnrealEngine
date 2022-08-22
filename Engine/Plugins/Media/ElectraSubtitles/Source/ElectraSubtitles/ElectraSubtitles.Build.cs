// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ElectraSubtitles : ModuleRules
	{
		public ElectraSubtitles(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseSharedPCHs;
			PrivatePCHHeaderFile = "Public/ElectraSubtitleModule.h";

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"ElectraBase",
					"Expat"
				});
			// Expat is a static library, not a DLL.
			PrivateDefinitions.Add("XML_STATIC=1");
		}
	}
}
