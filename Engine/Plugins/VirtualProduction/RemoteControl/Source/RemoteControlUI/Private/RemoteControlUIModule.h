// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IRemoteControlUIModule.h"

#include "CoreMinimal.h"	
#include "Input/Reply.h"
#include "LevelEditor.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"

class SRemoteControlPanel;
class URemoteControlPreset;

/**
 * A Remote Control module that allows exposing objects and properties from the editor.
 */
class FRemoteControlUIModule : public IRemoteControlUIModule
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static FRemoteControlUIModule& Get()
	{
		static const FName ModuleName = "RemoteControlUI";
		return FModuleManager::LoadModuleChecked<FRemoteControlUIModule>(ModuleName);
	}

	//~ Begin IModuleInterface
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	/**
	 * Create a remote control panel to display a given preset.
	 * @param The preset to display.
	 * @return The remote control widget.
	 */
	TSharedRef<SRemoteControlPanel> CreateRemoteControlPanel(URemoteControlPreset* Preset);

	/** 
	 * Get the toolbar extension generators.
	 * Usage: Bind a handler that adds a widget to the out array parameter.
	 */
	virtual FOnGenerateExtensions& GetExtensionGenerators() override { return ExtensionsGenerator; }

private:
	/**
	 * The status of a property.
	 */
	enum class EPropertyExposeStatus : uint8
	{
		Exposed,
		Unexposed,
		Unexposable
	};

private:
	//~ Asset tool actions
	void RegisterAssetTools();
	void UnregisterAssetTools();

	//~ Context menu extenders
	void RegisterContextMenuExtender();
	void UnregisterContextMenuExtender();
	
	//~ Detail row extensions
	void RegisterDetailRowExtension();
	void UnregisterDetailRowExtension();

	/** Handle creating the property row extensions.  */
	void HandleCreatePropertyRowExtension(const FOnGenerateGlobalRowExtensionArgs& InArgs, FOnGenerateGlobalRowExtensionArgs::EWidgetPosition InWidgetPosition, TArray<TSharedRef<SWidget>>& OutExtensions);

	/** Handle getting the icon displayed in the property row extension. */
	const FSlateBrush* OnGetExposedIcon(TSharedPtr<IPropertyHandle> Handle) const;

	/** Handle getting the expose button visibility. */
	EVisibility OnGetExposeButtonVisibility(TSharedPtr<IPropertyHandle> Handle) const;

	/** Handle clicking the expose button. */
	FReply OnToggleExposeProperty(TSharedPtr<IPropertyHandle> Handle);

	/** Returns whether a property is exposed, unexposed or unexposable. */
	EPropertyExposeStatus GetPropertyExposeStatus(const TSharedPtr<IPropertyHandle>& Handle) const;

	/** Handle adding an option to get the object path in the actors' context menu. */
	void AddGetPathOption(class FMenuBuilder& MenuBuilder, AActor* SelectedActor);

	/** Handle adding the menu extender for the actors. */
	TSharedRef<FExtender> ExtendLevelViewportContextMenuForRemoteControl(const TSharedRef<FUICommandList> CommandList, TArray<AActor*> SelectedActors);
private:
	/** The custom actions added to the actor context menu. */
	TSharedPtr<class FRemoteControlPresetActions> RemoteControlPresetActions;

	/** Holds the context menu extender. */
	FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors LevelViewportContextMenuRemoteControlExtender;

	/** Holds the menu extender delegate handle. */
	FDelegateHandle MenuExtenderDelegateHandle;

	/** Holds a weak ptr to the active control panel. */
	TWeakPtr<SRemoteControlPanel> WeakActivePanel;

	/** Delegate called to gather extensions added externally to the panel. */
	FOnGenerateExtensions ExtensionsGenerator;
};
