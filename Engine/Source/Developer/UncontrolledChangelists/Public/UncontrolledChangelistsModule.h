// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UncontrolledChangelistState.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"

/**
 * Interface for talking to Uncontrolled Changelists
 */
class UNCONTROLLEDCHANGELISTS_API FUncontrolledChangelistsModule : public IModuleInterface
{
	typedef TMap<FUncontrolledChangelist, TSharedRef<class FUncontrolledChangelistState, ESPMode::ThreadSafe>> FUncontrolledChangelistsStateCache;

public:
	static constexpr TCHAR* VERSION_NAME = TEXT("version");
	static constexpr TCHAR* CHANGELISTS_NAME = TEXT("changelists");
	static constexpr uint32 VERSION_NUMBER = 0;

public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Check whether uncontrolled changelist module is enabled.
	 */
	bool IsEnabled() const;

	/**
	 * Get the changelist state of each cached Uncontrolled Changelist.
	 */
	 TArray<FUncontrolledChangelistStateRef> GetChangelistStates() const;

	/**
	 * Called when a file has been made writable. Adds the file to the Default Uncontrolled Changelist
	 * @param	InFilename			The file to be added.
	 */
	void OnMakeWritable(const FString& InFilename);


	/**
	 * Updates the status of Uncontrolled Changelists and files.
	 */
	void UpdateStatus();

	/**
	 * Gets a reference to the UncontrolledChangelists module
	 * @return A reference to the UncontrolledChangelists module.
	 */
	static inline FUncontrolledChangelistsModule& Get()
	{
		static FName UncontrolledChangelistsModuleName("UncontrolledChangelists");
		return FModuleManager::LoadModuleChecked<FUncontrolledChangelistsModule>(UncontrolledChangelistsModuleName);
	}

	/**
	 * Delegate callback called when assets are added to AssetRegistry.
	 * @param 	AssetData 	The asset just added.
	 */
	void OnAssetAdded(const struct FAssetData& AssetData);

	/**
	 * Delegate callback called when an asset is loaded.
	 * @param 	InAsset 	The loaded asset.
	 */
	void OnAssetLoaded(UObject* InAsset);

	/**
	 * Delegate callback called when an object is transacted.
	 * @param 	InObject 	        The transacted object.
	 * @param 	InTransactionEvent 	The event representing the transaction.
	 */
	void OnObjectTransacted(UObject* InObject, const class FTransactionObjectEvent& InTransactionEvent);

	/**
	 * Called after an action moving files to an Uncontrolled Changelist (example: drag and drop).
	 * Moves the files to the provided uncontrolled changelist.
	 * @param 	InFilenames 	The files to move.
	 * @param 	InChangelist 	The Uncontrolled Changelist where to move the files.
	 * @return 	returnDesc
	 */
	void OnFilesMovedToUncontrolledChangelist(const TArray<FString> InFilenames, const FUncontrolledChangelist& InChangelist);

private:
	/**
	 * Saves the state of UncontrolledChangelists to Json for persistency.
	 */
	void SaveState() const;
	
	/**
	 * Restores the previously saved state from Json.
	 */
	void LoadState();

	/**
	 * Helper returning the location of the file used for persistency.
	 * @return 	A string containing the filepath.
	 */
	FString GetPersistentFilePath() const;

	/**
	 * Helper returning the package path where an UObject is located.
	 * @param 	InObject 	The object used to locate the package.
	 * @return 	A String containing the filepath of the package.
	 */
	FString GetUObjectPackageFullpath(const UObject* InObject) const;

private:
	FUncontrolledChangelistsStateCache UncontrolledChangelistsStateCache;
	FDelegateHandle OnAssetAddedDelegateHandle;
	FDelegateHandle OnAssetLoadedDelegateHandle;
	FDelegateHandle OnObjectTransactedDelegateHandle;
};
