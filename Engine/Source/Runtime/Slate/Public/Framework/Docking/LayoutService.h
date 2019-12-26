// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

struct SLATE_API FLayoutSaveRestore
{
	/** Gets the ini section label for the additional layout configs */
	static const FString& GetAdditionalLayoutConfigIni();

	/**
	 * Write the layout out into a named config file.
	 *
	 * @param ConfigFileName file to be saved to.
	 * @param LayoutToSave the layout to save.
	 */
	static void SaveToConfig(const FString& ConfigFileName, const TSharedRef<FTabManager::FLayout>& LayoutToSave );

	/**
	 * Given a named DefaultLayout, return any saved version of it from the given ini file, otherwise return the default, also default to open tabs based on bool.
	 *
	 * @param ConfigFileName file to be used to load an existing layout.
	 * @param DefaultLayout the layout to be used if the file does not exist.
	 *
	 * @return Loaded layout or the default.
	 */
	static TSharedRef<FTabManager::FLayout> LoadFromConfig( const FString& ConfigFileName, const TSharedRef<FTabManager::FLayout>& DefaultLayout );

	/**
	 * Write the desired FText value into the desired section of a named config file.
	 * This function should only be used to save localization names (e.g., LayoutName, LayoutDescription).
	 * For saving the FTabManager::FLayout, check SaveToConfig.
	 *
	 * @param InConfigFileName file to be saved to.
	 * @param InSectionName the section name where to save the value.
	 * @param InSectionValue the value to save.
	 */
	static void SaveSectionToConfig(const FString& InConfigFileName, const FString& InSectionName, const FText& InSectionValue);

	/**
	 * Read the desired FText value from the desired section of a named config file.
	 * This function should only be used to load localization names (e.g., LayoutName, LayoutDescription).
	 * For loading the FTabManager::FLayout, check SaveToConfig.
	 *
	 * @param InConfigFileName file to be used to load an existing layout.
	 * @param InSectionName the name of the section to be read.
	 *
	 * @return Loaded FText associated for that section.
	 */
	static FText LoadSectionFromConfig(const FString& InConfigFileName, const FString& InSectionName);

	/**
	 * Migrates the layout configuration from one config file to another.
	 *
	 * @param OldConfigFileName The name of the old configuration file.
	 * @param NewConfigFileName The name of the new configuration file.
	 */
	static void MigrateConfig( const FString& OldConfigFileName, const FString& NewConfigFileName );

	/**
	 * It checks whether a file is a valid layout config file.
	 * @param InConfigFileName file to be read.
	 * @return Whether the file is a valid layout config file.
	 */
	static bool IsValidConfig(const FString& InConfigFileName);

private:
	/**
	 * Make a Json string friendly for writing out to UE .ini config files.
	 * The opposite of GetLayoutStringFromIni.
	 */
	static FString PrepareLayoutStringForIni(const FString& LayoutString);

	/**
	 * Convert from UE .ini Json string to a vanilla Json string.
	 * The opposite of PrepareLayoutStringForIni.
	 */
	static FString GetLayoutStringFromIni(const FString& LayoutString);
};
