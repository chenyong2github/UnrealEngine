// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "Misc/Guid.h"

#include "DMXInputPortConfig.generated.h"

struct FDMXInputPortConfig;
class FDMXPort;

struct FGuid;


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
	/** Default constructor, only for CDOs */
	FDMXInputPortConfig() = default;

	/** Constructs a config from the guid */
	explicit FDMXInputPortConfig(const FGuid& InPortGuid);

	/** Constructs a config from the guid and given initialization data */
	FDMXInputPortConfig(const FGuid& InPortGuid, const FDMXInputPortConfigParams& InitializationData);

	/** Changes members to result in a valid config */
	void MakeValid();

	FORCEINLINE const FString& GetPortName() const { return PortName; }
	FORCEINLINE const FName& GetProtocolName() const { return ProtocolName; }
	FORCEINLINE EDMXCommunicationType GetCommunicationType() const { return CommunicationType; }
	FORCEINLINE const FString& GetDeviceAddress() const { return DeviceAddress; }
	FORCEINLINE int32 GetLocalUniverseStart() const { return LocalUniverseStart; }
	FORCEINLINE int32 GetNumUniverses() const { return NumUniverses; }
	FORCEINLINE int32 GetExternUniverseStart() const { return ExternUniverseStart; }
	FORCEINLINE const FGuid& GetPortGuid() const { return PortGuid; }

#if WITH_EDITOR
	static FName GetProtocolNamePropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, ProtocolName); }
	static FName GetCommunicationTypePropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, CommunicationType); }
	static FName GetDeviceAddressPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, DeviceAddress); }
	static FName GetPortGuidPropertyNameChecked() { return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, PortGuid); }
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

	/** The Network Interface Card's IP Adress, over which DMX is received */
	UPROPERTY(Config, BlueprintReadWrite, EditDefaultsOnly, Category = "Port Config", Meta = (DisplayName = "Network Interface Card IP Address"))
	FString DeviceAddress; 

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
