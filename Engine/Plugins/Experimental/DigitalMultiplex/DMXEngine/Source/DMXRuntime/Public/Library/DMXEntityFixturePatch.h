// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Library/DMXEntity.h"
#include "Library/DMXEntityController.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXProtocolTypes.h"
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings", meta = (ClampMin = 0))
	int32 UniverseID;

	/** Auto-assign channel from drag/drop list order and available channels */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Settings", meta = (DisplayName = "Auto-Assign Channel"))
	bool bAutoAssignAddress;

	/** Starting channel for when auto-assign channel is false */
	UPROPERTY(EditAnywhere, Category = "Fixture Settings", meta = (EditCondition = "!bAutoAssignAddress", DisplayName = "Manual Starting Channel", UIMin = "1", UIMax = "512", ClampMin = "1", ClampMax = "512"))
	int32 ManualStartingAddress;

	/** Starting channel from auto-assignment. Used when AutoAssignAddress is true */
	UPROPERTY(NonTransactional)
	int32 AutoStartingAddress;

	/** Property to point to the template parent fixture for details panel purposes */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Fixture Type Settings", meta = (DisplayName = "Fixture Type"))
	UDMXEntityFixtureType* ParentFixtureTypeTemplate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fixture Type Settings")
	int32 ActiveMode;

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

	/**
	 * Return an array of function names for the currently active mode.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TArray<FName> GetAllFunctionsInActiveMode() const;

	/**
	 * Return map of function names and default values.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FName, int32> GetFunctionDefaultMap() const;

	/**
	 * Return map of function names and their assigned channels.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FName, int32> GetFunctionChannelAssignments() const;

	/**
	 * Return map of function names and their Data Types.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FName, EDMXFixtureSignalFormat> GetFunctionSignalFormats() const;

	/**  Given a <Channel Index -> Raw Value> map , return map of function names and their values. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FName, int32> ConvertRawMapToFunctionMap(const TMap<int32, uint8>& RawMap) const;

	/**
	 * Return map of function channels and their values.
	 * Functions outside the Active Mode's channel span range are ignored.
	 */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<int32, uint8> ConvertFunctionMapToRawMap(const TMap<FName, int32>& FunctionMap) const;

	/**  Return if given function map valid for this fixture. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	bool IsMapValid(const TMap<FName, int32>& FunctionMap) const;

	/**  Return if fixture contains function. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	FORCEINLINE bool ContainsFunction(const FName& FunctionName) const
	{
		const TArray<FDMXFixtureFunction>& Functions = ParentFixtureTypeTemplate->Modes[ActiveMode].Functions;
		return Functions.ContainsByPredicate([&FunctionName](const FDMXFixtureFunction& Function)
			{
				return FunctionName.IsEqual(*Function.FunctionName);
			});
	}

	/**  Return a map that is valid for this fixture. */
	UFUNCTION(BlueprintPure, Category = "DMX|Fixture Patch")
	TMap<FName, int32> ConvertToValidMap(const TMap<FName, int32>& FunctionMap) const;

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

public:
	UDMXEntityFixturePatch();

	//~ Begin UDMXEntity Interface
	virtual bool IsValidEntity(FText& OutReason) const;
	//~ End UDMXEntity Interface

	// Called from Fixture Type to keep ActiveMode in valid range when Modes are removed from the Type
	void ValidateActiveMode();

	FORCEINLINE bool CanReadActiveMode() const
	{
		return ParentFixtureTypeTemplate != nullptr
			&& ParentFixtureTypeTemplate->Modes.Num() > 0
			&& ActiveMode < ParentFixtureTypeTemplate->Modes.Num();
	}
};
