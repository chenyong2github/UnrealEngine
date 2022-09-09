// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PrefabsRuntime : ModuleRules
	{
		public PrefabsRuntime(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                }
			); 
		}
	}
}
