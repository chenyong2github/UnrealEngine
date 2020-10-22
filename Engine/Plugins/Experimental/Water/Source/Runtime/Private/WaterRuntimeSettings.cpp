// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterRuntimeSettings.h"
#include "Materials/MaterialParameterCollection.h"

#if WITH_EDITOR
UWaterRuntimeSettings::FOnUpdateSettings UWaterRuntimeSettings::OnSettingsChange;
#endif //WITH_EDITOR

UWaterRuntimeSettings::UWaterRuntimeSettings()
	: MaterialParameterCollection(FSoftObjectPath(TEXT("/Water/Materials/MPC/MPC_Water.MPC_Water")))
	, DefaultWaterCollisionProfileName(TEXT("WaterBodyCollision"))
{

}

FName UWaterRuntimeSettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}

#if WITH_EDITOR
void UWaterRuntimeSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	OnSettingsChange.Broadcast(this, PropertyChangedEvent.ChangeType);
}
#endif // WITH_EDITOR
