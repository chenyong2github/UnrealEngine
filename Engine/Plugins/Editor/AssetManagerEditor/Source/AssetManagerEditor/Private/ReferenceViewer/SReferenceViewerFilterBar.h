// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Filters/SFilterBar.h"
#include "ReferenceViewer/ReferenceViewerSettings.h"
#include "ReferenceViewer/EdGraph_ReferenceViewer.h"

class SReferenceViewerFilterBar : public SFilterBar< FReferenceNodeInfo& > 
{

public:

	/** Saves any settings to config that should be persistent between editor sessions */
	void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const override;

	/** Loads any settings to config that should be persistent between editor sessions */
	void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) override;

};