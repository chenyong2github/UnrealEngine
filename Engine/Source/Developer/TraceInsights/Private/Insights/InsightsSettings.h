// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"

/** Contains all settings for the Unreal Insights, accessible through the main manager. */
class FInsightsSettings
{
public:
	FInsightsSettings(bool bInIsDefault = false)
		: bIsEditing(false)
		, bIsDefault(bInIsDefault)
		, bShowEmptyTracksByDefault(false)
	{
		if (!bIsDefault)
		{
			LoadFromConfig();
		}
	}

	~FInsightsSettings()
	{
		if (!bIsDefault)
		{
			SaveToConfig();
		}
	}

	void LoadFromConfig()
	{
		FConfigCacheIni::LoadGlobalIniFile(SettingsIni, TEXT("UnrealInsightsSettings"));

		GConfig->GetBool(TEXT("Insights.TimingProfiler"), TEXT("bShowEmptyTracksByDefault"), bShowEmptyTracksByDefault, SettingsIni);
	}

	void SaveToConfig()
	{
		GConfig->SetBool(TEXT("Insights.TimingProfiler"), TEXT("bShowEmptyTracksByDefault"), bShowEmptyTracksByDefault, SettingsIni);

		GConfig->Flush(false, SettingsIni);
	}

	void EnterEditMode()
	{
		bIsEditing = true;
	}

	void ExitEditMode()
	{
		bIsEditing = false;
	}

	const bool IsEditing() const
	{
		return bIsEditing;
	}

	const FInsightsSettings& GetDefaults() const
	{
		return Defaults;
	}

	void ResetToDefaults()
	{
		bShowEmptyTracksByDefault = Defaults.bShowEmptyTracksByDefault;
	}

//protected:
public:
	/** Contains default settings. */
	static FInsightsSettings Defaults;

	/** Setting filename ini. */
	FString SettingsIni;

	/** Whether profiler settings is in edit mode. */
	bool bIsEditing;

	/** Whether this instance contains defaults. */
	bool bIsDefault;

	//////////////////////////////////////////////////
	// Actual settings.

	/** If True, the Timing View will show empty tracks by default. */
	bool bShowEmptyTracksByDefault;
};
