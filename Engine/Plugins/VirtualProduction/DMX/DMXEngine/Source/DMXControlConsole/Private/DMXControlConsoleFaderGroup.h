// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DMXControlConsoleFaderGroup.generated.h"

struct FDMXAttributeName;
struct FDMXFixtureFunction;
class UDMXControlConsoleFaderBase;
class UDMXControlConsoleFaderGroupRow;
class UDMXControlConsoleFixturePatchFunctionFader;
class UDMXControlConsoleRawFader;
class UDMXEntityFixturePatch;


/** A Group of Faders in the DMX Control Console */
UCLASS()
class DMXCONTROLCONSOLE_API UDMXControlConsoleFaderGroup
	: public UObject
{
	GENERATED_BODY()

public:
	/** Adds a raw fader to this Fader Group */
	UDMXControlConsoleRawFader* AddRawFader();

	/** Adds a fixture patch function fader to this Fader Group */
	UDMXControlConsoleFixturePatchFunctionFader* AddFixturePatchFunctionFader(const FDMXFixtureFunction& FixtureFunction, const int32 InUniverseID, const int32 StartingChannel);

	/** Deletes a fader from this Fader Group */
	void DeleteFader(const TObjectPtr<UDMXControlConsoleFaderBase>& Fader);

	/** Clears Faders array */
	void ClearFaders();

	/** Gets the Faders array of this Fader Group */
	const TArray<UDMXControlConsoleFaderBase*>& GetFaders() const { return Faders; }

	/** Gets this Fader Group's index according to its Fader Group Row owner */
	int32 GetIndex() const;

	/** Gets the owner Fader Group Row of this Fader Group */
	UDMXControlConsoleFaderGroupRow& GetOwnerFaderGroupRowChecked() const;

	/** Gets the name of this Fader Group */
	FString GetFaderGroupName() const { return FaderGroupName; }

	/** Sets the name of the Fader Group */
	void SetFaderGroupName(const FString& NewName);

	/** Gets Fader Group color for Editor representation */
	const FLinearColor& GetEditorColor() const { return EditorColor; }

	/** Automatically generates a Fader Group based on Fixture Patch Ref property */
	void GenerateFromFixturePatch(UDMXEntityFixturePatch* InFixturePatch);

	/** Gets current binded Fixture Patch reference, if valid */
	UDMXEntityFixturePatch* GetFixturePatch() const { return FixturePatch.Get(); }

	/** Gets wheter this Fader Group is binded to a Fixture Patch */
	bool HasFixturePatch() const;

	/** Gets a universeID to fragment map for the current list of Raw Faders */
	TMap<int32, TMap<int32, uint8>> GetUniverseToFragmentMap() const;

	/** Gets an AttributeName to values map for the current list of Fixture Patch Function Faders */
	TMap<FDMXAttributeName, int32> GetAttributeMap() const;

	/** Destroys this Fader Group */
	void Destroy();

	/** Resets bForceRefresh condition */
	void ForceRefresh();

	/** True if this Fader Group needs to be refreshed */
	bool HasForceRefresh() const { return bForceRefresh; }

	// Property Name getters
	FORCEINLINE static FName GetFadersPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderGroup, Faders); }
	FORCEINLINE static FName GetFaderGroupNamePropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderGroup, FaderGroupName); }
	FORCEINLINE static FName GetFixturePatchPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderGroup, FixturePatch); }
	FORCEINLINE static FName GetEditorColorPropertyName() { return GET_MEMBER_NAME_CHECKED(UDMXControlConsoleFaderGroup, EditorColor); }

protected:
	//~ Begin UObject interface
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	//~ End UObject interface

private:	
	/** Gets the next Universe and Address available for a new fader */
	void GetNextAvailableUniverseAndAddress(int32& OutUniverse, int32& OutAddress) const;

	/** Name identifier of this Fader Group */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "1"), Category = "DMX Fader Group")
	FString FaderGroupName;

	/** Fixture Patch this Fader Group is based on */
	UPROPERTY(VisibleAnywhere, meta = (DisplayPriority = "3"), Category = "DMX Fixture Patch")
	TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch;
	
	/** Faders array of this Fader Group */
	UPROPERTY(EditAnywhere, Category = "DMX Fader Group")
	TArray<TObjectPtr<UDMXControlConsoleFaderBase>> Faders;

	/** Color for Fader Group representation on the Editor */
	UPROPERTY(EditAnywhere, meta = (DisplayPriority = "2"), Category = "DMX Fader Group")
	FLinearColor EditorColor = FLinearColor::White;

	/** Shows wheter this Fader Group needs to be refreshed or not */
	bool bForceRefresh = false;
};
