// Copyright Epic Games, Inc. All Rights Reserved.

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

		PublicDependencyModuleNames.AddRange(
		   new string[] {
				"DeveloperSettings"
		   }
	   );

		SetupModulePhysicsSupport(Target);
		

		// SetupModulePhysicsSupport adds a dependency on PhysicsCore, but we are PhysicsCore!
		PublicIncludePathModuleNames.Remove("PhysicsCore");
		PublicDependencyModuleNames.Remove("PhysicsCore");

		if (Target.bCompileChaos == false && Target.bUseChaos == false)
        {
            if (Target.bCompilePhysX)
            {
                // Not ideal but as this module publicly exposes PhysX types
                // to other modules when PhysX is enabled it requires that its
                // public files have access to PhysX includes
                PublicDependencyModuleNames.Add("PhysX");
            }

            if (Target.bCompileAPEX)
            {
                PublicDependencyModuleNames.Add("APEX");
            }
        }

		PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
	}
}
