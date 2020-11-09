// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySnapshot.h"

#include "BaseObjectInfo.generated.h"

USTRUCT()
struct FSerializedActorData
{
	GENERATED_BODY()

	bool Serialize(FArchive& Ar);

	TArray<uint8> Data;
};

USTRUCT()
struct FBaseObjectInfo
{
	GENERATED_BODY()

	FBaseObjectInfo() = default;

	// Extract the base object information from a given object
	explicit FBaseObjectInfo(UObject* TargetObject);
	
	bool operator==(const FBaseObjectInfo& Other) const
	{
		// Placeholder. Likely we want to do more extensive checks here
		return Other.ObjectName == this->ObjectName;
	};
	
	/** The name of the object when it was serialized */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FName ObjectName;

	/** The outer path name of the object when it was serialized */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FString ObjectOuterPathName;

	/** The path name of the object's class. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FString ObjectClassPathName;

	/** The object flags that would be loaded from an package file. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	uint32 ObjectFlags;

	/** The object internal flags. i.e IsPendingKill */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	uint32 InternalObjectFlags;

	/** The object pointer address used to help identify renamed/moved object. */
	UPROPERTY()
	uint64 ObjectAddress;

	/** The object internal index in the global object array used to help identify renamed/moved object. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	uint32 InternalIndex;

	/** List of references to other objects, captured as soft object paths. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TArray<FSoftObjectPath> ReferencedObjects;

	/** List of references to names. */
	UPROPERTY()
	TArray<FName> ReferencedNames;

	/** Map of property scopes found in this object snapshot. */
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TMap<FName, FInternalPropertySnapshot> Properties;

	/** Calculated offset of where the property blocks start in the snapshot data buffer. */
	UPROPERTY()
	uint32 PropertyBlockStart = 0;

	/** Calculated offset of where the property blocks end in the snapshot data buffer. */
	UPROPERTY()
	uint32 PropertyBlockEnd = 0;

	/** Actor Snapshot data buffer. */
	UPROPERTY()
	FSerializedActorData SerializedData;
};