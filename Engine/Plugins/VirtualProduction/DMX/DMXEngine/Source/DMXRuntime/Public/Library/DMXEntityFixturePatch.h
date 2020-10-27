// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntity.h"

#include "DMXAttribute.h"
#include "DMXProtocolTypes.h"
#include "DMXTypes.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"

#include "Tickable.h"

#include "DMXEntityFixturePatch.generated.h"

class UDMXEntityFixtureType;

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "DMX Fixture Patch"))
class DMXRUNTIME_API UDMXEntityFixturePatch
	: public UDMXEntity
	, public FTickableGameObject
{
	GENERATED_BODY()

	DECLARE_MULTICAST_DELEGATE_TwoParams(FDMXOnFixturePatchReceivedDMXDelegate, UDMXEntityFixturePatch* /** FixturePatch */, const FDMXNormalizedAttributeValueMap& /** ValuePerAttribute */);

public:
	UDMXEntityFixturePatch();

public:
#if WITH_EDITOR
	/** Sets if the patch ticks in editor, used by editor objects such as track recorder. */
	void SetTickInEditor(bool bShouldTickInEditor) { bTickInEditor = bShouldTickInEditor; }
#endif

protected:	
	// ~Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const;
	virtual bool IsTickableWhenPaused() const;
	virtual bool IsTickable() const;
	// ~End FTickableGameObject interface

#if WITH_EDITOR
	/** Whether the patch ticks in editor */
	bool bTickInEditor;
#endif // WITH_EDITOR

public:
	/** Broadcasts when the patch received dmx */
	FDMXOnFixturePatchReceivedDMXDelegate OnFixturePatchReceivedDMX;

	const TSharedPtr<FDMXSignal>& GetLastReceivedDMXSignal() const { return CachedLastDMXSignal; }

#if WITH_EDITOR
	/** Clears cached data. Useful in dmx to rest to default state on begin and end PIE */
	void ClearCachedData();
#endif // WITH_EDITOR

private:
	/** Updates CachedDMXValues, returns true if cached values changed. */
	bool UpdateCachedDMXValues();

	/** Updates CachedNormalizedValuesPerAttribute from the CachedDMXValues */
	void UpdateCachedNormalizedAttributeValues();

	/** The last received DMX signal */
	TSharedPtr<FDMXSignal> CachedLastDMXSignal;

	/** 
	 * Raw cached DMX values, last received. This only contains DMX data relevant to the patch,
	 * from starting channel to starting channel + channel span 
	 */
	TArray<uint8> CachedDMXValues;

	/** Map of normalized values per attribute, direct represpentation of CachedDMXValues. */
	FDMXNormalizedAttributeValueMap CachedNormalizedValuesPerAttribute;

	/** Standalone optimization to avoid looking up relevant controllers each tick. Updated each tick if WITH_EDITOR */
	UPROPERTY(Transient)
	UDMXEntityController* CachedRelevantController;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings")
	TArray<FName> CustomTags;

	/**
	 * If set to a value on a Controller's Universe IDs range (without the Range Offset),
	 * this Patch's functions are sent over the network by that Controller.
	 *
	 * When set to a value on several Controllers' range, the functions are sent by all of those Controllers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings", meta = (ClampMin = 0, DisplayName = "Universe"))
	int32 UniverseID;

	/** Auto-assign address from drag/drop list order and available channels */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings", meta = (DisplayName = "Auto-Assign Address"))
	bool bAutoAssignAddress;

	/** Starting channel for when auto-assign address is false */
	UPROPERTY(EditAnywhere, Category = "Fixture Settings", meta = (EditCondition = "!bAutoAssignAddress", DisplayName = "Manual Starting Address", UIMin = "1", UIMax = "512", ClampMin = "1", ClampMax = "512"))
	int32 ManualStartingAddress;

	/** Starting channel from auto-assignment. Used when AutoAssignAddress is true */
	UPROPERTY(NonTransactional)
	int32 AutoStartingAddress;

	/** Property to point to the template parent fixture for details panel purposes */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Fixture Type Settings", meta = (DisplayName = "Fixture Type"))
	UDMXEntityFixtureType* ParentFixtureTypeTemplate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type Settings")
	int32 ActiveMode;

#if WITH_EDITORONLY_DATA
	/** Color when displayed in the fixture patch editor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type Settings")
	FLinearColor EditorColor = FLinearColor(1.0f, 0.0f, 1.0f);
#endif

public:
	/**
	 * Returns the number of channels this Patch occupies with the Fixture functions from its Active Mode.
	 * It'll always be at least 1 channel.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	int32 GetChannelSpan() const;

	/**  Return the active starting channel, evaluated after checking if Auto-Assignment is activated. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	int32 GetStartingChannel() const;

	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	int32 GetEndingChannel() const;

	/**  Return the remote universe the patch is registered to. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	int32 GetRemoteUniverse() const;

	/**
	 * Return an array of function names for the currently active mode.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. Conversion from function name String to FName is lossy."))
	TArray<FName> GetAllFunctionsInActiveMode() const;

	/**
	 * Return an array of valid attributes for the currently active mode.
	 * Attributes outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TArray<FDMXAttributeName> GetAllAttributesInActiveMode() const;

	/**
	 * Return map of function names and attributes
	 * Attributes outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. Conversion from function name String to FName is lossy."))
	TMap<FName, FDMXAttributeName> GetFunctionAttributesMap() const;

	/**
	 * Return map of attributes and function names.
	 * Attributes outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FDMXAttributeName, FDMXFixtureFunction> GetAttributeFunctionsMap() const;

	/**
	 * Return map of function names and default values.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FDMXAttributeName, int32> GetAttributeDefaultMap() const;

	/** Return map of Attributes and their assigned channels */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FDMXAttributeName, int32> GetAttributeChannelAssignments() const;

	/**
	 * Return map of function names and their Data Types.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FDMXAttributeName, EDMXFixtureSignalFormat> GetAttributeSignalFormats() const;

	/**  Given a <Channel Index -> Raw Value> map , return map of function names and their values. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FDMXAttributeName, int32> ConvertRawMapToAttributeMap(const TMap<int32, uint8>& RawMap) const;

	/**
	 * Return map of function channels and their values.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<int32, uint8> ConvertAttributeMapToRawMap(const TMap<FDMXAttributeName, int32>& FunctionMap) const;

	/**  Return if given function map is valid for this fixture. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	bool IsMapValid(const TMap<FDMXAttributeName, int32>& FunctionMap) const;

	/**  Return if fixture contains function. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	FORCEINLINE bool ContainsAttribute(const FDMXAttributeName FunctionAttribute) const
	{
		const TArray<FDMXFixtureFunction>& Functions = ParentFixtureTypeTemplate->Modes[ActiveMode].Functions;
		return Functions.ContainsByPredicate([&FunctionAttribute](const FDMXFixtureFunction& Function)
			{
				return FunctionAttribute == Function.Attribute;
			});
	}

	/**  Return a map that is valid for this fixture. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FDMXAttributeName, int32> ConvertToValidMap(const TMap<FDMXAttributeName, int32>& FunctionMap) const;

	/**
	 * Scans the parent DMXLibrary and returns the Controllers which Universe range
	 * match this Patch's UniverseID
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TArray<UDMXEntityController*> GetRelevantControllers() const;

	/** Returns true if this Patch's UniverseID is in InController's range */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	FORCEINLINE bool IsInControllerRange(const UDMXEntityController* InController) const
	{
		return InController != nullptr &&
			UniverseID >= InController->UniverseLocalStart && UniverseID <= InController->UniverseLocalEnd;
	}

	/** Returns true if this Patch's UniverseID is in any of InControllers' ranges */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	bool IsInControllersRange(const TArray<UDMXEntityController*>& InControllers) const;

	/**
	 * Retrieve the value of an Attribute. Will fail and return 0 if the Attribute doesn't exist.
	 *
	 * @param Attribute	The Attribute to try to get the value from.
	 * @param bSuccess	Whether the Attribute was found in this Fixture Patch
	 * @return			The value of the Function mapped to the selected Attribute, if found.
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	int32 GetAttributeValue(FDMXAttributeName Attribute, bool& bSuccess);

	/**
	 * Retrieve the normalized value of an Attribute. Will fail and return 0 if the Attribute doesn't exist.
	 *
	 * @param Attribute	The Attribute to try to get the value from.
	 * @param bSuccess	Whether the Attribute was found in this Fixture Patch
	 * @return			The value of the Function mapped to the selected Attribute, if found.
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. Use GetNormalizedAttributeValue instead. Note, new method returns normalized values!"))
	float GetNormalizedAttributeValue(FDMXAttributeName Attribute, bool& bSuccess);

	/**
	 * Returns the value of each attribute, or zero if no value was ever received.
	 *
	 * @param AttributesValues	Out: Resulting map of Attributes with their values
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	void GetAttributesValues(TMap<FDMXAttributeName, int32>& AttributesValues);

	/**
	 * Returns the normalized value of each attribute, or zero if no value was ever received.
	 *
	 * @param AttributesValues	Out: Resulting map of Attributes with their normalized values
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	void GetNormalizedAttributesValues(FDMXNormalizedAttributeValueMap& NormalizedAttributesValues);

	/** Sends the DMX value of the Attribute to specified matrix coordinates */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool SendMatrixCellValue(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, const FDMXAttributeName& Attribute, int32 Value);

	/** Maps the normalized value to the Attribute's full value range and sends it to specified matrix coordinates  */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool SendNormalizedMatrixCellValue(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, const FDMXAttributeName& Attribute, float RelativeValue);

	/**  Get DMX Cell value using matrix coordinates. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool GetMatrixCellValues(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& ValuePerAttribute);
	
	/**  Get DMX Cell value using matrix coordinates. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool GetNormalizedMatrixCellValues(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, float>& NormalizedValuePerAttribute);

	/**  Gets the starting channel of each cell attribute at given coordinate, relative to the Starting Channel of the patch. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool GetMatrixCellChannelsRelative(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap);
	
	/**  Gets the absolute starting channel of each cell attribute at given coordinate */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool GetMatrixCellChannelsAbsolute(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap);

	/**  Get Matrix Fixture properties */
	UFUNCTION(BlueprintPure, Category = "DMX")
	bool GetMatrixProperties(FDMXFixtureMatrix& MatrixProperties);

	/**  Get all attributes for the fixture patch. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool GetCellAttributes(TArray<FDMXAttributeName>& CellAttributes);

	/**  Get data for single cell. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool GetMatrixCell(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, FDMXCell& Cell);

	/**  Get array of all cells and associated data. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	bool GetAllMatrixCells(TArray<FDMXCell>& Cells);

private:
	/** Try to access the FixtureMatrix config of this patch and logs issues. Returns the matrix of nullptr if it isn't valid. */
	FDMXFixtureMatrix* AccessFixtureMatrixValidated();

	/** Returns true if the specified coordinates are valid for the specified matrix */
	static bool AreCoordinatesValid(const FDMXFixtureMatrix& FixtureMatrix, const FIntPoint& Coordinate, bool bLogged = true);

public:
	//~ Begin UDMXEntity Interface
	virtual bool IsValidEntity(FText& OutReason) const override;
	//~ End UDMXEntity Interface

	/** Called from Fixture Type to keep ActiveMode in valid range when Modes are removed from the Type */
	void ValidateActiveMode();

	/**
	 * Return the function currently mapped to the passed in Attribute, if any.
	 * If no function is mapped to it, returns nullptr.
	 *
	 * @param Attribute The attribute name to search for.
	 * @return			The function mapped to the passed in Attribute or nullptr
	 *					if no function is mapped to it.
	 */
	const FDMXFixtureFunction* GetAttributeFunction(const FDMXAttributeName& Attribute) const;

	/** Return the first controller found which affects this Fixture Patch. nullptr if none */
	UDMXEntityController* GetFirstRelevantController() const;

	/** Checks if the current Mode for this Patch is valid for its Fixture Type */
	FORCEINLINE bool CanReadActiveMode() const
	{
		return ParentFixtureTypeTemplate != nullptr
			&& ParentFixtureTypeTemplate->IsValidLowLevelFast()
			&& ParentFixtureTypeTemplate->Modes.Num() > ActiveMode
			&& ActiveMode >= 0;
	}
};
