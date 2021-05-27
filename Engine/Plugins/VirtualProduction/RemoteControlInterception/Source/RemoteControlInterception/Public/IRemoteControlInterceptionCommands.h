// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FRCIObjectMetadata;
struct FRCIFunctionMetadata;
struct FRCIPropertiesMetadata;

/**
 * Interception flags that define how RemoteControl should behave after a message was intercepted.
 */
enum class ERCIResponse : uint8
{
	// Set/reset property on RemoteControl side even though the message was intercepted
	Apply,
	// Do not process the RC command on RemoteControl side. An interceptor will decide what to do with it.
	Intercept,
};

/**
 * Payload serialization type (proxy type for ERCPayloadType to avoid RC module dependency)
 */
enum class ERCIPayloadType : uint8
{
	Cbor,
	Json,
};

/**
 * Remote property access mode  (proxy type for ERCAccess to avoid RC module dependency)
 */
enum class ERCIAccess : uint8
{
	NO_ACCESS,
	READ_ACCESS,
	WRITE_ACCESS,
	WRITE_TRANSACTION_ACCESS,
};


/**
 * The list of remote control commands available for interception
 */
template <class TResponseType>
class IRemoteControlInterceptionCommands
{
public:
	virtual ~IRemoteControlInterceptionCommands() = default;

public:
	/**
	 * SetObjectProperty command to process
	 *
	 * @param InObjectProperties - Metadata of the object and its properties that are going to be modified
	 *
	 * @return - Return types depends on the interface implementation (template parameter)
	 */
	virtual TResponseType SetObjectProperties(FRCIPropertiesMetadata& InObjectProperties) = 0;

	/**
	 * ResetObject command to process
	 *
	 * @param InObject - Metadata of the object and its properties that are going to be modified
	 *
	 * @return - Return types depends on the interface implementation (template parameter)
	 * @param OutResponse - interception response flag (not required for the handlers)
	 */
	virtual TResponseType ResetObjectProperties(FRCIObjectMetadata& InObject) = 0;
	
	/**
	 * InvokeCall command to process
	 *
	 * @param InFunction - Metadata of the UFunction that are going to be modified
	 *
	 * @return - Return types depends on the interface implementation (template parameter)
	 */
	virtual TResponseType InvokeCall(FRCIFunctionMetadata& InFunction) = 0;
};


/**
 * Object metadata for custom interception/replication/processing purposes
 */
struct FRCIObjectMetadata
{
public:
	FRCIObjectMetadata()
	{ }

	FRCIObjectMetadata(const FString& InObjectPath, const FString& InPropertyPath, const FString& InPropertyPathInfo, const ERCIAccess InAccess)
		: ObjectPath(InObjectPath)
		, PropertyPath(InPropertyPath)
		, PropertyPathInfo(InPropertyPathInfo)
		, Access(InAccess)
	{
		UniquePath = *FString::Printf(TEXT("%s:%s"), *ObjectPath, *PropertyPath);
	}

	virtual ~FRCIObjectMetadata() = default;

public:
	/** Get Unique Path for Object metadata */
	const FName& GetUniquePath() const
	{ return UniquePath; }
	
	/** Returns true if reference is valid */
	virtual bool IsValid() const
	{
		return !ObjectPath.IsEmpty() && !PropertyPath.IsEmpty() && !PropertyPathInfo.IsEmpty();
	}

	/**
	 * FRCObjectMetadata serialization
	 *
	 * @param Ar       - The archive
	 * @param Interception - Instance to serialize/deserialize
	 *
	 * @return FArchive instance.
	 */
	friend FArchive& operator<<(FArchive& Ar, FRCIObjectMetadata& Interception)
	{
		Ar << Interception.ObjectPath;
		Ar << Interception.PropertyPath;
		Ar << Interception.PropertyPathInfo;
		Ar << Interception.Access;
		return Ar;
	}

public:
	/** Owner object path */
	FString ObjectPath;
	
	/** FProperty path */
	FString PropertyPath;

	/** Full property path, including path to inner structs, arrays, maps, etc */
	FString PropertyPathInfo;

	/** Property access type */
	ERCIAccess Access = ERCIAccess::NO_ACCESS;

public:
	/** Structure Name */
	static constexpr TCHAR const* Name = TEXT("RCIObjectMetadata");
	
private:
	/** Object Path + Property Path */
    FName UniquePath;
};


/**
 * Object properties metadata for custom interception/replication/processing purposes
 */
struct FRCIPropertiesMetadata : public FRCIObjectMetadata
{
public:
	FRCIPropertiesMetadata() = default;

	FRCIPropertiesMetadata(const FString& InObjectPath, const FString& InPropertyPath, const FString& InPropertyPathInfo, const ERCIAccess InAccess, const ERCIPayloadType InPayloadType, const TArray<uint8>& InPayload)
		: FRCIObjectMetadata(InObjectPath, InPropertyPath, InPropertyPathInfo, InAccess)
		, PayloadType(InPayloadType)
		, Payload(InPayload)
	{ }

	virtual ~FRCIPropertiesMetadata() = default;

public:
	//~ Begin FRCObjectMetadata
	virtual bool IsValid() const override
	{
		return FRCIObjectMetadata::IsValid() && Payload.Num() > 0;
	}
	//~ End FRCObjectMetadata

	/**
	 * FRCObjectPropertiesMetadata serialization
	 *
	 * @param Ar       - The archive
	 * @param Instance - Instance to serialize/deserialize
	 *
	 * @return FArchive instance.
	 */
	friend FArchive& operator<<(FArchive& Ar, FRCIPropertiesMetadata& Instance)
	{
		// Call archive from the parent class
		FRCIObjectMetadata& Super = static_cast<FRCIObjectMetadata&>(Instance);
		Ar << Super;
		
		Ar << Instance.PayloadType;
		// const cast is needed for keeping the const& on a constructor and as a class parameter
		Ar << const_cast<TArray<uint8>&>(Instance.Payload);
		return Ar;
	}

public:
	/** Intercepted payload type */
	ERCIPayloadType PayloadType = ERCIPayloadType::Json;

	/** Property payload to intercept */
	const TArray<uint8> Payload;

public:
	/** Structure Name */
	static constexpr TCHAR const* Name = TEXT("RCIPropertiesMetadata");
};

/**
* UFunction metadata for custom interception/replication/processing purposes
*/
struct FRCIFunctionMetadata
{
public:
	FRCIFunctionMetadata()
		: bGenerateTransaction(false)
		, PayloadType(ERCIPayloadType::Json)
	{ }

	FRCIFunctionMetadata(const FString& InObjectPath, const FString& InFunctionPath, const bool bInGenerateTransaction, const ERCIPayloadType InPayloadType, const TArray<uint8>& InPayload)
		: ObjectPath(InObjectPath)
		, FunctionPath(InFunctionPath)
		, bGenerateTransaction(bInGenerateTransaction)
		, PayloadType(InPayloadType)
		, Payload(InPayload)
	{
		UniquePath = *FString::Printf(TEXT("%s:%s"), *ObjectPath, *FunctionPath);
	}

public:
	/** Get Unique Path for Function metadata */
	const FName& GetUniquePath() const
	{ return UniquePath; }

	/** Returns true if reference is valid */
	bool IsValid() const
	{
		return !ObjectPath.IsEmpty() && !FunctionPath.IsEmpty() && Payload.Num() > 0;
	}

	/**
	* FRCObjectMetadata serialization
	*
	* @param Ar				- The archive
	* @param Interception	- Instance to serialize/deserialize
	*
	* @return FArchive instance.
	*/
	friend FArchive& operator<<(FArchive& Ar, FRCIFunctionMetadata& Interception)
	{
		Ar << Interception.ObjectPath;
		Ar << Interception.FunctionPath;
		Ar << Interception.bGenerateTransaction;
		Ar << Interception.PayloadType;
		// const cast is needed for keeping the const& on a constructor and as a class parameter
		Ar << const_cast<TArray<uint8>&>(Interception.Payload);
		return Ar;
	}

public:
	/** Owner object path */
	FString ObjectPath;

	/** UFunction object path */
	FString FunctionPath;

	/** Should the call generate transaction */
	bool bGenerateTransaction;

	/** Intercepted payload type */
	ERCIPayloadType PayloadType;

	/** Property payload to intercept */
	const TArray<uint8> Payload;

public:
	/** Structure Name */
	static constexpr TCHAR const* Name = TEXT("RCIFunctionMetadata");

private:
	/** Object Path + Function Path */
	FName UniquePath;
};
