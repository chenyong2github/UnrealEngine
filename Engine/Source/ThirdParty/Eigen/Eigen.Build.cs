// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Eigen : ModuleRules
{
    public Eigen(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
		
		PublicIncludePaths.Add(ModuleDirectory);
        PublicIncludePaths.Add( ModuleDirectory + "/Eigen/" );
        PublicDefinitions.Add("EIGEN_MPL2_ONLY");
		ShadowVariableWarningLevel = WarningLevel.Off;
	}
}
