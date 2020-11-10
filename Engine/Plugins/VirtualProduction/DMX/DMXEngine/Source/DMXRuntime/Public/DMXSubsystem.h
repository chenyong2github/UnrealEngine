// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "DMXProtocolTypes.h"
#include "DMXTypes.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntityFixtureType.h"
#include "Subsystems/EngineSubsystem.h"
#include "DMXSubsystem.generated.h"

class UDMXLibrary;
class IDMXProtocol;
class UDMXEntityFixturePatch;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProtocolReceivedDelegate, FDMXProtocolName, Protocol, int32, RemoteUniverse, const TArray<uint8>&, DMXBuffer);

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
	void SendDMX(UDMXEntityFixturePatch* FixturePatch, TMap<FDMXAttributeName, int32> AttributeMap, EDMXSendResult& OutResult);

	/**  Send DMX using channel and raw values. NOTE: Universe Index cannot be lower than 0. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void SendDMXRaw(FDMXProtocolName SelectedProtocol, int32 RemoteUniverse, TMap<int32, uint8> AddressValueMap, EDMXSendResult& OutResult);

	/**  Return reference to array of Fixture Patch objects of a given type. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (AutoCreateRefTerm = "FixtureType"))
	void GetAllFixturesOfType(const FDMXEntityFixtureTypeRef& FixtureType, TArray<UDMXEntityFixturePatch*>& OutResult);
	
	UE_DEPRECATED(4.26, "This function is deprecated. The node can simply be removed. Libraries are now accessible at all times, everywhere, without explicit loading.")
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
	void GetRawBuffer(FDMXProtocolName SelectedProtocol, int32 RemoteUniverse, TArray<uint8>& DMXBuffer);

	/**  Return map with all DMX functions and their associated values given DMX buffer and desired universe. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DisplayName = "Get Fixture Attributes"))
	void GetFixtureAttributes(const UDMXEntityFixturePatch* InFixturePatch, const TArray<uint8>& DMXBuffer, TMap<FDMXAttributeName, int32>& OutResult);

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
	const TArray<UDMXLibrary*>& GetAllDMXLibraries();

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
	static void IntValueToBytes(int32 InValue, EDMXFixtureSignalFormat InSignalFormat, TArray<uint8>& Bytes, bool bUseLSB = false);

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
	UFUNCTION(BlueprintPure, Category = "DMX", meta = (Keywords = "float", DisplayName = "GetNormalizedAttributeValue"))
	float GetNormalizedAttributeValue(UDMXEntityFixturePatch* InFixturePatch, FDMXAttributeName InFunctionAttribute, int32 InValue) const;

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
	 * @param	OutAttributesMap Function and Channel value output map
	 * @return	True if outputting was successfully
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE", AutoCreateRefTerm = "SelectedProtocol"), Category = "DMX")
	bool GetFunctionsMap(UDMXEntityFixturePatch* InFixturePatch, TMap<FDMXAttributeName, int32>& OutAttributesMap);

	/**
	 * Gets a function map based on you active mode from FixturePatch, but instead of passing a Protocol as parameters, it looks for
	 * the first Protocol found in the Patch's universe and use that one
	 * @param	InFixturePatch Selected Patch
	 * @param	OutAttributesMap Function and Channel value output map
	 * @return	True if outputting was successfully
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE", AutoCreateRefTerm = "SelectedProtocol"), Category = "DMX")
	bool GetFunctionsMapForPatch(UDMXEntityFixturePatch* InFixturePatch, TMap<FDMXAttributeName, int32>& OutAttributesMap);

	/**
	 * Gets function channel value by input function name
	 * @param	InName Looking fixture function name
	 * @param	InAttributesMap Function and Channel value input map
	 * @return	Function channel value
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE", AutoCreateRefTerm = "InName, InAttributesMap"), Category = "DMX")
	int32 GetFunctionsValue(const FName FunctionAttributeName, const TMap<FDMXAttributeName, int32>& InAttributesMap);

	/**
	 * Checks if a FixturePatchs is of a given FixtureType
	 * @param	InFixturePatch fixture patch to check
	 * @param	RefTypeValue a FixtureTypeRef to check against the fixture patch type
	 * @return	bool result of checking if the patch is of a given type 
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "TRUE"), Category = "DMX")
	bool PatchIsOfSelectedType(UDMXEntityFixturePatch* InFixturePatch, FString RefTypeValue);

	/** Get a DMX Subsystem, pure version */
	UFUNCTION(BlueprintPure, Category = "DMX Subsystem", meta = (BlueprintInternalUseOnly = "true"))
	static UDMXSubsystem* GetDMXSubsystem_Pure();

	/** Get a DMX Subsystem, callable version */
	UFUNCTION(BlueprintCallable, Category = "DMX Subsystem", meta = (BlueprintInternalUseOnly = "true"))
	static UDMXSubsystem* GetDMXSubsystem_Callable();

	/**
	 * Gets the FName for a FDMXAttributeName, since structs can't have UFUCNTIONS to create a getter
	 * @param	AttributeName the struct we want to grab the name from
	 * @return	FName the name of the AttributeName
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	FName GetAttributeLabel(FDMXAttributeName AttributeName);

	UE_DEPRECATED(4.26, "No longer supported in 4.26. Use DMXComponent's OnFixturePatchReceived event or GetRawBuffer instead.")
	UPROPERTY(BlueprintAssignable, Category = "DMX", meta = (DeprecatedProperty, DeprecationMessage = "WARNING: This can execute faster than tick leading to possible blueprint performance issues. Use DMXComponent's OnFixturePatchReceived event or GetRawBuffer instead."))
	FProtocolReceivedDelegate OnProtocolReceived_DEPRECATED;

	/**  Set DMX Cell value using matrix coordinates. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. DMXEntityFixurePatch::SendMatrixCellValue instead"))
	bool SetMatrixCellValue(UDMXEntityFixturePatch* FixturePatch, FIntPoint Coordinate /* Cell coordinate X/Y */, FDMXAttributeName Attribute, int32 Value);

	/**  Get DMX Cell value using matrix coordinates. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. DMXEntityFixurePatch::GetMatrixCellValues instead"))
	bool GetMatrixCellValue(UDMXEntityFixturePatch* FixturePatch, FIntPoint Coordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& AttributeValueMap);

	/**  Gets the starting channel of each cell attribute at given coordinate, relative to the Starting Channel of the patch. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. DMXEntityFixurePatch::GetMatrixCellChannelsRelative instead"))
	bool GetMatrixCellChannelsRelative(UDMXEntityFixturePatch* FixturePatch, FIntPoint Coordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap);
	
	/**  Gets the absolute starting channel of each cell attribute at given coordinate */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. DMXEntityFixurePatch::GetMatrixCellChannelsAbsolute instead"))
	bool GetMatrixCellChannelsAbsolute(UDMXEntityFixturePatch* FixturePatch, FIntPoint Coordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap);

	/**  Get Matrix Fixture properties */
	UFUNCTION(BlueprintPure, Category = "DMX", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. DMXEntityFixurePatch::GetMatrixProperties instead"))
	bool GetMatrixProperties(UDMXEntityFixturePatch* FixturePatch, FDMXFixtureMatrix& MatrixProperties);

	/**  Get all attributes for the fixture patch. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. DMXEntityFixurePatch::GetCellAttributes instead"))
	bool GetCellAttributes(UDMXEntityFixturePatch* FixturePatch, TArray<FDMXAttributeName>& CellAttributes);

	/**  Get data for single cell. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. DMXEntityFixurePatch::GetMatrixCell instead"))
	bool GetMatrixCell(UDMXEntityFixturePatch* FixturePatch, FIntPoint Coordinate /* Cell coordinate X/Y */, FDMXCell& Cell);

	/**  Get array of all cells and associated data. */
	UFUNCTION(BlueprintCallable, Category = "DMX", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. DMXEntityFixurePatch::GetAllMatrixCells instead"))
	bool GetAllMatrixCells(UDMXEntityFixturePatch* FixturePatch, TArray<FDMXCell>& Cells);

	/**  Sort an array according to the selected distribution pattern. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void PixelMappingDistributionSort(EDMXPixelMappingDistribution InDistribution, int32 InNumXPanels, int32 InNumYPanels, const TArray<int32>& InUnorderedList, TArray<int32>& OutSortedList);

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
	TMap<FName, FDelegateHandle> UniverseInputBufferUpdatedHandles;

private:
	/** Called when asset registry finished loading files */
	UFUNCTION()
	void OnAssetRegistryFinishedLoadingFiles();

	/** Called when asset registry added an asset */
	UFUNCTION()
	void OnAssetRegistryAddedAsset(const FAssetData& Asset);

	/** Called when asset registry removed an asset */
	UFUNCTION()
	void OnAssetRegistryRemovedAsset(const FAssetData& Asset);

	void OnGameThreadOnlyBufferUpdated(const FName& InProtocolName, int32 InUniverseID);

private:

	/** Strongly references all libraries at all times */
	UPROPERTY()
	TArray<UDMXLibrary*> LoadedDMXLibraries;
};
