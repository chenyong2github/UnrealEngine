// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class XGEController : ModuleRules
{
	public XGEController(ReadOnlyTargetRules TargetRules)
		: base(TargetRules)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});
		
		PrivateIncludePathModuleNames.Add("Engine");
	}
}
