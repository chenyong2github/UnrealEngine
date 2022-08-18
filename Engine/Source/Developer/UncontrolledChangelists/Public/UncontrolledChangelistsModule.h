// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UncontrolledChangelistState.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "Async/AsyncWork.h"

struct FAssetData;

/**
 * Interface for talking to Uncontrolled Changelists
 */
class UNCONTROLLEDCHANGELISTS_API FUncontrolledChangelistsModule : public IModuleInterface
{
	typedef TMap<FUncontrolledChangelist, FUncontrolledChangelistStateRef> FUncontrolledChangelistsStateCache;

public:	
	static constexpr const TCHAR* VERSION_NAME = TEXT("version");
	static constexpr const TCHAR* CHANGELISTS_NAME = TEXT("changelists");
	static constexpr uint32 VERSION_NUMBER = 0;

	/** Callback called when the state of the Uncontrolled Changelist Module (or any Uncontrolled Changelist) changed */
	DECLARE_MULTICAST_DELEGATE(FOnUncontrolledChangelistModuleChanged);
	FOnUncontrolledChangelistModuleChanged OnUncontrolledChangelistModuleChanged;

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
	  * Called when files have been made writable. Adds the files to the Default Uncontrolled Changelist
	  * @param	InFilenames			The files to be added.
	  * @return True if the files have been added to the Default Uncontrolled Changelist.
	  */
	 bool OnMakeWritable(const TArray<FString>& InFilenames);

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
	 * Gets a message indicating the status of SCC coherence.
	 * @return 	A text representing the status of SCC.
	 */
	FText GetReconcileStatus() const;

	/** Called when "Reconcile assets" button is clicked. Checks for uncontrolled modifications in previously added assets.
	 *	Adds modified files to Uncontrolled Changelists
	 *  @return True if new modifications found
	 */
	bool OnReconcileAssets();

	/**
	 * Delegate callback called when assets are added to AssetRegistry.
	 * @param 	AssetData 	The asset just added.
	 */
	void OnAssetAdded(const FAssetData& AssetData);

	/**
	 * Delegate callback called when an asset is loaded.
	 * @param 	InAsset 	The loaded asset.
	 */
	void OnAssetLoaded(UObject* InAsset);

	/** Called when "Revert files" button is clicked. Reverts modified files and deletes new ones.
	 *  @param	InFilenames		The files to be reverted
	 *	@return true if the provided files were reverted.
	 */
	bool OnRevert(const TArray<FString>& InFilenames);
	
	/**
	 * Delegate callback called before an asset has been written to disk.
	 * @param 	InObject 			The saved object.
	 * @param 	InPreSaveContext 	Interface used to access saved parameters.
	 */
	void OnObjectPreSaved(UObject* InObject, const FObjectPreSaveContext& InPreSaveContext);

	/**
	 * Moves files to an Uncontrolled Changelist.
	 * @param 	InControlledFileStates 		The Controlled files to move.
	 * @param 	InUncontrolledFileStates 	The Uncontrolled files to move.
	 * @param 	InChangelist 				The Uncontrolled Changelist where to move the files.
	 */
	void MoveFilesToUncontrolledChangelist(const TArray<FSourceControlStateRef>& InControlledFileStates, const TArray<FSourceControlStateRef>& InUncontrolledFileStates, const FUncontrolledChangelist& InUncontrolledChangelist);

	/**
	 * Moves files to a Controlled Changelist.
	 * @param 	InUncontrolledFileStates 	The files to move.
	 * @param 	InChangelist 				The Controlled Changelist where to move the files.
	 */
	void MoveFilesToControlledChangelist(const TArray<FSourceControlStateRef>& InUncontrolledFileStates, const FSourceControlChangelistPtr& InChangelist);
	
	/**
	 * Moves files to a Controlled Changelist.
	 * @param 	InUncontrolledFiles 	The files to move.
	 * @param 	InChangelist 			The Controlled Changelist where to move the files.
	 */
	void MoveFilesToControlledChangelist(const TArray<FString>& InUncontrolledFiles, const FSourceControlChangelistPtr& InChangelist);

private:
	/**
	 * Helper use by Startup task and OnAssetLoaded delegate.
	 * @param 	InAssetData 		The asset just added.
	 * @param  	InAddedAssetsCache 	The cache to add the asset to.
	 * @param	bInStartupTask		If true, this asset was added from the startup task.
	 * 
	 */
	void OnAssetAddedInternal(const FAssetData& InAssetData, TSet<FString>& InAddedAssetsCache, bool bInStartupTask);

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

	/**
	 * Displays a Package Dialog warning the user about conflicting packages. 
	 * @param 	InPackageConflicts 	The conflicting packages to display.
	 * @return 	True if the user decided to proceed. False if they cancelled.
	 */
	bool ShowConflictDialog(TArray<UPackage*> InPackageConflicts);

	/** Called when a state changed either in the module or an Uncontrolled Changelist. */
	void OnStateChanged();

	/** Removes from asset caches files already present in Uncontrolled Changelists */
	void CleanAssetsCaches();

	/**
	 * Try to add the provided filenames to the default Uncontrolled Changelist.
	 * @param 	InFilenames 	The files to add.
	 * @param 	InCheckFlags 	The required checks to check the file against before adding.
	 * @return 	True file have been added.
	 */
	bool AddFilesToDefaultUncontrolledChangelist(const TArray<FString>& InFilenames, const FUncontrolledChangelistState::ECheckFlags InCheckFlags);

private:
	class FStartupTask : public FNonAbandonableTask
	{
	public:
		
		FStartupTask(FUncontrolledChangelistsModule* InOwner)
			: Owner(InOwner)
		{}
		~FStartupTask() {}
		
		TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FStartupTask, STATGROUP_ThreadPoolAsyncTasks);
		}
		
		void DoWork();
		const TSet<FString>& GetAddedAssetsCache() const { return AddedAssetsCache; }
	
	private:
		FUncontrolledChangelistsModule* Owner;
		TSet<FString> AddedAssetsCache;
	};

	TUniquePtr<FAsyncTask<FStartupTask>> StartupTask;
	FUncontrolledChangelistsStateCache	UncontrolledChangelistsStateCache;
	TSet<FString>						AddedAssetsCache;
	FDelegateHandle						OnAssetAddedDelegateHandle;
	FDelegateHandle						OnObjectPreSavedDelegateHandle;
	bool								bIsEnabled = false;
};
