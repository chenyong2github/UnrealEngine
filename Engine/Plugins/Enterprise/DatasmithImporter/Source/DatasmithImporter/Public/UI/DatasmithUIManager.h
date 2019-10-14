// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Containers/Map.h"
#include "CoreMinimal.h"
#include "Framework/Commands/UIAction.h"

class FUICommandInfo;
class FUICommandList;
class FToolBarBuilder;

class DATASMITHIMPORTER_API FDatasmithUIManager
{
public:
	static void Initialize();
	static void Shutdown();

	/** Get the Datasmith UI manager singleton instance. */
	static FDatasmithUIManager& Get();

	/**
	 * Add a menu entry to the Datasmith importers drop-down menu
	 *
	 * @param CommandName		The command name (for internal references)
	 * @param Caption			The text displayed in the menu entry
	 * @param Description		The tooltip text displayed when hovering over the menu entry
	 * @param IconResourcePath	The relative path to the .png file for the icon to be displayed in the menu
	 * @param ExecuteAction		The action to be executed when the menu entry is selected
	 * @param FactoryClass		The UClass of the factory to associate with the menu entry
	 *
	 * @return the FUICommandInfo that was created for that menu entry
	 */
	TSharedPtr<FUICommandInfo> AddMenuEntry(const FString& CommandName, const FText& Caption, const FText& Description, const FString& IconResourcePath, FExecuteAction ExecuteAction, UClass* FactoryClass);

	/**
	 * Remove a menu entry from the Datasmith importers drop-down menu
	 *
	 * @param Command			The FUICommandInfo to remove
	 */
	void RemoveMenuEntry(const TSharedPtr<FUICommandInfo>& Command);

	/** Set the last factory used through the menu */
	void SetLastFactoryUsed(UClass* Class);

private:
	static TUniquePtr<FDatasmithUIManager> Instance;

	TSharedPtr<FUICommandList> DatasmithActions;
	TSharedPtr<FUICommandInfo> LastSelectedCommand;

	TMap<UClass*, TSharedPtr<FUICommandInfo>> FactoryClassToUICommandMap;

	void ExtendToolbar();
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);

	const TSharedRef<FUICommandInfo> GetLastSelectedCommand();
};