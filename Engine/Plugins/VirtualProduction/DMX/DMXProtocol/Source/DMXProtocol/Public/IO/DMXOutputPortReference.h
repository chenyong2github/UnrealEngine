// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"

#include "DMXOutputPortReference.generated.h"


/** Reference of an input port */
USTRUCT(BlueprintType)
struct DMXPROTOCOL_API FDMXOutputPortReference
{
	GENERATED_BODY()

	FDMXOutputPortReference()
		: bEnabledFlag(true)
	{}

	FDMXOutputPortReference(const FGuid& InPortGuid, bool bIsEnabledFlag)
		: PortGuid(InPortGuid)
		, bEnabledFlag(bIsEnabledFlag)
	{}

	FDMXOutputPortReference(const FDMXOutputPortReference& InOutputPortReference, bool bIsEnabledFlag)
		: PortGuid(InOutputPortReference.PortGuid)
		, bEnabledFlag(bIsEnabledFlag)
	{}

	/** Returns true if the port is enabled. Always true unless constructed with bIsAlwaysEnabled = false */
	FORCEINLINE bool IsEnabledFlagSet() const { return bEnabledFlag; }

	friend FArchive& operator<<(FArchive& Ar, FDMXOutputPortReference& OutputPortReference)
	{
		Ar << OutputPortReference.PortGuid;

		return Ar;
	}

	FORCEINLINE bool operator==(const FDMXOutputPortReference& Other) const
	{
		return PortGuid == Other.PortGuid;
	}

	FORCEINLINE bool operator!=(const FDMXOutputPortReference& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE_DEBUGGABLE friend uint32 GetTypeHash(const FDMXOutputPortReference& PortReference)
	{
		return GetTypeHash(PortReference.PortGuid);
	}

	const FGuid& GetPortGuid() const { return PortGuid; }

	static FName GetPortGuidPropertyName() { return GET_MEMBER_NAME_CHECKED(FDMXOutputPortReference, PortGuid); }
	static FName GetEnabledFlagPropertyName() { return GET_MEMBER_NAME_CHECKED(FDMXOutputPortReference, bEnabledFlag); }

protected:
	/**
	 * Unique identifier shared with port config and port instance.
	 * Note: This needs be BlueprintReadWrite to be accessible to property type customization, but is hidden by customization.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "DMX")
	FGuid PortGuid;

	/** Optional flag for port references that can be enabled or disabled */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "DMX")
	bool bEnabledFlag;
};
