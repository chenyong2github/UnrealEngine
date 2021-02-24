// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "RemoteControlEntity.generated.h"

class URemoteControlPreset;

/**
 * Base class for exposed objects, properties, functions etc...
 */
USTRUCT(BlueprintType)
struct REMOTECONTROL_API FRemoteControlEntity
{
	GENERATED_BODY()

	FRemoteControlEntity() = default;

	virtual ~FRemoteControlEntity(){}

	FName Rename(FName NewLabel);

	/**
	 * Get the label of this entity.
	 */
	FName GetLabel() const { return Label; }

	/**
	 * Get the id of this entity.
	 */
	FGuid GetId() const { return Id; }

	/**
	 * Get the preset that owns this entity.
	 */
	URemoteControlPreset* GetOwner() { return Owner.Get(); }

	bool operator==(const FRemoteControlEntity& InEntity) const;
	bool operator==(FGuid InEntityId) const;
	friend uint32 REMOTECONTROL_API GetTypeHash(const FRemoteControlEntity& InEntity);

public:
	/**
	 * User specified metadata for this entity.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	TMap<FString, FString> Metadata;

protected:
	FRemoteControlEntity(URemoteControlPreset* InPreset, FName InLabel);
	
	/** The preset that owns this entity. */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	TWeakObjectPtr<URemoteControlPreset> Owner;
	
protected:
	/**
	 * This exposed entity's alias.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FName Label;

	/**
	 * Unique identifier for this entity
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FGuid Id;

	friend class URemoteControlExposeRegistry;
	friend class URemoteControlPreset;
};