// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlEntity.h"

#include "RemoteControlBinding.h"
#include "RemoteControlPreset.h"

bool FRemoteControlEntity::operator==(const FRemoteControlEntity& InEntity) const
{
	return Id == InEntity.Id;
}

bool FRemoteControlEntity::operator==(FGuid InEntityId) const
{
	return Id == InEntityId;
}

FRemoteControlEntity::FRemoteControlEntity(URemoteControlPreset* InPreset, FName InLabel, const TArray<URemoteControlBinding*>& InBindings)
	: Owner(InPreset)
	, Label(InLabel)
	, Id(FGuid::NewGuid())
{
	Bindings.Append(InBindings);
}

FName FRemoteControlEntity::Rename(FName NewLabel)
{
	if (URemoteControlPreset* Preset = Owner.Get())
	{
		Preset->Modify();
		return Preset->RenameExposedEntity(Id, NewLabel);
	}

	checkNoEntry();
	return NAME_None;
}

uint32 GetTypeHash(const FRemoteControlEntity& InEntity)
{
	return GetTypeHash(InEntity.Id);
}