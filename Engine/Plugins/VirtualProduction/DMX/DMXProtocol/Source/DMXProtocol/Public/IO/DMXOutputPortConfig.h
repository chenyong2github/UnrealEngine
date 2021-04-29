// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "Misc/Guid.h"

#include "DMXOutputPortConfig.generated.h"

class FDMXPort;

struct FGuid;


/** 
 * Blueprint Configuration of a Port, used in DXM Settings to specify inputs and outputs.
 *
 * Property changes are handled in details customization consistently.
 */
USTRUCT(BlueprintType)
struct DMXPROTOCOL_API FDMXOutputPortConfig
{
	GENERATED_BODY()

public:
	FDMXOutputPortConfig() = default;

	/** Constructs a default config with a Guid */
	explicit FDMXOutputPortConfig(const FGuid& InPortGuid);

	/** The name displayed wherever the port can be displayed */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	FString PortName;

	/** DMX Protocol */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	FName ProtocolName;

	/** The type of communication used with this port */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	EDMXCommunicationType CommunicationType;

	/** The IP address of the network interface card over which outbound DMX is sent */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config", Meta = (DisplayName = "Network Interface Card IP Address"))
	FString DeviceAddress;

	/** For Unicast, the IP address outbound DMX is sent to */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config", Meta = (DisplayName = "Destination IP Address"))
	FString DestinationAddress; 

	/** If true, the signals of output to this port is input into to the engine */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config", Meta = (DisplayName = "Input into Engine"))
	bool bLoopbackToEngine;

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

	/** Priority on which packets are being sent */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config")
	int32 Priority;

	/** Returns the port Guid. Should not be called before a port guid was assigned by the port, i.e. GetPort().IsValid() */
	const FGuid& GetPortGuid() const;

	/** Expose the protected PortGuid property name */
	static FName GetPortGuidPropertyName() { return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, PortGuid); }

protected:
	/** Sets a valid port name if the name is empty */
	void SanetizePortName();

	/** Sets a valid protocol name if the name is invalid */
	void SanetizeProtocolName();
	
	/** Sets a valid communication type if the default one is not supported. If the communication type is mended, also sanetizes bLoopbackToEngine. */
	void SanetizeCommunicationType();
	/** 
	 * Unique identifier, shared with the port instance.
	 * Note: This needs be BlueprintReadWrite to be accessible to property type customization, but is hidden by customization.
	 */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config Guid", meta = (IgnoreForMemberInitializationTest))
	FGuid PortGuid;
};
