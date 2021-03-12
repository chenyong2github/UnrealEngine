// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"

#include "DMXInputPortReference.generated.h"


/** Reference of an input port */
USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXInputPortReference
{
	GENERATED_BODY()

	FDMXInputPortReference()
	{}

	FDMXInputPortReference(const FGuid& InPortGuid)
		: PortGuid(InPortGuid)
	{}

	friend FArchive& operator<<(FArchive& Ar, FDMXInputPortReference& InputPortReference)
	{
		Ar << InputPortReference.PortGuid;

		return Ar;
	}

	FORCEINLINE bool operator==(const FDMXInputPortReference& Other) const
	{
		return PortGuid == Other.PortGuid;
	}

	FORCEINLINE bool operator!=(const FDMXInputPortReference& Other) const
	{
		return !(*this == Other);
	}

	FORCEINLINE_DEBUGGABLE friend uint32 GetTypeHash(const FDMXInputPortReference& PortReference)
	{
		return GetTypeHash(PortReference.PortGuid);
	}

	const FGuid& GetPortGuid() const { return PortGuid; }

	/** Returns a non const Guid of the port, required for serialization only */
	FGuid& GetMutablePortGuid() { return PortGuid; }

	static FName GetPortGuidPropertyName() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortReference, PortGuid); }

protected:
	/**
	 * Unique identifier shared with port config and port instance.
	 * Note: This needs be BlueprintReadWrite to be accessible to property type customization, but is hidden by customization.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Port Config Guid")
	FGuid PortGuid;
};
