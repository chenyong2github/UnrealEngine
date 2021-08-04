// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolModule.h"
#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "Misc/Guid.h"

#include "DMXInputPortConfig.generated.h"

struct FDMXInputPortConfig;
class FDMXPort;

struct FGuid;

/**
 * Strategy for priority system (when receiving packets)
 * 
 * Not: Not all protocols have a use for this
*/
UENUM()
enum class EDMXPortPriorityStrategy : uint8
{
	/** Always manage the packet */
	None,
	/** Manage the packet only if the priority is equal to the specified value */
	Equal,
	/** Manage the packet only if the priority is higher than the specified value */
	HigherThan,
	/** Manage the packet only if the priority is lower than the specified value */
	LowerThan,
	/** Manage the packet only if it matches the highest received priority */
	Highest,
	/** Manage the packet only if it matches the lowest received priority */
	Lowest
};

/** Data to create a new input port config with related constructor. */
struct DMXPROTOCOL_API FDMXInputPortConfigParams
{
	FDMXInputPortConfigParams() = default;
	FDMXInputPortConfigParams(const FDMXInputPortConfig& InputPortConfig);

	FString PortName;
	FName ProtocolName;
	EDMXCommunicationType CommunicationType;
	FString DeviceAddress; 
	int32 LocalUniverseStart;
	int32 NumUniverses;
	int32 ExternUniverseStart;
};

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
	/** Default constructor, only for Default Objects */
	FDMXInputPortConfig();

	/** Constructs a config from the guid */
	explicit FDMXInputPortConfig(const FGuid& InPortGuid);

	/** Constructs a config from the guid and given initialization data */
	FDMXInputPortConfig(const FGuid& InPortGuid, const FDMXInputPortConfigParams& InitializationData);

	/** Changes members to result in a valid config */
	void MakeValid();

	FORCEINLINE const FString& GetPortName() const { return PortName; }
	FORCEINLINE const FName& GetProtocolName() const { return ProtocolName; }
	FORCEINLINE EDMXCommunicationType GetCommunicationType() const { return CommunicationType; }
	FString GetDeviceAddress() const;
	FORCEINLINE int32 GetLocalUniverseStart() const { return LocalUniverseStart; }
	FORCEINLINE int32 GetNumUniverses() const { return NumUniverses; }
	FORCEINLINE int32 GetExternUniverseStart() const { return ExternUniverseStart; }
	FORCEINLINE const FGuid& GetPortGuid() const { return PortGuid; }
	FORCEINLINE const EDMXPortPriorityStrategy GetPortPriorityStrategy() const { return PriorityStrategy; }
	FORCEINLINE const int32 GetPriority() const { return Priority; }

#if WITH_EDITOR
	static FName GetProtocolNamePropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, ProtocolName); }
	static FName GetCommunicationTypePropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, CommunicationType); }
	static FName GetDeviceAddressPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, DeviceAddress); }
	static FName GetPriorityStrategyPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, PriorityStrategy); }
	static FName GetPriorityPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, Priority); }
	static FName GetPortGuidPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, PortGuid); }
#endif // WITH_EDITOR

protected:
	/** The name displayed wherever the port can be displayed */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config")
	FString PortName;

	/** DMX Protocol */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config")
	FName ProtocolName = FDMXProtocolModule::DefaultProtocolArtNetName;

	/** The type of communication used with this port */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config")
	EDMXCommunicationType CommunicationType = EDMXCommunicationType::InternalOnly;

	/** The Network Interface Card's IP Adress, over which DMX is received */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config", Meta = (DisplayName = "Network Interface Card IP Address"))
	FString DeviceAddress = TEXT("127.0.0.1");

	/** Local Start Universe */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config")
	int32 LocalUniverseStart = 1;

	/** Number of Universes */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config", Meta = (DisplayName = "Amount of Universes"))
	int32 NumUniverses = 10;

	/** 
	 * The start address this being transposed to. 
	 * E.g. if LocalUniverseStart is 1 and this is 100, Local Universe 1 is sent/received as Universe 100.
	 */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config")
	int32 ExternUniverseStart = 1;

	/** How to deal with the priority value */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config")
	EDMXPortPriorityStrategy PriorityStrategy = EDMXPortPriorityStrategy::None;

	/** Priority value, can act as a filter or a threshold */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config")
	int32 Priority = 0;

protected:
	/** Generates a unique port name (unique for those stored in project settings) */
	void GenerateUniquePortName();
	
	/** 
	 * Unique identifier, shared with the port instance.
	 * Note: This needs be BlueprintReadWrite to be accessible to property type customization, but is hidden by customization.
	 */
	UPROPERTY(Config, BlueprintReadOnly, EditDefaultsOnly, Category = "Port Config Guid", meta = (IgnoreForMemberInitializationTest))
	FGuid PortGuid;
};
