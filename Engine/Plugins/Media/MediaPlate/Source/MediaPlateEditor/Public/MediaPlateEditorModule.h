// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"
#include "TickableEditorObject.h"
#include "UObject/ObjectPtr.h"

class IAssetTools;
class IAssetTypeActions;
class ISlateStyle;
class UMediaPlateComponent;

/** Log category for this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogMediaPlateEditor, Log, All);

class FMediaPlateEditorModule : public IModuleInterface, public FTickableEditorObject
{
public:
	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** FTickableEditorObject interface */
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FMediaPlateEditorModule, STATGROUP_Tickables); }
	
	/**
	 * Get the style used by this module.
	 **/
	TSharedPtr<ISlateStyle> GetStyle() { return Style; }
	
	/**
	 * Call this when a media plate starts playing so we can track it.
	 */
	void MediaPlateStartedPlayback(TObjectPtr<UMediaPlateComponent> MediaPlate);
	
private:

	/** Customization name to avoid reusing staticstruct during shutdown. */
	FName MediaPlateName;
	/** Holds all the media plates that are playing. */
	TArray<TWeakObjectPtr<UMediaPlateComponent>> ActiveMediaPlates;

	/** Holds the plug-ins style set. */
	TSharedPtr<ISlateStyle> Style;
	/** The collection of registered asset type actions. */
	TArray<TSharedRef<IAssetTypeActions>> RegisteredAssetTypeActions;
	/** Handle for our track editor. */
	FDelegateHandle TrackEditorBindingHandle;

	/**
	 * Registers all of our asset tools.
	 */
	void RegisterAssetTools();

	/**
	 * Registers a single asset type action.
	 *
	 * @param AssetTools	The asset tools object to register with.
	 * @param Action		The asset type action to register.
	 */
	void RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action);

	/**
	 * Unregisters all of our asset tools.
	 */
	void UnregisterAssetTools();
};
