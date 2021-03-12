// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "IdentifierTable/ConcertIdentifierTableData.h"
#include "ConcertTransactionEvents.generated.h"

UENUM()
enum class ETransactionFilterResult : uint8
{
	/** Include the object in the Concert Transaction */
	IncludeObject,
	/** Filter the object from the Concert Transaction */
	ExcludeObject,
	/** Filter the entire transaction and prevent propagation */
	ExcludeTransaction,
	/** Delegate the filtering decision to the default handlers. */
	UseDefault
};

USTRUCT()
struct FConcertObjectId
{
	GENERATED_BODY()

	FConcertObjectId()
		: ObjectPersistentFlags(0)
	{
	}

	explicit FConcertObjectId(const UObject* InObject)
		: ObjectClassPathName(*InObject->GetClass()->GetPathName())
		, ObjectPackageName(InObject->GetPackage()->GetFName())
		, ObjectName(InObject->GetFName())
		, ObjectOuterPathName(InObject->GetOuter() ? FName(*InObject->GetOuter()->GetPathName()) : FName())
		, ObjectExternalPackageName(InObject->GetExternalPackage() ? InObject->GetExternalPackage()->GetFName() : FName())
		, ObjectPersistentFlags(InObject->GetFlags() & RF_Load)
	{
	}

	FConcertObjectId(const FName InObjectClassPathName, const FName InObjectPackageName, const FName InObjectName, const FName InObjectOuterPathName, const FName InObjectExternalPackageName,  const uint32 InObjectFlags)
		: ObjectClassPathName(InObjectClassPathName)
		, ObjectPackageName(InObjectPackageName)
		, ObjectName(InObjectName)
		, ObjectOuterPathName(InObjectOuterPathName)
		, ObjectExternalPackageName(InObjectExternalPackageName)
		, ObjectPersistentFlags(InObjectFlags & RF_Load)
	{
	}

	UPROPERTY()
	FName ObjectClassPathName;

	UPROPERTY()
	FName ObjectPackageName;

	UPROPERTY()
	FName ObjectName;

	UPROPERTY()
	FName ObjectOuterPathName;

	UPROPERTY()
	FName ObjectExternalPackageName;

	UPROPERTY()
	uint32 ObjectPersistentFlags;
};

USTRUCT()
struct FConcertSerializedObjectData
{
	GENERATED_BODY()

	UPROPERTY()
	bool bAllowCreate = false;

	UPROPERTY()
	bool bIsPendingKill = false;

	UPROPERTY()
	FName NewPackageName;

	UPROPERTY()
	FName NewName;

	UPROPERTY()
	FName NewOuterPathName;

	UPROPERTY()
	FName NewExternalPackageName;

	UPROPERTY()
	TArray<uint8> SerializedData;
};

USTRUCT()
struct FConcertSerializedPropertyData
{
	GENERATED_BODY()

	UPROPERTY()
	FName PropertyName;

	UPROPERTY()
	TArray<uint8> SerializedData;
};

USTRUCT()
struct FConcertExportedObject
{
	GENERATED_BODY()

	UPROPERTY()
	FConcertObjectId ObjectId;

	UPROPERTY()
	int32 ObjectPathDepth = 0;

	UPROPERTY()
	FConcertSerializedObjectData ObjectData;

	UPROPERTY()
	TArray<FConcertSerializedPropertyData> PropertyDatas;

	UPROPERTY()
	TArray<uint8> SerializedAnnotationData;
};

USTRUCT()
struct FConcertTransactionEventBase
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid TransactionId;

	UPROPERTY()
	FGuid OperationId;

	UPROPERTY()
	FGuid TransactionEndpointId;

	UPROPERTY()
	uint8 TransactionUpdateIndex = 0;

	UPROPERTY()
	int32 VersionIndex = INDEX_NONE;

	UPROPERTY()
	TArray<FName> ModifiedPackages;

	UPROPERTY()
	FConcertObjectId PrimaryObjectId;

	UPROPERTY()
	TArray<FConcertExportedObject> ExportedObjects;
};

USTRUCT()
struct FConcertTransactionFinalizedEvent : public FConcertTransactionEventBase
{
	GENERATED_BODY()

	UPROPERTY()
	FConcertLocalIdentifierState LocalIdentifierState;

	UPROPERTY()
	FText Title;
};

USTRUCT()
struct FConcertTransactionSnapshotEvent : public FConcertTransactionEventBase
{
	GENERATED_BODY()
};

USTRUCT()
struct FConcertTransactionRejectedEvent
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid TransactionId;
};
