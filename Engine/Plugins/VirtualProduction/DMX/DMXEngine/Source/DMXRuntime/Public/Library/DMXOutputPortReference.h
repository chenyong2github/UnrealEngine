// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"

#include "DMXOutputPortReference.generated.h"


/** Reference of an input port */
USTRUCT(BlueprintType)
struct DMXRUNTIME_API FDMXOutputPortReference
{
	GENERATED_BODY()

	FDMXOutputPortReference()
	{}

	FDMXOutputPortReference(const FGuid& InPortGuid)
		: PortGuid(InPortGuid)
	{}

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

protected:
	/**
	 * Unique identifier shared with port config and port instance.
	 * Note: This needs be BlueprintReadWrite to be accessible to property type customization, but is hidden by customization.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Port Config Guid")
	FGuid PortGuid;
};
