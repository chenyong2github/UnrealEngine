// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/LevelSnapshotsEditorProjectSettings.h"

#include "Data/Util/ActorHashUtil.h"
#include "PropertyInfoHelpers.h"

#include "Application/SlateApplicationBase.h"
#include "HAL/PlatformApplicationMisc.h"

ULevelSnapshotsEditorProjectSettings::ULevelSnapshotsEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
{
	bEnableLevelSnapshotsToolbarButton = true;
	bUseCreationForm = true;
	
	const FVector2D DefaultClientSize = FVector2D(400.f, 400.f);

	float DPIScale = 1.0f;

	if (FSlateApplicationBase::IsInitialized())
	{
		const FSlateRect WorkAreaRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
		DPIScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(WorkAreaRect.Left, WorkAreaRect.Top);
	}

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

void ULevelSnapshotsEditorProjectSettings::PostInitProperties()
{
	UObject::PostInitProperties();
	
	FPropertyInfoHelpers::UpdateDecimalComparisionPrecision(FloatComparisonPrecision, DoubleComparisonPrecision);
	SnapshotUtil::GHashSettings = HashSettings;
}

#if WITH_EDITOR
void ULevelSnapshotsEditorProjectSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULevelSnapshotsEditorProjectSettings, FloatComparisonPrecision)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULevelSnapshotsEditorProjectSettings, DoubleComparisonPrecision))
	{
		FPropertyInfoHelpers::UpdateDecimalComparisionPrecision(FloatComparisonPrecision, DoubleComparisonPrecision);
	}

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULevelSnapshotsEditorProjectSettings, HashSettings))
	{
		SnapshotUtil::GHashSettings = HashSettings;
	}
	
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
