// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class Media : ModuleRules
	{
		public Media(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				});

			// Make sure we always have the info module for the common platforms
			DynamicallyLoadedModuleNames.Add(GetMediaInfoModuleName());

			PrivateIncludePaths.AddRange(
				new string[] {
					"Runtime/Media/Private",
				});
		}

		protected virtual string GetMediaInfoModuleName()
        {
			return "MediaInfo";
		}
	}
}
