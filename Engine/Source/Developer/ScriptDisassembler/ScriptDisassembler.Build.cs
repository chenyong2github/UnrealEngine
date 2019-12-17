// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ScriptDisassembler : ModuleRules
	{
		public ScriptDisassembler(ReadOnlyTargetRules Target) : base(Target)
        {
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"CoreUObject"
			});
        }
	}
}
