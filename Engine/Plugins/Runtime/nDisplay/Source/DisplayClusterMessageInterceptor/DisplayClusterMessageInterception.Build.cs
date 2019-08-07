// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterMessageInterception : ModuleRules
{
	public DisplayClusterMessageInterception(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DisplayCluster",
				"Engine",
				"Messaging",
			});
	}
}
