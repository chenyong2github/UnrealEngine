// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FractureEngine : ModuleRules
{
	public FractureEngine(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add(ModuleDirectory + "/Private");
		PublicIncludePaths.Add(ModuleDirectory + "/Public");
		PublicDependencyModuleNames.AddRange(
			new string[]
			{

				// ... add other public dependencies that you statically link with here ...
			}
            );
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{

				// ... add private dependencies that you statically link with here ...	
			}
            );
	}
}
