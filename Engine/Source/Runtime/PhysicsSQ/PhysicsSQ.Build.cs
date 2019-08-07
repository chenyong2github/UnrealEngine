// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PhysicsSQ: ModuleRules
{
	public PhysicsSQ(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"PhysicsCore"
			}
		);

		SetupModulePhysicsSupport(Target);
	}
}
