// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/LevelSnapshotsEditorProjectSettings.h"

#include "HAL/PlatformApplicationMisc.h"

ULevelSnapshotsEditorProjectSettings::ULevelSnapshotsEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
{
	bEnableLevelSnapshotsToolbarButton = true;
	bUseCreationForm = true;
	
	const FVector2D DefaultClientSize = FVector2D(400.f, 400.f);

	const FSlateRect WorkAreaRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
	const float DPIScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(WorkAreaRect.Left, WorkAreaRect.Right);
	
	PreferredCreationFormWindowWidth = DefaultClientSize.X * DPIScale;
	PreferredCreationFormWindowHeight = DefaultClientSize.Y * DPIScale;
}

FVector2D ULevelSnapshotsEditorProjectSettings::GetLastCreationWindowSize() const
{
	return FVector2D(PreferredCreationFormWindowWidth, PreferredCreationFormWindowHeight);
}

void ULevelSnapshotsEditorProjectSettings::SetLastCreationWindowSize(const FVector2D InLastSize)
{
	PreferredCreationFormWindowWidth = InLastSize.X;
	PreferredCreationFormWindowHeight = InLastSize.Y;
}
