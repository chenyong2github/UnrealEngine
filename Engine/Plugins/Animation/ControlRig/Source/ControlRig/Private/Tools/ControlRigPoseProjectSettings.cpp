// Copyright Epic Games, Inc. All Rights Reserved.
#include "Tools/ControlRigPoseProjectSettings.h"

UControlRigPoseProjectSettings::UControlRigPoseProjectSettings()
{
	FDirectoryPath RootSaveDir;
 	RootSaveDir.Path = TEXT("/Game/ControlRig/Pose");
	RootSaveDirs.Add(RootSaveDir);
}

TArray<FString> UControlRigPoseProjectSettings::GetAssetPaths() const
{
	TArray<FString> Paths;
	for (const FDirectoryPath& Path : RootSaveDirs)
	{
		Paths.Add(Path.Path);
	}
	return Paths;
}
