// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/LevelSnapshotsSettings.h"

#include "Data/Util/ActorHashUtil.h"
#include "PropertyInfoHelpers.h"

ULevelSnapshotsSettings* ULevelSnapshotsSettings::Get()
{
	return GetMutableDefault<ULevelSnapshotsSettings>();
}

void ULevelSnapshotsSettings::PostInitProperties()
{
	UObject::PostInitProperties();
	
	FPropertyInfoHelpers::UpdateDecimalComparisionPrecision(FloatComparisonPrecision, DoubleComparisonPrecision);
	SnapshotUtil::GHashSettings = HashSettings;
}

#if WITH_EDITOR
void ULevelSnapshotsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULevelSnapshotsSettings, FloatComparisonPrecision)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(ULevelSnapshotsSettings, DoubleComparisonPrecision))
	{
		FPropertyInfoHelpers::UpdateDecimalComparisionPrecision(FloatComparisonPrecision, DoubleComparisonPrecision);
	}

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULevelSnapshotsSettings, HashSettings))
	{
		SnapshotUtil::GHashSettings = HashSettings;
	}
	
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
