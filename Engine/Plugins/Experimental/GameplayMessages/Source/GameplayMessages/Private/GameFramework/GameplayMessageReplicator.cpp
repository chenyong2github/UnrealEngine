// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFramework/GameplayMessageReplicator.h"
#include "GameFramework/GameplayMessageRouter.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/PackageMapClient.h"
#include "Engine/World.h"
#include "HAL/UnrealMemory.h"
#include "Net/RepLayout.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/Archive.h"
#include "UObject/Class.h"

void FReplicatedMessageData::AllocateForStruct(UScriptStruct* InStructType)
{
	check(StructType == nullptr || StructType == InStructType);
	check(MessageBytes == nullptr);

	StructType = InStructType;

	if (StructType)
	{
		// TODO: Replace this malloc with a shared memory buffer FORT-340282
		MessageBytes = FMemory::Malloc(StructType->GetStructureSize());
		StructType->InitializeStruct(MessageBytes);

		bShouldFreeMemory = true;
	}
}

FReplicatedMessageData::~FReplicatedMessageData()
{
	if (bShouldFreeMemory && MessageBytes)
	{
		if (StructType)
		{
			StructType->DestroyStruct(MessageBytes);
		}

		FMemory::Free(MessageBytes);
	}
}

FReplicatedMessage::FReplicatedMessage(UScriptStruct* InStructType, void* InMessageBytes)
{
	MessageData = MakeShared<FReplicatedMessageData>(InStructType, InMessageBytes);
}

bool FReplicatedMessage::IsValid() const
{
	return MessageData.IsValid() && MessageData->StructType && MessageData->MessageBytes;
}

bool FReplicatedMessage::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = false;

	if (Ar.IsLoading())
	{
		MessageData = MakeShared<FReplicatedMessageData>();
	}

	if (MessageData.IsValid())
	{
		Ar << MessageData->StructType;

		if (MessageData->StructType)
		{
			if (Ar.IsLoading())
			{
				MessageData->AllocateForStruct(MessageData->StructType);
			}

			if (MessageData->StructType->StructFlags & STRUCT_NetSerializeNative)
			{
				// Try to use the native NetSerialize if this struct has one
				MessageData->StructType->GetCppStructOps()->NetSerialize(Ar, Map, bOutSuccess, MessageData->MessageBytes);
			}
			else
			{
				UNetConnection* Connection = CastChecked<UPackageMapClient>(Map)->GetConnection();
				UNetDriver* NetDriver = Connection ? Connection->GetDriver() : nullptr;
				TSharedPtr<FRepLayout> RepLayout = NetDriver ? NetDriver->GetStructRepLayout(MessageData->StructType) : nullptr;

				if (RepLayout.IsValid())
				{
					if (FBitArchive* BitAr = static_cast<FBitArchive*>(&Ar))
					{
						bool bHasUnmapped = false;				
						RepLayout->SerializePropertiesForStruct(MessageData->StructType, *BitAr, Map, MessageData->MessageBytes, bHasUnmapped);

						bOutSuccess = !bHasUnmapped;
					}
				}
			}
		}
	}

	if (!bOutSuccess)
	{
		UE_LOG(LogGameplayMessageRouter, Warning, TEXT("FReplicatedMessage::NetSerialize - Failed to serialize message"));

		// Maybe erroring the archive for any issue is a bit too nuclear but for initial work probably makes things simpler.
		Ar.SetError();
		bOutSuccess = false;
		return false;
	}

	return true;
}

AGameplayMessageReplicator::AGameplayMessageReplicator()
{
	bAlwaysRelevant = true;
	bReplicates = true;
}

void AGameplayMessageReplicator::ReplicateMessage(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes)
{
	FReplicatedMessage ReplicatedMessage(const_cast<UScriptStruct*>(StructType), const_cast<void*>(MessageBytes));

	Multicast_ServerMessageTriggered(Channel, ReplicatedMessage);
}

void AGameplayMessageReplicator::Multicast_ServerMessageTriggered_Implementation(FGameplayTag Channel, const FReplicatedMessage& ReplicatedMessage)
{
	if (!HasAuthority())
	{
		if (Channel.IsValid() && ReplicatedMessage.IsValid())
		{
			UGameplayMessageRouter::Get(GetWorld()).BroadcastMessageInternal(Channel, ReplicatedMessage.MessageData->StructType, ReplicatedMessage.MessageData->MessageBytes);
		}
	}
}
