// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXProtocolTypes.h"
#include "DMXTypes.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntityFixtureType.h"
#include "Subsystems/EngineSubsystem.h"
#include "DMXSubsystem.generated.h"

class UDMXLibrary;
class IDMXProtocol;
class UDMXEntityFixturePatch;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProtocolReceivedDelegate, FDMXProtocolName, Protocol, int32, Universe, const TArray<uint8>&, DMXBuffer);

/**
 * UDMXSubsystem
 * Collections of DMX context blueprint subsystem functions and internal functions for DMX K2Nodes
 */
UCLASS()
class DMXRUNTIME_API UDMXSubsystem 
	: public UEngineSubsystem
{
	GENERATED_BODY()
	
public:
	/**  Send DMX using function names and integer values. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void SendDMX(FDMXProtocolName SelectedProtocol, UDMXEntityFixturePatch* FixturePatch, TMap<FName, int32> FunctionMap, EDMXSendResult& OutResult);

	/**  Send DMX using channel and value raw values. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void SendDMXRaw(FDMXProtocolName SelectedProtocol, int32 UniverseIndex, TMap<int32, uint8> ChannelValuesMap, EDMXSendResult& OutResult);

	/**  Return reference to array of Fixture Patch objects of a given type. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (AutoCreateRefTerm = "FixtureType"))
	void GetAllFixturesOfType(const FDMXEntityFixtureTypeRef& FixtureType, TArray<UDMXEntityFixturePatch*>& OutResult);
	
	/** Load all referenced Objects in the given library. The DMX library should be Loaded at least one in any part of the Unreal Engine */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DisplayName = "Load DMX Library"))
	void LoadDMXLibrary(UDMXLibrary* DMXLibrary) {} // It could be empty. It automatically pre-loads all Objects if we have a reference in the blueprint.

	/**  Return reference to array of Fixture Patch objects of a given category. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void GetAllFixturesOfCategory(const UDMXLibrary* DMXLibrary, FDMXFixtureCategory Category, TArray<UDMXEntityFixturePatch*>& OutResult);

	/**  Return reference to array of Fixture Patch objects in a given universe. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void GetAllFixturesInUniverse(const UDMXLibrary* DMXLibrary, int32 UniverseId, TArray<UDMXEntityFixturePatch*>& OutResult);

	/**  Return reference to array of Fixture Patch objects in a given controller. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void GetAllUniversesInController(const UDMXLibrary* DMXLibrary, FString ControllerName, TArray<int32>& OutResult);

	/**  Return byte array from the DMX buffer given a universe. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void GetRawBuffer(FDMXProtocolName SelectedProtocol, int32 UniverseIndex, TArray<uint8>& DMXBuffer);

	/**  Return map with all DMX functions and their associated values given DMX buffer and desired universe. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void GetFixtureFunctions(const UDMXEntityFixturePatch* InFixturePatch, const TArray<uint8>& DMXBuffer, TMap<FName, int32>& OutResult);

	/**  Return reference to array of Fixture Patch objects with a given tag. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	TArray<UDMXEntityFixturePatch*> GetAllFixturesWithTag(const UDMXLibrary* DMXLibrary, FName CustomTag);

	/**  Return reference to array of Fixture Patch objects in library. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	TArray<UDMXEntityFixturePatch*> GetAllFixturesInLibrary(const UDMXLibrary* DMXLibrary);

	/**  Return reference to Fixture Patch object with a given name. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (AutoCreateRefTerm="Name"))
	UDMXEntityFixturePatch* GetFixtureByName(const UDMXLibrary* DMXLibrary, const FString& Name);

	/**  Return reference to array of Fixture Types objects in library. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	TArray<UDMXEntityFixtureType*> GetAllFixtureTypesInLibrary(const UDMXLibrary* DMXLibrary);

	/**  Return reference to Fixture Type object with a given name. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (AutoCreateRefTerm = "Name"))
	UDMXEntityFixtureType* GetFixtureTypeByName(const UDMXLibrary* DMXLibrary, const FString& Name);

	/**  Return reference to array of Controller objects in library. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	TArray<UDMXEntityController*> GetAllControllersInLibrary(const UDMXLibrary* DMXLibrary);

	/**  Return reference to Controller object with a given name. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (AutoCreateRefTerm = "Name"))
	UDMXEntityController* GetControllerByName(const UDMXLibrary* DMXLibrary, const FString& Name);

	/**  Return reference to array of DMX Library objects. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	TArray<UDMXLibrary*> GetAllDMXLibraries();

	/**
	 * Return integer given an array of bytes. Up to the first 4 bytes in the array will be used for the conversion.
	 * @param bUseLSB	Least Significant Byte mode makes the individual bytes (channels) of the function be
	 *					interpreted with the first bytes being the lowest part of the number.
	 *					Most Fixtures use MSB (Most Significant Byte).
	 */
	UFUNCTION(BlueprintPure, Category = "DMX")
	int32 BytesToInt(const TArray<uint8>& Bytes, bool bUseLSB = false) const;

	/**
	 * Return normalized value given an array of bytes. Up to the first 4 bytes in the array will be used for the conversion.
	 * @param bUseLSB	Least Significant Byte mode makes the individual bytes (channels) of the function be
	 *					interpreted with the first bytes being the lowest part of the number.
	 *					Most Fixtures use MSB (Most Significant Byte).
	 */
	UFUNCTION(BlueprintPure, Category = "DMX", meta = (Keywords = "float"))
	float BytesToNormalizedValue(const TArray<uint8>& Bytes, bool bUseLSB = false) const;

	/**
	 * Return the Bytes format of Value in the desired Signal Format.
	 * @param bUseLSB	Least Significant Byte mode makes the individual bytes (channels) of the function be
	 *					interpreted with the first bytes being the lowest part of the number.
	 *					Most Fixtures use MSB (Most Significant Byte).
	 */
	UFUNCTION(BlueprintPure, Category = "DMX", meta = (Keywords = "float"))
	void NormalizedValueToBytes(float InValue, EDMXFixtureSignalFormat InSignalFormat, TArray<uint8>& Bytes, bool bUseLSB = false) const;

	/**
	 * Return the Bytes format of Value in the desired Signal Format.
	 * @param bUseLSB	Least Significant Byte mode makes the individual bytes (channels) of the function be
	 *					interpreted with the first bytes being the lowest part of the number.
	 *					Most Fixtures use MSB (Most Significant Byte).
	 */
	UFUNCTION(BlueprintPure, Category = "DMX")
	void IntValueToBytes(int32 InValue, EDMXFixtureSignalFormat InSignalFormat, TArray<uint8>& Bytes, bool bUseLSB = false) const;

	/**
	 * Return the normalized value of an Int value from the specified Signal Format.
	 * @param bUseLSB	Least Significant Byte mode makes the individual bytes (channels) of the function be
	 *					interpreted with the first bytes being the lowest part of the number.
	 *					Most Fixtures use MSB (Most Significant Byte).
	 */
	UFUNCTION(BlueprintPure, Category = "DMX", meta = (Keywords = "float"))
	float IntToNormalizedValue(int32 InValue, EDMXFixtureSignalFormat InSignalFormat) const;

	/**
	 * Return the normalized value of an Int value from a Fixture Patch function.
	 * @return	The normalized value of the passed in Int using the Function's signal format.
	 *			-1.0 if the Function is not found in the Fixture Patch.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX", meta = (Keywords = "float"))
	float GetNormalizedFunctionValue(UDMXEntityFixturePatch* InFixturePatch, FName InFunctionName, int32 InValue) const;

	/**
	 * Creates a literal UDMXEntityFixturePatch reference
	 * @param	InFixturePatch	pointer to set the UDMXEntityFixturePatch to
	 * @return	The literal UDMXEntityFixturePatch
	 */
	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = "TRUE"), Category = "DMX")
	UDMXEntityFixturePatch* GetFixturePatch(FDMXEntityFixturePatchRef InFixturePatch);

	/**
	 * Gets a function map based on you active mode from FixturePatch
	 * @param	InFixturePatch Selected Patch
	 * @param	FDMXProtocolName Selected DMX protocol
	 * @param	OutFunctionsMap Function and Channel value output map
	 * @return	True if outputting was successfully
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE", AutoCreateRefTerm = "SelectedProtocol"), Category = "DMX")
	bool GetFunctionsMap(UDMXEntityFixturePatch* InFixturePatch, const FDMXProtocolName& SelectedProtocol, TMap<FName, int32>& OutFunctionsMap);

	/**
	 * Gets function channel value by input function name
	 * @param	InName Looking fixture function name
	 * @param	InFunctionsMap Function and Channel value input map
	 * @return	Function channel value
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE", AutoCreateRefTerm = "InName, InFunctionsMap"), Category = "DMX")
	int32 GetFunctionsValue(const FName& InName, const TMap<FName, int32>& InFunctionsMap);

	/** Get a DMX Subsystem, pure version */
	UFUNCTION(BlueprintPure, Category = "DMX Subsystem", meta = (BlueprintInternalUseOnly = "true"))
	static UDMXSubsystem* GetDMXSubsystem_Pure();

	/** Get a DMX Subsystem, callable version */
	UFUNCTION(BlueprintCallable, Category = "DMX Subsystem", meta = (BlueprintInternalUseOnly = "true"))
	static UDMXSubsystem* GetDMXSubsystem_Callable();

	UPROPERTY(BlueprintAssignable, Category = "DMX")
	FProtocolReceivedDelegate OnProtocolReceived;

public:
	//~ USubsystem interface begin
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ USubsystem interface end

private:
	/**
	 * Stores DelegateHandles for each Protocol's UniverseInputUpdate event.
	 * That way we can unbind them when this subsystem is being destroyed and prevent crashes.
	 */
	TMap<FName, FDelegateHandle> UniverseInputUpdateHandles;
};
