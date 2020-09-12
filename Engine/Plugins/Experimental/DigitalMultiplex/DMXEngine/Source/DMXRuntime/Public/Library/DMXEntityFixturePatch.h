// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntity.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXProtocolTypes.h"
#include "DMXAttribute.h"
#include "DMXEntityFixturePatch.generated.h"

class UDMXEntityFixtureType;


UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "DMX Fixture Patch"))
class DMXRUNTIME_API UDMXEntityFixturePatch
	: public UDMXEntity
{
	GENERATED_BODY()

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
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
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
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
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

	/**
	 * Return map of function names and their assigned channels.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
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

	/**  Return if given function map valid for this fixture. */
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
	 * Retrieve the value of a Function mapped to an Attribute. Will fail and return 0 if
	 * there's no Function mapped to the selected Attribute.
	 * 
	 * Note: if the Patch is affected by more than one Controller, the first one found
	 * will be used for protocol selection.
	 *
	 * @param Attribute	The Attribute to try to get the value from.
	 * @param bSuccess	Whether the Attribute was found in this Fixture Patch
	 * @return			The value of the Function mapped to the selected Attribute, if found.
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	int32 GetAttributeValue(FDMXAttributeName Attribute, bool& bSuccess);

	/**
	 * Retrieve the value of all Functions mapped to Attributes in this Fixture Patch.
	 * 
	 * Note: if the Patch is affected by more than one Controller, the first one found
	 * will be used for protocol selection.
	 *
	 * @param AttributesValues The values from Functions mapped to Attributes in this Patch.
	 */
	UFUNCTION(BlueprintCallable, Category = "DMX|Fixture Patch")
	void GetAttributesValues(TMap<FDMXAttributeName, int32>& AttributesValues);

public:
	UDMXEntityFixturePatch();

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
