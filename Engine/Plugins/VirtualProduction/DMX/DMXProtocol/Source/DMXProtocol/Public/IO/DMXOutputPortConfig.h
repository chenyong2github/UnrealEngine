// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "Misc/Guid.h"

#include "DMXOutputPortConfig.generated.h"

struct FDMXOutputPortConfig;
class FDMXPort;

struct FGuid;


/** Data to create a new output port config with related constructor */
struct DMXPROTOCOL_API FDMXOutputPortConfigParams
{
	FDMXOutputPortConfigParams() = default;
	FDMXOutputPortConfigParams(const FDMXOutputPortConfig& OutputPortConfig);

	FString PortName;
	FName ProtocolName;
	EDMXCommunicationType CommunicationType;
	FString DeviceAddress;
	FString DestinationAddress;
	bool bLoopbackToEngine;
	int32 LocalUniverseStart;
	int32 NumUniverses;
	int32 ExternUniverseStart;
	int32 Priority;
};

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
	/** Default constructor, only for CDOs */
	FDMXOutputPortConfig() = default;

	/** Constructs a config from the guid */
	explicit FDMXOutputPortConfig(const FGuid & InPortGuid);

	/** Constructs a config from the guid and given initialization data */
	FDMXOutputPortConfig(const FGuid & InPortGuid, const FDMXOutputPortConfigParams& InitializationData);

	/** Changes members to result in a valid config */
	void MakeValid();

	FORCEINLINE const FString& GetPortName() const { return PortName; }
	FORCEINLINE const FName& GetProtocolName() const { return ProtocolName; }
	FORCEINLINE EDMXCommunicationType GetCommunicationType() const { return CommunicationType; }
	FORCEINLINE const FString& GetDeviceAddress() const { return DeviceAddress; }
	FORCEINLINE const FString& GetDestinationAddress() const { return DestinationAddress; }
	FORCEINLINE bool NeedsLoopbackToEngine() const { return bLoopbackToEngine; }
	FORCEINLINE int32 GetLocalUniverseStart() const { return LocalUniverseStart; }
	FORCEINLINE int32 GetNumUniverses() const { return NumUniverses; }
	FORCEINLINE int32 GetExternUniverseStart() const { return ExternUniverseStart; }
	FORCEINLINE int32 GetPriority() const { return Priority; }
	FORCEINLINE const FGuid& GetPortGuid() const { return PortGuid; }

#if WITH_EDITOR
	static FName GetProtocolNamePropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, ProtocolName); }
	static FName GetCommunicationTypePropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, CommunicationType); }
	static FName GetDeviceAddressPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, DeviceAddress); }
	static FName GetDestinationAddressPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, DestinationAddress); }
	static FName GetPortGuidPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, PortGuid); }
#endif // WITH_EDITOR

protected:
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

protected:
	/** Generates a unique port name (unique for those stored in project settings) */
	void GenerateUniquePortName();

	/** 
	 * Unique identifier, shared with the port instance.
	 * Note: This needs be BlueprintReadWrite to be accessible to property type customization, but is hidden by customization.
	 */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config Guid", meta = (IgnoreForMemberInitializationTest))
	FGuid PortGuid;
};
