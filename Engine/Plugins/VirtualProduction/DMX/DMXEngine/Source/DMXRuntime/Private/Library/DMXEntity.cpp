// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXEntity.h"
#include "Library/DMXLibrary.h"
#include "Interfaces/IDMXProtocol.h"

#include "Dom/JsonObject.h"

UDMXEntity::UDMXEntity()
	: ParentLibrary(nullptr)
{
	FPlatformMisc::CreateGuid(Id);
}

FString UDMXEntity::GetDisplayName() const
{
	return Name;
}

void UDMXEntity::SetName(const FString& InNewName)
{
	Name = InNewName;
}

void UDMXEntity::SetParentLibrary(UDMXLibrary* InParent)
{
	ParentLibrary = InParent;
}

void UDMXEntity::RefreshID()
{
	FPlatformMisc::CreateGuid(Id);
}

void UDMXEntity::ReplicateID(UDMXEntity* Other)
{
	Id = Other->Id;
}
