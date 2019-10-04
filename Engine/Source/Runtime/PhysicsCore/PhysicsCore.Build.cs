// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PhysicsCore: ModuleRules
{
	public PhysicsCore(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicIncludePaths.Add("Runtime/PhysicsCore/Public");

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject"
			}
		);

		SetupModulePhysicsSupport(Target); 

		if(Target.bCompilePhysX)
        {
			// Not ideal but as this module publicly exposes PhysX types
			// to other modules when PhysX is enabled it requires that its
			// public files have access to PhysX includes
            PublicDependencyModuleNames.Add("PhysX");
        }

		if(Target.bCompileAPEX)
        {
            PublicDependencyModuleNames.Add("APEX");
        }
	}
}
