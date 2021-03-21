// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FNaniteVisualizationData
{
public:
	enum class FModeType : uint8
	{
		Overview,
		Standard,
		Advanced,
	};

	/** Describes a single available visualization mode. */
	struct FModeRecord
	{
		FString   ModeString;
		FName     ModeName;
		FText     ModeText;
		FModeType ModeType;
		uint32    ModeID;
	};

	/** Mapping of FName to a visualization mode record. */
	typedef TMultiMap<FName, FModeRecord> TModeMap;

public:
	FNaniteVisualizationData()
	: bIsInitialized(false)
	{
	}

	/** Initialize the system. */
	void Initialize();

	/** Check if system was initialized. */
	bool IsInitialized() const { return bIsInitialized; }

	/** Get the display name of a named mode from the available mode map. **/
	ENGINE_API FText GetModeDisplayName(FName InModeName) const;

	ENGINE_API uint32 GetModeID(FName InModeName) const;

	/** We cache the overview mode name list from the console command here, so all dynamically created views can re-use the existing cached list of modes. */
	void SetCurrentOverviewModeNames(const FString& InNameList);
	bool IsDifferentToCurrentOverviewModeNames(const FString& InNameList);

	/** Access the list of modes currently in use by the Nanite visualization overview. */
	//TArray<FName>& GetOverviewModes();

	inline const TModeMap& GetModeMap() const
	{
		return ModeMap;
	}

private:
	/** Internal helper function for creating the Nanite visualization system console commands. */
	void ConfigureConsoleCommand();

	void AddVisualizationMode(
		const TCHAR* ModeString,
		const FText& ModeText,
		const FModeType ModeType,
		uint32 ModeID
	);

private:
	/** The name->mode mapping table */
	TModeMap ModeMap;

	/** List of modes names to use in the Nanite visualization overview. */
	FString CurrentOverviewModeNames;

	/** Storage for console variable documentation strings. **/
	FString ConsoleDocumentationVisualizationMode;
	FString ConsoleDocumentationOverviewTargets;

	/** Flag indicating if system is initialized. **/
	bool bIsInitialized;
};

ENGINE_API FNaniteVisualizationData& GetNaniteVisualizationData();
