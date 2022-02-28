// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CrunchCompression : ModuleRules
	{
		public CrunchCompression(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDefinitions.Add("WITH_CRUNCH=1");

			// PrivateIncludePaths.Add("Runtime/CrunchCompression/Private");
			PublicIncludePaths.Add("Runtime/CrunchCompression/Public");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Crunch"
				}
			);
        }
	}
}
