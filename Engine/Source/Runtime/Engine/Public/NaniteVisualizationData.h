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
	inline bool IsInitialized() const { return bIsInitialized; }

	/** Check if visualization is active. */
	ENGINE_API bool IsActive() const;

	/** Update state and check if visualization is active. */
	ENGINE_API bool Update(const FName& InViewMode);

	/** Get the display name of a named mode from the available mode map. **/
	ENGINE_API FText GetModeDisplayName(FName InModeName) const;

	ENGINE_API int32 GetModeID(FName InModeName) const;

	/** We cache the overview mode name list from the console command here, so all dynamically created views can re-use the existing cached list of modes. */
	void SetCurrentOverviewModeList(const FString& InNameList);
	bool IsDifferentToCurrentOverviewModeList(const FString& InNameList);

	inline const TArray<uint32, TInlineAllocator<32>>& GetActiveModeIDs() const
	{
		return ActiveVisualizationModes;
	}

	/** Access the list of modes currently in use by the Nanite visualization overview. */
	inline const TArray<FName, TInlineAllocator<32>>& GetOverviewModeNames() const
	{
		return CurrentOverviewModeNames;
	}

	inline const TModeMap& GetModeMap() const
	{
		return ModeMap;
	}

	/** Return the console command name for enabling single mode visualization. */
	static const TCHAR* GetVisualizeConsoleCommandName()
	{
		return TEXT("r.Nanite.Visualize");
	}

	/** Return the console command name for enabling multi mode visualization. */
	static const TCHAR* GetOverviewConsoleCommandName()
	{
		return TEXT("r.Nanite.VisualizeOverview");
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

	TArray<uint32, TInlineAllocator<32>> ActiveVisualizationModes;

	/** List of modes names to use in the Nanite visualization overview. */
	FString CurrentOverviewModeList;

	/** Tokenized Nanite visualization mode names. */
	TArray<FName, TInlineAllocator<32>> CurrentOverviewModeNames;

	/** Storage for console variable documentation strings. **/
	FString ConsoleDocumentationVisualizationMode;
	FString ConsoleDocumentationOverviewTargets;

	/** Flag indicating if system is initialized. **/
	bool bIsInitialized;
};

ENGINE_API FNaniteVisualizationData& GetNaniteVisualizationData();
