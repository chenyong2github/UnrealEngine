// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Containers/Ticker.h"
#include "Framework/Docking/TabManager.h"
#include "Toolkits/IToolkit.h"
#include "Subsystems/AssetEditorSubsystem.h"

class IMessageContext;
class FMessageEndpoint;
class IToolkitHost;
struct FAssetEditorRequestOpenAsset;



/**
 * Implements a manager for Editor windows that are currently open and the assets they are editing.
 *
 * @todo toolkit: Merge this functionality into FToolkitManager
 */
class UNREALED_API FAssetEditorManager
	: public FGCObject
{
public:

	/** Get the singleton instance of the asset editor manager */
	static FAssetEditorManager& Get();

	/** Called when the editor is exiting to shutdown the manager */
	void OnExit();

	/** Opens an asset by path */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void OpenEditorForAsset(const FString& AssetPathName);

	/** 
	 * Tries to open an editor for the specified asset.  Returns true if the asset is open in an editor.
	 * If the file is already open in an editor, it will not create another editor window but instead bring it to front
	 */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	bool OpenEditorForAsset(UObject* Asset, const EToolkitMode::Type ToolkitMode = EToolkitMode::Standalone, TSharedPtr<IToolkitHost> OpenedFromLevelEditor = TSharedPtr<IToolkitHost>(), const bool bShowProgressWindow = true);

	/** 
	 * Tries to open an editor for all of the specified assets. 
	 * If any of the assets are already open, it will not create a new editor for them.
	 * If all assets are of the same type, the supporting AssetTypeAction (if it exists) is responsible for the details of how to handle opening multiple assets at once.
	 */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	bool OpenEditorForAssets(const TArray<UObject*>& Assets, const EToolkitMode::Type ToolkitMode = EToolkitMode::Standalone, TSharedPtr<IToolkitHost> OpenedFromLevelEditor = TSharedPtr<IToolkitHost>());

	/** Opens editors for the supplied assets (via OpenEditorForAsset) */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void OpenEditorsForAssets(const TArray<FString>& AssetsToOpen);
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void OpenEditorsForAssets(const TArray<FName>& AssetsToOpen);

	/** Returns the primary editor if one is already open for the specified asset.
	 * If there is one open and bFocusIfOpen is true, that editor will be brought to the foreground and focused if possible.
	 */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	IAssetEditorInstance* FindEditorForAsset(UObject* Asset, bool bFocusIfOpen);

	/** Returns all editors currently opened for the specified asset */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	TArray<IAssetEditorInstance*> FindEditorsForAsset(UObject* Asset);

	/** Close all active editors for the supplied asset and return the number of asset editors that were closed */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	int32 CloseAllEditorsForAsset(UObject* Asset);

	/** Close any editor which is not this one */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void CloseOtherEditors(UObject* Asset, IAssetEditorInstance* OnlyEditor);

	/** Remove given asset from all open editors */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void RemoveAssetFromAllEditors(UObject* Asset);

	/** Event called when CloseAllEditorsForAsset/RemoveAssetFromAllEditors is called */
	DECLARE_EVENT_TwoParams(FAssetEditorManager, FAssetEditorRequestCloseEvent, UObject*, EAssetEditorCloseReason);
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	virtual FAssetEditorRequestCloseEvent& OnAssetEditorRequestClose() { return AssetEditorRequestCloseEvent; }

	/** Get all assets currently being tracked with open editors */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	TArray<UObject*> GetAllEditedAssets();

	/** Notify the asset editor manager that an asset was opened */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void NotifyAssetOpened(UObject* Asset, IAssetEditorInstance* Instance);
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void NotifyAssetsOpened( const TArray< UObject* >& Assets, IAssetEditorInstance* Instance);

	/** Called when an asset has been opened in an editor */
	DECLARE_EVENT_TwoParams(FAssetEditorManager, FOnAssetOpenedInEditorEvent, UObject*, IAssetEditorInstance*);
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	virtual FOnAssetOpenedInEditorEvent& OnAssetOpenedInEditor() { return AssetOpenedInEditorEvent; }

	/** Notify the asset editor manager that an asset editor is done editing an asset */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void NotifyAssetClosed(UObject* Asset, IAssetEditorInstance* Instance);

	/** Notify the asset editor manager that an asset was closed */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void NotifyEditorClosed(IAssetEditorInstance* Instance);

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

	/** Close all open asset editors */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	bool CloseAllAssetEditors();
	
	/** Called when an asset editor is requested to be opened */
	DECLARE_EVENT_OneParam( FAssetEditorManager, FAssetEditorRequestOpenEvent, UObject* );
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	virtual FAssetEditorRequestOpenEvent& OnAssetEditorRequestedOpen() { return AssetEditorRequestOpenEvent; }

	/** Called when an asset editor is actually opened */
	DECLARE_EVENT_OneParam(FAssetEditorManager, FAssetEditorOpenEvent, UObject*);
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	FAssetEditorOpenEvent& OnAssetEditorOpened() { return AssetEditorOpenedEvent; }

	/** Request notification to restore the assets that were previously open when the editor was last closed */
	UE_DEPRECATED(4.24, "Use the matching function on AssetEditorSubsystem instead.")
	void RequestRestorePreviouslyOpenAssets();

private:

	/** Hidden default constructor since the asset editor manager is a singleton. */
	FAssetEditorManager();


private:

	/** Handles FAssetEditorRequestOpenAsset messages. */
	void HandleRequestOpenAssetMessage(const FAssetEditorRequestOpenAsset& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context);

	/** Handles ticks from the ticker. */
	bool HandleTicker( float DeltaTime );

	/** Spawn a notification asking the user if they want to restore their previously open assets */
	void SpawnRestorePreviouslyOpenAssetsNotification(const bool bCleanShutdown, const TArray<FString>& AssetsToOpen);

	/** Handler for when the "Restore Now" button is clicked on the RestorePreviouslyOpenAssets notification */
	void OnConfirmRestorePreviouslyOpenAssets(TArray<FString> AssetsToOpen);

	/** Handler for when the "Don't Restore" button is clicked on the RestorePreviouslyOpenAssets notification */
	void OnCancelRestorePreviouslyOpenAssets();

	/** Saves a list of open asset editors so they can be restored on editor restart */
	void SaveOpenAssetEditors(bool bOnShutdown);

	/** Restore the assets that were previously open when the editor was last closed */
	void RestorePreviouslyOpenAssets();

	/** Handles a package being reloaded */
	void HandlePackageReloaded(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent);

private:

	/** struct used by OpenedEditorTimes map to store editor names and times */
	struct FOpenedEditorTime
	{
		FName EditorName;
		FDateTime OpenedTime;
	};

	/** struct used to track total time and # of invocations during an overall UnrealEd session */
	struct FAssetEditorAnalyticInfo
	{
		FTimespan SumDuration;
		int32 NumTimesOpened;

		FAssetEditorAnalyticInfo()
			: SumDuration(0)
			, NumTimesOpened(0)
		{
		}
	};

	/** Holds the opened assets. */
	TMultiMap<UObject*, IAssetEditorInstance*> OpenedAssets;

	/** Holds the opened editors. */
	TMultiMap<IAssetEditorInstance*, UObject*> OpenedEditors;

	/** Holds the times that editors were opened. */
	TMap<IAssetEditorInstance*, FOpenedEditorTime> OpenedEditorTimes;

	/** Holds the cumulative time editors have been open by type. */
	TMap<FName, FAssetEditorAnalyticInfo> EditorUsageAnalytics;

private:

	/** The singleton instance */
	static FAssetEditorManager* Instance;

	/** Holds the messaging endpoint. */
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	/** Holds a delegate to be invoked when the widget ticks. */
	FTickerDelegate TickDelegate;

	/** Call to request closing editors for an asset */
	FAssetEditorRequestCloseEvent AssetEditorRequestCloseEvent;

	/** Called when an asset has been opened in an editor */
	FOnAssetOpenedInEditorEvent AssetOpenedInEditorEvent;

	/** Multicast delegate executed when an asset editor is requested to be opened */
	FAssetEditorRequestOpenEvent AssetEditorRequestOpenEvent;

	/** Multicast delegate executed when an asset editor is actually opened */
	FAssetEditorOpenEvent AssetEditorOpenedEvent;

	/** Flag whether we are currently shutting down */
	bool bSavingOnShutdown;

	/** Flag whether there has been a request to notify whether to restore previously open assets */
	bool bRequestRestorePreviouslyOpenAssets;

	/** A pointer to the notification used by RestorePreviouslyOpenAssets */
	TWeakPtr<SNotificationItem> RestorePreviouslyOpenAssetsNotificationPtr;
};
