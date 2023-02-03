// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/MultiBox/SToolBarButtonBlock.h"
#include "Framework/Commands/UICommandInfo.h"
#include "InteractiveToolManager.h"
#include "ToolElementRegistry.h"
#include "ToolMenus.h"
#include "Widgets/SBoxPanel.h"


class SWidget;
class FToolElement;

/** A struct that provides the data for a single tool Palette*/
struct WIDGETREGISTRATION_API FToolPalette : TSharedFromThis<FToolPalette>
{
	FToolPalette(TSharedPtr<FUICommandInfo> InLoadToolPaletteAction,
		const TArray<TSharedPtr< FUICommandInfo >>& InPaletteActions) :
		LoadToolPaletteAction(InLoadToolPaletteAction)
	{
		for (const TSharedPtr< const FUICommandInfo > CommandInfo : InPaletteActions)
		{
			TSharedPtr<FButtonArgs> Button = MakeShareable(new FButtonArgs);
			Button->Command = CommandInfo;
			PaletteActions.Add(Button.ToSharedRef());
		}
	}

	/** The FUICommandInfo button which loads a particular set of toads */
	const TSharedPtr<FUICommandInfo> LoadToolPaletteAction;

	/** The ButtonArgs that has the data to initialize the buttons in the FToolPalette loaded by LoadToolPaletteAction */
	TArray<TSharedRef<FButtonArgs>> PaletteActions;

	/** The FUICommandList associated with this Palette */
	TSharedPtr<FUICommandList>PaletteActionsCommandList;
};

/** An FToolPalette to which you can add and remove actions */
struct WIDGETREGISTRATION_API FEditablePalette : public FToolPalette
{
	FEditablePalette(TSharedPtr<FUICommandInfo> InLoadToolPaletteAction,
		TArray<FString>& InPaletteCommandNameArray,
		TSharedPtr<FUICommandInfo> InAddToPaletteAction,
		TSharedPtr<FUICommandInfo> InRemoveFromPaletteAction);

	/** The FUICommandInfo which adds an action to this palette */
	const TSharedPtr<FUICommandInfo> AddToPaletteAction;
	
	/** The FUICommandInfo which removes an action to this palette */
	const TSharedPtr<FUICommandInfo> RemoveFromPaletteAction;
	
	/** The TArray of Command names that are the current FuiCommandInfo actions in this Palette */
	TArray<FString>& PaletteCommandNameArray;

	/**
	 * Given a reference to a FUICommandInfo, returns whether it is in the current Palette
	 *
	 * @param CommandName the name of the FUICommandInfo queried as to whether it is in the Palette
	 */
	bool IsInPalette(const FName CommandName) const;
};


/**
 * The FToolElementRegistrationArgs which is specified for Toolkits
 */
class WIDGETREGISTRATION_API FToolkitBuilder : public FToolElementRegistrationArgs
{
public:
	FToolkitBuilder(
		FName InToolbarCustomizationName,
		TSharedPtr<FUICommandList> InToolkitCommandList);

	/** A delegate for any clean up that may be needed when the load tool palette FUICommandAction ends/unloads */
	DECLARE_DELEGATE(FOnToolEnded);
	FOnToolEnded OnToolEnded;

	/**
	 * Adds the FToolPalette Palette to this FToolkitBuilder
	 *
	 * @param Palette the FToolPalette being added to this FToolkitBuilder
	 */
	void AddPalette(TSharedPtr<FToolPalette> Palette);

	/**
	 * Adds the FEditablePalette Palette to this FToolkitBuilder
	 *
	 * @param Palette the FEditablePalette being added to this FToolkitBuilder
	 */
	void AddPalette(TSharedPtr<FEditablePalette> Palette);

	/** Cleans up any previously set data in this FToolkitBuilder and reset the members to their initial values  */
	virtual void ResetWidget() override;

	/** Updates the Toolkit. This should be called after any changes to the data of this FToolkitBuilder, and the UI
	 * will be regenerated to reflect it.  */
	virtual void UpdateWidget() override;

	/** Creates the Toolbar for the widget with the FUICommandInfos that load the Palettes */
	TSharedRef<SWidget> CreateToolbarWidget() const;

	/** returns a TSharedPointer to the FToolbarBuilder with the FUICommandInfos that load the Palettes */
	TSharedPtr<FToolBarBuilder> GetLoadPaletteToolbar();
	
	/** returns a TSharedPointer to the FToolbarBuilder with the FUICommandInfos that load the Palettes */
	TSharedPtr<FToolElement> VerticalToolbarElement;

	/** returns a TSharedPointer to the FToolbarBuilder with the FUICommandInfos that load the Palettes */
	TSharedRef<SWidget> GetToolPaletteWidget() const;

	/** returns true is there is an active palette selected, else it returns false */
	bool HasActivePalette() const;

	/**
	 * Returns true if the FUICommandInfo with the name CommandName is the active tool palette,
	 * else it returns false
	 *
	 * @param CommandName the name of the FUICommandInfo we are checking to see if it is the active tool palette
	 */
	ECheckBoxState IsActiveToolPalette(FName CommandName) const;
	
private:

	/** the tool element registry this class will use to register UI tool elements */
	static FToolElementRegistry ToolRegistry;
	
	/** Name of the toolbar this mode uses and can be used by external systems to customize that mode toolbar */
	FName ToolbarCustomizationName;
	TMap<FString, TSharedPtr<FButtonArgs>> PaletteCommandNameToButtonArgsMap;

	/** A TSharedPointer to the FUICommandList for the FUICommandInfos which load a tool palette */
	TSharedPtr<FUICommandList> LoadToolPaletteCommandList;

	/** A TSharedPointer to the FUICommandList for the current mode */
	TSharedPtr<FUICommandList> ToolkitCommandList;

	/** A TArray of FEditablePalettes, kept to update the commands which are on them */
	TArray<TSharedRef<FEditablePalette>> EditablePalettesArray;

	/** The tool palette which is currently loaded/active */
	TSharedPtr<FToolPalette> ActivePalette;

	/** The SVerticalBox which holds the tool palette (the Buttons which load each tool) */
	TSharedPtr<SVerticalBox> ToolPaletteWidget;

	/** The toolbar builder for the toolbar which has the FUICommandInfos which load the various palettes */
	TSharedPtr<FToolBarBuilder> LoadPaletteToolBarBuilder;

	/** the FName of each Load palette command to the FToolbarBuilder for the palette which it loads */
	TMap<FName, TSharedPtr<FToolBarBuilder>> LoadCommandNameToPaletteToolbarBuilderMap;

	/** Updates the Editable Palette with any commands that are on it */
	void UpdateEditablePalette(FEditablePalette& EditablePalette);

	/** Resets the tool palette widget, on which the buttons for the currently chosen toolset are shown */
	void ResetToolPaletteWidget();

	/**
	 * Creates the UI for the Palette specified by the FToolPalette Palette
	 *
	 * @param Palette the FToolPalette for which this method will build the UI 
	 */
	void CreatePalette(TSharedPtr<FToolPalette> Palette);

	/**
	 * Creates the UI for the Palette specified by the FToolPalette Palette if it is not currently active,
	 * else it removes it.
	 *
	 * @param Palette the FToolPalette for which this method will build the UI 
	 */
	void TogglePalette(TSharedPtr<FToolPalette> Palette);

	/**
	 * Adds the FUICommandInfo with the command name CommandNameString to the FEditablePalette Palette if
	 * it is not on Palette, and removes the FUICommandInfo from Palette if it is on the Palette
	 *
	 * @param Palette the FEditablePalette to which the FUICommandInfo will be toggled on/off
	 * @param CommandNameString the name of the FUICommandInfo command which will be toggled on/off of Palette
	 */
	void ToggleCommandInPalette(TSharedRef<FEditablePalette> Palette, FString CommandNameString);

	/**
	 * Retrieves the context menu content for the FUICommandInfo with the command name CommandName
	 *
	 * @param CommandName the command name of the FUICommandInfo to get the context menu widget for
	 * @return the TSharedRef<SWidget> which contains the context menu for the FUICommandInfo with the name CommandName 
	 */
	TSharedRef<SWidget> GetContextMenuContent(const FName CommandName);
};