// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"

class UToolMenu;

/**
 * Static load-related helper functions for populating the "Layouts" menu.
 */
class FLayoutsMenuLoad
{
public:
	/**
	 * Static
	 * It creates the layout load selection menu.
	 */
	static void MakeLoadLayoutsMenu(UToolMenu* InToolMenu);

	/**
	 * Static
	 * Checks if the load menu can choose the selected layout to load it.
	 * @param	InLayoutIndex  Index from the selected layout.
	 * @return true if the selected layout can be read.
	 */
	static bool CanLoadChooseLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Checks if the load menu can choose the selected user-created layout to load it.
	 * @param	InLayoutIndex  Index from the selected user-created layout.
	 * @return true if the selected user-created layout can be read.
	 */
	static bool CanLoadChooseUserLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * It re-loads the current Editor UI Layout (from GEditorLayoutIni).
	 * This function is used for many of the functions of FLayoutsMenuLoad.
	 */
	static void ReloadCurrentLayout();

	/**
	 * Static
	 * Load the visual layout state of the editor from an existing layout profile ini file, given its file path.
	 * @param InLayoutPath File path associated with the desired layout profile ini file to be read/written.
	 */
	static void LoadLayout(const FString& InLayoutPath);

	/**
	 * Static
	 * Load the visual layout state of the editor from an existing layout profile ini file, that was previously created by the UE developers.
	 * @param InLayoutIndex Index associated with the desired layout profile ini file to be read/written.
	 */
	static void LoadLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Load the visual layout state of the editor from an existing layout profile ini file, that was previously created by the user.
	 * @param InLayoutIndex Index associated with the desired layout profile ini file to be read/written.
	 */
	static void LoadUserLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Import a visual layout state of the editor from a custom directory path and with a custom file name chosen by the user.
	 * It saves it into the user layout folder, and loads it.
	 */
	static void ImportLayout();
};

/**
 * Static save-related helper functions for populating the "Layouts" menu.
 */
class FLayoutsMenuSave
{
public:
	/**
	 * Static
	 * It creates the layout save selection menu.
	 */
	static void MakeSaveLayoutsMenu(UToolMenu* InToolMenu);

	/**
	 * Static
	 * Checks if the save menu can choose the selected layout to modify it.
	 * @param	InLayoutIndex  Index from the selected layout.
	 * @return true if the selected layout can be modified/removed.
	 */
	static bool CanSaveChooseLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Checks if the save menu can choose the selected user-created layout to modify it.
	 * @param	InLayoutIndex  Index from the selected user-created layout.
	 * @return true if the selected user-created layout can be modified/removed.
	 */
	static bool CanSaveChooseUserLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Override the visual layout state of the editor in an existing layout profile ini file, that was previously created by the UE developers.
	 * @param InLayoutIndex Index associated with the desired layout profile ini file to be read/written.
	 */
	static void OverrideLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Override the visual layout state of the editor in an existing layout profile ini file, that was previously created by the user.
	 * @param InLayoutIndex Index associated with the desired layout profile ini file to be read/written.
	 */
	static void OverrideUserLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Save the visual layout state of the editor (if changes to the layout have been made since the last time it was saved).
	 * If no changes has been made to the layout, the file is not updated (given that it would not be required).
	 * Any function that saves the layout (e.g., OverrideLayoutI, OverrideUserLayoutI, SaveLayoutAs, ExportLayout, etc.) should internally call this function.
	*/
	static void SaveLayout();

	/**
	 * Static
	 * Save the visual layout state of the editor with a custom file name chosen by the user.
	 */
	static void SaveLayoutAs();

	/**
	 * Static
	 * Export the visual layout state of the editor in a custom directory path and with a custom file name chosen by the user.
	 */
	static void ExportLayout();
};

/**
 * Static remove-related helper functions for populating the "Layouts" menu.
 */
class FLayoutsMenuRemove
{
public:
	/**
	 * Static
	 * It creates the layout remove selection menu.
	 */
	static void MakeRemoveLayoutsMenu(UToolMenu* InToolMenu);

	/**
	 * Static
	 * Checks if the remove menu can choose the selected layout to remove it.
	 * @param	InLayoutIndex  Index from the selected layout.
	 * @return true if the selected layout can be modified/removed.
	 */
	static bool CanRemoveChooseLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Checks if the remove menu can choose the selected user-created layout to remove it.
	 * @param	InLayoutIndex  Index from the selected user-created layout.
	 * @return true if the selected user-created layout can be modified/removed.
	 */
	static bool CanRemoveChooseUserLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Remove an existing layout profile ini file, that was previously created by the UE developers.
	 * @param InLayoutIndex Index associated with the desired layout profile ini file to be read/written.
	 */
	static void RemoveLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Remove an existing layout profile ini file, that was previously created by the UE developers.
	 * @param InLayoutIndex Index associated with the desired layout profile ini file to be read/written.
	 */
	static void RemoveUserLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Remove all the layout customizations created by the user.
	 */
	static void RemoveUserLayouts();
};

/**
 * Static helper functions for populating the "Layouts" menu.
 */
class FLayoutsMenuBase
{
public:
	/**
	 * Static
	 * Get the full layout file path.
	 * Helper function for LoadLayoutI, SaveLayoutI, and RemoveLayoutI.
	 * @param InLayoutIndex Index associated with the desired layout profile ini file to be read/written.
	 */
	static FString GetLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Get the full user-created layout file path.
	 * Helper function for LoadUserLayoutI, SaveUserLayoutI, and RemoveUserLayoutI.
	 * @param InLayoutIndex Index associated with the desired user-created layout profile ini file to be read/written.
	 */
	static FString GetUserLayout(const int32 InLayoutIndex);

	/**
	 * Static
	 * Checks whether there are user-created layouts.
	 * @return true if there is at least a layout in the user layouts directory.
	 */
	static bool IsThereUserLayouts();

	/**
	 * Static
	 * Checks which layout entry should be checked.
	 * @param	InLayoutIndex  Index from the selected layout.
	 * @return true if the menu entry should be checked.
	 */
	static bool IsLayoutChecked(const int32 InLayoutIndex);

	/**
	 * Static
	 * Checks which user layout entry should be checked.
	 * @param	InLayoutIndex  Index from the selected layout.
	 * @return true if the menu entry should be checked.
	 */
	static bool IsUserLayoutChecked(const int32 InLayoutIndex);
};

#endif
