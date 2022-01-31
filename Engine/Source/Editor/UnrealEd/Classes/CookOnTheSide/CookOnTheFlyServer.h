// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Containers/ArrayView.h"
#include "Containers/RingBuffer.h"
#include "CookOnTheSide/CookLog.h"
#include "Engine/ICookInfo.h"
#include "INetworkFileSystemModule.h"
#include "IPlatformFileSandboxWrapper.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/UniquePtr.h"
#include "TickableEditorObject.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtr.h"

#include "CookOnTheFlyServer.generated.h"

class FAssetRegistryGenerator;
class FAsyncIODelete;
class FDiffModeCookServerUtils;
class FReferenceCollector;
class FSavePackageContext;
class IAssetRegistry;
class ICookedPackageWriter;
class IPlugin;
class ITargetPlatform;
struct FPropertyChangedEvent;

enum class ECookInitializationFlags
{
	None =										0x00000000, // No flags
	//unused =									0x00000001, 
	Iterative =									0x00000002, // use iterative cooking (previous cooks will not be cleaned unless detected out of date, experimental)
	SkipEditorContent =							0x00000004, // do not cook any content in the content\editor directory
	Unversioned =								0x00000008, // save the cooked packages without a version number
	AutoTick =									0x00000010, // enable ticking (only works in the editor)
	AsyncSave =									0x00000020, // save packages async
	// unused =									0x00000040,
	IncludeServerMaps =							0x00000080, // should we include the server maps when cooking
	UseSerializationForPackageDependencies =	0x00000100, // should we use the serialization code path for generating package dependencies (old method will be deprecated)
	BuildDDCInBackground =						0x00000200, // build ddc content in background while the editor is running (only valid for modes which are in editor IsCookingInEditor())
	GeneratedAssetRegistry =					0x00000400, // have we generated asset registry yet
	OutputVerboseCookerWarnings =				0x00000800, // output additional cooker warnings about content issues
	EnablePartialGC =							0x00001000, // mark up with an object flag objects which are in packages which we are about to use or in the middle of using, this means we can gc more often but only gc stuff which we have finished with
	TestCook =									0x00002000, // test the cooker garbage collection process and cooking (cooker will never end just keep testing).
	//unused =									0x00004000,
	LogDebugInfo =								0x00008000, // enables additional debug log information
	IterateSharedBuild =						0x00010000, // iterate from a build in the SharedIterativeBuild directory 
	IgnoreIniSettingsOutOfDate =				0x00020000, // if the inisettings say the cook is out of date keep using the previously cooked build
	IgnoreScriptPackagesOutOfDate =				0x00040000, // for incremental cooking, ignore script package changes
};
ENUM_CLASS_FLAGS(ECookInitializationFlags);

enum class ECookByTheBookOptions
{
	None =								0x00000000, // no flags
	CookAll	=							0x00000001, // cook all maps and content in the content directory
	MapsOnly =							0x00000002, // cook only maps
	NoDevContent =						0x00000004, // don't include dev content
	ForceDisableCompressed =			0x00000010, // force compression to be disabled even if the cooker was initialized with it enabled
	ForceEnableCompressed =				0x00000020, // force compression to be on even if the cooker was initialized with it disabled
	ForceDisableSaveGlobalShaders =		0x00000040, // force global shaders to not be saved (used if cooking multiple times for the same platform and we know we are up todate)
	NoGameAlwaysCookPackages =			0x00000080, // don't include the packages specified by the game in the cook (this cook will probably be missing content unless you know what you are doing)
	NoAlwaysCookMaps =					0x00000100, // don't include always cook maps (this cook will probably be missing content unless you know what you are doing)
	NoDefaultMaps =						0x00000200, // don't include default cook maps (this cook will probably be missing content unless you know what you are doing)
	NoInputPackages =					0x00000800, // don't include slate content (this cook will probably be missing content unless you know what you are doing)
	SkipSoftReferences =				0x00001000, // Don't follow soft references when cooking. Usually not viable for a real cook and the results probably wont load properly, but can be useful for debugging
	SkipHardReferences =				0x00002000, // Don't follow hard references when cooking. Not viable for a real cook, only useful for debugging
	FullLoadAndSave =					0x00004000, // Load all packages into memory and save them all at once in one tick for speed reasons. This requires a lot of RAM for large games.
	CookAgainstFixedBase =				0x00010000, // If cooking DLC, assume that the base content can not be modified. 
	DlcLoadMainAssetRegistry =			0x00020000, // If cooking DLC, populate the main game asset registry
	ZenStore =							0x00040000, // Store cooked data in Zen Store

	// Deprecated flags
	NoSlatePackages UE_DEPRECATED(5.0, "The [UI]ContentDirectories is deprecated. You may use DirectoriesToAlwaysCook in your project settings instead.") = 0x00000400, // don't include slate content
	DisableUnsolicitedPackages UE_DEPRECATED(4.26, "Use SkipSoftReferences and/or SkipHardReferences instead") = SkipSoftReferences | SkipHardReferences,
};
ENUM_CLASS_FLAGS(ECookByTheBookOptions);


UENUM()
namespace ECookMode
{
	enum Type
	{
		/** Default mode, handles requests from network. */
		CookOnTheFly,
		/** Cook on the side. */
		CookOnTheFlyFromTheEditor,
		/** Precook all resources while in the editor. */
		CookByTheBookFromTheEditor,
		/** Cooking by the book (not in the editor). */
		CookByTheBook,
	};
}

UENUM()
enum class ECookTickFlags : uint8
{
	None =									0x00000000, /* no flags */
	MarkupInUsePackages =					0x00000001, /** Markup packages for partial gc */
	HideProgressDisplay =					0x00000002, /** Hides the progress report */
};
ENUM_CLASS_FLAGS(ECookTickFlags);

namespace UE::Cook
{
	class FBuildDefinitions;
	class FExternalRequests;
	class FRequestCluster;
	class FSaveCookedPackageContext;
	class ICookOnTheFlyRequestManager;
	struct FConstructPackageData;
	struct FCookerTimer;
	struct FCookSavePackageContext;
	struct FGeneratorPackage;
	struct FPackageData;
	struct FPackageDatas;
	struct FPackageTracker;
	struct FPendingCookedPlatformData;
	struct FPlatformManager;
	struct FPopulatePackageContext;
	struct FTickStackData;
}
namespace UE::Cook::Private
{
	class FRegisteredCookPackageSplitter;
}

UCLASS()
class UNREALED_API UCookOnTheFlyServer : public UObject, public FTickableEditorObject, public FExec, public UE::Cook::ICookInfo
{
	GENERATED_BODY()

	UCookOnTheFlyServer(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	UCookOnTheFlyServer(FVTableHelper& Helper); // Declare the FVTableHelper constructor manually so that we can forward-declare-only TUniquePtrs in the header without getting compile error in generated cpp

public:
	struct FCookByTheBookOptions;

	//////////////////////////////////////////////////////////////////////////
	// Cook on the fly startup options
	struct FCookOnTheFlyOptions
	{
		/** Wether the network file server or the I/O store connection server should bind to any port */
		bool bBindAnyPort = false;
		/** Whether to save the cooked output to the Zen storage server. */
		bool bZenStore = false;
		/** Target platforms */
		TArray<ITargetPlatform*> TargetPlatforms;
	};

private:
	//////////////////////////////////////////////////////////////////////////
	// Cook on the fly server interface adapter
	class FCookOnTheFlyServerInterface;
	friend class FCookOnTheFlyServerInterface;
	TUniquePtr<FCookOnTheFlyServerInterface> CookOnTheFlyServerInterface;

	/** Current cook mode the cook on the fly server is running in */
	ECookMode::Type CurrentCookMode = ECookMode::CookOnTheFly;
	/** Directory to output to instead of the default should be empty in the case of DLC cooking */ 
	FString OutputDirectoryOverride;

	FCookByTheBookOptions* CookByTheBookOptions = nullptr;
	TUniquePtr<UE::Cook::FPlatformManager> PlatformManager;

	//////////////////////////////////////////////////////////////////////////
	// Cook on the fly options

	FCookOnTheFlyOptions CookOnTheFlyOptions;
	/** Cook on the fly server uses the NetworkFileServer */
	TArray<class INetworkFileServer*> NetworkFileServers;
	FOnFileModifiedDelegate FileModifiedDelegate;
	TUniquePtr<UE::Cook::ICookOnTheFlyRequestManager> CookOnTheFlyRequestManager;

	//////////////////////////////////////////////////////////////////////////
	// General cook options

	/** Number of packages to load before performing a garbage collect. Set to 0 to never GC based on number of loaded packages */
	uint32 PackagesPerGC;
	/** Amount of time that is allowed to be idle before forcing a garbage collect. Set to 0 to never force GC due to idle time */
	double IdleTimeToGC;
	// Memory Limits for when to Collect Garbage
	uint64 MemoryMaxUsedVirtual;
	uint64 MemoryMaxUsedPhysical;
	uint64 MemoryMinFreeVirtual;
	uint64 MemoryMinFreePhysical;
	/** Max number of packages to save before we partial gc */
	int32 MaxNumPackagesBeforePartialGC;
	/** Max number of concurrent shader jobs reducing this too low will increase cook time */
	int32 MaxConcurrentShaderJobs;
	/** Min number of free UObject indices before the cooker should partial gc */
	int32 MinFreeUObjectIndicesBeforeGC;
	/** The maximum number of packages that should be preloaded at once. Once this is full, packages in LoadPrepare will remain unpreloaded in LoadPrepare until the existing preloaded packages exit {LoadPrepare,LoadReady} state. */
	uint32 MaxPreloadAllocated;
	/**
	 * A knob to tune performance - How many packages should be present in the SaveQueue before we start processing the SaveQueue. If number of files is less, we will look for other work to do, and save packages only if all other work is done.
	 * This allows us to have enough population in the SaveQueue to get benefit from the asynchronous work done on packages in the SaveQueue.
	 */
	uint32 DesiredSaveQueueLength;
	/**
	 * A knob to tune performance - How many packages should be present in the LoadPrepare+LoadReady queues before we start processing the LoadQueue. If number of files is less, we will look for other work to do, and load packages only if all other work is done.
	 * This allows us to have enough population in the LoadPrepareQueue to get benefit from the asynchronous work done on packages in the LoadPrepareQueue.
	 */
	uint32 DesiredLoadQueueLength;
	/** A knob to tune performance - how many packages to pull off in each call to PumpRequests. */
	uint32 RequestBatchSize;
	/** A knob to tune performance - how many packages to load in each call to PumpLoads. */
	int32 LoadBatchSize;

	ECookInitializationFlags CookFlags = ECookInitializationFlags::None;
	TUniquePtr<class FSandboxPlatformFile> SandboxFile;
	TUniquePtr<FAsyncIODelete> AsyncIODelete; // Helper for deleting the old cook directory asynchronously
	FString DefaultAsyncIODeletePlatformName; // Default platform name to be used by GetAsyncIODelete()
	bool bIsSavingPackage = false; // used to stop recursive mark package dirty functions
	/** Set to true during CookOnTheFly if a plugin is calling RequestPackage and we should therefore not make assumptions about when platforms are done cooking */
	bool bCookOnTheFlyExternalRequests = false;

	TMap<FName, int32> MaxAsyncCacheForType; // max number of objects of a specific type which are allowed to async cache at once
	mutable TMap<FName, int32> CurrentAsyncCacheForType; // max number of objects of a specific type which are allowed to async cache at once

	/** List of additional plugin directories to remap into the sandbox as needed */
	TArray<TSharedRef<IPlugin> > PluginsToRemap;

	TMultiMap<UClass*, UE::Cook::Private::FRegisteredCookPackageSplitter*> RegisteredSplitDataClasses;

	//////////////////////////////////////////////////////////////////////////
	// precaching system
	//
	// this system precaches materials and textures before we have considered the object 
	// as requiring save so as to utilize the system when it's idle
	
	TArray<FWeakObjectPtr> CachedMaterialsToCacheArray;
	TArray<FWeakObjectPtr> CachedTexturesToCacheArray;
	int32 LastUpdateTick = 0;
	int32 MaxPrecacheShaderJobs = 0;
	void TickPrecacheObjectsForPlatforms(const float TimeSlice, const TArray<const ITargetPlatform*>& TargetPlatform);

	//////////////////////////////////////////////////////////////////////////

	int32 LastCookPendingCount = 0;
	int32 LastCookedPackagesCount = 0;
	double LastProgressDisplayTime = 0;
	double LastDiagnosticsDisplayTime = 0;

	/** Get dependencies for package */
	const TArray<FName>& GetFullPackageDependencies(const FName& PackageName) const;
	mutable TMap<FName, TArray<FName>> CachedFullPackageDependencies;

	/** Cached copy of asset registry */
	IAssetRegistry* AssetRegistry = nullptr;

	/** Map of platform name to scl.csv files we saved out. */
	TMap<FName, TArray<FString>> OutSCLCSVPaths;

	/** List of filenames that may be out of date in the asset registry */
	TSet<FName> ModifiedAssetFilenames;

	//////////////////////////////////////////////////////////////////////////
	// iterative ini settings checking
	// growing list of ini settings which are accessed over the course of the cook

	// tmap of the Config name, Section name, Key name, to the value
	typedef TMap<FName, TMap<FName, TMap<FName, TArray<FString>>>> FIniSettingContainer;

	mutable FCriticalSection ConfigFileCS;
	mutable bool IniSettingRecurse = false;
	mutable FIniSettingContainer AccessedIniStrings;
	TArray<const FConfigFile*> OpenConfigFiles;
	TArray<FString> ConfigSettingDenyList;
	void OnFConfigDeleted(const FConfigFile* Config);
	void OnFConfigCreated(const FConfigFile* Config);

	void ProcessAccessedIniSettings(const FConfigFile* Config, FIniSettingContainer& AccessedIniStrings) const;

	/**
	* OnTargetPlatformChangedSupportedFormats
	* called when target platform changes the return value of supports shader formats 
	* used to reset the cached cooked shaders
	*/
	void OnTargetPlatformChangedSupportedFormats(const ITargetPlatform* TargetPlatform);

	/* Version of AddCookOnTheFlyPlatform that takes the Platform name instead of an ITargetPlatform*.  Returns the Platform if found */
	const ITargetPlatform* AddCookOnTheFlyPlatform(const FName& PlatformName);
	/* Internal helper for AddCookOnTheFlyPlatform.  Initializing Platforms must be done on the tickloop thread; Platform data is read only on other threads */
	void AddCookOnTheFlyPlatformFromGameThread(ITargetPlatform* TargetPlatform);

	/* Callback to recalculate all ITargetPlatform* pointers when they change due to modules reloading */
	void OnTargetPlatformsInvalidated();

	/* Update polled fields used by CookOnTheFly's network request handlers */
	void TickNetwork();

	/** Execute operations that need to be done after each Scheduler task, such as checking for new external requests. */
	void TickCookStatus(UE::Cook::FTickStackData& StackData);
	void SetSaveBusy(bool bInSaveBusy);
	void SetLoadBusy(bool bInLoadBusy);
	void UpdateDisplay(ECookTickFlags CookFlags, bool bForceDisplay);
	enum class ECookAction
	{
		Done,			// The cook is complete; no requests remain in any non-idle state
		Request,		// Process the RequestQueue
		Load,			// Process the LoadQueue
		LoadLimited,	// Process the LoadQueue, stopping when loadqueuelength reaches the desired population level
		Save,			// Process the SaveQueue
		SaveLimited,	// Process the SaveQueue, stopping when savequeuelength reaches the desired population level
		YieldTick,		// Progress is blocked by an async result. Temporarily exit TickCookOnTheSide.
		Cancel,			// Cancel the current CookByTheBook
	};
	/** Inspect all tasks the scheduler could do and return which one it should do. */
	ECookAction DecideNextCookAction(UE::Cook::FTickStackData& StackData);
	/** Execute any existing external callbacks and push any existing external cook requests into new RequestClusters. */
	void PumpExternalRequests(const UE::Cook::FCookerTimer& CookerTimer);
	/** Send the PackageData back to request state to create a request cluster, if it has not yet been explored. */
	bool TryCreateRequestCluster(UE::Cook::FPackageData& PackageData);
	/** Inspect the next package in the RequestQueue and push it on to its next state. Report the number of PackageDatas that were pushed to load. */
	void PumpRequests(UE::Cook::FTickStackData& StackData, int32& OutNumPushed);

	/** Load all packages in the LoadQueue until it's time to break. Report the number of loads that were pushed to save. */
	void PumpLoads(UE::Cook::FTickStackData& StackData, uint32 DesiredQueueLength, int32& OutNumPushed, bool& bOutBusy);
	/** Move packages from LoadPrepare's entry queue into the PreloadingQueue until we run out of Preload slots. */
	void PumpPreloadStarts();
	/** Move preload-completed packages from LoadPrepare into state LoadReady until we find the first one that is not yet finished preloading. */
	void PumpPreloadCompletes();
	/** Load the given PackageData that was in the load queue and send it on to its next state. Report the number of PackageDatas that were pushed to save (0 or 1) */
	void LoadPackageInQueue(UE::Cook::FPackageData& PackageData, uint32& ResultFlags, int32& OutNumPushed);
	/** Mark that the given PackageData failed to load and return it to idle. */
	void RejectPackageToLoad(UE::Cook::FPackageData& PackageData, const TCHAR* Reason);

	/** Try to save all packages in the SaveQueue until it's time to break. Report the number of requests that were completed (either skipped or successfully saved or failed to save) */
	void PumpSaves(UE::Cook::FTickStackData& StackData, uint32 DesiredQueueLength, int32& OutNumPushed, bool& bOutBusy);
	/**
	 * Inspect the given package and queue it for saving if necessary.
	 *
	 * @param Package				The package to be considered for saving.
	 * @param bOutWasInProgress		Report whether the package was already in progress.
	 * @return						Returns the PackageData for this queued package.
	 */
	UE::Cook::FPackageData* QueueDiscoveredPackage(UPackage* Package, UE::Cook::FInstigator&& Instigator,
		bool* bOutWasInProgress = nullptr);
	/**
	 * Inspect the given package and queue it for saving if necessary.
	 *
	 * @param PackageData			The PackageData to be considered for saving.
	 * @param bLoadReady			If true send on to LoadReady, otherwise add it at the Request stage.
	 */
	void QueueDiscoveredPackageData(UE::Cook::FPackageData& PackageData, UE::Cook::FInstigator&& Instigator,
		bool bLoadReady = false);

	/** Check whether the package filter has change, and if so call QueueDiscoveredPackage again on each existing package. */
	void UpdatePackageFilter();

	/**
	 * Remove all request data about the given platform from any data in the CookOnTheFlyServer.
	 * Called in response to a platform being removed from the list of session platforms e.g. because it has not been recently used by CookOnTheFly.
	 * Does not modify Cooked platforms.
	 */
	void OnRemoveSessionPlatform(const ITargetPlatform* TargetPlatform);

public:

	enum ECookOnTheSideResult
	{
		COSR_None					= 0x00000000,
		COSR_CookedMap				= 0x00000001,
		COSR_CookedPackage			= 0x00000002,
		COSR_ErrorLoadingPackage	= 0x00000004,
		COSR_RequiresGC				= 0x00000008,
		COSR_WaitingOnCache			= 0x00000010,
		COSR_MarkedUpKeepPackages	= 0x00000040
	};


	virtual ~UCookOnTheFlyServer();

	/**
	* FTickableEditorObject interface used by cook on the side
	*/
	TStatId GetStatId() const override;
	void Tick(float DeltaTime) override;
	bool IsTickable() const override;
	ECookMode::Type GetCookMode() const { return CurrentCookMode; }


	/**
	* FExec interface used in the editor
	*/
	virtual bool Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;

	/**
	* ICookInfo interface
	*/
	virtual UE::Cook::FInstigator GetInstigator(FName PackageName) override;
	virtual TArray<UE::Cook::FInstigator> GetInstigatorChain(FName PackageName) override;

	/**
	 * Dumps cooking stats to the log
	 *  run from the exec command "Cook stats"
	 */
	void DumpStats();

	/**
	* Initialize the CookServer so that either CookOnTheFly can be called or Cook on the side can be started and ticked
	*/
	void Initialize( ECookMode::Type DesiredCookMode, ECookInitializationFlags InCookInitializationFlags, const FString& OutputDirectoryOverride = FString() );

	/**
	* Initialize cook on the fly server
	*
	* @param InCookOnTheFlyOptions Cook on the fly startup options
	*
	* @return true on success, false otherwise.
	*/
	bool StartCookOnTheFly(FCookOnTheFlyOptions InCookOnTheFlyOptions); 

	/**
	* Broadcast our the fileserver presence on the network
	*/
	bool BroadcastFileserverPresence( const FGuid &InstanceId );
	
	/** 
	* Shutdown cook on the fly server
	*/
	void ShutdownCookOnTheFly();

	struct FCookByTheBookStartupOptions
	{
	public:
		TArray<ITargetPlatform*> TargetPlatforms;
		TArray<FString> CookMaps;
		TArray<FString> CookDirectories;
		TArray<FString> NeverCookDirectories;
		TArray<FString> CookCultures; 
		TArray<FString> IniMapSections;
		TArray<FString> CookPackages; // list of packages we should cook, used to specify specific packages to cook
		ECookByTheBookOptions CookOptions = ECookByTheBookOptions::None;
		FString DLCName;
		FString CreateReleaseVersion;
		FString BasedOnReleaseVersion;
		bool bGenerateStreamingInstallManifests = false;
		bool bGenerateDependenciesForMaps = false;
		bool bErrorOnEngineContentUse = false; // this is a flag for dlc, will cause the cooker to error if the dlc references engine content
	};

	/**
	* Start a cook by the book session
	* Cook on the fly can't run at the same time as cook by the book
	*/
	void StartCookByTheBook( const FCookByTheBookStartupOptions& CookByTheBookStartupOptions );

	/**
	* Queue a cook by the book cancel (you might want to do this instead of calling cancel directly so that you don't have to be in the game thread when canceling
	*/
	void QueueCancelCookByTheBook();

	/**
	* Cancel the currently running cook by the book (needs to be called from the game thread)
	*/
	void CancelCookByTheBook();

	bool IsCookByTheBookRunning() const;
	/** Report whether the CookOnTheFlyServer is in a cook session, either CookByTheBook or CookOnTheFly. Used to restrict operations when cooking and reduce cputime when not cooking. */
	bool IsInSession() const;

	/**
	* Get any packages which are in memory, these were probably required to be loaded because of the current package we are cooking, so we should probably cook them also
	*/
	UE_DEPRECATED(4.26, "Unsolicited packages are now added directly to the savequeue and are not marked as unsolicited")
	TArray<UPackage*> GetUnsolicitedPackages(const TArray<const ITargetPlatform*>& TargetPlatforms) const;

	/** Execute class-specific special case cook postloads and reference discovery on a given package. */
	void PostLoadPackageFixup(UPackage* Package, TArray<FName>* OutDiscoveredPackageNames,
		TMap<FName, UE::Cook::FInstigator>* OutInstigators);

	/**
	* Handles cook package requests until there are no more requests, then returns
	*
	* @param  CookFlags output of the things that might have happened in the cook on the side
	* 
	* @return returns ECookOnTheSideResult
	*/
	uint32 TickCookOnTheSide( const float TimeSlice, uint32 &CookedPackagesCount, ECookTickFlags TickFlags = ECookTickFlags::None );

	/**
	* Clear all the previously cooked data all cook requests from now on will be considered recook requests
	*/
	void ClearAllCookedData();

	/** Demote all PackageDatas in any queue back to Idle, and eliminate any pending requests. Used when canceling a cook. */
	void CancelAllQueues();


	/**
	* Clear any cached cooked platform data for a platform
	*  call ClearCachedCookedPlatformData on all UObjects
	* @param TargetPlatform platform to clear all the cached data for
	*/
	void ClearCachedCookedPlatformDataForPlatform(const ITargetPlatform* TargetPlatform);

	UE_DEPRECATED(4.25, "Use version that takes const ITargetPlatform* instead")
	void ClearCachedCookedPlatformDataForPlatform(const FName& PlatformName);


	/**
	* Clear all the previously cooked data for the platform passed in 
	* 
	* @param TargetPlatform the platform to clear the cooked packages for
	*/
	void ClearPlatformCookedData(const ITargetPlatform* TargetPlatform);

	UE_DEPRECATED(4.25, "Use version that takes const ITargetPlatform* instead")
	void ClearPlatformCookedData(const FString& PlatformName);

	/**
	 * Clear platforms' explored flags for all PackageDatas and optionally clear the cookresult flags.
	 *
	 * @param TargetPlatforms List of pairs with targetplatforms to reset and bool bResetResults indicating whether
	 *        the platform should clear bCookAttempted in addition to clearing bExplored.
	 */
	void ResetCook(TConstArrayView<TPair<const ITargetPlatform*, bool>> TargetPlatforms);

	/**
	* Recompile any global shader changes 
	* if any are detected then clear the cooked platform data so that they can be rebuilt
	* 
	* @return return true if shaders were recompiled
	*/
	bool RecompileChangedShaders(const TArray<const ITargetPlatform*>& TargetPlatforms);

	UE_DEPRECATED(4.25, "Use version that takes const ITargetPlatform* instead")
	bool RecompileChangedShaders(const TArray<FName>& TargetPlatformNames);


	/**
	* Force stop whatever pending cook requests are going on and clear all the cooked data
	* Note cook on the side / cook on the fly clients may not be able to recover from this if they are waiting on a cook request to complete
	*/
	void StopAndClearCookedData();

	/** 
	* Process any shader recompile requests
	*/
	void TickRecompileShaderRequests();

	bool HasRecompileShaderRequests() const;

	UE_DEPRECATED(4.26, "Use HasRemainingWork instead")
	bool HasCookRequests() const
	{
		return HasRemainingWork();
	}
	/** Return whether TickCookOnTheSide needs to take any action for the current session. If not, the session is done. Used for external managers of the cooker to know when to tick it. */
	bool HasRemainingWork() const;
	void WaitForRequests(int TimeoutMs);

	uint32 NumConnections() const;

	/**
	* Is this cooker running in the editor
	*
	* @return true if we are running in the editor
	*/
	bool IsCookingInEditor() const;

	/**
	* Is this cooker running in real time mode (where it needs to respect the timeslice) 
	* 
	* @return returns true if this cooker is running in realtime mode like in the editor
	*/
	bool IsRealtimeMode() const;

	/**
	* Helper function returns if we are in any cook by the book mode
	*
	* @return if the cook mode is a cook by the book mode
	*/
	bool IsCookByTheBookMode() const;

	bool IsUsingShaderCodeLibrary() const;

	bool IsUsingZenStore() const;

	/**
	* Helper function returns if we are in any cook on the fly mode
	*
	* @return if the cook mode is a cook on the fly mode
	*/
	bool IsCookOnTheFlyMode() const;


	virtual void BeginDestroy() override;

	/** Returns the configured number of packages to process before GC */
	uint32 GetPackagesPerGC() const;

	/** Returns the configured number of packages to process before partial GC */
	uint32 GetPackagesPerPartialGC() const;

	/** Returns the configured amount of idle time before forcing a GC */
	double GetIdleTimeToGC() const;

	/** Returns the configured amount of memory allowed before forcing a GC */
	UE_DEPRECATED(4.26, "Use HasExceededMaxMemory instead")
	uint64 GetMaxMemoryAllowance() const;

	/** Mark package as keep around for the cooker (don't GC) */
	UE_DEPRECATED(4.25, "UCookOnTheFlyServer now uses FGCObject to interact with garbage collection")
	void MarkGCPackagesToKeepForCooker()
	{
	}

	bool HasExceededMaxMemory() const;

	/**
	* RequestPackage to be cooked
	*
	* @param StandardFileName FileName of the package in standard format as returned by FPaths::MakeStandardFilename
	* @param TargetPlatforms The TargetPlatforms we want this package cooked for
	* @param bForceFrontOfQueue should we put this package in the front of the cook queue (next to be processed) or at the end
	*/
	bool RequestPackage(const FName& StandardFileName, const TArrayView<const ITargetPlatform* const>& TargetPlatforms, const bool bForceFrontOfQueue);

	UE_DEPRECATED(4.25, "Use Version that takes TArray<const ITargetPlatform*> instead")
	bool RequestPackage(const FName& StandardFileName, const TArrayView<const FName>& TargetPlatformNames, const bool bForceFrontOfQueue);

	/**
	* RequestPackage to be cooked
	* This function can only be called while the cooker is in cook by the book mode
	*
	* @param StandardPackageFName name of the package in standard format as returned by FPaths::MakeStandardFilename
	* @param bForceFrontOfQueue should we put this package in the front of the cook queue (next to be processed) or at the end
	*/
	bool RequestPackage(const FName& StandardPackageFName, const bool bForceFrontOfQueue);


	/**
	* Callbacks from editor 
	*/

	void OnObjectModified( UObject *ObjectMoving );
	void OnObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);
	void OnObjectUpdated( UObject *Object );
	void OnObjectSaved( UObject *ObjectSaved, FObjectPreSaveContext SaveContext );

	/**
	* Marks a package as dirty for cook
	* causes package to be recooked on next request (and all dependent packages which are currently cooked)
	*/
	void MarkPackageDirtyForCooker( UPackage *Package, bool bAllowInSession = false );

	/** Helper function for MarkPackageDirtyForCooker. Executes the MarkPackageDirtyForCooker operations that are only safe to execute from the scheduler's designated point for handling external requests. */
	void MarkPackageDirtyForCookerFromSchedulerThread(const FName& PackageName);

	/**
	* MaybeMarkPackageAsAlreadyLoaded
	* Mark the package as already loaded if we have already cooked the package for all requested target platforms
	* this hints to the objects on load that we don't need to load all our bulk data
	* 
	* @param Package to mark as not requiring reload
	*/
	void MaybeMarkPackageAsAlreadyLoaded(UPackage* Package);

	/**
	* Callbacks from UObject globals
	*/
	void PreGarbageCollect();
	void CookerAddReferencedObjects(FReferenceCollector& Ar);
	void PostGarbageCollect();

private:

	//////////////////////////////////////////////////////////////////////////
	// cook by the book specific functions
	/**
	* Collect all the files which need to be cooked for a cook by the book session
	*/
	void CollectFilesToCook(TArray<FName>& FilesInPath, TMap<FName, UE::Cook::FInstigator>& Instigators,
		const TArray<FString>& CookMaps, const TArray<FString>& CookDirectories, 
		const TArray<FString>& IniMapSections, ECookByTheBookOptions FilesToCookFlags,
		const TArrayView<const ITargetPlatform* const>& TargetPlatforms,
		const TMap<FName, TArray<FName>>& GameDefaultObjects);

	/**
	* Gets all game default objects for all platforms.
	*/
	static void GetGameDefaultObjects(const TArray<ITargetPlatform*>& TargetPlatforms, TMap<FName, TArray<FName>>& GameDefaultObjectsOut);

	/* Collect filespackages that should not be cooked from ini settings and commandline. Does not include checking UAssetManager, which has to be queried later */
	TArray<FName> GetNeverCookPackageFileNames(TArrayView<const FString> ExtraNeverCookDirectories = TArrayView<const FString>());


	/**
	* AddFileToCook add file to cook list 
	*/
	void AddFileToCook( TArray<FName>& InOutFilesToCook, TMap<FName, UE::Cook::FInstigator>& InOutInstigators,
		const FString &InFilename, const UE::Cook::FInstigator& Instigator) const;

	/**
	* Invokes the necessary FShaderCodeLibrary functions to start cooking the shader code library.
	*/
	void InitShaderCodeLibrary(void);
    
	/**
	 * Opens Global shader library. Global shaderlib isn't split into chunks nor associated with the assets, so it a special case
	 */
	void OpenGlobalShaderLibrary();

	/**
	 * Saves Global shader library. Global shaderlib isn't split into chunks nor associated with the assets, so it a special case
	 */
	void SaveAndCloseGlobalShaderLibrary();

	/**
	 * Invokes the necessary FShaderCodeLibrary functions to open a named code library.
	 */
	void OpenShaderLibrary(FString const& Name);
    
	/**
	 * Invokes the necessary FShaderCodeLibrary functions to save and close a named code library.
	 */
	void SaveShaderLibrary(const ITargetPlatform* TargetPlatform, FString const& Name);

	/**
	* Calls the ShaderPipelineCacheToolsCommandlet to build a upipelinecache file from the stable pipeline cache (.spc, in the past textual .stablepc.csv) file, if any
	*/
	void CreatePipelineCache(const ITargetPlatform* TargetPlatform, const FString& LibraryName);

	/**
	* Invokes the necessary FShaderCodeLibrary functions to clean out all the temporary files.
	*/
	void CleanShaderCodeLibraries();

	/**
	* Call back from the TickCookOnTheSide when a cook by the book finishes (when started form StartCookByTheBook)
	*/
	void CookByTheBookFinished();

	/** Print some stats when finishing or cancelling CookByTheBook */
	void PrintFinishStats();

	/**
	* Get all the packages which are listed in asset registry passed in.  
	*
	* @param AssetRegistryPath path of the assetregistry.bin file to read
	* @param bVerifyPackagesExist whether or not we should verify the packages exist on disk.
	* @param OutPackageDatas out list of packagename/filenames for packages contained in the asset registry file
	* @return true if successfully read false otherwise
	*/
	bool GetAllPackageFilenamesFromAssetRegistry( const FString& AssetRegistryPath, bool bVerifyPackagesExist,
		TArray<UE::Cook::FConstructPackageData>& OutPackageDatas) const;

	/**
	* BuildMapDependencyGraph
	* builds a map of dependencies from maps
	* 
	* @param TargetPlatform the platform we want to build a map for
	*/
	void BuildMapDependencyGraph(const ITargetPlatform* TargetPlatform);

	/**
	* WriteMapDependencyGraph
	* write a previously built map dependency graph out to the sandbox directory for a platform
	*
	* @param TargetPlatform the platform we want to save out the dependency graph for
	*/
	void WriteMapDependencyGraph(const ITargetPlatform* TargetPlatform);

	//////////////////////////////////////////////////////////////////////////
	// cook on the fly specific functions

	void GetCookOnTheFlyUnsolicitedFiles(const ITargetPlatform* TargetPlatform, const FString& PlatformName, TArray<FString>& UnsolicitedFiles, const FString& Filename, bool bIsCookable);

	//////////////////////////////////////////////////////////////////////////
	// general functions

	/** Perform any special processing for freshly loaded packages 
	 */
	void ProcessUnsolicitedPackages(TArray<FName>* OutDiscoveredPackageNames = nullptr, TMap<FName, UE::Cook::FInstigator>* OutInstigators = nullptr);

	/**
	 * Loads a package and prepares it for cooking
	 *  this is the same as a normal load but also ensures that the sublevels are loaded if they are streaming sublevels
	 *
	 * @param BuildFilename long package name of the package to load 
	 * @param OutPackage UPackage of the package loaded, non-null on success, may be non-null on failure if the UPackage existed but had another failure
	 * @param OverrideFileName Filename to use instead of PackageData.GetFileName(), for e.g. generated packages
	 * @param ReportingPackageData PackageData which caused the load, for e.g. generated packages
	 * @return Whether LoadPackage was completely successful and the package can be cooked
	 */
	bool LoadPackageForCooking(UE::Cook::FPackageData& PackageData, UPackage*& OutPackage, FString* LoadFromFileName = nullptr,
		UE::Cook::FPackageData* ReportingPackageData = nullptr);

	/**
	* Initialize the sandbox for a new cook session with @param TargetPlatforms
	*/
	void BeginCookSandbox(TConstArrayView<const ITargetPlatform*> TargetPlatforms);

	/** Set parameters that rely on config and CookByTheBook settings. */
	void SetBeginCookConfigSettings();

	/**
	* Finalize the package store
	*/
	void FinalizePackageStore();

	/**
	* Empties SavePackageContexts and deletes the contents
	*/
	void ClearPackageStoreContexts();

	/**
	* Initialize platforms in @param NewTargetPlatforms
	*/
	void InitializeTargetPlatforms(const TArrayView<ITargetPlatform* const>& NewTargetPlatforms);

	/**
	* Initialize shaders for the specified platforms when running cook on the fly.
	*/
	void InitializeShadersForCookOnTheFly(const TArrayView<ITargetPlatform* const>& NewTargetPlatforms);

	/**
	* Some content plugins does not support all target platforms.
	* Build up a map of unsupported packages per platform that can be checked before saving.
	*/
	void DiscoverPlatformSpecificNeverCookPackages(
		const TArrayView<const ITargetPlatform* const>& TargetPlatforms, const TArray<FString>& UBTPlatformStrings);

	/**
	* Clean up the sandbox
	*/
	void TermSandbox();

	/**
	* GetDependentPackages
	* get package dependencies according to the asset registry
	* 
	* @param Packages List of packages to use as the root set for dependency checking
	* @param Found return value, all objects which package is dependent on
	*/
	void GetDependentPackages( const TSet<UPackage*>& Packages, TSet<FName>& Found);

	/**
	* GetDependentPackages
	* get package dependencies according to the asset registry
	*
	* @param Root set of packages to use when looking for dependencies
	* @param FoundPackages list of packages which were found
	*/
	void GetDependentPackages(const TSet<FName>& RootPackages, TSet<FName>& FoundPackages);

	/**
	* ContainsWorld
	* use the asset registry to determine if a Package contains a UWorld or ULevel object
	* 
	* @param PackageName to return if it contains the a UWorld object or a ULevel
	* @return true if the Package contains a UWorld or ULevel false otherwise
	*/
	bool ContainsMap(const FName& PackageName) const;


	/** 
	 * Returns true if this package contains a redirector, and fills in paths
	 *
	 * @param PackageName to return if it contains a redirector
	 * @param RedirectedPaths map of original to redirected object paths
	 * @return true if the Package contains a redirector false otherwise
	 */
	bool ContainsRedirector(const FName& PackageName, TMap<FName, FName>& RedirectedPaths) const;
	
	/**
	 * Prepares save by calling BeginCacheForCookedPlatformData on all UObjects in the package. 
	 * Also splits the package if an object of this package has its class registered to a corresponding 
	 * FRegisteredCookPackageSplitter and an instance of this splitter class returns true ShouldSplitPackage 
	 * for this UObject.
	 *
	 * @param PackageData the PackageData used to gather all uobjects from
	 * @param bIsPreCaching true if called for precaching
	 * @return false if time slice was reached, true if all objects have had BeginCacheForCookedPlatformData called
	 */
	bool BeginPrepareSave(UE::Cook::FPackageData& PackageData, UE::Cook::FCookerTimer& Timer, bool bIsPreCaching);
	
	/**
	 * Returns true when all objects in package have all their cooked platform data loaded
	 *	confirms that BeginCacheForCookedPlatformData is called and will return true after all objects return IsCachedCookedPlatformDataLoaded true
	 *
	 * @param PackageData the PackageData used to gather all uobjects from
	 * @return false if time slice was reached, true if all return true for IsCachedCookedPlatformDataLoaded 
	 */
	bool FinishPrepareSave(UE::Cook::FPackageData& PackageData, UE::Cook::FCookerTimer& Timer);

	/**
	 * Frees all the memory used to call BeginCacheForCookedPlatformData on all the objects in PackageData.
	 * If the calls were incomplete because the PackageData's save was cancelled, handles canceling them and leaving any required CancelManagers in GetPendingCookedPlatformDatas
	 * 
	 * @param bCompletedSave If false, data that can not be efficiently recomputed will be preserved to try the save again. If true, all data will be wiped.
	 */
	void ReleaseCookedPlatformData(UE::Cook::FPackageData& PackageData, bool bCompletedSave);

	/**
	 * Poll the GetPendingCookedPlatformDatas and release their resources when they are complete.
	 * This is done inside of PumpSaveQueue, but is also required after a cancelled cook so that references to the pending objects will be eventually dropped.
	 */
	void TickCancels();

	/**
	* GetCurrentIniVersionStrings gets the current ini version strings for compare against previous cook
	* 
	* @param IniVersionStrings return list of the important current ini version strings
	* @return false if function fails (should assume all platforms are out of date)
	*/
	bool GetCurrentIniVersionStrings( const ITargetPlatform* TargetPlatform, FIniSettingContainer& IniVersionStrings ) const;

	/**
	* GetCookedIniVersionStrings gets the ini version strings used in previous cook for specified target platform
	* 
	* @param IniVersionStrings return list of the previous cooks ini version strings
	* @return false if function fails to find the ini version strings
	*/
	bool GetCookedIniVersionStrings( const ITargetPlatform* TargetPlatform, FIniSettingContainer& IniVersionStrings, TMap<FString, FString>& AdditionalStrings ) const;


	/**
	* Convert a path to a full sandbox path
	* is effected by the cooking dlc settings
	* This function should be used instead of calling the FSandbox Sandbox->ConvertToSandboxPath functions
	*/
	FString ConvertToFullSandboxPath( const FString &FileName, bool bForWrite = false ) const;
	FString ConvertToFullSandboxPath( const FString &FileName, bool bForWrite, const FString& PlatformName ) const;

	/**
	* GetSandboxAssetRegistryFilename
	* 
	* return full path of the asset registry in the sandbox
	*/
	const FString GetSandboxAssetRegistryFilename();

	const FString GetCookedAssetRegistryFilename(const FString& PlatformName);

	/**
	* Get the sandbox root directory for that platform
	* is effected by the CookingDlc settings
	* This should be used instead of calling the Sandbox function
	*/
	FString GetSandboxDirectory( const FString& PlatformName ) const;

	/** If not already done, set the default platform name to be used by GetAsyncIODelete(). */
	void TrySetDefaultAsyncIODeletePlatform(const FString& PlatformName);

	/* Create the delete-old-cooked-directory helper.*/
	FAsyncIODelete& GetAsyncIODelete();

	bool IsCookingDLC() const;

	/**
	* Returns true if we're cooking against a fixed release version
	*/
	bool IsCookingAgainstFixedBase() const;

	/**
	* Returns whether or not we should populate the Asset Registry using the main game content
	*/
	bool ShouldPopulateFullAssetRegistry() const;

	/**
	* GetBaseDirectoryForDLC
	* 
	* @return return the path to the DLC
	*/
	FString GetBaseDirectoryForDLC() const;

	FString GetContentDirectoryForDLC() const;

	bool IsCreatingReleaseVersion();

	/**
	* Loads the cooked ini version settings maps into the Ini settings cache
	* 
	* @param TargetPlatforms to look for ini settings for
	*/
	//bool CacheIniVersionStringsMap( const ITargetPlatform* TargetPlatform ) const;

	/**
	* Checks if important ini settings have changed since last cook for each target platform 
	* 
	* @param TargetPlatforms to check if out of date
	* @param OutOfDateTargetPlatforms return list of out of date target platforms which should be cleaned
	*/
	bool IniSettingsOutOfDate( const ITargetPlatform* TargetPlatform ) const;

	/**
	* Saves ini settings which are in the memory cache to the hard drive in ini files
	*
	* @param TargetPlatforms to save
	*/
	bool SaveCurrentIniSettings( const ITargetPlatform* TargetPlatform ) const;



	/**
	* IsCookFlagSet
	* 
	* @param InCookFlag used to check against the current cook flags
	* @return true if the cook flag is set false otherwise
	*/
	bool IsCookFlagSet( const ECookInitializationFlags& InCookFlags ) const 
	{
		return (CookFlags & InCookFlags) != ECookInitializationFlags::None;
	}

	/** Cook (save) a package and process the results */
	void SaveCookedPackage(UE::Cook::FSaveCookedPackageContext& Context);
	friend class UE::Cook::FSaveCookedPackageContext;

	/**
	*  Save the global shader map
	*  
	*  @param	Platforms		List of platforms to make global shader maps for
	*/
	void SaveGlobalShaderMapFiles(const TArrayView<const ITargetPlatform* const>& Platforms);


	/** Create sandbox file in directory using current settings supplied */
	void CreateSandboxFile();
	/** Gets the output directory respecting any command line overrides */
	FString GetOutputDirectoryOverride() const;

	/**
	 * Populate cooked packages from the PackageWriter's previous manifest and assetregistry of cooked output.
	 * Delete any out of date packages from the PackageWriter's manifest.
	 */
	void PopulateCookedPackages(const TConstArrayView<const ITargetPlatform*> TargetPlatforms);

	/** Loads the platform-independent asset registry for use by the cooker */
	void GenerateAssetRegistry();
	/** Waits for the AssetRegistry to complete so that we know any missing assets are missing on disk */
	void BlockOnAssetRegistry();
	/** Setup necessary only once for CookOnTheFly, but that are not required until the first request. */
	void CookOnTheFlyDeferredInitialize();

	/** Construct or refresh-for-filechanges the platform-specific asset registry for the given platforms */
	void RefreshPlatformAssetRegistries(const TArrayView<const ITargetPlatform* const>& TargetPlatforms);

	/** Generates long package names for all files to be cooked */
	void GenerateLongPackageNames(TArray<FName>& FilesInPath, TMap<FName, UE::Cook::FInstigator>& Instigators);

	/**
	* Try to find a registered CookPackageSplitter to split the given package.
	*
	* @param PackageData			Package to be potentially split
	* @param OutSplitDataObject		Return package's object to handle splitting
	* @param bOutHasError			Return True if function fails
	*/
	UE::Cook::Private::FRegisteredCookPackageSplitter* TryGetRegisteredCookPackageSplitter(UE::Cook::FPackageData& PackageData, UObject*& OutSplitDataObject, bool& bOutError);

	/**
	* Generate a creator package helper object that will be used to split package.
	*
	* @param PackageData			Package to split
	* @param SplitDataObject		Package's object to handle splitting
	* @param Splitter				Registered CookPackageSplitter to be used for splitting the package
	*/
	UE::Cook::FGeneratorPackage* CreateGeneratorPackage(UE::Cook::FPackageData& PackageData, UObject* SplitDataObject, UE::Cook::Private::FRegisteredCookPackageSplitter* Splitter);

	/**
	* Split the package and generate other packages.
	*
	* @param PackageData			Package to be split
	* @param SplitDataObject		Package's object to handle splitting
	* @param bOutCompleted			Return true is function is done splitting
	* @param bOutError				Return true if function fails
	*/
	void SplitPackage(UE::Cook::FGeneratorPackage* Generator, bool& bCompleted, bool& bOutError);

	/** Try calling the splitter's populate to create the package */
	UPackage* TryPopulateGeneratedPackage(UE::Cook::FPopulatePackageContext& Context);

	uint32 FullLoadAndSave(uint32& CookedPackageCount);

	ICookedPackageWriter& FindOrCreatePackageWriter(const ITargetPlatform* TargetPlatform);
	void FindOrCreateSaveContexts(TConstArrayView<const ITargetPlatform*> TargetPlatforms);
	UE::Cook::FCookSavePackageContext& FindOrCreateSaveContext(const ITargetPlatform* TargetPlatform);
	/** Allocate a new FCookSavePackageContext and ICookedPackageWriter for the given platform. */
	UE::Cook::FCookSavePackageContext* CreateSaveContext(const ITargetPlatform* TargetPlatform);

	uint32		StatLoadedPackageCount = 0;
	uint32		StatSavedPackageCount = 0;

	/** This is set to true when the decision about which packages we need to cook changes because e.g. a platform was added to the sessionplatforms. */
	bool bPackageFilterDirty = false;
	/** Set to true when PumpLoads has detected it is blocked on async work and CookOnTheFlyServer should do work elsewhere. */
	bool bLoadBusy = false;
	/** Set to true when PumpSaves has detected it is blocked on async work and CookOnTheFlyServer should do work elsewhere. */
	bool bSaveBusy = false;
	/** If preloading is enabled, we call TryPreload until it returns true before sending the package to LoadReady, otherwise we skip TryPreload and it goes immediately. */
	bool bPreloadingEnabled = false;
	/** If enabled, we load/save TargetDomainKey hashes and use those to test whether packages have already been cooked in hybrid-iterative builds. */
	bool bHybridIterativeEnabled = true;
	/** Whether to explore transitive dependencies up front, or discover them as we load packages. */
	bool bPreexploreDependenciesEnabled = true;
	/** Test mode for the debug of hybrid iterative dependencies. */
	bool bHybridIterativeDebug = false;
	bool bHasBlockedOnAssetRegistry = false;
	bool bHasDeferredInitializeCookOnTheFly = false;


	/** Timers for tracking how long we have been busy, to manage retries and warnings of deadlock */
	float SaveBusyTimeLastRetry = 0.f;
	float SaveBusyTimeStarted = 0.f;
	float LoadBusyTimeLastRetry = 0.f;
	float LoadBusyTimeStarted = 0.f;
	/** Tracking for the ticking of tickable cook objects */
	float LastCookableObjectTickTime = 0.f;

	// These helper structs are all TUniquePtr rather than inline members so that we can keep their headers private.  See class header comments for their purpose.
	TUniquePtr<UE::Cook::FPackageTracker> PackageTracker;
	TUniquePtr<UE::Cook::FPackageDatas> PackageDatas;
	TUniquePtr<UE::Cook::FExternalRequests> ExternalRequests;
	TUniquePtr<UE::Cook::FBuildDefinitions> BuildDefinitions;

	TArray<UE::Cook::FCookSavePackageContext*> SavePackageContexts;
	/** Objects that were collected during the single-threaded PreGarbageCollect callback and that should be reported as referenced in CookerAddReferencedObjects. */
	TArray<UObject*> GCKeepObjects;
	UE::Cook::FPackageData* SavingPackageData = nullptr;
	/** Helper struct for running cooking in diagnostic modes */
	TUniquePtr<FDiffModeCookServerUtils> DiffModeHelper;

	friend UE::Cook::FPackageData;
	friend UE::Cook::FPendingCookedPlatformData;
	friend UE::Cook::FPlatformManager;
	friend UE::Cook::FRequestCluster;
};
