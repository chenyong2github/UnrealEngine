// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "Misc/Guid.h"

#include "DMXInputPortConfig.generated.h"

class FDMXPort;

struct FGuid;


/** 
 * Blueprint Configuration of a Port, used in DXM Settings to specify inputs and outputs.
 *
 * Property changes are handled in details customization consistently.
 */
USTRUCT(BlueprintType)
struct DMXPROTOCOL_API FDMXInputPortConfig
{
	GENERATED_BODY()

public:
	FDMXInputPortConfig();

	/** Initializes properties that need initializataion. Returns the Port's Guid */
	FGuid Initialize();

	/** Returns true if the port is initialized */
	bool IsInitialized() const;

	/** The name displayed wherever the port can be displayed */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	FString PortName;

	/** DMX Protocol */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	FName ProtocolName;

	/** The type of communication used with this port */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	EDMXCommunicationType CommunicationType;

	/** The Network Interface Card's IP Adress, over which DMX is received */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config", Meta = (DisplayName = "Network Interface Card IP Address"))
	FString Address; // Invariant of networking, may be a USB device in the future 

	/** Local Start Universe */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	int32 LocalUniverseStart;

	/** Number of Universes */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config", Meta = (DisplayName = "Amount of Universes"))
	int32 NumUniverses;

	/** 
	 * The start address this being transposed to. 
	 * E.g. if LocalUniverseStart is 1 and this is 100, Local Universe 1 is sent/received as Universe 100.
	 */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	int32 ExternUniverseStart;

	/** Returns the port Guid. Should not be called before a port guid was assigned by the port, i.e. GetPort().IsValid() */
	const FGuid& GetPortGuid() const;

	/** Expose the protected PortGuid property name */
	static FName GetPortGuidPropertyName() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, PortGuid); }

protected:
	/** Sets a valid port name if the name is empty */
	void SanetizePortName();

	/** Sets a valid protocol name if the name is invalid */
	void SanetizeProtocolName();

	/** Sets a valid communication type if the default one is not supported */
	void SanetizeCommunicationType();
	
	/** 
	 * Unique identifier, shared with the port instance.
	 * Note: This needs be BlueprintReadWrite to be accessible to property type customization, but is hidden by customization.
	 */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config Guid")
	FGuid PortGuid;
};
