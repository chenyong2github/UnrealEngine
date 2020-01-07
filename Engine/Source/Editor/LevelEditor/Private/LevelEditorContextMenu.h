// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editor/UnrealEdTypes.h"
#include "LevelEditorMenuContext.h"

class FExtender;
class UToolMenu;
struct FToolMenuContext;
class SLevelEditor;
class SWidget;

/**
 * Context menu construction class 
 */
class FLevelEditorContextMenu
{

public:

	/**
	 * Summons the level viewport context menu
	 * @param	LevelEditor		The level editor using this menu.
	 * @param	ContextType		The context we should use to specialize this menu
	 */
	static void SummonMenu( const TSharedRef< class SLevelEditor >& LevelEditor, ELevelEditorMenuContext ContextType );

	/**
	 * Summons the viewport view option menu
	 * @param LevelEditor		The level editor using this menu.
	 */
	static void SummonViewOptionMenu( const TSharedRef< class SLevelEditor >& LevelEditor, const ELevelViewportType ViewOption );

	/**
	 * Creates a widget for the context menu that can be inserted into a pop-up window
	 *
	 * @param	LevelEditor		The level editor using this menu.
	 * @param	ContextType		The context we should use to specialize this menu
	 * @param	Extender		Allows extension of this menu based on context.
	 * @return	Widget for this context menu
	 */
	static TSharedPtr< SWidget > BuildMenuWidget(TWeakPtr< SLevelEditor > LevelEditor, ELevelEditorMenuContext ContextType, TSharedPtr<FExtender> Extender = TSharedPtr<FExtender>());

	/**
	 * Populates the specified menu builder for the context menu that can be inserted into a pop-up window
	 *
	 * @param	Menu			The menu to fill
	 * @param	LevelEditor		The level editor using this menu.
	 * @param	ContextType		The context we should use to specialize this menu
	 * @param	Extender		Allows extension of this menu based on context.
	 */
	static UToolMenu* GenerateMenu(TWeakPtr< SLevelEditor > LevelEditor, ELevelEditorMenuContext ContextType, TSharedPtr<FExtender> Extender = TSharedPtr<FExtender>());

	/* Adds required information to Context for build menu based on current selection */
	static FName InitMenuContext(FToolMenuContext& Context, TWeakPtr<SLevelEditor> LevelEditor, ELevelEditorMenuContext ContextType);

	/* Returns name of menu to display based on current selection */
	static FName GetContextMenuName(ELevelEditorMenuContext ContextType);

private:

	static void RegisterComponentContextMenu();
	static void RegisterActorContextMenu();
	static void RegisterSceneOutlinerContextMenu();

	/**
	 * Builds the actor group menu
	 *
	 * @param Menu		The menu to add items to.
	 * @param SelectedActorInfo	Information about the selected actors.
	 */
	static void BuildGroupMenu(UToolMenu* Menu, const struct FSelectedActorInfo& SelectedActorInfo);
};
