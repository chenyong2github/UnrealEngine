// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "IRemoteControlModule.h"

/**
 * Object replication references
 */
struct FRCObjectReplication
{
	virtual ~FRCObjectReplication() {}

	FRCObjectReplication() {}

	FRCObjectReplication(const FString& InObjectPath, const FString& InPropertyPath, const FString& InPropertyPathInfo, const ERCAccess InAccess)
		: ObjectPath(InObjectPath)
		, PropertyPath(InPropertyPath)
		, PropertyPathInfo(InPropertyPathInfo)
		, Access(InAccess)
	{}

	/** Returns true if reference is valid */
	virtual bool IsValid() const
	{
		return !ObjectPath.IsEmpty() && !PropertyPath.IsEmpty() && !PropertyPathInfo.IsEmpty();
	}

	/** Owner object path */
	FString ObjectPath;
	
	/** FProperty path */
	FString PropertyPath;

	/** Full property path, including path to inner structs, arrays, maps, etc*/
	FString PropertyPathInfo;

	/** Property access type */
	ERCAccess Access = ERCAccess::NO_ACCESS;

	/**
	 * Serializes an FRCObjectReplication value from or into this archive.
	 *
	 * @param Value The value to serialize.
	 * @return FArchive instance.
	 */
	friend FArchive& operator<<(FArchive& Ar, FRCObjectReplication& Replication)
	{
		Ar << Replication.ObjectPath;
		Ar << Replication.PropertyPath;
		Ar << Replication.PropertyPathInfo;
		Ar << Replication.Access;
		return Ar;
	}
};

/**
 * Set Object Properties replication references
 */
struct FRCSetObjectPropertiesReplication : public FRCObjectReplication
{
	FRCSetObjectPropertiesReplication(const TArray<uint8>& InPayload)
		: Payload(InPayload)
	{}

	FRCSetObjectPropertiesReplication(const FString& InObjectPath, const FString& InPropertyPath, const FString& InPropertyPathInfo, const ERCAccess InAccess, const ERCPayloadType InPayloadType, const TArray<uint8>& InPayload)
		: FRCObjectReplication(InObjectPath, InPropertyPath, InPropertyPathInfo, InAccess)
		, PayloadType(InPayloadType)
		, Payload(InPayload)
	{}

	FRCSetObjectPropertiesReplication() = delete;

	//~ Begin FRCObjectReplication Interface
	virtual bool IsValid() const override
	{
		return FRCObjectReplication::IsValid() && PayloadType != ERCPayloadType::Json && Payload.Num() > 0;
	}
	//~ End FRCObjectReplication Interface

	/**
	 * Serializes an FRCSetObjectPropertiesReplication value from or into this archive.
	 *
	 * @param Value The value to serialize.
	 * @return FArchive instance.
	 */
	friend FArchive& operator<<(FArchive& Ar, FRCSetObjectPropertiesReplication& Replication)
	{
		// Call achive from the parent class
		FRCObjectReplication& Super = static_cast<FRCObjectReplication&>(Replication);
		Ar << Super;
		
		Ar << Replication.PayloadType;
		// const cast is needed for keeping the const& on a constractor and as a class parameter
		Ar << const_cast<TArray<uint8>&>(Replication.Payload);
		return Ar;
	}

	/** Replicated payload type */
	ERCPayloadType PayloadType = ERCPayloadType::Json;

	/** Property payload to replicate */
	const TArray<uint8>& Payload;
};

/**
 * Flags for replication
 */
enum ERemoteControlReplicatorFlag
{
	RCRF_NoFlags			= 0x00000000, // Any action should be taken after this flag
	RCRF_Apply				= 0x00000001, // Set/reset property before replication
	RCRF_Intercept			= 0x00000002, // Do not Set/reset property before replication
};

/**
 * Interface for replicate remote control data
 */
class IRemoteControlReplicator : public TSharedFromThis<IRemoteControlReplicator>
{
public:
	/** Virtual destructor */
	virtual ~IRemoteControlReplicator() {}

	/**
	 * Register Set Object Property Intercept
	 * @param InReplicatorReference the reference holds a replication reference and payload
	 * @return the replication flag
	 */
	virtual ERemoteControlReplicatorFlag InterceptSetObjectProperties(FRCSetObjectPropertiesReplication& InReplicatorReference) = 0;

	/**
	 * Register Reset Object Property Intercept
	 * @param InReplicatorReference the reference holds a replication reference
	 * @return the replication flag
	 */
	virtual ERemoteControlReplicatorFlag InterceptResetObjectProperties(FRCObjectReplication& InReplicatorReference) = 0;

	/** Get unique name of the replicator */
	virtual FName GetName() const = 0;
 
public:
	/** Compare if the RHS has LHS replication flag */
	static bool HasAnyFlags(const ERemoteControlReplicatorFlag InLHSFlag, const ERemoteControlReplicatorFlag InRHSFlag)
	{
		return (InLHSFlag & InRHSFlag) != 0;
	}
};
