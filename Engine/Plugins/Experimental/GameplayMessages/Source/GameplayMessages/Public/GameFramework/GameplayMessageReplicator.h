// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTags/Classes/GameplayTagContainer.h"

#include "GameplayMessageReplicator.generated.h"

class FArchive;
class UPackageMap;

// On the client once we receive the USTRUCT payload type we need to malloc some memory to deserialize the payload into.
// To manage the lifetime and freeing of this memory we wrap it in a shared pointer inside the container which is replicated.
USTRUCT()
struct FReplicatedMessageData
{
	GENERATED_BODY()

	FReplicatedMessageData() { }
	FReplicatedMessageData(UScriptStruct* InStructType, void* InMessageBytes)
		: StructType(InStructType)
		, MessageBytes(InMessageBytes)
	{ }

	~FReplicatedMessageData();

	void AllocateForStruct(UScriptStruct* InStructType);

	UPROPERTY()
	UScriptStruct* StructType = nullptr;

	void* MessageBytes = nullptr;

private:
	// We only need to malloc when deserializing on the client so we only need to free when this has happened.
	bool bShouldFreeMemory = false;
};

USTRUCT()
struct FReplicatedMessage
{
	GENERATED_BODY()

	FReplicatedMessage() { }
	FReplicatedMessage(UScriptStruct* InStructType, void* InMessageBytes);

	TSharedPtr<FReplicatedMessageData> MessageData;

	bool IsValid() const;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FReplicatedMessage> : public TStructOpsTypeTraitsBase2<FReplicatedMessage>
{
	enum
	{
		WithNetSerializer = true
	};
};

UCLASS()
class AGameplayMessageReplicator : public AActor
{
	GENERATED_BODY()

public:
	AGameplayMessageReplicator();

	void ReplicateMessage(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes);

private:

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_ServerMessageTriggered(FGameplayTag Channel, const FReplicatedMessage& MessageData);
};