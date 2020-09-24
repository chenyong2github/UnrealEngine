// Copyright Epic Games, Inc. All Rights Reserved.
#include "RemoteControlPresetActorFactory.h"
#include "AssetData.h"
#include "Engine/Level.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Actor.h"
#include "IRemoteControlModule.h"
#include "RemoteControlPreset.h"
#include "RemoteControlPresetActor.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Notifications/SNotificationList.h"

URemoteControlPresetActorFactory::URemoteControlPresetActorFactory()
{
	DisplayName = NSLOCTEXT("RemoteControlPresetActorFactory", "RemoteControlPresetDisplayName", "RemoteControlPreset");
	NewActorClass = ARemoteControlPresetActor::StaticClass();
}

bool URemoteControlPresetActorFactory::CanCreateActorFrom( const FAssetData& AssetData, FText& OutErrorMsg )
{
	if (AssetData.IsValid() && !AssetData.GetClass()->IsChildOf(URemoteControlPreset::StaticClass()))
	{
		OutErrorMsg = NSLOCTEXT("CanCreateActor", "NoRemoteControlPresetAsset", "A valid remote control preset asset must be specified.");
		return false;
	}

	return true;
}

AActor* URemoteControlPresetActorFactory::SpawnActor( UObject* Asset, ULevel* InLevel, const FTransform& Transform, EObjectFlags InObjectFlags, const FName Name )
{
	ARemoteControlPresetActor* NewActor = Cast<ARemoteControlPresetActor>(Super::SpawnActor(Asset, InLevel, Transform, InObjectFlags, Name));

	if (NewActor)
	{
		if (URemoteControlPreset* Preset = Cast<URemoteControlPreset>(Asset))
		{
			NewActor->Preset = Preset;
		}
	}

	return NewActor;
}

UObject* URemoteControlPresetActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
	if (ARemoteControlPresetActor* RemoteControlPresetActor = Cast<ARemoteControlPresetActor>(ActorInstance))
	{
		return RemoteControlPresetActor->Preset;
	}
	return nullptr;
}
