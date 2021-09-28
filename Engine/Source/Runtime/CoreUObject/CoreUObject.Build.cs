// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CoreUObject : ModuleRules
{
	public CoreUObject(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivatePCHHeaderFile = "Private/CoreUObjectPrivatePCH.h";

		SharedPCHHeaderFile = "Public/CoreUObjectSharedPCH.h";

		PrivateIncludePaths.Add("Runtime/CoreUObject/Private");

        PrivateIncludePathModuleNames.AddRange(
                new string[] 
			    {
				    "TargetPlatform",
			    }
            );

		PublicDependencyModuleNames.Add("Core");
        PublicDependencyModuleNames.Add("TraceLog");

		PrivateDependencyModuleNames.Add("Projects");
        PrivateDependencyModuleNames.Add("Json");

		//@TODO: UE-127233
		// UnsafeTypeCastWarningLevel = WarningLevel.Warning;

		if (Target.bBuildWithEditorOnlyData)
		{
			PrivateDependencyModuleNames.Add("DerivedDataCache");
		}
	}
}
