// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "Library/DMXEntity.h"

#include "DMXAttribute.h"
#include "DMXProtocolCommon.h"
#include "DMXTypes.h"
#include "Library/DMXEntityFixtureType.h"

#include "Tickable.h"

#include "DMXEntityFixturePatch.generated.h"

class UDMXEntityController;
class UDMXEntityFixtureType;

struct FPropertyChangedEvent;


/** 
 * A DMX fixture patch that can be patch to channels in a DMX Universe via the DMX Library Editor. 
 * 
 * Use in DMXComponent or call SetReceiveDMXEnabled with 'true' to enable receiving DMX. 
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "DMX Fixture Patch"))
class DMXRUNTIME_API UDMXEntityFixturePatch
	: public UDMXEntity
	, public FTickableGameObject
{
	GENERATED_BODY()

	DECLARE_MULTICAST_DELEGATE_TwoParams(FDMXOnFixturePatchReceivedDMXDelegate, UDMXEntityFixturePatch* /** FixturePatch */, const FDMXNormalizedAttributeValueMap& /** ValuePerAttribute */);

public:
	UDMXEntityFixturePatch();

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	// ~End UObject Interface

	// ~Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	// ~End FTickableGameObject interface

public:
	/**  Send DMX using attribute names and integer values. */
	UFUNCTION(BlueprintCallable, Category = "DMX")
	void SendDMX(TMap<FDMXAttributeName, int32> AttributeMap);



#if WITH_EDITOR
/** Clears cached data. Useful in dmx to rest to default state on begin and end PIE */
	void ClearCachedData();
#endif // WITH_EDITOR

	/** Returns the last received DMX signal. */
	const FDMXSignalSharedPtr& GetLastReceivedDMXSignal() const { return LastDMXSignal; }

	/** Broadcasts when the patch received dmx, see DMXComponent for an example of use */
	FDMXOnFixturePatchReceivedDMXDelegate OnFixturePatchReceivedDMX;

private:
	/** Updates the cached values. Returns true if the values got updated (if the values changed) */
	bool UpdateCachedValues();

	/** The last received DMX signal */
	FDMXSignalSharedPtr LastDMXSignal;

	/** 
	 * Raw cached DMX values, last received. This only contains DMX data relevant to the patch,
	 * from starting channel to starting channel + channel span.
	 */
	TArray<uint8> CachedDMXValues;

	/** Map of normalized values per attribute, direct represpentation of CachedDMXValues. */
	FDMXNormalizedAttributeValueMap CachedNormalizedValuesPerAttribute;

public:
	//~ Begin UDMXEntity Interface
	virtual bool IsValidEntity(FText& OutReason) const override;
	//~ End UDMXEntity Interface

	/** Called from Fixture Type to keep ActiveMode in valid range when Modes are removed from the Type */
	void ValidateActiveMode();

	/** Checks if the current Mode for this Patch is valid for its Fixture Type */
	UE_DEPRECATED(4.27, "Use GetActiveMode instead.")
	bool CanReadActiveMode() const;

	/** Returns the active mode, or nullptr if there is no valid active mode */
	const FDMXFixtureMode* GetActiveMode() const;

public:
	/** The local universe of the patch */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Patch", meta = (ClampMin = 0, DisplayName = "Universe"))
	int32 UniverseID;

	/** Auto-assign address from drag/drop list order and available channels */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Patch", meta = (DisplayName = "Auto-Assign Address"))
	bool bAutoAssignAddress;

	/** Starting channel for when auto-assign address is false */
	UPROPERTY(EditAnywhere, Category = "Fixture Patch", meta = (EditCondition = "!bAutoAssignAddress", DisplayName = "Manual Starting Address", UIMin = "1", UIMax = "512", ClampMin = "1", ClampMax = "512"))
	int32 ManualStartingAddress;

	/** Starting channel from auto-assignment. Used when AutoAssignAddress is true */
	UPROPERTY(NonTransactional)
	int32 AutoStartingAddress;

	/** Property to point to the template parent fixture for details panel purposes */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Fixture Patch", meta = (DisplayName = "Fixture Type"))
	UDMXEntityFixtureType* ParentFixtureTypeTemplate;

	/** The fixture type mode the patch should use */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Patch")
	int32 ActiveMode;	
	
	/** Custom tags for filtering patches  */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Patch")
	TArray<FName> CustomTags;

#if WITH_EDITORONLY_DATA
	/** Color when displayed in the fixture patch editor */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Patch")
	FLinearColor EditorColor = FLinearColor(1.0f, 0.0f, 1.0f);
#endif

public:
	/** Returns the number of channels this Patch occupies with the Fixture functions from its Active Mode or 0 if the patch has no valid Active Mode. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	int32 GetChannelSpan() const;

	/**  Return the active starting channel, evaluated after checking if Auto-Assignment is activated. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	int32 GetStartingChannel() const;

	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	int32 GetEndingChannel() const;

	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "Controllers are replaced with DMX Ports.")
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.27. Controllers are replaced with DMX Ports."))
	int32 GetRemoteUniverse() const;

	/**
	 * Return an array of valid attributes for the currently active mode.
	 * Attributes outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TArray<FDMXAttributeName> GetAllAttributesInActiveMode() const;

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
	bool ContainsAttribute(const FDMXAttributeName FunctionAttribute) const;

	/**  Return a map that is valid for this fixture. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FDMXAttributeName, int32> ConvertToValidMap(const TMap<FDMXAttributeName, int32>& FunctionMap) const;
	
	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "Controllers are replaced with DMX Ports.")
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.27. Controllers are replaced with DMX Ports."))
	TArray<UDMXEntityController*> GetRelevantControllers() const;

	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "Controllers are replaced with DMX Ports.")
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.27. Controllers are replaced with DMX Ports."))
	bool IsInControllerRange(const UDMXEntityController* InController) const { return false; }

	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "Controllers are replaced with DMX Ports.")
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.27. Controllers are replaced with DMX Ports."))
	bool IsInControllersRange(const TArray<UDMXEntityController*>& InControllers) const { return false; }

	/**
	 * Return the function currently mapped to the passed in Attribute, if any.
	 * If no function is mapped to it, returns nullptr.
	 *
	 * @param Attribute The attribute name to search for.
	 * @return			The function mapped to the passed in Attribute or nullptr
	 *					if no function is mapped to it.
	 */
	const FDMXFixtureFunction* GetAttributeFunction(const FDMXAttributeName& Attribute) const;

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
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool SendMatrixCellValue(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, const FDMXAttributeName& Attribute, int32 Value);

	/** Maps the normalized value to the Attribute's full value range and sends it to specified matrix coordinates  */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool SendNormalizedMatrixCellValue(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, const FDMXAttributeName& Attribute, float RelativeValue);

	/**  Get DMX Cell value using matrix coordinates. */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool GetMatrixCellValues(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& ValuePerAttribute);
	
	/**  Get DMX Cell value using matrix coordinates. */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool GetNormalizedMatrixCellValues(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, float>& NormalizedValuePerAttribute);

	/**  Gets the starting channel of each cell attribute at given coordinate, relative to the Starting Channel of the patch. */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool GetMatrixCellChannelsRelative(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap);
	
	/**  Gets the absolute starting channel of each cell attribute at given coordinate */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool GetMatrixCellChannelsAbsolute(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, TMap<FDMXAttributeName, int32>& AttributeChannelMap);

	/**  Get Matrix Fixture properties */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	bool GetMatrixProperties(FDMXFixtureMatrix& MatrixProperties) const;

	/**  Get all attributes for the fixture patch. */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool GetCellAttributes(TArray<FDMXAttributeName>& CellAttributes);

	/**  Get data for single cell. */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool GetMatrixCell(const FIntPoint& CellCoordinate /* Cell coordinate X/Y */, FDMXCell& Cell);

	/**  Get array of all cells and associated data. */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	bool GetAllMatrixCells(TArray<FDMXCell>& Cells);

private:
	/** Try to access the FixtureMatrix config of this patch and logs issues. Returns the matrix of nullptr if it isn't valid. */
	const FDMXFixtureMatrix* GetFixtureMatrixValidated() const;

	/** Returns true if the specified coordinates are valid for the specified matrix */
	static bool AreCoordinatesValid(const FDMXFixtureMatrix& FixtureMatrix, const FIntPoint& Coordinate, bool bLogged = true);
};
