// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	CookOnTheFlyServer.cpp: handles polite cook requests via network ;)
=============================================================================*/

#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "Cooker/PackageNameCache.h"

#include "Cooker/CookPackageData.h"
#include "Cooker/CookPlatformManager.h"
#include "Cooker/CookProfiling.h"
#include "Cooker/CookRequests.h"
#include "Cooker/CookTypes.h"
#include "Cooker/PackageTracker.h"
#include "Containers/RingBuffer.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Stats/Stats.h"
#include "Stats/StatsMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/LocalTimestampDirectoryVisitor.h"
#include "Serialization/CustomVersion.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/LinkerDiff.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectArray.h"
#include "Misc/PackageName.h"
#include "Misc/RedirectCollector.h"
#include "Engine/Level.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/AssetManager.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Texture.h"
#include "SceneUtils.h"
#include "Settings/ProjectPackagingSettings.h"
#include "EngineGlobals.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "FileServerMessages.h"
#include "LocalizationChunkDataGenerator.h"
#include "Internationalization/Culture.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"
#include "PackageHelperFunctions.h"
#include "DerivedDataCacheInterface.h"
#include "GlobalShader.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IAudioFormat.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/ITextureFormat.h"
#include "IMessageContext.h"
#include "MessageEndpoint.h"
#include "MessageEndpointBuilder.h"
#include "INetworkFileServer.h"
#include "INetworkFileSystemModule.h"
#include "PlatformInfo.h"
#include "Serialization/ArchiveStackTrace.h"
#include "DistanceFieldAtlas.h"
#include "Cooker/AsyncIODelete.h"
#include "Serialization/BulkDataManifest.h"
#include "Misc/PathViews.h"
#include "String/Find.h"

#include "AssetRegistryModule.h"
#include "AssetRegistryState.h"
#include "CookerSettings.h"
#include "BlueprintNativeCodeGenModule.h"

#include "GameDelegates.h"
#include "IPAddress.h"

#include "Interfaces/IPluginManager.h"
#include "ProjectDescriptor.h"
#include "Interfaces/IProjectManager.h"

// cook by the book requirements
#include "Commandlets/AssetRegistryGenerator.h"
#include "Engine/WorldComposition.h"

// error message log
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"

// shader compiler processAsyncResults
#include "ShaderCompiler.h"
#include "ShaderCodeLibrary.h"
#include "ShaderLibraryChunkDataGenerator.h"
#include "Engine/LevelStreaming.h"
#include "Engine/TextureLODSettings.h"
#include "ProfilingDebugging/CookStats.h"
#include "ProfilingDebugging/PlatformFileTrace.h"

#include "Misc/NetworkVersion.h"

#include "Algo/Find.h"
#include "Async/ParallelFor.h"

#include "Commandlets/ShaderPipelineCacheToolsCommandlet.h"

#define LOCTEXT_NAMESPACE "Cooker"
#define REMAPPED_PLUGINS TEXT("RemappedPlugins")

DEFINE_LOG_CATEGORY(LogCook);

int32 GCookProgressDisplay = (int32)ECookProgressDisplayMode::RemainingPackages;
static FAutoConsoleVariableRef CVarCookDisplayMode(
	TEXT("cook.displaymode"),
	GCookProgressDisplay,
	TEXT("Controls the display for cooker logging of packages:\n")
	TEXT("  0: No display\n")
	TEXT("  1: Display packages remaining\n")
	TEXT("  2: Display each package by name\n")
	TEXT("  3: Both\n"),
	ECVF_Default);

float GCookProgressUpdateTime = 2.0f;
static FAutoConsoleVariableRef CVarCookDisplayUpdateTime(
	TEXT("cook.display.updatetime"),
	GCookProgressUpdateTime,
	TEXT("Controls the time before the cooker will send a new progress message.\n"),
	ECVF_Default);

float GCookProgressDiagnosticTime = 30.0f;
static FAutoConsoleVariableRef CVarCookDisplayDiagnosticTime(
	TEXT("Cook.display.diagnostictime"),
	GCookProgressDiagnosticTime,
	TEXT("Controls the time between cooker diagnostics messages.\n"),
	ECVF_Default);

float GCookProgressRepeatTime = 5.0f;
static FAutoConsoleVariableRef CVarCookDisplayRepeatTime(
	TEXT("cook.display.repeattime"),
	GCookProgressRepeatTime,
	TEXT("Controls the time before the cooker will repeat the same progress message.\n"),
	ECVF_Default);


#define PROFILE_NETWORK 0

#if PROFILE_NETWORK
double TimeTillRequestStarted = 0.0;
double TimeTillRequestForfilled = 0.0;
double TimeTillRequestForfilledError = 0.0;
double WaitForAsyncFilesWrites = 0.0;
FEvent *NetworkRequestEvent = nullptr;
#endif

#if ENABLE_COOK_STATS
namespace DetailedCookStats
{
	// These times are externable so CookCommandlet can pick them up and merge them with its cook stats
	double TickCookOnTheSideTimeSec = 0.0;
	double TickCookOnTheSideLoadPackagesTimeSec = 0.0;
	double TickCookOnTheSideResolveRedirectorsTimeSec = 0.0;
	double TickCookOnTheSideSaveCookedPackageTimeSec = 0.0;
	double TickCookOnTheSideBeginPackageCacheForCookedPlatformDataTimeSec = 0.0;
	double TickCookOnTheSideFinishPackageCacheForCookedPlatformDataTimeSec = 0.0;
	double GameCookModificationDelegateTimeSec = 0.0;
	int32 PeakRequestQueueSize = 0;
	int32 PeakLoadQueueSize = 0;
	int32 PeakSaveQueueSize = 0;

	// Stats tracked through FAutoRegisterCallback
	uint32 NumPreloadedDependencies = 0;
	FCookStatsManager::FAutoRegisterCallback RegisterCookOnTheFlyServerStats([](FCookStatsManager::AddStatFuncRef AddStat)
		{
			AddStat(TEXT("Package.Load"), FCookStatsManager::CreateKeyValueArray(TEXT("NumPreloadedDependencies"), NumPreloadedDependencies));
			AddStat(TEXT("CookOnTheFlyServer"), FCookStatsManager::CreateKeyValueArray(TEXT("PeakRequestQueueSize"), PeakRequestQueueSize));
			AddStat(TEXT("CookOnTheFlyServer"), FCookStatsManager::CreateKeyValueArray(TEXT("PeakLoadQueueSize"), PeakLoadQueueSize));
			AddStat(TEXT("CookOnTheFlyServer"), FCookStatsManager::CreateKeyValueArray(TEXT("PeakSaveQueueSize"), PeakSaveQueueSize));
		});
}
#endif



////////////////////////////////////////////////////////////////
/// Cook on the fly server
///////////////////////////////////////////////////////////////

DECLARE_STATS_GROUP(TEXT("Cooking"), STATGROUP_Cooking, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Precache Derived data for platform"), STAT_TickPrecacheCooking, STATGROUP_Cooking);
DECLARE_CYCLE_STAT(TEXT("Tick cooking"), STAT_TickCooker, STATGROUP_Cooking);

constexpr uint32 ExpectedMaxNumPlatforms = 32;


/* helper structs functions
 *****************************************************************************/


/** Helper to assign to any variable for a scope period */
template<class T>
struct FScopeAssign
{
private:
	T* Setting;
	T OriginalValue;
public:
	FScopeAssign(T& InSetting, const T NewValue)
	{
		Setting = &InSetting;
		OriginalValue = *Setting;
		*Setting = NewValue;
	}
	~FScopeAssign()
	{
		*Setting = OriginalValue;
	}
};


class FPackageSearchVisitor : public IPlatformFile::FDirectoryVisitor
{
	TArray<FString>& FoundFiles;
public:
	FPackageSearchVisitor(TArray<FString>& InFoundFiles)
		: FoundFiles(InFoundFiles)
	{}
	virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory == false)
		{
			FString Filename(FilenameOrDirectory);
			if (Filename.EndsWith(TEXT(".uasset")) || Filename.EndsWith(TEXT(".umap")))
			{
				FoundFiles.Add(Filename);
			}
		}
		return true;
	}
};

class FAdditionalPackageSearchVisitor: public IPlatformFile::FDirectoryVisitor
{
	TSet<FString>& FoundMapFilesNoExt;
	TArray<FString>& FoundOtherFiles;
public:
	FAdditionalPackageSearchVisitor(TSet<FString>& InFoundMapFiles, TArray<FString>& InFoundOtherFiles)
		: FoundMapFilesNoExt(InFoundMapFiles), FoundOtherFiles(InFoundOtherFiles)
	{}
	virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory == false)
		{
			FString Filename(FilenameOrDirectory);
			if (Filename.EndsWith(TEXT(".uasset")) || Filename.EndsWith(TEXT(".umap")))
			{
				FoundMapFilesNoExt.Add(FPaths::SetExtension(Filename, ""));
			}
			else if ( Filename.EndsWith(TEXT(".uexp")) || Filename.EndsWith(TEXT(".ubulk")) )
			{
				FoundOtherFiles.Add(Filename);
			}
		}
		return true;
	}
};

const FString& GetAssetRegistryPath()
{
	static const FString AssetRegistryPath = FPaths::ProjectDir();
	return AssetRegistryPath;
}

/**
 * Return the release asset registry filename for the release version supplied
 */
static FString GetReleaseVersionAssetRegistryPath(const FString& ReleaseVersion, const FString& PlatformName, const FString& RootOverride)
{
	// cache the part of the path which is static because getting the ProjectDir is really slow and also string manipulation
	const FString* ReleasesRoot;
	if (RootOverride.IsEmpty())
	{
		const static FString DefaultReleasesRoot = FPaths::ProjectDir() / FString(TEXT("Releases"));
		ReleasesRoot = &DefaultReleasesRoot;
	}
	else
	{
		ReleasesRoot = &RootOverride;
	}
	return (*ReleasesRoot) / ReleaseVersion / PlatformName;
}

template<typename T>
struct FOneTimeCommandlineReader
{
	T Value;
	FOneTimeCommandlineReader(const TCHAR* Match)
	{
		FParse::Value(FCommandLine::Get(), Match, Value);
	}
};

static FString GetCreateReleaseVersionAssetRegistryPath(const FString& ReleaseVersion, const FString& PlatformName)
{
	static FOneTimeCommandlineReader<FString> CreateReleaseVersionRoot(TEXT("-createreleaseversionroot="));
	return GetReleaseVersionAssetRegistryPath(ReleaseVersion, PlatformName, CreateReleaseVersionRoot.Value);
}

static FString GetBasedOnReleaseVersionAssetRegistryPath(const FString& ReleaseVersion, const FString& PlatformName)
{
	static FOneTimeCommandlineReader<FString> BasedOnReleaseVersionRoot(TEXT("-basedonreleaseversionroot="));
	return GetReleaseVersionAssetRegistryPath(ReleaseVersion, PlatformName, BasedOnReleaseVersionRoot.Value);
}

const FString& GetAssetRegistryFilename()
{
	static const FString AssetRegistryFilename = FString(TEXT("AssetRegistry.bin"));
	return AssetRegistryFilename;
}

const FString& GetDevelopmentAssetRegistryFilename()
{
	static const FString DevelopmentAssetRegistryFilename = FString(TEXT("DevelopmentAssetRegistry.bin"));
	return DevelopmentAssetRegistryFilename;
}

/**
 * Uses the FMessageLog to log a message
 * 
 * @param Message to log
 * @param Severity of the message
 */
void LogCookerMessage( const FString& MessageText, EMessageSeverity::Type Severity)
{
	FMessageLog MessageLog("LogCook");

	TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(Severity);

	Message->AddToken( FTextToken::Create( FText::FromString(MessageText) ) );
	// Message->AddToken(FTextToken::Create(MessageLogTextDetail)); 
	// Message->AddToken(FDocumentationToken::Create(TEXT("https://docs.unrealengine.com/latest/INT/Platforms/iOS/QuickStart/6/index.html"))); 
	MessageLog.AddMessage(Message);

	MessageLog.Notify(FText(), EMessageSeverity::Warning, false);
}

//////////////////////////////////////////////////////////////////////////
// Cook by the book options

struct UCookOnTheFlyServer::FCookByTheBookOptions
{
public:
	/** Should we generate streaming install manifests (only valid option in cook by the book) */
	bool							bGenerateStreamingInstallManifests = false;

	/** Should we generate a seperate manifest for map dependencies */
	bool							bGenerateDependenciesForMaps = false;

	/** Is cook by the book currently running */
	bool							bRunning = false;

	/** Cancel has been queued will be processed next tick */
	bool							bCancel = false;

	/** DlcName setup if we are cooking dlc will be used as the directory to save cooked files to */
	FString							DlcName;

	/** Create a release from this manifest and store it in the releases directory for this cgame */
	FString							CreateReleaseVersion;

	/** Dependency graph of maps as root objects. */
	TFastPointerMap<const ITargetPlatform*,TMap<FName,TSet<FName>>> MapDependencyGraphs;

	/** If we are based on a release version of the game this is the set of packages which were cooked in that release. Map from platform name to list of uncooked package filenames */
	TMap<FName, TArray<FName>>			BasedOnReleaseCookedPackages;

	/** Timing information about cook by the book */
	double							CookTime = 0.0;
	double							CookStartTime = 0.0;

	/** error when detecting engine content being used in this cook */
	bool							bErrorOnEngineContentUse = false;
	bool							bSkipHardReferences = false;
	bool							bSkipSoftReferences = false;
	bool							bFullLoadAndSave = false;
	bool							bPackageStore = false;
	bool							bCookAgainstFixedBase = false;
	bool							bDlcLoadMainAssetRegistry = false;
	TArray<FName>					StartupPackages;

	/** Mapping from source packages to their localized variants (based on the culture list in FCookByTheBookStartupOptions) */
	TMap<FName, TArray<FName>>		SourceToLocalizedPackageVariants;
};

/* UCookOnTheFlyServer functions
 *****************************************************************************/

UCookOnTheFlyServer::UCookOnTheFlyServer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	CurrentCookMode(ECookMode::CookOnTheFly),
	CookByTheBookOptions(nullptr),
	CookFlags(ECookInitializationFlags::None),
	bIsInitializingSandbox(false),
	bIsSavingPackage(false),
	AssetRegistry(nullptr),
	PackageDatas(MakeUnique<UE::Cook::FPackageDatas>(*this))
{
	PlatformManager = MakeUnique<UE::Cook::FPlatformManager>();
	ExternalRequests = MakeUnique<UE::Cook::FExternalRequests>();
	PackageTracker = MakeUnique<UE::Cook::FPackageTracker>(*PackageDatas.Get());
	bSaveAsyncAllowed = true;
	FString Temp;
	const TCHAR* CommandLine = FCommandLine::Get();
	if (FParse::Value(CommandLine, TEXT("-diffagainstcookdirectory="), Temp) || FParse::Value(CommandLine, TEXT("-breakonfile="), Temp))
	{
		// async save doesn't work with any of these flags
		bSaveAsyncAllowed = false;
	}
}

UCookOnTheFlyServer::UCookOnTheFlyServer(FVTableHelper& Helper) :Super(Helper) {}

UCookOnTheFlyServer::~UCookOnTheFlyServer()
{
	ClearPackageStoreContexts();

	FCoreDelegates::OnFConfigCreated.RemoveAll(this);
	FCoreDelegates::OnFConfigDeleted.RemoveAll(this);
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);
	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);
	GetTargetPlatformManager()->GetOnTargetPlatformsInvalidatedDelegate().RemoveAll(this);

	delete CookByTheBookOptions;
	CookByTheBookOptions = nullptr;

	ClearHierarchyTimers();
}

// This tick only happens in the editor.  The cook commandlet directly calls tick on the side.
void UCookOnTheFlyServer::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::Tick);

	check(IsCookingInEditor());

	if (IsCookByTheBookMode() && !IsCookByTheBookRunning() && !GIsSlowTask)
	{
		// if we are in the editor then precache some stuff ;)
		TArray<const ITargetPlatform*> CacheTargetPlatforms;
		const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();
		if (PlaySettings && (PlaySettings->LastExecutedLaunchModeType == LaunchMode_OnDevice))
		{
			FString DeviceName = PlaySettings->LastExecutedLaunchDevice.Left(PlaySettings->LastExecutedLaunchDevice.Find(TEXT("@")));
			CacheTargetPlatforms.Add(GetTargetPlatformManager()->FindTargetPlatform(DeviceName));
		}
		if (CacheTargetPlatforms.Num() > 0)
		{
			// early out all the stuff we don't care about 
			if (!IsCookFlagSet(ECookInitializationFlags::BuildDDCInBackground))
			{
				return;
			}
			TickPrecacheObjectsForPlatforms(0.001, CacheTargetPlatforms);
		}
	}

	uint32 CookedPackagesCount = 0;
	const static float CookOnTheSideTimeSlice = 0.1f; // seconds
	TickCookOnTheSide( CookOnTheSideTimeSlice, CookedPackagesCount);
	TickRecompileShaderRequests();
}

bool UCookOnTheFlyServer::IsTickable() const 
{ 
	return IsCookFlagSet(ECookInitializationFlags::AutoTick); 
}

TStatId UCookOnTheFlyServer::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCookServer, STATGROUP_Tickables);
}

bool UCookOnTheFlyServer::StartNetworkFileServer(const bool BindAnyPort, const TArray<ITargetPlatform*>& TargetPlatforms)
{
	check(IsCookOnTheFlyMode());
	//GetDerivedDataCacheRef().WaitForQuiescence(false);

#if PROFILE_NETWORK
	NetworkRequestEvent = FPlatformProcess::GetSynchEventFromPool();
#endif

	// Precreate the map of all possible target platforms so we can access the collection of existing platforms in a threadsafe manner
	// Each PlatformData in the map will be uninitialized until we call AddCookOnTheFlyPlatform for the platform
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	for (const ITargetPlatform* TargetPlatform : TPM.GetTargetPlatforms())
	{
		PlatformManager->CreatePlatformData(TargetPlatform);
	}
	PlatformManager->SetArePlatformsPrepopulated(true);

	CreateSandboxFile();
	GenerateAssetRegistry();

	{
		UE::Cook::FPlatformManager::FReadScopeLock PlatformScopeLock(PlatformManager->ReadLockPlatforms());
		for (ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			AddCookOnTheFlyPlatform(TargetPlatform->PlatformName());
		}
	}

	// start the listening thread
	FNewConnectionDelegate NewConnectionDelegate(FNewConnectionDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkFileServerNewConnection));
	FFileRequestDelegate FileRequestDelegate(FFileRequestDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkFileServerFileRequest));
	FRecompileShadersDelegate RecompileShadersDelegate(FRecompileShadersDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkFileServerRecompileShaders));
	FSandboxPathDelegate SandboxPathDelegate(FSandboxPathDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkGetSandboxPath));
	FInitialPrecookedListDelegate InitialPrecookedListDelegate(FInitialPrecookedListDelegate::CreateUObject(this, &UCookOnTheFlyServer::HandleNetworkGetPrecookedList));


	FNetworkFileDelegateContainer NetworkFileDelegateContainer;
	NetworkFileDelegateContainer.NewConnectionDelegate = NewConnectionDelegate;
	NetworkFileDelegateContainer.InitialPrecookedListDelegate = InitialPrecookedListDelegate;
	NetworkFileDelegateContainer.FileRequestDelegate = FileRequestDelegate;
	NetworkFileDelegateContainer.RecompileShadersDelegate = RecompileShadersDelegate;
	NetworkFileDelegateContainer.SandboxPathOverrideDelegate = SandboxPathDelegate;
	
	NetworkFileDelegateContainer.OnFileModifiedCallback = &FileModifiedDelegate;


	INetworkFileServer *TcpFileServer = FModuleManager::LoadModuleChecked<INetworkFileSystemModule>("NetworkFileSystem")
		.CreateNetworkFileServer(true, BindAnyPort ? 0 : -1, NetworkFileDelegateContainer, ENetworkFileServerProtocol::NFSP_Tcp);
	if ( TcpFileServer )
	{
		NetworkFileServers.Add(TcpFileServer);
	}

#if 0 // cookonthefly server via http
	INetworkFileServer *HttpFileServer = FModuleManager::LoadModuleChecked<INetworkFileSystemModule>("NetworkFileSystem")
		.CreateNetworkFileServer(true, BindAnyPort ? 0 : -1, NetworkFileDelegateContainer, ENetworkFileServerProtocol::NFSP_Http);
	if ( HttpFileServer )
	{
		NetworkFileServers.Add( HttpFileServer );
	}
#endif

	ExternalRequests->CookRequestEvent = FPlatformProcess::GetSynchEventFromPool();

	// loop while waiting for requests
	return true;
}

const ITargetPlatform* UCookOnTheFlyServer::AddCookOnTheFlyPlatform(const FString& PlatformNameString)
{
	FName PlatformName(*PlatformNameString);
	const UE::Cook::FPlatformData* PlatformData = PlatformManager->GetPlatformDataByName(PlatformName);
	if (!PlatformData)
	{
		UE_LOG(LogCook, Warning, TEXT("Target platform %s wasn't found."), *PlatformNameString);
		return nullptr;
	}

	if (PlatformData->bIsSandboxInitialized)
	{
		// Platform has already been added by this function or by StartCookByTheBook
		return PlatformData->TargetPlatform;
	}

	if (IsInGameThread())
	{
		AddCookOnTheFlyPlatformFromGameThread(PlatformData->TargetPlatform);
	}
	else
	{
		// Registering a new platform is not thread safe; queue the command for TickCookOnTheSide to execute
		ExternalRequests->AddCallback([this, PlatformName]()
			{
				const UE::Cook::FPlatformData* PlatformData = PlatformManager->GetPlatformDataByName(PlatformName);
				check(PlatformData);
				AddCookOnTheFlyPlatformFromGameThread(PlatformData->TargetPlatform);
			});
		if (ExternalRequests->CookRequestEvent)
		{
			ExternalRequests->CookRequestEvent->Trigger();
		}
	}
	return PlatformData->TargetPlatform;
}

void UCookOnTheFlyServer::AddCookOnTheFlyPlatformFromGameThread(ITargetPlatform* TargetPlatform)
{
	check(!!(CookFlags & ECookInitializationFlags::GeneratedAssetRegistry)); // GenerateAssetRegistry should have been called in StartNetworkFileServer

	UE::Cook::FPlatformData* PlatformData = PlatformManager->GetPlatformData(TargetPlatform);
	check(PlatformData != nullptr); // should have been checked by the caller
	if (PlatformData->bIsSandboxInitialized)
	{
		return;
	}

	TArrayView<ITargetPlatform* const> NewTargetPlatforms(&TargetPlatform,1);

	RefreshPlatformAssetRegistries(NewTargetPlatforms);
	InitializeSandbox(NewTargetPlatforms);
	InitializeTargetPlatforms(NewTargetPlatforms);

	// When cooking on the fly the full registry is saved at the beginning
	// in cook by the book asset registry is saved after the cook is finished
	FAssetRegistryGenerator* Generator = PlatformData->RegistryGenerator.Get();
	if (Generator)
	{
		Generator->SaveAssetRegistry(GetSandboxAssetRegistryFilename(), true);
	}
	check(PlatformData->bIsSandboxInitialized); // This should have been set by InitializeSandbox, and it is what we use to determine whether a platform has been initialized
}

void UCookOnTheFlyServer::OnTargetPlatformsInvalidated()
{
	check(IsInGameThread());
	TMap<ITargetPlatform*, ITargetPlatform*> Remap = PlatformManager->RemapTargetPlatforms();

	if (CookByTheBookOptions)
	{
		RemapMapKeys(CookByTheBookOptions->MapDependencyGraphs, Remap);
	}
	PackageDatas->RemapTargetPlatforms(Remap);
	PackageTracker->RemapTargetPlatforms(Remap);
	ExternalRequests->RemapTargetPlatforms(Remap);

	if (PlatformManager->GetArePlatformsPrepopulated())
	{
		for (const ITargetPlatform* TargetPlatform : GetTargetPlatformManager()->GetTargetPlatforms())
		{
			PlatformManager->CreatePlatformData(TargetPlatform);
		}
	}
}

bool UCookOnTheFlyServer::BroadcastFileserverPresence( const FGuid &InstanceId )
{
	
	TArray<FString> AddressStringList;

	for ( int i = 0; i < NetworkFileServers.Num(); ++i )
	{
		TArray<TSharedPtr<FInternetAddr> > AddressList;
		INetworkFileServer *NetworkFileServer = NetworkFileServers[i];
		if ((NetworkFileServer == NULL || !NetworkFileServer->IsItReadyToAcceptConnections() || !NetworkFileServer->GetAddressList(AddressList)))
		{
			LogCookerMessage( FString(TEXT("Failed to create network file server")), EMessageSeverity::Error );
			continue;
		}

		// broadcast our presence
		if (InstanceId.IsValid())
		{
			for (int32 AddressIndex = 0; AddressIndex < AddressList.Num(); ++AddressIndex)
			{
				AddressStringList.Add(FString::Printf( TEXT("%s://%s"), *NetworkFileServer->GetSupportedProtocol(),  *AddressList[AddressIndex]->ToString(true)));
			}

		}
	}

	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint = FMessageEndpoint::Builder("UCookOnTheFlyServer").Build();

	if (MessageEndpoint.IsValid())
	{
		MessageEndpoint->Publish(new FFileServerReady(AddressStringList, InstanceId), EMessageScope::Network);
	}		
	
	return true;
}

/*----------------------------------------------------------------------------
	FArchiveFindReferences.
----------------------------------------------------------------------------*/
/**
 * Archive for gathering all the object references to other objects
 */
class FArchiveFindReferences : public FArchiveUObject
{
private:
	/**
	 * I/O function.  Called when an object reference is encountered.
	 *
	 * @param	Obj		a pointer to the object that was encountered
	 */
	FArchive& operator<<( UObject*& Obj ) override
	{
		if( Obj )
		{
			FoundObject( Obj );
		}
		return *this;
	}

	virtual FArchive& operator<< (struct FSoftObjectPtr& Value) override
	{
		if ( Value.Get() )
		{
			Value.Get()->Serialize( *this );
		}
		return *this;
	}
	virtual FArchive& operator<< (struct FSoftObjectPath& Value) override
	{
		if ( Value.ResolveObject() )
		{
			Value.ResolveObject()->Serialize( *this );
		}
		return *this;
	}


	void FoundObject( UObject* Object )
	{
		if ( RootSet.Find(Object) == NULL )
		{
			if ( Exclude.Find(Object) == INDEX_NONE )
			{
				// remove this check later because don't want this happening in development builds
				//check(RootSetArray.Find(Object)==INDEX_NONE);

				RootSetArray.Add( Object );
				RootSet.Add(Object);
				Found.Add(Object);
			}
		}
	}


	/**
	 * list of Outers to ignore;  any objects encountered that have one of
	 * these objects as an Outer will also be ignored
	 */
	TArray<UObject*> &Exclude;

	/** list of objects that have been found */
	TSet<UObject*> &Found;
	
	/** the objects to display references to */
	TArray<UObject*> RootSetArray;
	/** Reflection of the rootsetarray */
	TSet<UObject*> RootSet;

public:

	/**
	 * Constructor
	 * 
	 * @param	inOutputAr		archive to use for logging results
	 * @param	inOuter			only consider objects that do not have this object as its Outer
	 * @param	inSource		object to show references for
	 * @param	inExclude		list of objects that should be ignored if encountered while serializing SourceObject
	 */
	FArchiveFindReferences( TSet<UObject*> InRootSet, TSet<UObject*> &inFound, TArray<UObject*> &inExclude )
		: Exclude(inExclude)
		, Found(inFound)
		, RootSet(InRootSet)
	{
		ArIsObjectReferenceCollector = true;
		this->SetIsSaving(true);

		for ( UObject* Object : RootSet )
		{
			RootSetArray.Add( Object );
		}
		
		// loop through all the objects in the root set and serialize them
		for ( int RootIndex = 0; RootIndex < RootSetArray.Num(); ++RootIndex )
		{
			UObject* SourceObject = RootSetArray[RootIndex];

			// quick sanity check
			check(SourceObject);
			check(SourceObject->IsValidLowLevel());

			SourceObject->Serialize( *this );
		}

	}

	/**
	 * Returns the name of the Archive.  Useful for getting the name of the package a struct or object
	 * is in when a loading error occurs.
	 *
	 * This is overridden for the specific Archive Types
	 **/
	virtual FString GetArchiveName() const override { return TEXT("FArchiveFindReferences"); }
};

void UCookOnTheFlyServer::GetDependentPackages(const TSet<UPackage*>& RootPackages, TSet<FName>& FoundPackages)
{
	TSet<FName> RootPackageFNames;
	for (const UPackage* RootPackage : RootPackages)
	{
		RootPackageFNames.Add(RootPackage->GetFName());
	}


	GetDependentPackages(RootPackageFNames, FoundPackages);

}


void UCookOnTheFlyServer::GetDependentPackages( const TSet<FName>& RootPackages, TSet<FName>& FoundPackages )
{
	TArray<FName> FoundPackagesArray;
	for (const FName& RootPackage : RootPackages)
	{
		FoundPackagesArray.Add(RootPackage);
		FoundPackages.Add(RootPackage);
	}

	int FoundPackagesCounter = 0;
	while ( FoundPackagesCounter < FoundPackagesArray.Num() )
	{
		TArray<FName> PackageDependencies;
		if (AssetRegistry->GetDependencies(FoundPackagesArray[FoundPackagesCounter], PackageDependencies, UE::AssetRegistry::EDependencyCategory::Package) == false)
		{
			// this could happen if we are in the editor and the dependency list is not up to date

			if (IsCookingInEditor() == false)
			{
				UE_LOG(LogCook, Fatal, TEXT("Unable to find package %s in asset registry.  Can't generate cooked asset registry"), *FoundPackagesArray[FoundPackagesCounter].ToString());
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Unable to find package %s in asset registry, cooked asset registry information may be invalid "), *FoundPackagesArray[FoundPackagesCounter].ToString());
			}
		}
		++FoundPackagesCounter;
		for ( const FName& OriginalPackageDependency : PackageDependencies )
		{
			// check(PackageDependency.ToString().StartsWith(TEXT("/")));
			FName PackageDependency = OriginalPackageDependency;
			FString PackageDependencyString = PackageDependency.ToString();

			FText OutReason;
			const bool bIncludeReadOnlyRoots = true; // Dependency packages are often script packages (read-only)
			if (!FPackageName::IsValidLongPackageName(PackageDependencyString, bIncludeReadOnlyRoots, &OutReason))
			{
				const FText FailMessage = FText::Format(LOCTEXT("UnableToGeneratePackageName", "Unable to generate long package name for {0}. {1}"),
					FText::FromString(PackageDependencyString), OutReason);

				LogCookerMessage(FailMessage.ToString(), EMessageSeverity::Warning);
				continue;
			}
			else if (FPackageName::IsScriptPackage(PackageDependencyString) || FPackageName::IsMemoryPackage(PackageDependencyString))
			{
				continue;
			}

			if ( FoundPackages.Contains(PackageDependency) == false )
			{
				FoundPackages.Add(PackageDependency);
				FoundPackagesArray.Add( PackageDependency );
			}
		}
	}

}

bool UCookOnTheFlyServer::ContainsMap(const FName& PackageName) const
{
	TArray<FAssetData> Assets;
	ensure(AssetRegistry->GetAssetsByPackageName(PackageName, Assets, true));

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.GetClass()->IsChildOf(UWorld::StaticClass()) || Asset.GetClass()->IsChildOf(ULevel::StaticClass()))
		{
			return true;
		}
	}
	return false;
}

bool UCookOnTheFlyServer::ContainsRedirector(const FName& PackageName, TMap<FName, FName>& RedirectedPaths) const
{
	bool bFoundRedirector = false;
	TArray<FAssetData> Assets;
	ensure(AssetRegistry->GetAssetsByPackageName(PackageName, Assets, true));

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.IsRedirector())
		{
			FName RedirectedPath;
			FString RedirectedPathString;
			if (Asset.GetTagValue("DestinationObject", RedirectedPathString))
			{
				ConstructorHelpers::StripObjectClass(RedirectedPathString);
				RedirectedPath = FName(*RedirectedPathString);
				FAssetData DestinationData = AssetRegistry->GetAssetByObjectPath(RedirectedPath, true);
				TSet<FName> SeenPaths;

				SeenPaths.Add(RedirectedPath);

				// Need to follow chain of redirectors
				while (DestinationData.IsRedirector())
				{
					if (DestinationData.GetTagValue("DestinationObject", RedirectedPathString))
					{
						ConstructorHelpers::StripObjectClass(RedirectedPathString);
						RedirectedPath = FName(*RedirectedPathString);

						if (SeenPaths.Contains(RedirectedPath))
						{
							// Recursive, bail
							DestinationData = FAssetData();
						}
						else
						{
							SeenPaths.Add(RedirectedPath);
							DestinationData = AssetRegistry->GetAssetByObjectPath(RedirectedPath, true);
						}
					}
					else
					{
						// Can't extract
						DestinationData = FAssetData();						
					}
				}

				// DestinationData may be invalid if this is a subobject, check package as well
				bool bDestinationValid = DestinationData.IsValid();

				if (!bDestinationValid)
				{
					// we can't call GetCachedStandardFileName with None
					if (RedirectedPath != NAME_None)
					{
						FName StandardPackageName = GetPackageNameCache().GetCachedStandardFileName(FName(*FPackageName::ObjectPathToPackageName(RedirectedPathString)));
						if (StandardPackageName != NAME_None)
						{
							bDestinationValid = true;
						}
					}
				}

				if (bDestinationValid)
				{
					RedirectedPaths.Add(Asset.ObjectPath, RedirectedPath);
				}
				else
				{
					RedirectedPaths.Add(Asset.ObjectPath, NAME_None);
					UE_LOG(LogCook, Log, TEXT("Found redirector in package %s pointing to deleted object %s"), *PackageName.ToString(), *RedirectedPathString);
				}

				bFoundRedirector = true;
			}
		}
	}
	return bFoundRedirector;
}

bool UCookOnTheFlyServer::IsCookingInEditor() const
{
	return CurrentCookMode == ECookMode::CookByTheBookFromTheEditor || CurrentCookMode == ECookMode::CookOnTheFlyFromTheEditor;
}

bool UCookOnTheFlyServer::IsRealtimeMode() const 
{
	return CurrentCookMode == ECookMode::CookByTheBookFromTheEditor || CurrentCookMode == ECookMode::CookOnTheFlyFromTheEditor;
}

bool UCookOnTheFlyServer::IsCookByTheBookMode() const
{
	return CurrentCookMode == ECookMode::CookByTheBookFromTheEditor || CurrentCookMode == ECookMode::CookByTheBook;
}

bool UCookOnTheFlyServer::IsUsingShaderCodeLibrary() const
{
	return IsCookByTheBookMode();
}

bool UCookOnTheFlyServer::IsUsingPackageStore() const
{
	return IsCookByTheBookMode() && CookByTheBookOptions->bPackageStore;
}

bool UCookOnTheFlyServer::IsCookOnTheFlyMode() const
{
	return CurrentCookMode == ECookMode::CookOnTheFly || CurrentCookMode == ECookMode::CookOnTheFlyFromTheEditor; 
}

bool UCookOnTheFlyServer::IsCreatingReleaseVersion()
{
	if (CookByTheBookOptions)
	{
		return !CookByTheBookOptions->CreateReleaseVersion.IsEmpty();
	}

	return false;
}

bool UCookOnTheFlyServer::IsCookingDLC() const
{
	// can only cook DLC in cook by the book
	// we are cooking DLC when the DLC name is setup
	
	if (CookByTheBookOptions)
	{
		return !CookByTheBookOptions->DlcName.IsEmpty();
	}

	return false;
}

bool UCookOnTheFlyServer::IsCookingAgainstFixedBase() const
{
	return IsCookingDLC() && CookByTheBookOptions && CookByTheBookOptions->bCookAgainstFixedBase;
}

bool UCookOnTheFlyServer::ShouldPopulateFullAssetRegistry() const
{
	return !IsCookingDLC() || (CookByTheBookOptions && CookByTheBookOptions->bDlcLoadMainAssetRegistry);
}

FString UCookOnTheFlyServer::GetBaseDirectoryForDLC() const
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(CookByTheBookOptions->DlcName);
	if (Plugin.IsValid())
	{
		return Plugin->GetBaseDir();
	}

	return FPaths::ProjectPluginsDir() / CookByTheBookOptions->DlcName;
}

FString UCookOnTheFlyServer::GetContentDirectoryForDLC() const
{
	return GetBaseDirectoryForDLC() / TEXT("Content");
}

COREUOBJECT_API extern bool GOutputCookingWarnings;

void UCookOnTheFlyServer::WaitForRequests(int TimeoutMs)
{
	if (ExternalRequests->CookRequestEvent)
	{
		ExternalRequests->CookRequestEvent->Wait(TimeoutMs, true);
	}
}

bool UCookOnTheFlyServer::HasRemainingWork() const
{ 
	return ExternalRequests->HasRequests() || PackageDatas->GetMonitor().GetNumInProgress() > 0;
}

bool UCookOnTheFlyServer::RequestPackage(const FName& StandardFileName, const TArrayView<const ITargetPlatform* const>& TargetPlatforms, const bool bForceFrontOfQueue)
{
	if (!IsCookByTheBookMode())
	{
		bCookOnTheFlyExternalRequests = true;
		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			AddCookOnTheFlyPlatformFromGameThread(const_cast<ITargetPlatform*>(TargetPlatform));
			PlatformManager->AddRefCookOnTheFlyPlatform(FName(*TargetPlatform->PlatformName()), *this);
		}
	}

	ExternalRequests->EnqueueUnique(UE::Cook::FFilePlatformRequest(StandardFileName, TargetPlatforms), bForceFrontOfQueue);
	return true;
}

bool UCookOnTheFlyServer::RequestPackage(const FName& StandardFileName, const TArrayView<const FName>& TargetPlatformNames, const bool bForceFrontOfQueue)
{
	TArray<const ITargetPlatform*> TargetPlatforms;
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	for (const FName& TargetPlatformName : TargetPlatformNames)
	{
		const ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(TargetPlatformName.ToString());
		if (TargetPlatform)
		{
			TargetPlatforms.Add(TargetPlatform);
		}
	}
	return RequestPackage(StandardFileName, TargetPlatforms, bForceFrontOfQueue);
}

bool UCookOnTheFlyServer::RequestPackage(const FName& StandardPackageFName, const bool bForceFrontOfQueue)
{
	check(IsCookByTheBookMode()); // Invalid to call RequestPackage without a list of TargetPlatforms unless we are in cook by the book mode
	return RequestPackage(StandardPackageFName, PlatformManager->GetSessionPlatforms(), bForceFrontOfQueue);
}

uint32 UCookOnTheFlyServer::TickCookOnTheSide(const float TimeSlice, uint32 &CookedPackageCount, ECookTickFlags TickFlags)
{
	TickCancels();
	TickNetwork();
	if (!IsInSession())
	{
		return COSR_None;
	}

	if (IsCookByTheBookMode() && CookByTheBookOptions->bRunning && CookByTheBookOptions->bFullLoadAndSave)
	{
		uint32 Result = FullLoadAndSave(CookedPackageCount);

		CookByTheBookFinished();

		return Result;
	}

	COOK_STAT(FScopedDurationTimer TickTimer(DetailedCookStats::TickCookOnTheSideTimeSec));

	{
		if (AssetRegistry == nullptr || AssetRegistry->IsLoadingAssets())
		{
			// early out
			return COSR_None;
		}
	}

	UE::Cook::FTickStackData StackData(TimeSlice, IsRealtimeMode(), TickFlags);
	bool bCookComplete = false;

	{
		UE_SCOPED_HIERARCHICAL_COOKTIMER(TickCookOnTheSide); // Make sure no UE_SCOPED_HIERARCHICAL_COOKTIMERs are around CookByTheBookFinishes, as that function deletes memory for them

		bSaveBusy = false;
		bLoadBusy = false;
		bool bContinueTick = true;
		while (bContinueTick && (!IsEngineExitRequested() || CurrentCookMode == ECookMode::CookByTheBook))
		{
			TickCookStatus(StackData);

			ECookAction CookAction = DecideNextCookAction(StackData);
			switch (CookAction)
			{
			case ECookAction::Request:
				PumpRequests(StackData);
				bLoadBusy = false;
				break;
			case ECookAction::Load:
				PumpLoads(StackData, 0);
				bSaveBusy = false;
				break;
			case ECookAction::LoadLimited:
				PumpLoads(StackData, DesiredLoadQueueLength);
				bSaveBusy = false;
				break;
			case ECookAction::Save:
				PumpSaves(StackData, 0);
				break;
			case ECookAction::SaveLimited:
				PumpSaves(StackData, DesiredSaveQueueLength);
				break;
			case ECookAction::Done:
				bContinueTick = false;
				bCookComplete = true;
				break;
			case ECookAction::YieldTick:
				bContinueTick = false;
				break;
			case ECookAction::Cancel:
				CancelCookByTheBook();
				bContinueTick = false;
				break;
			default:
				check(false);
				break;
			}
		}
	}

	if (IsCookOnTheFlyMode() && (IsCookingInEditor() == false))
	{
		static int32 TickCounter = 0;
		++TickCounter;
		if (TickCounter > 50)
		{
			// dump stats every 50 ticks or so
			DumpStats();
			TickCounter = 0;
		}
	}

	if (CookByTheBookOptions)
	{
		CookByTheBookOptions->CookTime += StackData.Timer.GetTimeTillNow();
	}

	if (IsCookByTheBookRunning() && bCookComplete)
	{
		check(IsCookByTheBookMode());

		// if we are out of stuff and we are in cook by the book from the editor mode then we finish up
		UpdateDisplay(TickFlags, true /* bForceDisplay */);
		CookByTheBookFinished();
	}

	CookedPackageCount += StackData.CookedPackageCount;
	return StackData.ResultFlags;
}

void UCookOnTheFlyServer::TickCookStatus(UE::Cook::FTickStackData& StackData)
{
	UE_SCOPED_COOKTIMER(TickCookStatus);
	UpdateDisplay(StackData.TickFlags, false /* bForceDisplay */);

	// prevent autosave from happening until we are finished cooking
	// causes really bad hitches
	if (GUnrealEd)
	{
		const static float SecondsWarningTillAutosave = 10.0f;
		GUnrealEd->GetPackageAutoSaver().ForceMinimumTimeTillAutoSave(SecondsWarningTillAutosave);
	}

	ProcessUnsolicitedPackages();
	UpdatePackageFilter();
	PumpExternalRequests(StackData.Timer);
}

void UCookOnTheFlyServer::UpdateDisplay(ECookTickFlags TickFlags, bool bForceDisplay)
{
	const float CurrentTime = FPlatformTime::Seconds();
	const float DeltaProgressDisplayTime = CurrentTime - LastProgressDisplayTime;
	const int32 CookedPackagesCount = PackageDatas->GetNumCooked();
	const int32 CookPendingCount = ExternalRequests->GetNumRequests() + PackageDatas->GetMonitor().GetNumInProgress();
	if (bForceDisplay ||
		(DeltaProgressDisplayTime >= GCookProgressUpdateTime && CookPendingCount != 0 &&
			(LastCookedPackagesCount != CookedPackagesCount || LastCookPendingCount != CookPendingCount || DeltaProgressDisplayTime > GCookProgressRepeatTime)))
	{
		UE_CLOG(!(TickFlags & ECookTickFlags::HideProgressDisplay) && (GCookProgressDisplay & (int32)ECookProgressDisplayMode::RemainingPackages),
			LogCook,
			Display,
			TEXT("Cooked packages %d Packages Remain %d Total %d"),
			CookedPackagesCount,
			CookPendingCount,
			CookedPackagesCount + CookPendingCount);

		LastCookedPackagesCount = CookedPackagesCount;
		LastCookPendingCount = CookPendingCount;
		LastProgressDisplayTime = CurrentTime;
	}
	const float DeltaDiagnosticsDisplayTime = CurrentTime - LastDiagnosticsDisplayTime;
	if (bForceDisplay || DeltaDiagnosticsDisplayTime > GCookProgressDiagnosticTime)
	{
		uint32 OpenFileHandles = 0;
#if PLATFORMFILETRACE_ENABLED
		OpenFileHandles = FPlatformFileTrace::GetOpenFileHandleCount();
#endif
		UE_CLOG(!(TickFlags & ECookTickFlags::HideProgressDisplay) && (GCookProgressDisplay != (int32) ECookProgressDisplayMode::Nothing),
			LogCook, Display,
			TEXT("Cook Diagnostics: OpenFileHandles=%d, VirtualMemory=%dMiB"),
			OpenFileHandles, FPlatformMemory::GetStats().UsedVirtual / 1024 / 1024);
		LastDiagnosticsDisplayTime = CurrentTime;
	}
}
UCookOnTheFlyServer::ECookAction UCookOnTheFlyServer::DecideNextCookAction(UE::Cook::FTickStackData& StackData)
{
	if (IsCookByTheBookMode() && CookByTheBookOptions->bCancel)
	{
		return ECookAction::Cancel;
	}

	if (StackData.ResultFlags & COSR_RequiresGC)
	{
		// if we just cooked a map then don't process anything the rest of this tick
		return ECookAction::YieldTick;
	}
	else if (StackData.Timer.IsTimeUp())
	{
		return ECookAction::YieldTick;
	}

	UE::Cook::FPackageDataMonitor& Monitor = PackageDatas->GetMonitor();
	if (Monitor.GetNumUrgent() > 0)
	{
		if (Monitor.GetNumUrgent(UE::Cook::EPackageState::Save) > 0)
		{
			return ECookAction::Save;
		}
		else if (Monitor.GetNumUrgent(UE::Cook::EPackageState::LoadPrepare) > 0)
		{
			return ECookAction::Load;
		}
		else if (Monitor.GetNumUrgent(UE::Cook::EPackageState::LoadReady) > 0)
		{
			return ECookAction::Load;
		}
		else if (Monitor.GetNumUrgent(UE::Cook::EPackageState::Request) > 0)
		{
			return ECookAction::Request;
		}
		else
		{
			checkf(false, TEXT("Urgent request is in state not yet handled by DecideNextCookAction"));
		}
	}


	int32 NumSaves = PackageDatas->GetSaveQueue().Num();
	bool bSaveAvailable = ((!bSaveBusy) & (NumSaves > 0)) != 0;
	if (bSaveAvailable & (NumSaves > static_cast<int32>(DesiredSaveQueueLength)))
	{
		return ECookAction::SaveLimited;
	}

	int32 NumLoads = PackageDatas->GetLoadReadyQueue().Num() + PackageDatas->GetLoadPrepareQueue().Num();
	bool bLoadAvailable = ((!bLoadBusy) & (NumLoads > 0)) != 0;
	if (bLoadAvailable & (NumLoads > static_cast<int32>(DesiredLoadQueueLength)))
	{
		return ECookAction::LoadLimited;
	}

	int32 NumRequests = PackageDatas->GetRequestQueue().Num();
	bool bRequestAvailable = NumRequests > 0;
	if (bRequestAvailable)
	{
		return ECookAction::Request;
	}

	if (bSaveAvailable)
	{
		return ECookAction::Save;
	}

	if (bLoadAvailable)
	{
		return ECookAction::Load;
	}

	if (PackageDatas->GetMonitor().GetNumInProgress() > 0)
	{
		return ECookAction::YieldTick;
	}

	return ECookAction::Done;
}

void UCookOnTheFlyServer::PumpExternalRequests(const UE::Cook::FCookerTimer& CookerTimer)
{
	if (!ExternalRequests->HasRequests())
	{
		return;
	}
	UE_SCOPED_COOKTIMER(PumpExternalRequests);

	UE::Cook::FFilePlatformRequest ToBuild;
	TArray<UE::Cook::FSchedulerCallback> SchedulerCallbacks;
	UE::Cook::EExternalRequestType RequestType;
	while (!CookerTimer.IsTimeUp())
	{
		RequestType = ExternalRequests->DequeueRequest(SchedulerCallbacks, ToBuild);
		if (RequestType == UE::Cook::EExternalRequestType::None)
		{
			// No more requests to process
			break;
		}
		else if (RequestType == UE::Cook::EExternalRequestType::Callback)
		{
			// An array of TickCommands to process; execute through them all
			for (UE::Cook::FSchedulerCallback& SchedulerCallback : SchedulerCallbacks)
			{
				SchedulerCallback();
			}
		}
		else
		{
			check(RequestType == UE::Cook::EExternalRequestType::Cook && ToBuild.IsValid());
			FName FileName = ToBuild.GetFilename();
#if PROFILE_NETWORK
			if (NetworkRequestEvent)
			{
				NetworkRequestEvent->Trigger();
			}
#endif
#if DEBUG_COOKONTHEFLY
			UE_LOG(LogCook, Display, TEXT("Processing request for package %s"), *FileName.ToString());
#endif

			const FName* PackageName = GetPackageNameCache().GetCachedPackageNameFromStandardFileName(FileName, /* bExactMatchRequired */ false, &FileName);
			if (!PackageName)
			{
				LogCookerMessage(FString::Printf(TEXT("Could not find package at file %s!"), *ToBuild.GetFilename().ToString()), EMessageSeverity::Error);
				UE_LOG(LogCook, Error, TEXT("Could not find package at file %s!"), *ToBuild.GetFilename().ToString());
				UE::Cook::FCompletionCallback CompletionCallback(MoveTemp(ToBuild.GetCompletionCallback()));
				if (CompletionCallback)
				{
					CompletionCallback();
				}
				continue;
			}

			UE::Cook::FPackageData& PackageData(PackageDatas->FindOrAddPackageData(*PackageName, FileName));
			bool bIsUrgent = IsCookOnTheFlyMode();
			PackageData.UpdateRequestData(ToBuild.GetPlatforms(), bIsUrgent, MoveTemp(ToBuild.GetCompletionCallback()));
		}
	}
}

void UCookOnTheFlyServer::PumpRequests(UE::Cook::FTickStackData& StackData)
{
	UE_SCOPED_COOKTIMER(PumpRequests);
	using namespace UE::Cook;

	FRequestQueue& RequestQueue = PackageDatas->GetRequestQueue();
	COOK_STAT(DetailedCookStats::PeakRequestQueueSize = FMath::Max(DetailedCookStats::PeakRequestQueueSize, static_cast<int32>(RequestQueue.Num())));
	if (!RequestQueue.IsEmpty())
	{
		FPackageData* PackageData = RequestQueue.PopRequest();
		FPoppedPackageDataScope Scope(*PackageData);
		ProcessRequest(*PackageData, StackData);
	}
}

void UCookOnTheFlyServer::ProcessRequest(UE::Cook::FPackageData& PackageData, UE::Cook::FTickStackData& StackData)
{
	using namespace UE::Cook;

	if (PackageData.HasAllCookedPlatforms(PackageData.GetRequestedPlatforms(), true /* bIncludeFailed */))
	{
#if DEBUG_COOKONTHEFLY
		UE_LOG(LogCook, Display, TEXT("Package for platform already cooked %s, discarding request"), *OutToBuild.GetFilename().ToString());
#endif
		PackageData.SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
		return;
	}

	const FName& BuildFileName = PackageData.GetFileName();
	FString BuildFileNameString = BuildFileName.ToString();
	if (IsCookByTheBookMode() && CookByTheBookOptions->bErrorOnEngineContentUse)
	{
		check(IsCookingDLC());
		FString DLCPath = FPaths::Combine(*GetBaseDirectoryForDLC(), TEXT("Content"));
		FPaths::MakeStandardFilename(DLCPath);
		if (BuildFileNameString.StartsWith(DLCPath) == false) // if we don't start with the dlc path then we shouldn't be cooking this data 
		{
			UE_LOG(LogCook, Error, TEXT("Engine or Game content %s is being referenced by DLC!"), *BuildFileNameString);
			RejectPackageToLoad(PackageData, TEXT("is base Game/Engine content and we are building DLC that is not allowed to refer to it"));
			return;
		}
	}

	if (PackageTracker->NeverCookPackageList.Contains(BuildFileName))
	{
#if DEBUG_COOKONTHEFLY
		UE_LOG(LogCook, Display, TEXT("Package %s requested but is in the never cook package list, discarding request"), *BuildFileNameString);
#else
		UE_LOG(LogCook, Verbose, TEXT("Package %s requested but is in the never cook package list, discarding request"), *BuildFileNameString);
#endif
		RejectPackageToLoad(PackageData, TEXT("is in the never cook list"));
		return;
	}


	if (!PackageData.GetIsUrgent() && (!IsCookByTheBookMode() || !CookByTheBookOptions->bSkipHardReferences))
	{
		AddDependenciesToLoadQueue(PackageData);
	}
	// AddDependenciesToLoadQueue is supposed to add the dependencies only and not add the passed-in packagedata, so it should still be in request
	check(PackageData.GetState() == EPackageState::Request);
	PackageData.SendToState(EPackageState::LoadPrepare, ESendFlags::QueueAdd);
}

void UCookOnTheFlyServer::AddDependenciesToLoadQueue(UE::Cook::FPackageData& PackageData)
{
	using namespace UE::Cook;

	struct FPackageAndDependencies
	{
		FPackageData& PackageData;
		TArray<FPackageData*> Dependencies;
		int NextDependency = 0;

		explicit FPackageAndDependencies(FPackageData& InPackageData, TArray<FName>& AssetDependenciesScratch, IAssetRegistry* AssetRegistry, FPackageDatas& PackageDatas)
			: PackageData(InPackageData)
		{
			check(!PackageData.GetIsVisited());
			PackageData.SetIsVisited(true);

			AssetDependenciesScratch.Reset();
			// TODO EditorOnly References: We only load Game dependencies, because if we explictly load an EditorOnly dependency, that causes StaticLoadObjectInternal to SetLoadedByEditorPropertiesOnly(false), and we do not want to impact that value with our preloading of required packages 
			if (AssetRegistry->GetDependencies(PackageData.GetPackageName(), AssetDependenciesScratch, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard | UE::AssetRegistry::EDependencyQuery::Game))
			{
				Dependencies.Reserve(AssetDependenciesScratch.Num());
				for (const FName& DependencyName : AssetDependenciesScratch)
				{
					TStringBuilder<256> NameBuffer;
					DependencyName.ToString(NameBuffer);
					if (FPackageName::IsScriptPackage(NameBuffer))
					{
						continue;
					}
					FPackageData* DependencyData = PackageDatas.TryAddPackageDataByPackageName(DependencyName);
					if (!DependencyData || DependencyData == &PackageData)
					{
						continue;
					}
					Dependencies.Add(DependencyData);
				}
			}
		}

		FPackageAndDependencies(FPackageAndDependencies&& Other) = default;
		
		FPackageAndDependencies(const FPackageAndDependencies& Other) = delete;
		FPackageAndDependencies& operator=(const FPackageAndDependencies& Other) = delete;
		FPackageAndDependencies& operator=(FPackageAndDependencies&& Other) = delete;
	};
	TRingBuffer<FPackageAndDependencies> LoadStack;
	TArray<FName> AssetDependenciesScratch;
	const TArray<const ITargetPlatform*>& SessionPlatforms = PlatformManager->GetSessionPlatforms();
	FRequestQueue& RequestQueue = PackageDatas->GetRequestQueue();

	LoadStack.AddFront(FPackageAndDependencies(PackageData, AssetDependenciesScratch, AssetRegistry, *PackageDatas));

	while (!LoadStack.IsEmpty())
	{
		FPackageAndDependencies& PackageAndDependencies(LoadStack.First());
		FPackageData& CurrentPackageData(PackageAndDependencies.PackageData);
		TArray<FPackageData*>& Dependencies(PackageAndDependencies.Dependencies);
		int32& NextDependency(PackageAndDependencies.NextDependency);

		// We search in DFS order so that we end up with a topological sort (or a mostly topological sort when there are cycles)
		bool bAddedDependency = false;
		while (NextDependency < Dependencies.Num())
		{
			FPackageData& DependencyData(*Dependencies[NextDependency++]);
			if (DependencyData.GetState() >= EPackageState::LoadPrepare || DependencyData.GetIsVisited())
			{
				// If it's already been visited, or it's already loading or saving, don't add it again
				// Skipping the add if it's already been visited is how we make sure we still terminate even with cycles in the dependency graph
				continue;
			}
			if (FindObjectFast<UPackage>(nullptr, DependencyData.GetPackageName(), false /* ExactClass */, false /* AnyPackage */))
			{
				// If it's already loaded, no work to do for it
				continue;
			}

			// Move the dependency into the request state and push it onto the DependencyStack, closer to front than the current PackageData that depends on it.
			// Note that since we're moving it into the LoadQueue before we return, we do not need to spend time to add it to (or leave it in) the RequestQueue
			bool bIsUrgent = false;
			if (DependencyData.GetState() == EPackageState::Request)
			{
				RequestQueue.RemoveRequest(&DependencyData);
				DependencyData.UpdateRequestData(SessionPlatforms, bIsUrgent, FCompletionCallback(), ESendFlags::QueueNone);
			}
			else
			{
				DependencyData.UpdateRequestData(SessionPlatforms, bIsUrgent, FCompletionCallback(), ESendFlags::QueueRemove);
			}
			LoadStack.AddFront(FPackageAndDependencies(DependencyData, AssetDependenciesScratch, AssetRegistry, *PackageDatas));
			// PackageAndDependencies is now invalidated
			bAddedDependency = true;
			break;
		}
		if (bAddedDependency)
		{
			// PackageAndDependencies is now invalidated
			continue;
		}
		LoadStack.PopFront();
		// PackageAndDependencies is now invalidated

		check(CurrentPackageData.GetIsVisited());
		CurrentPackageData.SetIsVisited(false);
		if (&CurrentPackageData != &PackageData) // Caller is responsible for queueing the original PackageData; this function just queues dependencies
		{
			// We only record dependencies that were in idle or request, and we push idle to request when adding it to the dependency stack. If a package is no longer in request at this point, then there is a bug up above and we have pushed it into load twice
			check(CurrentPackageData.GetState() == EPackageState::Request); 
			// Send the package to the load queue
			CurrentPackageData.SendToState(EPackageState::LoadPrepare, ESendFlags::QueueAdd);
			COOK_STAT(++DetailedCookStats::NumPreloadedDependencies);
		}
	}
}

void UCookOnTheFlyServer::PumpLoads(UE::Cook::FTickStackData& StackData, uint32 DesiredQueueLength)
{
	using namespace UE::Cook;
	FPackageDataQueue& LoadReadyQueue = PackageDatas->GetLoadReadyQueue();
	FLoadPrepareQueue& LoadPrepareQueue = PackageDatas->GetLoadPrepareQueue();
	FPackageDataMonitor& Monitor = PackageDatas->GetMonitor();
	bool bIsUrgentInProgress = Monitor.GetNumUrgent() > 0;

	while (LoadReadyQueue.Num() + LoadPrepareQueue.Num() > static_cast<int32>(DesiredQueueLength))
	{
		if (StackData.Timer.IsTimeUp())
		{
			return;
		}
		if (bIsUrgentInProgress && !Monitor.GetNumUrgent(EPackageState::LoadPrepare) && !Monitor.GetNumUrgent(EPackageState::LoadReady))
		{
			return;
		}
		COOK_STAT(DetailedCookStats::PeakLoadQueueSize = FMath::Max(DetailedCookStats::PeakLoadQueueSize, LoadPrepareQueue.Num() + LoadReadyQueue.Num()));
		PumpPreloadStarts(); // PumpPreloadStarts after every load so that we keep adding preloads ahead of our need for them

		if (LoadReadyQueue.IsEmpty())
		{
			PumpPreloadCompletes();
			if (LoadReadyQueue.IsEmpty())
			{
				if (!LoadPrepareQueue.IsEmpty())
				{
					bLoadBusy = true;
				}
				break;
			}
		}

		FPackageData& PackageData(*LoadReadyQueue.PopFrontValue());
		FPoppedPackageDataScope Scope(PackageData);
		LoadPackageInQueue(PackageData, StackData.ResultFlags);
		ProcessUnsolicitedPackages(); // May add new packages into the LoadQueue
	}
}

void UCookOnTheFlyServer::PumpPreloadCompletes()
{
	using namespace UE::Cook;

	FPackageDataQueue& PreloadingQueue = PackageDatas->GetLoadPrepareQueue().PreloadingQueue;
	const bool bLocalPreloadingEnabled = bPreloadingEnabled;
	while (!PreloadingQueue.IsEmpty())
	{
		FPackageData* PackageData = PreloadingQueue.First();
		if (!bLocalPreloadingEnabled || PackageData->TryPreload())
		{
			// Ready to go
			PreloadingQueue.PopFront();
			PackageData->SendToState(EPackageState::LoadReady, ESendFlags::QueueAdd);
			continue;
		}
		break;
	}
}

void UCookOnTheFlyServer::PumpPreloadStarts()
{
	using namespace UE::Cook;

	FPackageDataMonitor& Monitor = PackageDatas->GetMonitor();
	FLoadPrepareQueue& LoadPrepareQueue = PackageDatas->GetLoadPrepareQueue();
	FPackageDataQueue& PreloadingQueue = LoadPrepareQueue.PreloadingQueue;
	FPackageDataQueue& EntryQueue = LoadPrepareQueue.EntryQueue;

	const bool bLocalPreloadingEnabled = bPreloadingEnabled;
	while (!EntryQueue.IsEmpty() && Monitor.GetNumPreloadAllocated() < static_cast<int32>(MaxPreloadAllocated))
	{
		FPackageData* PackageData = EntryQueue.PopFrontValue();
		if (bLocalPreloadingEnabled)
		{
			PackageData->TryPreload();
		}
		PreloadingQueue.Add(PackageData);
	}
}

void UCookOnTheFlyServer::LoadPackageInQueue(UE::Cook::FPackageData& PackageData, uint32& ResultFlags)
{
	UPackage* LoadedPackage = nullptr;

	FName PackageFileName(PackageData.GetFileName());
	bool bLoadFullySuccessful = LoadPackageForCooking(PackageData, LoadedPackage);
	if (!bLoadFullySuccessful)
	{
		ResultFlags |= COSR_ErrorLoadingPackage;
		UE_LOG(LogCook, Verbose, TEXT("Not cooking package %s"), *PackageFileName.ToString());
		RejectPackageToLoad(PackageData, TEXT("failed to load"));
		return;
	}
	check(LoadedPackage != nullptr && LoadedPackage->IsFullyLoaded());

	FName NewPackageFileName(GetPackageNameCache().GetCachedStandardFileName(LoadedPackage));
	if (LoadedPackage->GetFName() != PackageData.GetPackageName())
	{
		// The PackageName is not the name that we loaded. This can happen due to CoreRedirects.
		// We refuse to cook requests for packages that no longer exist in PumpExternalRequests, but it is possible
		// that a CoreRedirect exists from a (externally requested or requested as a reference) package that still exists.
		// Mark the original PackageName as cooked for all platforms and send a request to cook the new FileName
		check(NewPackageFileName != PackageFileName);

		UE_LOG(LogCook, Verbose, TEXT("Request for %s received going to save %s"), *PackageFileName.ToString(), *NewPackageFileName.ToString());
		UE::Cook::FPackageData& OtherPackageData = PackageDatas->AddPackageDataByPackageNameChecked(LoadedPackage->GetFName());
		OtherPackageData.UpdateRequestData(PackageData.GetRequestedPlatforms(), PackageData.GetIsUrgent(), UE::Cook::FCompletionCallback());

		PackageData.AddCookedPlatforms(PlatformManager->GetSessionPlatforms(), true);
		RejectPackageToLoad(PackageData, TEXT("is redirected to another filename"));
		return;
	}

	if (NewPackageFileName != PackageFileName)
	{
		// This case should never happen since we are checking for the existence of the file in PumpExternalRequests
		UE_LOG(LogCook, Warning, TEXT("Unexpected change in FileName when loading a requested package. \"%s\" changed to \"%s\"."),
			*PackageFileName.ToString(), *NewPackageFileName.ToString());

		UE_LOG(LogCook, Verbose, TEXT("Request for %s received going to save %s"), *PackageFileName.ToString(), *NewPackageFileName.ToString());
		// The package FileName is not the FileName that we loaded
		//  sounds unpossible.... but it is due to searching for files and such
		PackageDatas->UpdateFileName(LoadedPackage->GetFName());
		// Register the FileName that we loaded as pointing to this PackageData
		PackageDatas->RegisterFileNameAlias(PackageData, PackageFileName);
	}

	if (PackageData.HasAllCookedPlatforms(PackageData.GetRequestedPlatforms(), true))
	{
		// Already cooked. This can happen if we needed to load a package that was previously cooked and garbage collected because it is a loaddependency of a new request.
		// Send the package back to idle, nothing further to do with it.
		PackageData.SendToState(UE::Cook::EPackageState::Idle, UE::Cook::ESendFlags::QueueAdd);
	}
	else
	{
		PackageData.SetPackage(LoadedPackage);
		PackageData.SendToState(UE::Cook::EPackageState::Save, UE::Cook::ESendFlags::QueueAdd);
	}
}

void UCookOnTheFlyServer::RejectPackageToLoad(UE::Cook::FPackageData& PackageData, const TCHAR* Reason)
{
	// make sure this package doesn't exist
	for (const ITargetPlatform* TargetPlatform : PackageData.GetRequestedPlatforms())
	{
		const FString SandboxFilename = ConvertToFullSandboxPath(PackageData.GetFileName().ToString(), true, TargetPlatform->PlatformName());
		if (IFileManager::Get().FileExists(*SandboxFilename))
		{
			// if we find the file this means it was cooked on a previous cook, however source package can't be found now. 
			// this could be because the source package was deleted or renamed, and we are using iterative cooking
			// perhaps in this case we should delete it?
			UE_LOG(LogCook, Warning, TEXT("Found cooked file '%s' which shouldn't exist as it %s."), *SandboxFilename, Reason);
			IFileManager::Get().Delete(*SandboxFilename);
		}
	}
	PackageData.SendToState(UE::Cook::EPackageState::Idle, UE::Cook::ESendFlags::QueueAdd);
}

//////////////////////////////////////////////////////////////////////////

void UCookOnTheFlyServer::FilterLoadedPackage(UPackage* Package, bool bUpdatePlatforms)
{
	check(Package != nullptr);

	const FName FileName = GetPackageNameCache().GetCachedStandardFileName(Package);
	if (FileName.IsNone())
	{
		return;	// if we have name none that means we are in core packages or something...
	}
	UE::Cook::FPackageData& PackageData = PackageDatas->FindOrAddPackageData(Package->GetFName(), FileName);

	const TArray<const ITargetPlatform*>& TargetPlatforms = PlatformManager->GetSessionPlatforms();
	if (PackageData.HasAllCookedPlatforms(TargetPlatforms, true /* bIncludeFailed */))
	{
		// All SessionPlatforms have already been cooked for the package, so we don't need to save it again
		return;
	}

	bool bIsUrgent = false;
	if (PackageData.IsInProgress())
	{
		if (bUpdatePlatforms)
		{
			PackageData.UpdateRequestData(TargetPlatforms, bIsUrgent, UE::Cook::FCompletionCallback());
		}
	}
	else
	{
		if (!IsCookByTheBookMode() || !CookByTheBookOptions->bSkipHardReferences)
		{
			// Send this unsolicited package into the LoadReadyQueue to fully load it and send it on to the SaveQueue
			PackageData.SetRequestData(TargetPlatforms, bIsUrgent, UE::Cook::FCompletionCallback());
			PackageData.SendToState(UE::Cook::EPackageState::LoadReady, UE::Cook::ESendFlags::QueueNone);
			// Send it to the front of the LoadReadyQueue since it is mostly loaded already
			PackageDatas->GetLoadReadyQueue().AddFront(&PackageData);
		}
	}
}

void UCookOnTheFlyServer::UpdatePackageFilter()
{
	if (!bPackageFilterDirty)
	{
		return;
	}
	bPackageFilterDirty = false;

	UE_SCOPED_COOKTIMER(UpdatePackageFilter);
	for (UPackage* Package : PackageTracker->LoadedPackages)
	{
		FilterLoadedPackage(Package, true);
	}
}

void UCookOnTheFlyServer::OnRemoveSessionPlatform(const ITargetPlatform* TargetPlatform)
{
	PackageDatas->OnRemoveSessionPlatform(TargetPlatform);
	ExternalRequests->OnRemoveSessionPlatform(TargetPlatform);
}

void UCookOnTheFlyServer::TickNetwork()
{
	// Only CookOnTheFly handles network requests
	// It is not safe to call PruneUnreferencedSessionPlatforms in CookByTheBook because StartCookByTheBook does not AddRef its session platforms
	if (IsCookOnTheFlyMode())
	{
		if (IsInSession() && !bCookOnTheFlyExternalRequests)
		{
			PlatformManager->PruneUnreferencedSessionPlatforms(*this);
		}
		else
		{
			// Process callbacks in case there is a callback pending that needs to create a session
			TArray<UE::Cook::FSchedulerCallback> Callbacks;
			if (ExternalRequests->DequeueCallbacks(Callbacks))
			{
				for (UE::Cook::FSchedulerCallback& Callback : Callbacks)
				{
					Callback();
				}
			}
		}
	}
}

bool UCookOnTheFlyServer::BeginPackageCacheForCookedPlatformData(UE::Cook::FPackageData& PackageData, UE::Cook::FCookerTimer& Timer)
{
	if (PackageData.GetCookedPlatformDataCalled())
	{
		return true;
	}

	if (!PackageData.GetCookedPlatformDataStarted())
	{
		if (PackageData.GetNumPendingCookedPlatformData() > 0)
		{
			// A previous Save was started and deleted after some calls to BeginCacheForCookedPlatformData occurred, and some of those objects have still not returned true for IsCachedCookedPlatformDataLoaded
			// We need to wait for all of pending async calls from the cancelled save to finish before we start the new ones
			return false;
		}
		PackageData.SetCookedPlatformDataStarted(true);
	}

	UE_SCOPED_HIERARCHICAL_COOKTIMER_AND_DURATION(BeginPackageCacheForCookedPlatformData, DetailedCookStats::TickCookOnTheSideBeginPackageCacheForCookedPlatformDataTimeSec);

#if DEBUG_COOKONTHEFLY 
	UE_LOG(LogCook, Display, TEXT("Caching objects for package %s"), *Package->GetFName().ToString());
#endif
	UPackage* Package = PackageData.GetPackage();
	check(Package && Package->IsFullyLoaded());
	check(PackageData.GetState() == UE::Cook::EPackageState::Save);
	PackageData.CreateObjectCache();

	// Note that we cache cooked data for all requested platforms, rather than only for the requested platforms that have not cooked yet.  This allows
	// us to avoid the complexity of needing to cancel the Save and keep track of the old list of uncooked platforms whenever the cooked platforms change
	// while BeginPackageCacheForCookedPlatformData is active.
	// Currently this does not cause significant cost since saving new platforms with some platforms already saved is a rare operation.

	const TArray<const ITargetPlatform*>& TargetPlatforms = PackageData.GetRequestedPlatforms();
	int NumPlatforms = TargetPlatforms.Num();
	TArray<FWeakObjectPtr>& CachedObjectsInOuter = PackageData.GetCachedObjectsInOuter();
	int32& CookedPlatformDataNextIndex = PackageData.GetCookedPlatformDataNextIndex();
	FWeakObjectPtr* CachedObjectsInOuterData = CachedObjectsInOuter.GetData();

	int NumIndexes = CachedObjectsInOuter.Num() * NumPlatforms;
	while (CookedPlatformDataNextIndex < NumIndexes)
	{
		int ObjectIndex = CookedPlatformDataNextIndex / NumPlatforms;
		int PlatformIndex = CookedPlatformDataNextIndex - ObjectIndex * NumPlatforms;
		UObject* Obj = CachedObjectsInOuterData[ObjectIndex].Get();
		if (!Obj)
		{
			// Objects can be marked as pending kill even without a garbage collect, and our weakptr.get will return null for them, so we have to always check the WeakPtr before using it
			// Treat objects that have been marked as pending kill or deleted as no-longer-required for BeginCacheForCookedPlatformData and ClearAllCachedCookedPlatformData
			CachedObjectsInOuterData[ObjectIndex] = nullptr; // If the weakptr is merely pendingkill, set it to null explicitly so we don't think that we've called BeginCacheForCookedPlatformData on it if it gets unmarked pendingkill later
			++CookedPlatformDataNextIndex;
			continue;
		}
		const ITargetPlatform* TargetPlatform = TargetPlatforms[PlatformIndex];

		if (Obj->IsA(UMaterialInterface::StaticClass()))
		{
			if (GShaderCompilingManager->GetNumRemainingJobs() + 1 > MaxConcurrentShaderJobs)
			{
#if DEBUG_COOKONTHEFLY
				UE_LOG(LogCook, Display, TEXT("Delaying shader compilation of material %s"), *Obj->GetFullName());
#endif
				return false;
			}
		}

		const FName ClassFName = Obj->GetClass()->GetFName();
		int32* CurrentAsyncCache = CurrentAsyncCacheForType.Find(ClassFName);
		if (CurrentAsyncCache != nullptr)
		{
			if (*CurrentAsyncCache < 1)
			{
				return false;
			}
			*CurrentAsyncCache -= 1;
		}

		Obj->BeginCacheForCookedPlatformData(TargetPlatform);
		++CookedPlatformDataNextIndex;
		if (Obj->IsCachedCookedPlatformDataLoaded(TargetPlatform))
		{
			if (CurrentAsyncCache)
			{
				*CurrentAsyncCache += 1;
			}
		}
		else
		{
			bool bNeedsResourceRelease = CurrentAsyncCache != nullptr;
			PackageDatas->GetPendingCookedPlatformDatas().Emplace(Obj, TargetPlatform, PackageData, bNeedsResourceRelease, *this);
		}

		if (Timer.IsTimeUp())
		{
#if DEBUG_COOKONTHEFLY
			UE_LOG(LogCook, Display, TEXT("Object %s took too long to cache"), *Obj->GetFullName());
#endif
			return false;
		}
	}

	PackageData.SetCookedPlatformDataCalled(true);
	return true;
}

bool UCookOnTheFlyServer::FinishPackageCacheForCookedPlatformData(UE::Cook::FPackageData& PackageData, UE::Cook::FCookerTimer& Timer)
{
	if (PackageData.GetCookedPlatformDataComplete())
	{
		return true;
	}

	if (!PackageData.GetCookedPlatformDataCalled())
	{
		if (!BeginPackageCacheForCookedPlatformData(PackageData, Timer))
		{
			return false;
		}
		check(PackageData.GetCookedPlatformDataCalled());
	}

	if (PackageData.GetNumPendingCookedPlatformData() > 0)
	{
		return false;
	}

	PackageData.SetCookedPlatformDataComplete(true);
	return true;
}

void UCookOnTheFlyServer::ReleaseCookedPlatformData(UE::Cook::FPackageData& PackageData)
{
	using namespace UE::Cook;

	if (!PackageData.GetCookedPlatformDataStarted())
	{
		PackageData.CheckCookedPlatformDataEmpty();
		return;
	}

	// For every Object on which we called BeginCacheForCookedPlatformData, we need to call ClearAllCachedCookedPlatformData
	if (PackageData.GetCookedPlatformDataComplete())
	{
		// Since we have completed CookedPlatformData, we know we called BeginCacheForCookedPlatformData on all objects in the package, and none are pending
		if (!IsCookingInEditor()) // ClearAllCachedCookedPlatformData calls are only used when not in editor
		{
			UE_SCOPED_HIERARCHICAL_COOKTIMER(ClearAllCachedCookedPlatformData);
			for (FWeakObjectPtr& WeakPtr: PackageData.GetCachedObjectsInOuter())
			{
				UObject* Object = WeakPtr.Get();
				if (Object)
				{
					Object->ClearAllCachedCookedPlatformData();
				}
			}
		}
		PackageData.ClearCookedPlatformData();
		return;
	}

	// This is an exceptional flow handling case; we are releasing the CookedPlatformData before we called SavePackage
	// Note that even after we return from this function, some objects with pending IsCachedCookedPlatformDataLoaded calls may still exist for this Package in PackageDatas->GetPendingCookedPlatformDatas(),
	// and this PackageData may therefore still have GetNumPendingCookedPlatformData > 0
	if (!IsCookingInEditor()) // ClearAllCachedCookedPlatformData calls are only used when not in editor.
	{
		int32 NumPlatforms = PackageData.GetRequestedPlatforms().Num();
		if (NumPlatforms > 0) // Shouldn't happen because PumpSaves checks for this, but avoid a divide by 0 if it does.
		{
			// We have only called BeginCacheForCookedPlatformData on Object,Platform pairs up to GetCookedPlatformDataNextIndex.
			// Further, some of those calls might still be pending.

			// Find all pending BeginCacheForCookedPlatformData for this FPackageData
			TMap<UObject*, TArray<FPendingCookedPlatformData*>> PendingObjects;
			for (FPendingCookedPlatformData& PendingCookedPlatformData : PackageDatas->GetPendingCookedPlatformDatas())
			{
				if (&PendingCookedPlatformData.PackageData == &PackageData && !PendingCookedPlatformData.PollIsComplete())
				{
					UObject* Object = PendingCookedPlatformData.Object.Get();
					check(Object); // Otherwise PollIsComplete would have returned true
					check(!PendingCookedPlatformData.bHasReleased); // bHasReleased should be false since PollIsComplete returned false
					PendingObjects.FindOrAdd(Object).Add(&PendingCookedPlatformData);
				}
			}

			// Iterate over all objects in the FPackageData up to GetCookedPlatformDataNextIndex
			TArray<FWeakObjectPtr>& CachedObjects = PackageData.GetCachedObjectsInOuter();
			int32 NumIndexes = PackageData.GetCookedPlatformDataNextIndex();
			check(NumIndexes <= NumPlatforms * CachedObjects.Num());
			// GetCookedPlatformDataNextIndex is a value in an inline iteration over the two-dimensional array of Objects x Platforms, in Object-major order.
			// We take the ceiling of NextIndex/NumPlatforms to get the number of objects.
			int32 NumObjects = (NumIndexes + NumPlatforms - 1) / NumPlatforms;
			for (int32 ObjectIndex = 0; ObjectIndex < NumObjects; ++ObjectIndex)
			{
				UObject* Object = CachedObjects[ObjectIndex].Get();
				if (!Object)
				{
					continue;
				}
				TArray<FPendingCookedPlatformData*>* PendingDatas = PendingObjects.Find(Object);
				if (!PendingDatas || PendingDatas->Num() == 0)
				{
					// No pending BeginCacheForCookedPlatformData calls for this object; clear it now.
					Object->ClearAllCachedCookedPlatformData();
				}
				else
				{
					// For any pending Objects, we add a CancelManager to the FPendingCookedPlatformData to call ClearAllCachedCookedPlatformData when the pending Object,Platform pairs for that object complete.
					FPendingCookedPlatformDataCancelManager* CancelManager = new FPendingCookedPlatformDataCancelManager();
					CancelManager->NumPendingPlatforms = PendingDatas->Num();
					for (FPendingCookedPlatformData* PendingCookedPlatformData : *PendingDatas)
					{
						// We never start a new package until after the previous cancel finished, so all of the FPendingCookedPlatformData for the PlatformData we are cancelling can not have been cancelled before.  We would leak the CancelManager if we overwrote it here.
						check(PendingCookedPlatformData->CancelManager == nullptr);
						// If bHasReleaased on the PendingCookedPlatformData were already true, we would leak the CancelManager because the PendingCookedPlatformData would never call Release on it.
						check(!PendingCookedPlatformData->bHasReleased);
						PendingCookedPlatformData->CancelManager = CancelManager;
					}
				}
			}
		}
	}
	PackageData.ClearCookedPlatformData();
}

void UCookOnTheFlyServer::TickCancels()
{
	PackageDatas->PollPendingCookedPlatformDatas();
}

bool UCookOnTheFlyServer::LoadPackageForCooking(UE::Cook::FPackageData& PackageData, UPackage*& OutPackage)
{
	UE_SCOPED_HIERARCHICAL_COOKTIMER_AND_DURATION(LoadPackageForCooking, DetailedCookStats::TickCookOnTheSideLoadPackagesTimeSec);

	check(PackageTracker->LoadingPackageData == nullptr);
	PackageTracker->LoadingPackageData = &PackageData;
	ON_SCOPE_EXIT
	{
		PackageTracker->LoadingPackageData = nullptr;
	};

	OutPackage = NULL;
	FString PackageNameString;
	OutPackage = FindObject<UPackage>(ANY_PACKAGE, *PackageData.GetPackageName().ToString());

	FString FileName(PackageData.GetFileName().ToString());
#if DEBUG_COOKONTHEFLY
	UE_LOG(LogCook, Display, TEXT("Processing request %s"), *FileName);
#endif
	static TSet<FString> CookWarningsList;
	if (CookWarningsList.Contains(FileName) == false)
	{
		CookWarningsList.Add(FileName);
		GOutputCookingWarnings = IsCookFlagSet(ECookInitializationFlags::OutputVerboseCookerWarnings);
	}

	bool bSuccess = true;
	//  if the package is not yet fully loaded then fully load it
	if (OutPackage == nullptr || !OutPackage->IsFullyLoaded())
	{
		bool bWasPartiallyLoaded = OutPackage != nullptr;
		GIsCookerLoadingPackage = true;
		UPackage* LoadedPackage = LoadPackage(NULL, *FileName, LOAD_None);
		if (LoadedPackage)
		{
			OutPackage = LoadedPackage;

			if (bWasPartiallyLoaded)
			{
				// If fully loading has caused a blueprint to be regenerated, make sure we eliminate all meta data outside the package
				UMetaData* MetaData = LoadedPackage->GetMetaData();
				MetaData->RemoveMetaDataOutsidePackage();
			}
		}
		else
		{
			bSuccess = false;
		}

		++this->StatLoadedPackageCount;

		GIsCookerLoadingPackage = false;
	}
#if DEBUG_COOKONTHEFLY
	else
	{
		UE_LOG(LogCook, Display, TEXT("Package already loaded %s avoiding reload"), *FileName);
	}
#endif

	if (!bSuccess)
	{
		if ((!IsCookOnTheFlyMode()) || (!IsCookingInEditor()))
		{
			LogCookerMessage(FString::Printf(TEXT("Error loading %s!"), *FileName), EMessageSeverity::Error);
		}
	}
	GOutputCookingWarnings = false;
	return bSuccess;
}


void UCookOnTheFlyServer::ProcessUnsolicitedPackages()
{
	// Ensure sublevels are loaded by iterating all recently loaded packages and invoking
	// PostLoadPackageFixup

	{
		UE_SCOPED_HIERARCHICAL_COOKTIMER(PostLoadPackageFixup);

		TArray<UPackage*> NewPackages = PackageTracker->GetNewPackages();

		for (UPackage* Package : NewPackages)
		{
			if (!IsCookByTheBookMode() || !CookByTheBookOptions->bSkipSoftReferences)
			{
				PostLoadPackageFixup(Package);
			}
			FilterLoadedPackage(Package, false);
		}
	}
}

UE_TRACE_EVENT_BEGIN(UE_CUSTOM_COOKTIMER_LOG, SaveCookedPackage, NoSync)
	UE_TRACE_EVENT_FIELD(Trace::WideString, PackageName)
UE_TRACE_EVENT_END()

void UCookOnTheFlyServer::PumpSaves(UE::Cook::FTickStackData& StackData, uint32 DesiredQueueLength)
{
	using namespace UE::Cook;

	UE_SCOPED_HIERARCHICAL_COOKTIMER(SavingPackages);
	check(IsInGameThread());

	// save as many packages as we can during our time slice
	FPackageDataQueue& SaveQueue = PackageDatas->GetSaveQueue();
	const uint32 OriginalPackagesToSaveCount = SaveQueue.Num();
	uint32 HandledCount = 0;
	TArray<const ITargetPlatform*, TInlineAllocator<ExpectedMaxNumPlatforms>> PlatformsForPackage;
	COOK_STAT(DetailedCookStats::PeakSaveQueueSize = FMath::Max(DetailedCookStats::PeakSaveQueueSize, SaveQueue.Num()));
	while (SaveQueue.Num() > static_cast<int32>(DesiredQueueLength))
	{
		FPackageData& PackageData(*SaveQueue.PopFrontValue());
		FPoppedPackageDataScope PoppedScope(PackageData);
		UPackage* Package = PackageData.GetPackage();
		
		check(Package != nullptr);
		++HandledCount;

#if DEBUG_COOKONTHEFLY
		UE_LOG(LogCook, Display, TEXT("Processing save for package %s"), *Package->GetName());
#endif

		if (Package->IsLoadedByEditorPropertiesOnly() && PackageTracker->UncookedEditorOnlyPackages.Contains(Package->GetFName()))
		{
			// We already attempted to cook this package and it's still not referenced by any non editor-only properties.
			PackageData.SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
			continue;
		}

		// This package is valid, so make sure it wasn't previously marked as being an uncooked editor only package or it would get removed from the
		// asset registry at the end of the cook
		PackageTracker->UncookedEditorOnlyPackages.Remove(Package->GetFName());

		if (PackageTracker->NeverCookPackageList.Contains(PackageData.GetFileName()))
		{
			// refuse to save this package, it's clearly one of the undesirables
			PackageData.SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
			continue;
		}

		// Cook only the session platforms that have not yet been cooked for the given package
		PackageData.GetUncookedPlatforms(PackageData.GetRequestedPlatforms(), PlatformsForPackage);
		if (PlatformsForPackage.Num() == 0)
		{
			// We've already saved all possible platforms for this package; this should not be possible.
			// All places that add a package to the save queue check for existence of incomplete platforms before adding
			UE_LOG(LogCook, Warning, TEXT("Package '%s' in SaveQueue has no more platforms left to cook; this should not be possible!"), *PackageData.GetFileName().ToString());
			PackageData.SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
			continue;
		}

		bool bShouldFinishTick = false;
		if (IsCookOnTheFlyMode())
		{
			if (!PackageData.GetIsUrgent())
			{
				if (ExternalRequests->HasRequests() || PackageDatas->GetMonitor().GetNumUrgent() > 0)
				{
					bShouldFinishTick = true;
				}
				if (StackData.Timer.IsTimeUp())
				{
					// our timeslice is up
					bShouldFinishTick = true;
				}
			}
			else
			{
				if (IsRealtimeMode())
				{
					if (StackData.Timer.IsTimeUp())
					{
						// our timeslice is up
						bShouldFinishTick = true;
					}
				}
				else
				{
					// if we are cook on the fly and not in the editor then save the requested package as fast as we can because the client is waiting on it
					// Until we are blocked on async work, ignore the timer
				}
			}
		}
		else // !IsCookOnTheFlyMode
		{
			check(IsCookByTheBookMode());
			if (StackData.Timer.IsTimeUp())
			{
				// our timeslice is up
				bShouldFinishTick = true;
			}
		}
		if (bShouldFinishTick)
		{
			SaveQueue.AddFront(&PackageData);
			return;
		}

		// Release any completed pending CookedPlatformDatas, so that slots in the per-class limits on calls to BeginCacheForCookedPlatformData are freed up for new objects to use
		PackageDatas->PollPendingCookedPlatformDatas();

		// Always wait for FinishPackageCacheForCookedPlatformData before attempting to save the package
		bool AllObjectsCookedDataCached = FinishPackageCacheForCookedPlatformData(PackageData, StackData.Timer);

		// If the CookPlatformData is not ready then postpone the package, exit, or wait for it as appropriate
		if (!AllObjectsCookedDataCached)
		{
			// Can we postpone?
			if (!PackageData.GetIsUrgent())
			{
				bool HasCheckedAllPackagesAreCached = HandledCount >= OriginalPackagesToSaveCount;
				if (!HasCheckedAllPackagesAreCached)
				{
					SaveQueue.Add(&PackageData);
					continue;
				}
			}
			// Should we wait?
			if (PackageData.GetIsUrgent() && !IsRealtimeMode())
			{
				UE_SCOPED_HIERARCHICAL_COOKTIMER(WaitingForCachedCookedPlatformData);
				do
				{
					// FinishPackageCacheForCookedPlatformData might block on pending CookedPlatformDatas, and it might block on BeginPackageCacheForCookedPlatformData, which can
					// block on resources held by other CookedPlatformDatas. Calling PollPendingCookedPlatformDatas should handle pumping all of those.
					check(PackageDatas->GetPendingCookedPlatformDatas().Num() || !PackageData.GetCookedPlatformDataCalled()); // FinishPackageCacheForCookedPlatformData can only return false in one of these cases
					// sleep for a bit
					FPlatformProcess::Sleep(0.0f);
					// Poll the results again and check whether we are now done
					PackageDatas->PollPendingCookedPlatformDatas();
					AllObjectsCookedDataCached = FinishPackageCacheForCookedPlatformData(PackageData, StackData.Timer);
				} while (!StackData.Timer.IsTimeUp() && !AllObjectsCookedDataCached);
			}
			// If we couldn't postpone or wait, then we need to exit and try again later
			if (!AllObjectsCookedDataCached)
			{
				StackData.ResultFlags |= COSR_WaitingOnCache;
				bSaveBusy = true;
				SaveQueue.AddFront(&PackageData);
				return;
			}
		}
		check(AllObjectsCookedDataCached == true); // We are not allowed to save until FinishPackageCacheForCookedPlatformData returns true.  We should have early exited above if it didn't

		// precache the next few packages
		if (!IsCookOnTheFlyMode() && SaveQueue.Num() != 0)
		{
			UE_SCOPED_HIERARCHICAL_COOKTIMER(PrecachePlatformDataForNextPackage);
			const int32 NumberToPrecache = 2;
			int32 LeftToPrecache = NumberToPrecache;
			for (FPackageData* NextData : SaveQueue)
			{
				if (LeftToPrecache == 0)
				{
					break;
				}
				--LeftToPrecache;
				BeginPackageCacheForCookedPlatformData(*NextData, StackData.Timer);
			}

			// If we're in RealTimeMode, check whether the precaching overflowed our timer and if so exit before we do the potentially expensive SavePackage
			// For non-realtime, overflowing the timer is not a critical issue.
			if (IsRealtimeMode() && StackData.Timer.IsTimeUp())
			{
				SaveQueue.AddFront(&PackageData);
				return;
			}
		}

		TArray<bool> SucceededSavePackage;
		TArray<FSavePackageResultStruct> SavePackageResults;
		{
			UE_SCOPED_HIERARCHICAL_CUSTOM_COOKTIMER_AND_DURATION(SaveCookedPackage, DetailedCookStats::TickCookOnTheSideSaveCookedPackageTimeSec)
				UE_ADD_CUSTOM_COOKTIMER_META(SaveCookedPackage, PackageName, *PackageData.GetFileName().ToString());

			uint32 SaveFlags = SAVE_KeepGUID | (bSaveAsyncAllowed ? SAVE_Async : SAVE_None) | (IsCookFlagSet(ECookInitializationFlags::Unversioned) ? SAVE_Unversioned : 0);

			bool KeepEditorOnlyPackages = false;
			// removing editor only packages only works when cooking in commandlet and non iterative cooking
			// also doesn't work in multiprocess cooking
			KeepEditorOnlyPackages = !(IsCookByTheBookMode() && !IsCookingInEditor());
			KeepEditorOnlyPackages |= IsCookFlagSet(ECookInitializationFlags::Iterative);
			SaveFlags |= KeepEditorOnlyPackages ? SAVE_KeepEditorOnlyCookedPackages : SAVE_None;
			SaveFlags |= CookByTheBookOptions ? SAVE_ComputeHash : SAVE_None;

			GOutputCookingWarnings = IsCookFlagSet(ECookInitializationFlags::OutputVerboseCookerWarnings);

			{
				// SaveCookedPackage can CollectGarbage, so we need to store the currently-unqueued PackageData in a separate variable that we register for garbage collection
				check(SavingPackageData == nullptr);
				SavingPackageData = &PackageData;
				try
				{
					SaveCookedPackage(PackageData, SaveFlags, PlatformsForPackage, SavePackageResults);
				}
				catch (std::exception&)
				{
					FString TargetPlatforms;
					for (const ITargetPlatform* Platform : PlatformsForPackage)
					{
						TargetPlatforms += FString::Printf(TEXT("%s, "), *Platform->PlatformName());
					}
					UE_LOG(LogCook, Warning, TEXT("Tried to save package %s for target platforms %s but threw an exception"), *Package->GetName(), *TargetPlatforms);
					SavePackageResults.Empty(PlatformsForPackage.Num());
					for (int n = 0; n < PlatformsForPackage.Num(); ++n)
					{
						SavePackageResults.Add(ESavePackageResult::Error);
					}
				}
				SavingPackageData = nullptr;
			}

			GOutputCookingWarnings = false;
			check(PlatformsForPackage.Num() == SavePackageResults.Num());
			for (int iResultIndex = 0; iResultIndex < SavePackageResults.Num(); iResultIndex++)
			{
				FSavePackageResultStruct& SavePackageResult = SavePackageResults[iResultIndex];

				if (SavePackageResult == ESavePackageResult::Success || SavePackageResult == ESavePackageResult::GenerateStub || SavePackageResult == ESavePackageResult::ReplaceCompletely)
				{
					SucceededSavePackage.Add(true);
					// Update flags used to determine garbage collection.
					if (Package->ContainsMap())
					{
						StackData.ResultFlags |= COSR_CookedMap;
					}
					else
					{
						++StackData.CookedPackageCount;
						StackData.ResultFlags |= COSR_CookedPackage;
					}

					// Update asset registry
					if (CookByTheBookOptions)
					{
						FAssetRegistryGenerator* Generator = PlatformManager->GetPlatformData(PlatformsForPackage[iResultIndex])->RegistryGenerator.Get();
						UpdateAssetRegistryPackageData(Generator, *Package, SavePackageResult);
					}
				}
				else
				{
					SucceededSavePackage.Add(false);
				}
			}
			check(SavePackageResults.Num() == SucceededSavePackage.Num());
			StackData.Timer.SavedPackage();
		}

		if (!IsCookingInEditor())
		{
			ReleaseCookedPlatformData(PackageData);
			if (CurrentCookMode == ECookMode::CookByTheBook)
			{
				// For each object for which data is cached we can call FinishedCookedPlatformDataCache
				// we can only safely call this when we are finished caching the object completely.
				// this doesn't ever happen for cook in editor or cook on the fly mode
				for (FWeakObjectPtr& WeakPtr: PackageData.GetCachedObjectsInOuter())
				{
					UObject* Obj = WeakPtr.Get();
					if (Obj)
					{
						Obj->WillNeverCacheCookedPlatformDataAgain();
					}
				}

				if (Package->LinkerLoad)
				{
					// Loaders and their handles can have large buffers held in process memory and in the system file cache from the
					// data that was loaded.  Keeping this for the lifetime of the cook is costly, so we try and unload it here.
					Package->LinkerLoad->FlushCache();
				}
			}
		}

		FName FileName = PackageData.GetFileName();

		// We always want to mark package as processed unless it wasn't saved because it was referenced by editor-only data
		// in which case we may still need to save it later when new content loads it through non editor-only references
		if (!FileName.IsNone())
		{
			// mark the package as cooked
			bool bWasReferencedOnlyByEditorOnlyData = false;
			for (const FSavePackageResultStruct& SavePackageResult : SavePackageResults)
			{
				if (SavePackageResult == ESavePackageResult::ReferencedOnlyByEditorOnlyData)
				{
					bWasReferencedOnlyByEditorOnlyData = true;
					// if this is the case all of the platforms should be referenced only by editor only data
				}
			}
			if (!bWasReferencedOnlyByEditorOnlyData)
			{
				PackageData.AddCookedPlatforms(PackageData.GetRequestedPlatforms(), SucceededSavePackage);

				if ((CurrentCookMode == ECookMode::CookOnTheFly) && !PackageData.GetIsUrgent())
				{
					// this is an unsolicited package
					if (FPaths::FileExists(FileName.ToString()))
					{
						PackageTracker->UnsolicitedCookedPackages.AddCookedPackage(FFilePlatformRequest(FileName, PlatformsForPackage));

#if DEBUG_COOKONTHEFLY
						UE_LOG(LogCook, Display, TEXT("UnsolicitedCookedPackages: %s"), *FileName.ToString());
#endif
					}
				}
			}
			else
			{
				PackageTracker->UncookedEditorOnlyPackages.AddUnique(Package->GetFName());
			}
		}
		else
		{
			for (const bool bSucceededSavePackage : SucceededSavePackage)
			{
				check(bSucceededSavePackage == false);
			}
		}

		PackageData.SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
	}
}

void UCookOnTheFlyServer::UpdateAssetRegistryPackageData(FAssetRegistryGenerator* Generator, const UPackage& Package, FSavePackageResultStruct& SavePackageResult)
{
	if (!Generator)
		return;

	// Ensure all assets in the package are recorded in the registry
	Generator->CreateOrFindAssetDatas(Package);

	const FName PackageName = Package.GetFName();
	FAssetPackageData* AssetPackageData = Generator->GetAssetPackageData(PackageName);
	AssetPackageData->DiskSize = SavePackageResult.TotalFileSize;
	// If there is no hash (e.g.: when SavePackageResult == ESavePackageResult::ReplaceCompletely), don't attempt to setup a continuation to update
	// the AssetRegistry entry with it later.  Just leave the asset registry entry with a default constructed FMD5Hash which is marked as invalid.
	if (SavePackageResult.CookedHash.IsValid())
	{
		SavePackageResult.CookedHash.Next([AssetPackageData](const FMD5Hash& CookedHash)
		{
			// Store the cooked hash in the Asset Registry when it is done computing in another thread.
			// NOTE: For this to work, we rely on:
			// 1) UPackage::WaitForAsyncFileWrites to have been called before any use of the CookedHash - it is called in CookByTheBookFinished before the registry does any work with the registries
			// 2) AssetPackageData must continue to be a valid pointer - the asset registry allocates the FAssetPackageData individually and doesn't relocate or delete them until pruning, which happens after WaitForAsyncFileWrites
				AssetPackageData->CookedHash = CookedHash;
		});
	}
}

void UCookOnTheFlyServer::PostLoadPackageFixup(UPackage* Package)
{
	if (Package->ContainsMap() == false)
	{
		return;
	}
	UWorld* World = UWorld::FindWorldInPackage(Package);
	if (!World)
	{
		return;
	}

	// Ensure we only process the package once
	if (PackageTracker->PostLoadFixupPackages.Find(Package) != nullptr)
	{
		return;
	}
	PackageTracker->PostLoadFixupPackages.Add(Package);

	// Perform special processing for UWorld
	World->PersistentLevel->HandleLegacyMapBuildData();

	if (IsCookByTheBookMode() == false)
	{
		return;
	}

	GIsCookerLoadingPackage = true;
	if (World->GetStreamingLevels().Num())
	{
		UE_SCOPED_COOKTIMER(PostLoadPackageFixup_LoadSecondaryLevels);
		TSet<FName> NeverCookPackageNames;
		PackageTracker->NeverCookPackageList.GetValues(NeverCookPackageNames);

		UE_LOG(LogCook, Display, TEXT("Loading secondary levels for package '%s'"), *World->GetName());

		World->LoadSecondaryLevels(true, &NeverCookPackageNames);
	}
	GIsCookerLoadingPackage = false;

	TArray<FString> NewPackagesToCook;

	// Collect world composition tile packages to cook
	if (World->WorldComposition)
	{
		World->WorldComposition->CollectTilesToCook(NewPackagesToCook);
	}

	for (const FString& PackageName : NewPackagesToCook)
	{
		UE::Cook::FPackageData* NewPackageData = PackageDatas->TryAddPackageDataByPackageName(FName(*PackageName));
		if (NewPackageData)
		{
			bool bIsUrgent = false;
			NewPackageData->UpdateRequestData(PlatformManager->GetSessionPlatforms(), bIsUrgent, UE::Cook::FCompletionCallback());
		}
	}
}

void UCookOnTheFlyServer::TickPrecacheObjectsForPlatforms(const float TimeSlice, const TArray<const ITargetPlatform*>& TargetPlatforms) 
{
	SCOPE_CYCLE_COUNTER(STAT_TickPrecacheCooking);


	UE::Cook::FCookerTimer Timer(TimeSlice, true);

	if (LastUpdateTick > 50 ||
		((CachedMaterialsToCacheArray.Num() == 0) && (CachedTexturesToCacheArray.Num() == 0)))
	{
		LastUpdateTick = 0;
		TArray<UObject*> Materials;
		GetObjectsOfClass(UMaterial::StaticClass(), Materials, true);
		for (UObject* Material : Materials)
		{
			if ( Material->GetOutermost() == GetTransientPackage())
				continue;

			CachedMaterialsToCacheArray.Add(Material);
		}
		TArray<UObject*> Textures;
		GetObjectsOfClass(UTexture::StaticClass(), Textures, true);
		for (UObject* Texture : Textures)
		{
			if (Texture->GetOutermost() == GetTransientPackage())
				continue;

			CachedTexturesToCacheArray.Add(Texture);
		}
	}
	++LastUpdateTick;

	if (Timer.IsTimeUp())
	{
		return;
	}

	bool AllMaterialsCompiled = true;
	// queue up some shaders for compilation

	while (CachedMaterialsToCacheArray.Num() > 0)
	{
		UMaterial* Material = (UMaterial*)(CachedMaterialsToCacheArray[0].Get());
		CachedMaterialsToCacheArray.RemoveAtSwap(0, 1, false);

		if (Material == nullptr)
		{
			continue;
		}

		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			if (!TargetPlatform)
			{
				continue;
			}
			if (!Material->IsCachedCookedPlatformDataLoaded(TargetPlatform))
			{
				Material->BeginCacheForCookedPlatformData(TargetPlatform);
				AllMaterialsCompiled = false;
			}
		}

		if (Timer.IsTimeUp())
		{
			return;
		}

		if (GShaderCompilingManager->GetNumRemainingJobs() > MaxPrecacheShaderJobs)
		{
			return;
		}
	}


	if (!AllMaterialsCompiled)
	{
		return;
	}

	while (CachedTexturesToCacheArray.Num() > 0)
	{
		UTexture* Texture = (UTexture*)(CachedTexturesToCacheArray[0].Get());
		CachedTexturesToCacheArray.RemoveAtSwap(0, 1, false);

		if (Texture == nullptr)
		{
			continue;
		}

		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			if (!TargetPlatform)
			{
				continue;
			}
			if (!Texture->IsCachedCookedPlatformDataLoaded(TargetPlatform))
			{
				Texture->BeginCacheForCookedPlatformData(TargetPlatform);
			}
		}
		if (Timer.IsTimeUp())
		{
			return;
		}
	}

}

bool UCookOnTheFlyServer::HasExceededMaxMemory() const
{
	if (IsCookByTheBookMode() && CookByTheBookOptions->bFullLoadAndSave)
	{
		// FullLoadAndSave does the entire cook in one tick, so there is no need to GC after
		return false;
	}

#if UE_GC_TRACK_OBJ_AVAILABLE
	if (GUObjectArray.GetObjectArrayEstimatedAvailable() < MinFreeUObjectIndicesBeforeGC)
	{
		UE_LOG(LogCook, Display, TEXT("Running out of available UObject indices (%d remaining)"), GUObjectArray.GetObjectArrayEstimatedAvailable());
		return true;
	}
#endif // UE_GC_TRACK_OBJ_AVAILABLE


	// Only report exceeded memory if all the active memory usage triggers have fired
	int ActiveTriggers = 0;
	int FiredTriggers = 0;

	TStringBuilder<256> TriggerMessages;
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	if (MemoryMinFreeVirtual > 0 || MemoryMinFreePhysical > 0)
	{
		++ActiveTriggers;
		bool bFired = false;
		if (MemoryMinFreeVirtual > 0 && MemStats.AvailableVirtual < MemoryMinFreeVirtual)
		{
			TriggerMessages.Appendf(TEXT("\n  CookSettings.MemoryMinFreeVirtual: Available virtual memory %dMiB is less than %dMiB."),
				static_cast<uint32>(MemStats.AvailableVirtual / 1024 / 1024), static_cast<uint32>(MemoryMinFreeVirtual / 1024 / 1024));
			bFired = true;
		}
		if (MemoryMinFreePhysical > 0 && MemStats.AvailablePhysical < MemoryMinFreePhysical)
		{
			TriggerMessages.Appendf(TEXT("\n  CookSettings.MemoryMinFreePhysical: Available physical memory %dMiB is less than %dMiB."),
				static_cast<uint32>(MemStats.AvailablePhysical / 1024 / 1024), static_cast<uint32>(MemoryMinFreePhysical / 1024 / 1024));
			bFired = true;
		}
		if (bFired)
		{
			++FiredTriggers;
		}
	}

	if (MemoryMaxUsedVirtual > 0 || MemoryMaxUsedPhysical > 0)
	{
		++ActiveTriggers;
		bool bFired = false;
		if (MemoryMaxUsedVirtual > 0 && MemStats.UsedVirtual >= MemoryMaxUsedVirtual)
		{
			TriggerMessages.Appendf(TEXT("\n  CookSettings.MemoryMaxUsedVirtual: Used virtual memory %dMiB is greater than %dMiB."),
				static_cast<uint32>(MemStats.UsedVirtual / 1024 / 1024), static_cast<uint32>(MemoryMaxUsedVirtual / 1024 / 1024));
			bFired = true;
		}
		if (MemoryMaxUsedPhysical > 0 && MemStats.UsedPhysical >= MemoryMaxUsedPhysical)
		{
			TriggerMessages.Appendf(TEXT("\n  CookSettings.MemoryMaxUsedPhysical: Used physical memory %dMiB is greater than %dMiB."),
				static_cast<uint32>(MemStats.UsedPhysical / 1024 / 1024), static_cast<uint32>(MemoryMaxUsedPhysical / 1024 / 1024));
			bFired = true;
		}
		if (bFired)
		{
			++FiredTriggers;
		}
	}

	if (ActiveTriggers > 0 && FiredTriggers == ActiveTriggers)
	{
		UE_LOG(LogCook, Display, TEXT("Exceeded max memory on all configured triggers:%s"), TriggerMessages.ToString());
		return true;
	}
	else
	{
		return false;
	}
}

TArray<UPackage*> UCookOnTheFlyServer::GetUnsolicitedPackages(const TArray<const ITargetPlatform*>& TargetPlatforms) const
{
	// No longer supported
	return TArray<UPackage*>();
}

void UCookOnTheFlyServer::OnObjectModified( UObject *ObjectMoving )
{
	if (IsGarbageCollecting())
	{
		return;
	}
	OnObjectUpdated( ObjectMoving );
}

void UCookOnTheFlyServer::OnObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (IsGarbageCollecting())
	{
		return;
	}
	if ( PropertyChangedEvent.Property == nullptr && 
		PropertyChangedEvent.MemberProperty == nullptr )
	{
		// probably nothing changed... 
		return;
	}

	OnObjectUpdated( ObjectBeingModified );
}

void UCookOnTheFlyServer::OnObjectSaved( UObject* ObjectSaved )
{
	if (GIsCookerLoadingPackage)
	{
		// This is the cooker saving a cooked package, ignore
		return;
	}

	UPackage* Package = ObjectSaved->GetOutermost();
	if (Package == nullptr || Package == GetTransientPackage())
	{
		return;
	}

	MarkPackageDirtyForCooker(Package);

	// Register the package filename as modified. We don't use the cache because the file may not exist on disk yet at this point
	const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
	ModifiedAssetFilenames.Add(FName(*PackageFilename));
}

void UCookOnTheFlyServer::OnObjectUpdated( UObject *Object )
{
	// get the outer of the object
	UPackage *Package = Object->GetOutermost();

	MarkPackageDirtyForCooker( Package );
}

void UCookOnTheFlyServer::MarkPackageDirtyForCooker(UPackage* Package, bool bAllowInSession)
{
	if (Package->RootPackageHasAnyFlags(PKG_PlayInEditor))
	{
		return;
	}

	if (Package->HasAnyPackageFlags(PKG_PlayInEditor | PKG_ContainsScript | PKG_InMemoryOnly) == true && !GetClass()->HasAnyClassFlags(CLASS_DefaultConfig | CLASS_Config))
	{
		return;
	}

	if (Package == GetTransientPackage())
	{
		return;
	}

	if (Package->GetOuter() != nullptr)
	{
		return;
	}

	FName PackageName = Package->GetFName();
	if (FPackageName::IsMemoryPackage(PackageName.ToString()))
	{
		return;
	}

	if (bIsSavingPackage)
	{
		return;
	}

	if (IsInSession() && !bAllowInSession && !(IsCookByTheBookMode() && CookByTheBookOptions->bFullLoadAndSave))
	{
		ExternalRequests->AddCallback([this, PackageName]() { MarkPackageDirtyForCookerFromSchedulerThread(PackageName); });
	}
	else
	{
		MarkPackageDirtyForCookerFromSchedulerThread(PackageName);
	}
}

void UCookOnTheFlyServer::MarkPackageDirtyForCookerFromSchedulerThread(const FName& PackageName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MarkPackageDirtyForCooker);

	// could have just cooked a file which we might need to write
	UPackage::WaitForAsyncFileWrites();

	// Update the package's FileName if it has changed
	UE::Cook::FPackageData* PackageData = PackageDatas->UpdateFileName(PackageName);

	// force the package to be recooked
	UE_LOG(LogCook, Verbose, TEXT("Modification detected to package %s"), *PackageName.ToString());
	if ( PackageData && IsCookingInEditor() )
	{
		check(IsInGameThread()); // We're editing scheduler data, which is only allowable from the scheduler thread
		bool bHadCookedPlatforms = PackageData->GetNumCookedPlatforms() > 0;
		PackageData->ClearCookedPlatforms();
		if (PackageData->IsInProgress())
		{
			PackageData->SendToState(UE::Cook::EPackageState::Request, UE::Cook::ESendFlags::QueueAddAndRemove);
		}
		else if (IsCookByTheBookRunning() && bHadCookedPlatforms)
		{
			PackageData->UpdateRequestData(PlatformManager->GetSessionPlatforms(), false /* bIsUrgent */, UE::Cook::FCompletionCallback());
		}

		if ( IsCookOnTheFlyMode() && FileModifiedDelegate.IsBound())
		{
			FString PackageFileNameString = PackageData->GetFileName().ToString();
			FileModifiedDelegate.Broadcast(PackageFileNameString);
			if (PackageFileNameString.EndsWith(".uasset") || PackageFileNameString.EndsWith(".umap"))
			{
				FileModifiedDelegate.Broadcast( FPaths::ChangeExtension(PackageFileNameString, TEXT(".uexp")) );
				FileModifiedDelegate.Broadcast( FPaths::ChangeExtension(PackageFileNameString, TEXT(".ubulk")) );
				FileModifiedDelegate.Broadcast( FPaths::ChangeExtension(PackageFileNameString, TEXT(".ufont")) );
			}
		}
	}
}

bool UCookOnTheFlyServer::IsInSession() const
{
	return IsCookByTheBookRunning() || (IsCookOnTheFlyMode() && PlatformManager->HasSelectedSessionPlatforms());
}

void UCookOnTheFlyServer::EndNetworkFileServer()
{
	for ( int i = 0; i < NetworkFileServers.Num(); ++i )
	{
		INetworkFileServer *NetworkFileServer = NetworkFileServers[i];
		// shutdown the server
		NetworkFileServer->Shutdown();
		delete NetworkFileServer;
		NetworkFileServer = NULL;
	}
	NetworkFileServers.Empty();
}

uint32 UCookOnTheFlyServer::GetPackagesPerGC() const
{
	return PackagesPerGC;
}

uint32 UCookOnTheFlyServer::GetPackagesPerPartialGC() const
{
	return MaxNumPackagesBeforePartialGC;
}


double UCookOnTheFlyServer::GetIdleTimeToGC() const
{
	return IdleTimeToGC;
}

uint64 UCookOnTheFlyServer::GetMaxMemoryAllowance() const
{
	return MemoryMaxUsedPhysical;
}

const TArray<FName>& UCookOnTheFlyServer::GetFullPackageDependencies(const FName& PackageName ) const
{
	TArray<FName>* PackageDependencies = CachedFullPackageDependencies.Find(PackageName);
	if ( !PackageDependencies )
	{
		static const FName NAME_CircularReference(TEXT("CircularReference"));
		static int32 UniqueArrayCounter = 0;
		++UniqueArrayCounter;
		FName CircularReferenceArrayName = FName(NAME_CircularReference,UniqueArrayCounter);
		{
			// can't initialize the PackageDependencies array here because we call GetFullPackageDependencies below and that could recurse and resize CachedFullPackageDependencies
			TArray<FName>& TempPackageDependencies = CachedFullPackageDependencies.Add(PackageName); // IMPORTANT READ ABOVE COMMENT
			// initialize TempPackageDependencies to a dummy dependency so that we can detect circular references
			TempPackageDependencies.Add(CircularReferenceArrayName);
			// when someone finds the circular reference name they look for this array name in the CachedFullPackageDependencies map
			// and add their own package name to it, so that they can get fixed up 
			CachedFullPackageDependencies.Add(CircularReferenceArrayName);
		}

		TArray<FName> ChildDependencies;
		if ( AssetRegistry->GetDependencies(PackageName, ChildDependencies, UE::AssetRegistry::EDependencyCategory::Package) )
		{
			TArray<FName> Dependencies = ChildDependencies;
			Dependencies.AddUnique(PackageName);
			for ( const FName& ChildDependency : ChildDependencies)
			{
				const TArray<FName>& ChildPackageDependencies = GetFullPackageDependencies(ChildDependency);
				for ( const FName& ChildPackageDependency : ChildPackageDependencies )
				{
					if ( ChildPackageDependency == CircularReferenceArrayName )
					{
						continue;
					}

					if ( ChildPackageDependency.GetComparisonIndex() == NAME_CircularReference.GetComparisonIndex() )
					{
						// add our self to the package which we are circular referencing
						TArray<FName>& TempCircularReference = CachedFullPackageDependencies.FindChecked(ChildPackageDependency);
						TempCircularReference.AddUnique(PackageName); // add this package name so that it's dependencies get fixed up when the outer loop returns
					}

					Dependencies.AddUnique(ChildPackageDependency);
				}
			}

			// all these packages referenced us apparently so fix them all up
			const TArray<FName>& PackagesForFixup = CachedFullPackageDependencies.FindChecked(CircularReferenceArrayName);
			for ( const FName& FixupPackage : PackagesForFixup )
			{
				TArray<FName> &FixupList = CachedFullPackageDependencies.FindChecked(FixupPackage);
				// check( FixupList.Contains( CircularReferenceArrayName) );
				ensure( FixupList.Remove(CircularReferenceArrayName) == 1 );
				for( const FName& AdditionalDependency : Dependencies )
				{
					FixupList.AddUnique(AdditionalDependency);
					if ( AdditionalDependency.GetComparisonIndex() == NAME_CircularReference.GetComparisonIndex() )
					{
						// add our self to the package which we are circular referencing
						TArray<FName>& TempCircularReference = CachedFullPackageDependencies.FindChecked(AdditionalDependency);
						TempCircularReference.AddUnique(FixupPackage); // add this package name so that it's dependencies get fixed up when the outer loop returns
					}
				}
			}
			CachedFullPackageDependencies.Remove(CircularReferenceArrayName);

			PackageDependencies = CachedFullPackageDependencies.Find(PackageName);
			check(PackageDependencies);

			Swap(*PackageDependencies, Dependencies);
		}
		else
		{
			PackageDependencies = CachedFullPackageDependencies.Find(PackageName);
			PackageDependencies->Add(PackageName);
		}
	}

	return *PackageDependencies;
}

void UCookOnTheFlyServer::PreGarbageCollect()
{
	using namespace UE::Cook;
	if (!IsInSession())
	{
		return;
	}

#if COOK_CHECKSLOW_PACKAGEDATA
	// Verify that only packages in the save state have pointers to objects
	for (const FPackageData* PackageData : *PackageDatas.Get())
	{
		check(PackageData->GetState() == EPackageState::Save || !PackageData->HasReferencedObjects());
	}
#endif
	if (SavingPackageData)
	{
		check(SavingPackageData->GetPackage());
		GCKeepObjects.Add(SavingPackageData->GetPackage());
	}

	const bool bPartialGC = IsCookFlagSet(ECookInitializationFlags::EnablePartialGC);
	if (bPartialGC)
	{
		GCKeepObjects.Empty(1000);

		// Keep all inprogress packages (including packages that have only made it to the request list) that have been partially loaded
		// Additionally, keep all partially loaded packages that are transitively dependended on by any inprogress packages
		// Keep all UObjects that have been loaded so far under these packages
		TMap<const FPackageData*, int32> DependenciesCount;

		TSet<FName> KeepPackages;
		for (const FPackageData* PackageData : *PackageDatas)
		{
			if (PackageData->GetState() == UE::Cook::EPackageState::Save)
			{
				// already handled above
				continue;
			}
			const TArray<FName>& NeededPackages = GetFullPackageDependencies(PackageData->GetPackageName());
			DependenciesCount.Add(PackageData, NeededPackages.Num());
			KeepPackages.Append(NeededPackages);
		}

		TSet<FName> LoadedPackages;
		TArray<UObject*> ObjectsWithOuter;
		for (UPackage* Package : PackageTracker->LoadedPackages)
		{
			const FName& PackageName = Package->GetFName();
			if (KeepPackages.Contains(PackageName))
			{
				LoadedPackages.Add(PackageName);
				GCKeepObjects.Add(Package);
				ObjectsWithOuter.Reset();
				GetObjectsWithOuter(Package, ObjectsWithOuter);
				for (UObject* Obj : ObjectsWithOuter)
				{
					GCKeepObjects.Add(Obj);
				}
			}
		}

		FRequestQueue& RequestQueue = PackageDatas->GetRequestQueue();
		TArray<FPackageData*> Requests;
		Requests.Reserve(RequestQueue.Num());
		while (!RequestQueue.IsEmpty())
		{
			Requests.Add(RequestQueue.PopRequest());
		}
		// Sort the cook requests by the packages which are loaded first
		// then sort by the number of dependencies which are referenced by the package
		// we want to process the packages with the highest dependencies so that they can
		// be evicted from memory and are likely to be able to be released on next GC pass
		Algo::Sort(Requests, [&DependenciesCount, &LoadedPackages](const FPackageData* A, const FPackageData* B)
			{
				int32 ADependencies = DependenciesCount.FindChecked(A);
				int32 BDependencies = DependenciesCount.FindChecked(B);
				bool ALoaded = LoadedPackages.Contains(A->GetPackageName());
				bool BLoaded = LoadedPackages.Contains(B->GetPackageName());
				return (ALoaded == BLoaded) ? (ADependencies > BDependencies) : ALoaded > BLoaded;
			}
		);
		for (FPackageData* Request : Requests)
		{
			RequestQueue.AddRequest(Request); // Urgent requests will still be moved to the front of the RequestQueue by AddRequest
		}
	}
}

void UCookOnTheFlyServer::CookerAddReferencedObjects(FReferenceCollector& Collector)
{
	using namespace UE::Cook;

	// GCKeepObjects are the objects that we want to keep loaded but we only have a WeakPtr to
	Collector.AddReferencedObjects(GCKeepObjects);
}

void UCookOnTheFlyServer::PostGarbageCollect()
{
	using namespace UE::Cook;

	// If any PackageDatas with ObjectPointers lost had any of their object pointers deleted out from under them, demote them back to request
	TArray<FPackageData*> Demotes;
	for (FPackageData* PackageData : PackageDatas->GetSaveQueue())
	{
		if (PackageData->IsSaveInvalidated())
		{
			Demotes.Add(PackageData);
		}
	}
	for (FPackageData* PackageData : Demotes)
	{
		PackageData->SendToState(EPackageState::Request, ESendFlags::QueueRemove);
		PackageDatas->GetRequestQueue().AddRequest(PackageData, /* bForceUrgent */ true);
	}

	// If there was a GarbageCollect while we are saving a package, some of the WeakObjectPtr in SavingPackageData->CachedObjectPointers may have been deleted and set to null
	// We need to handle nulls in that array at any point after calling SavePackage. We do not want to declare them as references and prevent their GC, in case there is 
	// the expectation by some licensee code that removing references to an object will cause it to not be saved
	// However, if garbage collection deleted the package WHILE WE WERE SAVING IT, then we have problems.
	check(!SavingPackageData || SavingPackageData->GetPackage() != nullptr);

	GCKeepObjects.Empty();
}

void UCookOnTheFlyServer::BeginDestroy()
{
	EndNetworkFileServer();

	Super::BeginDestroy();
}

void UCookOnTheFlyServer::TickRecompileShaderRequests()
{
	// try to pull off a request
	UE::Cook::FRecompileRequest* Request = NULL;

	PackageTracker->RecompileRequests.Dequeue(&Request);

	// process it
	if (Request)
	{
		HandleNetworkFileServerRecompileShaders(Request->RecompileData);

		// all done! other thread can unblock now
		Request->bComplete = true;
	}
}

bool UCookOnTheFlyServer::HasRecompileShaderRequests() const 
{ 
	return PackageTracker->RecompileRequests.HasItems();
}

class FDiffModeCookServerUtils
{
	/** Misc / common settings */
	bool bDiffEnabled;
	bool bLinkerDiffEnabled;
	FString PackageFilter;

	/** DumpObjList settings */
	bool bDumpObjList;
	FString DumpObjListParams;

	/** DumpObjects settings */
	bool bDumpObjects;
	bool bDumpObjectsSorted;

public:

	FDiffModeCookServerUtils()
	{
		bDiffEnabled = FParse::Param(FCommandLine::Get(), TEXT("DIFFONLY"));
		bLinkerDiffEnabled = FParse::Param(FCommandLine::Get(), TEXT("LINKERDIFF"));

		bDumpObjList = false;
		bDumpObjects = false;
		bDumpObjectsSorted = false;

		ParseCmds();
	}

	bool IsRunningCookDiff() const
	{
		return bDiffEnabled;
	}

	bool IsRunningCookLinkerDiff() const
	{
		return bLinkerDiffEnabled;
	}

	void ProcessPackage(UPackage* InPackage)
	{
		ConditionallyDumpObjList(InPackage);
		ConditionallyDumpObjects(InPackage);
	}

private:

	void RemoveParam(FString& InOutParams, const TCHAR* InParamToRemove)
	{
		int32 ParamIndex = InOutParams.Find(InParamToRemove);
		if (ParamIndex >= 0)
		{
			int32 NextParamIndex = InOutParams.Find(TEXT(" -"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ParamIndex + 1);
			if (NextParamIndex < ParamIndex)
			{
				NextParamIndex = InOutParams.Len();
			}
			InOutParams = InOutParams.Mid(0, ParamIndex) + InOutParams.Mid(NextParamIndex);
		}
	}
	void ParseDumpObjList(FString InParams)
	{
		const TCHAR* PackageFilterParam = TEXT("-packagefilter=");
		FParse::Value(*InParams, PackageFilterParam, PackageFilter);
		RemoveParam(InParams, PackageFilterParam);

		// Add support for more parameters here
		// After all parameters have been parsed and removed, pass the remaining string as objlist params
		DumpObjListParams = InParams;
	}
	void ParseDumpObjects(FString InParams)
	{
		const TCHAR* PackageFilterParam = TEXT("-packagefilter=");
		FParse::Value(*InParams, PackageFilterParam, PackageFilter);
		RemoveParam(InParams, PackageFilterParam);

		const TCHAR* SortParam = TEXT("sort");
		bDumpObjectsSorted = FParse::Param(*InParams, SortParam);
		RemoveParam(InParams, SortParam);
	}

	void ParseCmds()
	{
		const TCHAR* DumpObjListParam = TEXT("dumpobjlist");
		const TCHAR* DumpObjectsParam = TEXT("dumpobjects");

		FString CmdsText;
		if (FParse::Value(FCommandLine::Get(), TEXT("-diffcmds="), CmdsText, false))
		{
			CmdsText = CmdsText.TrimQuotes();
			TArray<FString> CmdsList;
			CmdsText.ParseIntoArray(CmdsList, TEXT(","));
			for (FString Cmd : CmdsList)
			{
				if (Cmd.StartsWith(DumpObjListParam))
				{
					bDumpObjList = true;
					ParseDumpObjList(*Cmd + FCString::Strlen(DumpObjListParam));
				}
				else if (Cmd.StartsWith(DumpObjectsParam))
				{
					bDumpObjects = true;
					ParseDumpObjects(*Cmd + FCString::Strlen(DumpObjectsParam));
				}
			}
		}
	}
	bool FilterPackageName(UPackage* InPackage, const FString& InWildcard)
	{
		bool bInclude = false;
		FString PackageName = InPackage->GetName();
		if (PackageName.MatchesWildcard(InWildcard))
		{
			bInclude = true;
		}
		else if (FPackageName::GetShortName(PackageName).MatchesWildcard(InWildcard))
		{
			bInclude = true;
		}
		else if (InPackage->LinkerLoad && InPackage->LinkerLoad->Filename.MatchesWildcard(InWildcard))
		{
			bInclude = true;
		}
		return bInclude;
	}
	void ConditionallyDumpObjList(UPackage* InPackage)
	{
		if (bDumpObjList)
		{
			if (FilterPackageName(InPackage, PackageFilter))
			{
				FString ObjListExec = TEXT("OBJ LIST ");
				ObjListExec += DumpObjListParams;

				TGuardValue<ELogTimes::Type> GuardLogTimes(GPrintLogTimes, ELogTimes::None);
				TGuardValue<bool> GuardLogCategory(GPrintLogCategory, false);
				TGuardValue<bool> GuardPrintLogVerbosity(GPrintLogVerbosity, false);

				GEngine->Exec(nullptr, *ObjListExec);
			}
		}
	}
	void ConditionallyDumpObjects(UPackage* InPackage)
	{
		if (bDumpObjects)
		{
			if (FilterPackageName(InPackage, PackageFilter))
			{
				TArray<FString> AllObjects;
				for (FThreadSafeObjectIterator It; It; ++It)
				{
					AllObjects.Add(*It->GetFullName());
				}
				if (bDumpObjectsSorted)
				{
					AllObjects.Sort();
				}

				TGuardValue<ELogTimes::Type> GuardLogTimes(GPrintLogTimes, ELogTimes::None);
				TGuardValue<bool> GuardLogCategory(GPrintLogCategory, false);
				TGuardValue<bool> GuardPrintLogVerbosity(GPrintLogVerbosity, false);

				for (const FString& Obj : AllObjects)
				{
					UE_LOG(LogCook, Display, TEXT("%s"), *Obj);
				}
			}
		}
	}
};

void UCookOnTheFlyServer::SaveCookedPackage(UE::Cook::FPackageData& PackageData, uint32 SaveFlags, const TArrayView<const ITargetPlatform* const>& TargetPlatforms, TArray<FSavePackageResultStruct>& SavePackageResults)
{
	check( SavePackageResults.Num() == 0);
	check( bIsSavingPackage == false );
	bIsSavingPackage = true;

	UPackage* Package = PackageData.GetPackage();
	check(Package && Package->IsFullyLoaded());
	const FString PackageName(Package->GetName());
	check(Package->GetPathName().Equals(Package->GetName())); // We should only be saving outermost packages, so the path name should be the same as the package name
	FString Filename(PackageData.GetFileName().ToString());

	// Also request any localized variants of this package
	if (IsCookByTheBookMode() && !CookByTheBookOptions->bSkipSoftReferences && !FPackageName::IsLocalizedPackage(PackageName))
	{
		const TArray<FName>* LocalizedVariants = CookByTheBookOptions->SourceToLocalizedPackageVariants.Find(Package->GetFName());
		if (LocalizedVariants)
		{
			for (const FName& LocalizedPackageName : *LocalizedVariants)
			{
				UE::Cook::FPackageData* LocalizedPackageData = PackageDatas->TryAddPackageDataByPackageName(LocalizedPackageName);
				if (LocalizedPackageData)
				{
					bool bIsUrgent = false;
					LocalizedPackageData->UpdateRequestData(PackageData.GetRequestedPlatforms(), bIsUrgent, UE::Cook::FCompletionCallback());
				}
			}
		}
	}

	// Don't resolve, just add to request list as needed
	TSet<FName> SoftObjectPackages;

	if (!IsCookByTheBookMode() || !CookByTheBookOptions->bSkipSoftReferences)
	{
		GRedirectCollector.ProcessSoftObjectPathPackageList(Package->GetFName(), false, SoftObjectPackages);

		for (FName SoftObjectPackage : SoftObjectPackages)
		{
			TMap<FName, FName> RedirectedPaths;

			// If this is a redirector, extract destination from asset registry
			if (ContainsRedirector(SoftObjectPackage, RedirectedPaths))
			{
				for (TPair<FName, FName>& RedirectedPath : RedirectedPaths)
				{
					GRedirectCollector.AddAssetPathRedirection(RedirectedPath.Key, RedirectedPath.Value);
				}
			}

			// Verify package actually exists

			if (IsCookByTheBookMode())
			{
				UE::Cook::FPackageData* SoftObjectPackageData = PackageDatas->TryAddPackageDataByPackageName(SoftObjectPackage);
				if (SoftObjectPackageData)
				{
					bool bIsUrgent = false;
					SoftObjectPackageData->UpdateRequestData(PackageData.GetRequestedPlatforms(), bIsUrgent, UE::Cook::FCompletionCallback());
				}
			}
		}
	}

	if (Filename.Len() != 0 )
	{
		if (Package->HasAnyPackageFlags(PKG_ReloadingForCooker))
		{
			UE_LOG(LogCook, Warning, TEXT("Package %s marked as reloading for cook by was requested to save"), *Package->GetName());
			UE_LOG(LogCook, Fatal, TEXT("Package %s marked as reloading for cook by was requested to save"), *Package->GetName());
		}

		// Use SandboxFile to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
		Filename = ConvertToFullSandboxPath(*Filename, true);

		uint32 OriginalPackageFlags = Package->GetPackageFlags();
		UWorld* World = nullptr;
		EObjectFlags FlagsToCook = RF_Public;

		ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();

		static FDiffModeCookServerUtils DiffModeHelper;
		if (DiffModeHelper.IsRunningCookLinkerDiff())
		{
			SaveFlags |= SAVE_CompareLinker;
		}

		for (int32 PlatformIndex = 0; PlatformIndex < TargetPlatforms.Num(); ++PlatformIndex)
		{
			SavePackageResults.Add(FSavePackageResultStruct(ESavePackageResult::Success));
			const ITargetPlatform* Target = TargetPlatforms[PlatformIndex];
			FString PlatFilename = Filename.Replace(TEXT("[Platform]"), *Target->PlatformName());

			FSavePackageResultStruct& Result = SavePackageResults[PlatformIndex];

			bool bCookPackage = true;

			// don't save Editor resources from the Engine if the target doesn't have editoronly data
			if (IsCookFlagSet(ECookInitializationFlags::SkipEditorContent) &&
				(PackageName.StartsWith(TEXT("/Engine/Editor")) || PackageName.StartsWith(TEXT("/Engine/VREditor"))) &&
				!Target->HasEditorOnlyData())
			{
				Result = ESavePackageResult::ContainsEditorOnlyData;
				bCookPackage = false;
			}
			// Check whether or not game-specific behaviour should prevent this package from being cooked for the target platform
			else if (UAssetManager::IsValid() && !UAssetManager::Get().ShouldCookForPlatform(Package, Target))
			{
				Result = ESavePackageResult::ContainsEditorOnlyData;
				bCookPackage = false;
				UE_LOG(LogCook, Display, TEXT("Excluding %s -> %s"), *Package->GetName(), *PlatFilename);
			}
			// check if this package is unsupported for the target platform (typically plugin content)
			else 
			{
				TSet<FName>* NeverCookPackages = PackageTracker->PlatformSpecificNeverCookPackages.Find(Target);
				if (NeverCookPackages && NeverCookPackages->Find(FName(*PackageName)))
				{
					Result = ESavePackageResult::ContainsEditorOnlyData;
					bCookPackage = false;
					UE_LOG(LogCook, Display, TEXT("Excluding %s -> %s"), *Package->GetName(), *PlatFilename);
				}
			}

			if (bCookPackage == true)
			{
				// look for a world object in the package (if there is one, there's a map)
				if (UWorld::FindWorldInPackage(Package))
				{
					FlagsToCook = RF_NoFlags;
				}

				UE_CLOG(GCookProgressDisplay & (int32)ECookProgressDisplayMode::PackageNames, LogCook, Display, TEXT("Cooking %s -> %s"), *Package->GetName(), *PlatFilename);

				bool bSwap = (!Target->IsLittleEndian()) ^ (!PLATFORM_LITTLE_ENDIAN);


				if (!Target->HasEditorOnlyData())
				{
					Package->SetPackageFlags(PKG_FilterEditorOnly);
				}
				else
				{
					Package->ClearPackageFlags(PKG_FilterEditorOnly);
				}

				if (World)
				{
					// Fixup legacy lightmaps before saving
					// This should be done after loading, but Core loads UWorlds with LoadObject so there's no opportunity to handle this fixup on load
					World->PersistentLevel->HandleLegacyMapBuildData();
				}

				const FString FullFilename = FPaths::ConvertRelativePathToFull(PlatFilename);
				if (FullFilename.Len() >= FPlatformMisc::GetMaxPathLength())
				{
					LogCookerMessage(FString::Printf(TEXT("Couldn't save package, filename is too long (%d >= %d): %s"), FullFilename.Len(), FPlatformMisc::GetMaxPathLength(), *PlatFilename), EMessageSeverity::Error);
					Result = ESavePackageResult::Error;
				}
				else
				{
					UE_SCOPED_HIERARCHICAL_COOKTIMER(GEditorSavePackage);
					GIsCookerLoadingPackage = true;

					if (DiffModeHelper.IsRunningCookDiff())
					{
						FSavePackageContext* const SavePackageContext = (IsCookByTheBookMode() && SavePackageContexts.Num() > 0) ? SavePackageContexts[PlatformIndex] : nullptr;

						DiffModeHelper.ProcessPackage(Package);

						// When looking for deterministic cook issues, first serialize the package to memory and do a simple diff with the existing package
						uint32 DiffSaveFlags = SaveFlags | SAVE_DiffOnly;
						FArchiveDiffMap DiffMap;
						Result = GEditor->Save(Package, World, FlagsToCook, *PlatFilename, GError, NULL, bSwap, false, DiffSaveFlags, Target, FDateTime::MinValue(), false, &DiffMap, SavePackageContext);
						if (Result == ESavePackageResult::DifferentContent)
						{
							// If the simple memory diff was not identical, collect callstacks for all Serialize calls and dump differences to log
							DiffSaveFlags = SaveFlags | SAVE_DiffCallstack;
							Result = GEditor->Save(Package, World, FlagsToCook, *PlatFilename, GError, NULL, bSwap, false, DiffSaveFlags, Target, FDateTime::MinValue(), false, &DiffMap, SavePackageContext);
						}
					}
					else
					{
						FSavePackageContext* const SavePackageContext = (IsCookByTheBookMode() && SavePackageContexts.Num() > 0) ? SavePackageContexts[PlatformIndex] : nullptr;

						Result = GEditor->Save(	Package, World, FlagsToCook, *PlatFilename, 
												GError, nullptr, bSwap, false, SaveFlags, Target,
												FDateTime::MinValue(), false, /*DiffMap*/ nullptr, 
												SavePackageContext);
					}

					// if running linker diff, resave the package again using the other save algorithm
					if (DiffModeHelper.IsRunningCookLinkerDiff())
					{
						static IConsoleVariable* EnableNewSave = IConsoleManager::Get().FindConsoleVariable(TEXT("SavePackage.EnableNewSave"));
						bool bPreviousCvarValue = EnableNewSave->GetBool();
						EnableNewSave->Set(!bPreviousCvarValue);
						FSavePackageResultStruct NewResult = GEditor->Save(Package, World, FlagsToCook, *PlatFilename,
							GError, nullptr, bSwap, false, SaveFlags|SAVE_DiffOnly, Target,
							FDateTime::MinValue(), false, /*DiffMap*/ nullptr,
							nullptr);
						EnableNewSave->Set(bPreviousCvarValue);

						if (Result.LinkerSave && NewResult.LinkerSave)
						{
							FLinkerDiff LinkerDiff = FLinkerDiff::CompareLinkers(Result.LinkerSave.Get(), NewResult.LinkerSave.Get());
							LinkerDiff.PrintDiff(*GWarn);
						}
						Result.LinkerSave.Reset();
						NewResult.LinkerSave.Reset();
					}

					GIsCookerLoadingPackage = false;
					{
						UE_SCOPED_HIERARCHICAL_COOKTIMER(ConvertingBlueprints);
						IBlueprintNativeCodeGenModule::Get().Convert(Package, Result.Result, *(Target->PlatformName()));
					}

					++this->StatSavedPackageCount;

					// If package was actually saved check with asset manager to make sure it wasn't excluded for being a development or never cook package. We do this after Editor Only filtering
					if (Result == ESavePackageResult::Success && UAssetManager::IsValid())
					{
						UE_SCOPED_HIERARCHICAL_COOKTIMER(VerifyCanCookPackage);
						if (!UAssetManager::Get().VerifyCanCookPackage(Package->GetFName()))
						{
							Result = ESavePackageResult::Error;
						}
					}
				}
			}
		}

		Package->SetPackageFlagsTo(OriginalPackageFlags);
	}
	else
	{
		for (int32 PlatformIndex = 0; PlatformIndex < TargetPlatforms.Num(); ++PlatformIndex)
		{
			SavePackageResults.Add(FSavePackageResultStruct(ESavePackageResult::MissingFile));
		}
	}

	check(bIsSavingPackage == true);
	bIsSavingPackage = false;

}

void UCookOnTheFlyServer::Initialize( ECookMode::Type DesiredCookMode, ECookInitializationFlags InCookFlags, const FString &InOutputDirectoryOverride )
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::Initialize);
	UE::Cook::InitializeTls();
	UE::Cook::FPlatformManager::InitializeTls();

	OutputDirectoryOverride = InOutputDirectoryOverride;
	CurrentCookMode = DesiredCookMode;
	CookFlags = InCookFlags;

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddUObject(this, &UCookOnTheFlyServer::PreGarbageCollect);
	FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &UCookOnTheFlyServer::PostGarbageCollect);

	if (IsCookingInEditor())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UCookOnTheFlyServer::OnObjectPropertyChanged);
		FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &UCookOnTheFlyServer::OnObjectModified);
		FCoreUObjectDelegates::OnObjectSaved.AddUObject(this, &UCookOnTheFlyServer::OnObjectSaved);

		FCoreDelegates::OnTargetPlatformChangedSupportedFormats.AddUObject(this, &UCookOnTheFlyServer::OnTargetPlatformChangedSupportedFormats);
	}

	FCoreDelegates::OnFConfigCreated.AddUObject(this, &UCookOnTheFlyServer::OnFConfigCreated);
	FCoreDelegates::OnFConfigDeleted.AddUObject(this, &UCookOnTheFlyServer::OnFConfigDeleted);

	GetTargetPlatformManager()->GetOnTargetPlatformsInvalidatedDelegate().AddUObject(this, &UCookOnTheFlyServer::OnTargetPlatformsInvalidated);

	MaxPrecacheShaderJobs = FPlatformMisc::NumberOfCores() - 1; // number of cores -1 is a good default allows the editor to still be responsive to other shader requests and allows cooker to take advantage of multiple processors while the editor is running
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxPrecacheShaderJobs"), MaxPrecacheShaderJobs, GEditorIni);

	MaxConcurrentShaderJobs = FPlatformMisc::NumberOfCores() * 4; // TODO: document why number of cores * 4 is a good default
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxConcurrentShaderJobs"), MaxConcurrentShaderJobs, GEditorIni);

	PackagesPerGC = 500;
	int32 ConfigPackagesPerGC = 0;
	if (GConfig->GetInt( TEXT("CookSettings"), TEXT("PackagesPerGC"), ConfigPackagesPerGC, GEditorIni ))
	{
		// Going unsigned. Make negative values 0
		PackagesPerGC = ConfigPackagesPerGC > 0 ? ConfigPackagesPerGC : 0;
	}

	IdleTimeToGC = 20.0;
	GConfig->GetDouble( TEXT("CookSettings"), TEXT("IdleTimeToGC"), IdleTimeToGC, GEditorIni );

	auto ReadMemorySetting = [](const TCHAR* SettingName, uint64& TargetVariable)
	{
		int32 ValueInMB = 0;
		if (GConfig->GetInt(TEXT("CookSettings"), SettingName, ValueInMB, GEditorIni))
		{
			ValueInMB = FMath::Max(ValueInMB, 0);
			TargetVariable = ValueInMB * 1024LL * 1024LL;
			return true;
		}
		return false;
	};
	MemoryMaxUsedVirtual = 0;
	MemoryMaxUsedPhysical = 0;
	MemoryMinFreeVirtual = 0;
	MemoryMinFreePhysical = 0;
	ReadMemorySetting(TEXT("MemoryMaxUsedVirtual"), MemoryMaxUsedVirtual);
	ReadMemorySetting(TEXT("MemoryMaxUsedPhysical"), MemoryMaxUsedPhysical);
	ReadMemorySetting(TEXT("MemoryMinFreeVirtual"), MemoryMinFreeVirtual);
	ReadMemorySetting(TEXT("MemoryMinFreePhysical"), MemoryMinFreePhysical);

	uint64 MaxMemoryAllowance;
	if (ReadMemorySetting(TEXT("MaxMemoryAllowance"), MaxMemoryAllowance))
	{ 
		UE_LOG(LogCook, Warning, TEXT("CookSettings.MaxMemoryAllowance is deprecated. Use CookSettings.MemoryMaxUsedPhysical instead."));
		MemoryMaxUsedPhysical = MaxMemoryAllowance;
	}
	uint64 MinMemoryBeforeGC;
	if (ReadMemorySetting(TEXT("MinMemoryBeforeGC"), MinMemoryBeforeGC))
	{
		UE_LOG(LogCook, Warning, TEXT("CookSettings.MinMemoryBeforeGC is deprecated. Use CookSettings.MemoryMaxUsedVirtual instead."));
		MemoryMaxUsedVirtual = MinMemoryBeforeGC;
	}
	uint64 MinFreeMemory;
	if (ReadMemorySetting(TEXT("MinFreeMemory"), MinFreeMemory))
	{
		UE_LOG(LogCook, Warning, TEXT("CookSettings.MinFreeMemory is deprecated. Use CookSettings.MemoryMinFreePhysical instead."));
		MemoryMinFreePhysical = MinFreeMemory;
	}
	uint64 MinReservedMemory;
	if (ReadMemorySetting(TEXT("MinReservedMemory"), MinReservedMemory))
	{
		UE_LOG(LogCook, Warning, TEXT("CookSettings.MinReservedMemory is deprecated. Use CookSettings.MemoryMinFreePhysical instead."));
		MemoryMinFreePhysical = MinReservedMemory;
	}
	
	MaxPreloadAllocated = 16;
	DesiredSaveQueueLength = 8;
	DesiredLoadQueueLength = 8;

	MinFreeUObjectIndicesBeforeGC = 100000;
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MinFreeUObjectIndicesBeforeGC"), MinFreeUObjectIndicesBeforeGC, GEditorIni);
	MinFreeUObjectIndicesBeforeGC = FMath::Max(MinFreeUObjectIndicesBeforeGC, 0);

	MaxNumPackagesBeforePartialGC = 400;
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxNumPackagesBeforePartialGC"), MaxNumPackagesBeforePartialGC, GEditorIni);
	
	GConfig->GetArray(TEXT("CookSettings"), TEXT("ConfigSettingBlacklist"), ConfigSettingBlacklist, GEditorIni);

	UE_LOG(LogCook, Display, TEXT("CookSettings for Memory: MemoryMaxUsedVirtual %dMiB, MemoryMaxUsedPhysical %dMiB, MemoryMinFreeVirtual %dMiB, MemoryMinFreePhysical %dMiB"),
		MemoryMaxUsedVirtual / 1024 / 1024, MemoryMaxUsedPhysical / 1024 / 1024, MemoryMinFreeVirtual / 1024 / 1024, MemoryMinFreePhysical / 1024 / 1024);

	if (IsCookByTheBookMode() && !IsCookingInEditor() &&
		FPlatformMisc::SupportsMultithreadedFileHandles()// Preloading moves file handles between threads
		)
	{
		bPreloadingEnabled = true;
		FLinkerLoad::SetPreloadingEnabled(true);
	}

	{
		const FConfigSection* CacheSettings = GConfig->GetSectionPrivate(TEXT("CookPlatformDataCacheSettings"), false, true, GEditorIni);
		if ( CacheSettings )
		{
			for ( const auto& CacheSetting : *CacheSettings )
			{
				
				const FString& ReadString = CacheSetting.Value.GetValue();
				int32 ReadValue = FCString::Atoi(*ReadString);
				int32 Count = FMath::Max( 2,  ReadValue );
				MaxAsyncCacheForType.Add( CacheSetting.Key,  Count );
			}
		}
		CurrentAsyncCacheForType = MaxAsyncCacheForType;
	}


	if (IsCookByTheBookMode())
	{
		CookByTheBookOptions = new FCookByTheBookOptions();
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			if ((*It) != GetTransientPackage())
			{
				CookByTheBookOptions->StartupPackages.Add(It->GetFName());
				UE_LOG(LogCook, Verbose, TEXT("Cooker startup package %s"), *It->GetName());
			}
		}
	}
	
	UE_LOG(LogCook, Display, TEXT("Mobile HDR setting %d"), IsMobileHDR());

	// See if there are any plugins that need to be remapped for the sandbox
	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
	if (Project != nullptr)
	{
		PluginsToRemap = IPluginManager::Get().GetEnabledPlugins();
		TArray<FString> AdditionalPluginDirs = Project->GetAdditionalPluginDirectories();
		// Remove any plugin that is in the additional directories since they are handled normally and don't need remapping
		for (int32 Index = PluginsToRemap.Num() - 1; Index >= 0; Index--)
		{
			bool bRemove = true;
			for (const FString& PluginDir : AdditionalPluginDirs)
			{
				// If this plugin is in a directory that needs remapping
				if (PluginsToRemap[Index]->GetBaseDir().StartsWith(PluginDir))
				{
					bRemove = false;
					break;
				}
			}
			if (bRemove)
			{
				PluginsToRemap.RemoveAt(Index);
			}
		}
	}

	bool bDisableEDLWarning = false;
	GConfig->GetBool(TEXT("/Script/Engine.StreamingSettings"), TEXT("s.DisableEDLDeprecationWarnings"), /* out */ bDisableEDLWarning, GEngineIni);
	if (!IsEventDrivenLoaderEnabledInCookedBuilds() && !bDisableEDLWarning)
	{
		UE_LOG(LogCook, Warning, TEXT("Cooking with Event Driven Loader disabled. Loading code will use deprecated path which will be removed in future release."));
	}
}

bool UCookOnTheFlyServer::Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("package")))
	{
		FString PackageName;
		if (!FParse::Value(Cmd, TEXT("name="), PackageName))
		{
			Ar.Logf(TEXT("Required package name for cook package function. \"cook package name=<name> platform=<platform>\""));
			return true;
		}

		FString PlatformName;
		if (!FParse::Value(Cmd, TEXT("platform="), PlatformName))
		{
			Ar.Logf(TEXT("Required package name for cook package function. \"cook package name=<name> platform=<platform>\""));
			return true;
		}

		if (FPackageName::IsShortPackageName(PackageName))
		{
			FString OutFilename;
			if (FPackageName::SearchForPackageOnDisk(PackageName, NULL, &OutFilename))
			{
				PackageName = OutFilename;
			}
		}

		FName RawPackageName(*PackageName);
		TArray<FName> PackageNames;
		PackageNames.Add(RawPackageName);

		GenerateLongPackageNames(PackageNames);
		

		ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
		ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(PlatformName);
		if (TargetPlatform == nullptr)
		{
			Ar.Logf(TEXT("Target platform %s wasn't found."), *PlatformName);
			return true;
		}

		FCookByTheBookStartupOptions StartupOptions;

		StartupOptions.TargetPlatforms.Add(TargetPlatform);
		for (const FName& StandardPackageName : PackageNames)
		{
			FName PackageFileFName = GetPackageNameCache().GetCachedStandardFileName(StandardPackageName);
			StartupOptions.CookMaps.Add(StandardPackageName.ToString());
		}
		StartupOptions.CookOptions = ECookByTheBookOptions::NoAlwaysCookMaps | ECookByTheBookOptions::NoDefaultMaps | ECookByTheBookOptions::NoGameAlwaysCookPackages | ECookByTheBookOptions::NoInputPackages | ECookByTheBookOptions::NoSlatePackages | ECookByTheBookOptions::SkipSoftReferences | ECookByTheBookOptions::ForceDisableSaveGlobalShaders;
		
		StartCookByTheBook(StartupOptions);
	}
	else if (FParse::Command(&Cmd, TEXT("clearall")))
	{
		StopAndClearCookedData();
	}
	else if (FParse::Command(&Cmd, TEXT("stats")))
	{
		DumpStats();
	}

	return false;
}

void UCookOnTheFlyServer::DumpStats()
{
	UE_LOG(LogCook, Display, TEXT("IntStats:"));
	UE_LOG(LogCook, Display, TEXT("  %s=%d"), L"LoadPackage", this->StatLoadedPackageCount);
	UE_LOG(LogCook, Display, TEXT("  %s=%d"), L"SavedPackage", this->StatSavedPackageCount);

	OutputHierarchyTimers();
#if PROFILE_NETWORK
	UE_LOG(LogCook, Display, TEXT("Network Stats \n"
		"TimeTillRequestStarted %f\n"
		"TimeTillRequestForfilled %f\n"
		"TimeTillRequestForfilledError %f\n"
		"WaitForAsyncFilesWrites %f\n"),
		TimeTillRequestStarted,
		TimeTillRequestForfilled,
		TimeTillRequestForfilledError,

		WaitForAsyncFilesWrites);
#endif
}

uint32 UCookOnTheFlyServer::NumConnections() const
{
	int Result= 0;
	for ( int i = 0; i < NetworkFileServers.Num(); ++i )
	{
		INetworkFileServer *NetworkFileServer = NetworkFileServers[i];
		if ( NetworkFileServer )
		{
			Result += NetworkFileServer->NumConnections();
		}
	}
	return Result;
}

FString UCookOnTheFlyServer::GetOutputDirectoryOverride() const
{
	FString OutputDirectory = OutputDirectoryOverride;
	// Output directory override.	
	if (OutputDirectory.Len() <= 0)
	{
		if ( IsCookingDLC() )
		{
			check( IsCookByTheBookMode() );
			OutputDirectory = FPaths::Combine(*GetBaseDirectoryForDLC(), TEXT("Saved"), TEXT("Cooked"), TEXT("[Platform]"));
		}
		else if ( IsCookingInEditor() )
		{
			// Full path so that the sandbox wrapper doesn't try to re-base it under Sandboxes
			OutputDirectory = FPaths::Combine(*FPaths::ProjectDir(), TEXT("Saved"), TEXT("EditorCooked"), TEXT("[Platform]"));
		}
		else
		{
			// Full path so that the sandbox wrapper doesn't try to re-base it under Sandboxes
			OutputDirectory = FPaths::Combine(*FPaths::ProjectDir(), TEXT("Saved"), TEXT("Cooked"), TEXT("[Platform]"));
		}
		
		OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	}
	else if (!OutputDirectory.Contains(TEXT("[Platform]"), ESearchCase::IgnoreCase, ESearchDir::FromEnd) )
	{
		// Output directory needs to contain [Platform] token to be able to cook for multiple targets.
		if ( IsCookByTheBookMode() )
		{
			checkf(PlatformManager->GetSessionPlatforms().Num() == 1,
				TEXT("If OutputDirectoryOverride is provided when cooking multiple platforms, it must include [Platform] in the text, to be replaced with the name of each of the requested Platforms.") );
		}
		else
		{
			// In cook on the fly mode we always add a [Platform] subdirectory rather than requiring the command-line user to include it in their path it because we assume they 
			// don't know which platforms they are cooking for up front
			OutputDirectory = FPaths::Combine(*OutputDirectory, TEXT("[Platform]"));
		}
	}
	FPaths::NormalizeDirectoryName(OutputDirectory);

	return OutputDirectory;
}

template<class T>
void GetVersionFormatNumbersForIniVersionStrings( TArray<FString>& IniVersionStrings, const FString& FormatName, const TArray<const T> &FormatArray )
{
	for ( const T& Format : FormatArray )
	{
		TArray<FName> SupportedFormats;
		Format->GetSupportedFormats(SupportedFormats);
		for ( const FName& SupportedFormat : SupportedFormats )
		{
			int32 VersionNumber = Format->GetVersion(SupportedFormat);
			FString IniVersionString = FString::Printf( TEXT("%s:%s:VersionNumber%d"), *FormatName, *SupportedFormat.ToString(), VersionNumber);
			IniVersionStrings.Emplace( IniVersionString );
		}
	}
}




template<class T>
void GetVersionFormatNumbersForIniVersionStrings(TMap<FString, FString>& IniVersionMap, const FString& FormatName, const TArray<T> &FormatArray)
{
	for (const T& Format : FormatArray)
	{
		TArray<FName> SupportedFormats;
		Format->GetSupportedFormats(SupportedFormats);
		for (const FName& SupportedFormat : SupportedFormats)
		{
			int32 VersionNumber = Format->GetVersion(SupportedFormat);
			FString IniVersionString = FString::Printf(TEXT("%s:%s:VersionNumber"), *FormatName, *SupportedFormat.ToString());
			IniVersionMap.Add(IniVersionString, FString::Printf(TEXT("%d"), VersionNumber));
		}
	}
}


void GetAdditionalCurrentIniVersionStrings( const ITargetPlatform* TargetPlatform, TMap<FString, FString>& IniVersionMap )
{
	FConfigFile EngineSettings;
	FConfigCacheIni::LoadLocalIniFile(EngineSettings, TEXT("Engine"), true, *TargetPlatform->IniPlatformName());

	TArray<FString> VersionedRValues;
	EngineSettings.GetArray(TEXT("/Script/UnrealEd.CookerSettings"), TEXT("VersionedIntRValues"), VersionedRValues);

	for (const FString& RValue : VersionedRValues)
	{
		const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(*RValue);
		if (CVar)
		{
			IniVersionMap.Add(*RValue, FString::Printf(TEXT("%d"), CVar->GetValueOnGameThread()));
		}
	}

	// save off the ddc version numbers also
	ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
	check(TPM);

	{
		TArray<FName> AllWaveFormatNames;
		TargetPlatform->GetAllWaveFormats(AllWaveFormatNames);
		TArray<const IAudioFormat*> SupportedWaveFormats;
		for ( const auto& WaveName : AllWaveFormatNames )
		{
			const IAudioFormat* AudioFormat = TPM->FindAudioFormat(WaveName);
			if (AudioFormat)
			{
				SupportedWaveFormats.Add(AudioFormat);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Unable to find audio format \"%s\" which is required by \"%s\""), *WaveName.ToString(), *TargetPlatform->PlatformName());
			}
			
		}
		GetVersionFormatNumbersForIniVersionStrings(IniVersionMap, TEXT("AudioFormat"), SupportedWaveFormats);
	}

	{
		TArray<FName> AllTextureFormats;
		TargetPlatform->GetAllTextureFormats(AllTextureFormats);
		TArray<const ITextureFormat*> SupportedTextureFormats;
		for (const auto& TextureName : AllTextureFormats)
		{
			const ITextureFormat* TextureFormat = TPM->FindTextureFormat(TextureName);
			if ( TextureFormat )
			{
				SupportedTextureFormats.Add(TextureFormat);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Unable to find texture format \"%s\" which is required by \"%s\""), *TextureName.ToString(), *TargetPlatform->PlatformName());
			}
		}
		GetVersionFormatNumbersForIniVersionStrings(IniVersionMap, TEXT("TextureFormat"), SupportedTextureFormats);
	}

	{
		TArray<FName> AllFormatNames;
		TargetPlatform->GetAllTargetedShaderFormats(AllFormatNames);
		TArray<const IShaderFormat*> SupportedFormats;
		for (const auto& FormatName : AllFormatNames)
		{
			const IShaderFormat* Format = TPM->FindShaderFormat(FormatName);
			if ( Format )
			{
				SupportedFormats.Add(Format);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("Unable to find shader \"%s\" which is required by format \"%s\""), *FormatName.ToString(), *TargetPlatform->PlatformName());
			}
		}
		GetVersionFormatNumbersForIniVersionStrings(IniVersionMap, TEXT("ShaderFormat"), SupportedFormats);
	}


	// TODO: Add support for physx version tracking, currently this happens so infrequently that invalidating a cook based on it is not essential
	//GetVersionFormatNumbersForIniVersionStrings(IniVersionMap, TEXT("PhysXCooking"), TPM->GetPhysXCooking());


	if ( FParse::Param( FCommandLine::Get(), TEXT("fastcook") ) )
	{
		IniVersionMap.Add(TEXT("fastcook"));
	}

	FCustomVersionContainer AllCurrentVersions = FCurrentCustomVersions::GetAll();
	for (const FCustomVersion& CustomVersion : AllCurrentVersions.GetAllVersions())
	{
		FString CustomVersionString = FString::Printf(TEXT("%s:%s"), *CustomVersion.GetFriendlyName().ToString(), *CustomVersion.Key.ToString());
		FString CustomVersionValue = FString::Printf(TEXT("%d"), CustomVersion.Version);
		IniVersionMap.Add(CustomVersionString, CustomVersionValue);
	}

	FString UE4Ver = FString::Printf(TEXT("PackageFileVersions:%d"), GPackageFileUE4Version);
	FString UE4Value = FString::Printf(TEXT("%d"), GPackageFileLicenseeUE4Version);
	IniVersionMap.Add(UE4Ver, UE4Value);

	/*FString UE4EngineVersionCompatibleName = TEXT("EngineVersionCompatibleWith");
	FString UE4EngineVersionCompatible = FEngineVersion::CompatibleWith().ToString();
	
	if ( UE4EngineVersionCompatible.Len() )
	{
		IniVersionMap.Add(UE4EngineVersionCompatibleName, UE4EngineVersionCompatible);
	}*/

	IniVersionMap.Add(TEXT("MaterialShaderMapDDCVersion"), *GetMaterialShaderMapDDCKey());
	IniVersionMap.Add(TEXT("GlobalDDCVersion"), *GetGlobalShaderMapDDCKey());
}



bool UCookOnTheFlyServer::GetCurrentIniVersionStrings( const ITargetPlatform* TargetPlatform, FIniSettingContainer& IniVersionStrings ) const
{
	IniVersionStrings = AccessedIniStrings;

	// this should be called after the cook is finished
	TArray<FString> IniFiles;
	GConfig->GetConfigFilenames(IniFiles);

	TMap<FString, int32> MultiMapCounter;

	for ( const FString& ConfigFilename : IniFiles )
	{
		if ( ConfigFilename.Contains(TEXT("CookedIniVersion.txt")) )
		{
			continue;
		}

		const FConfigFile *ConfigFile = GConfig->FindConfigFile(ConfigFilename);
		ProcessAccessedIniSettings(ConfigFile, IniVersionStrings);
		
	}

	for (const FConfigFile* ConfigFile : OpenConfigFiles)
	{
		ProcessAccessedIniSettings(ConfigFile, IniVersionStrings);
	}


	// remove any which are filtered out
	FString EditorPrefix(TEXT("Editor."));
	for ( const FString& Filter : ConfigSettingBlacklist )
	{
		TArray<FString> FilterArray;
		Filter.ParseIntoArray( FilterArray, TEXT(":"));

		FString *ConfigFileName = nullptr;
		FString *SectionName = nullptr;
		FString *ValueName = nullptr;
		switch ( FilterArray.Num() )
		{
		case 3:
			ValueName = &FilterArray[2];
		case 2:
			SectionName = &FilterArray[1];
		case 1:
			ConfigFileName = &FilterArray[0];
			break;
		default:
			continue;
		}

		if ( ConfigFileName )
		{
			for ( auto ConfigFile = IniVersionStrings.CreateIterator(); ConfigFile; ++ConfigFile )
			{
				// Some ConfigBlacklistSettings are written as *.Engine, and are intended to affect the platform-less Editor Engine.ini, which is just "Engine"
				// To make *.Engine match the editor-only config files as well, we check whether the wildcard matches either Engine or Editor.Engine for the editor files
				FString IniVersionStringFilename = ConfigFile.Key().ToString();
				if (IniVersionStringFilename.MatchesWildcard(*ConfigFileName) ||
					(!IniVersionStringFilename.Contains(TEXT(".")) && (EditorPrefix + IniVersionStringFilename).MatchesWildcard(*ConfigFileName)))
				{
					if ( SectionName )
					{
						for ( auto Section = ConfigFile.Value().CreateIterator(); Section; ++Section )
						{
							if ( Section.Key().ToString().MatchesWildcard(*SectionName))
							{
								if (ValueName)
								{
									for ( auto Value = Section.Value().CreateIterator(); Value; ++Value )
									{
										if ( Value.Key().ToString().MatchesWildcard(*ValueName))
										{
											Value.RemoveCurrent();
										}
									}
								}
								else
								{
									Section.RemoveCurrent();
								}
							}
						}
					}
					else
					{
						ConfigFile.RemoveCurrent();
					}
				}
			}
		}
	}
	return true;
}


bool UCookOnTheFlyServer::GetCookedIniVersionStrings(const ITargetPlatform* TargetPlatform, FIniSettingContainer& OutIniSettings, TMap<FString,FString>& OutAdditionalSettings) const
{
	const FString EditorIni = FPaths::ProjectDir() / TEXT("Metadata") / TEXT("CookedIniVersion.txt");
	const FString SandboxEditorIni = ConvertToFullSandboxPath(*EditorIni, true);


	const FString PlatformSandboxEditorIni = SandboxEditorIni.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());

	TArray<FString> SavedIniVersionedParams;

	FConfigFile ConfigFile;
	ConfigFile.Read(*PlatformSandboxEditorIni);

	

	const static FName NAME_UsedSettings(TEXT("UsedSettings")); 
	const FConfigSection* UsedSettings = ConfigFile.Find(NAME_UsedSettings.ToString());
	if (UsedSettings == nullptr)
	{
		return false;
	}


	const static FName NAME_AdditionalSettings(TEXT("AdditionalSettings"));
	const FConfigSection* AdditionalSettings = ConfigFile.Find(NAME_AdditionalSettings.ToString());
	if (AdditionalSettings == nullptr)
	{
		return false;
	}


	for (const auto& UsedSetting : *UsedSettings )
	{
		FName Key = UsedSetting.Key;
		const FConfigValue& UsedValue = UsedSetting.Value;

		TArray<FString> SplitString;
		Key.ToString().ParseIntoArray(SplitString, TEXT(":"));

		if (SplitString.Num() != 4)
		{
			UE_LOG(LogCook, Warning, TEXT("Found unparsable ini setting %s for platform %s, invalidating cook."), *Key.ToString(), *TargetPlatform->PlatformName());
			return false;
		}


		check(SplitString.Num() == 4); // We generate this ini file in SaveCurrentIniSettings
		const FString& Filename = SplitString[0];
		const FString& SectionName = SplitString[1];
		const FString& ValueName = SplitString[2];
		const int32 ValueIndex = FCString::Atoi(*SplitString[3]);

		auto& OutFile = OutIniSettings.FindOrAdd(FName(*Filename));
		auto& OutSection = OutFile.FindOrAdd(FName(*SectionName));
		auto& ValueArray = OutSection.FindOrAdd(FName(*ValueName));
		if ( ValueArray.Num() < (ValueIndex+1) )
		{
			ValueArray.AddZeroed( ValueIndex - ValueArray.Num() +1 );
		}
		ValueArray[ValueIndex] = UsedValue.GetSavedValue();
	}



	for (const auto& AdditionalSetting : *AdditionalSettings)
	{
		const FName& Key = AdditionalSetting.Key;
		const FString& Value = AdditionalSetting.Value.GetSavedValue();
		OutAdditionalSettings.Add(Key.ToString(), Value);
	}

	return true;
}



void UCookOnTheFlyServer::OnFConfigCreated(const FConfigFile* Config)
{
	FScopeLock Lock(&ConfigFileCS);
	if (IniSettingRecurse)
	{
		return;
	}

	OpenConfigFiles.Add(Config);
}

void UCookOnTheFlyServer::OnFConfigDeleted(const FConfigFile* Config)
{
	FScopeLock Lock(&ConfigFileCS);
	if (IniSettingRecurse)
	{
		return;
	}

	ProcessAccessedIniSettings(Config, AccessedIniStrings);

	OpenConfigFiles.Remove(Config);
}

void UCookOnTheFlyServer::ProcessAccessedIniSettings(const FConfigFile* Config, FIniSettingContainer& OutAccessedIniStrings) const
{	
	if (Config->Name == NAME_None)
	{
		return;
	}

	// try to figure out if this config file is for a specific platform 
	FString PlatformName;
	bool bFoundPlatformName = false;

	if (!GConfig->ContainsConfigFile(Config)) // If the ConfigFile is in GConfig, then it is the editor's config and is not platform specific
	{
		// For the config files not in GConfig, we assume they were loaded from LoadConfigFile, and we match these to a platform
		// By looking for a platform-specific filepath in their SourceIniHierarchy.
		// Examples:
		// (1) ROOT\Engine\Config\Windows\WindowsEngine.ini
		// (2) ROOT\Engine\Config\Android\DataDrivePlatformInfo.ini
		// (3) ROOT\Engine\Config\Android\AndroidWindowsCompatability.ini
		// 
		// Note that for config files of form #3, we want them to be matched to Android rather than windows;
		// we assume that an exact match on a directory component is more definitive than a substring match
		ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
		const TArray<ITargetPlatform*>& Platforms = TPM.GetTargetPlatforms();
		bool bFoundPlatformGuess = false;
		for (const ITargetPlatform* Platform : Platforms )
		{
			const FString CurrentPlatformName = Platform->IniPlatformName();
			if (CurrentPlatformName.IsEmpty())
			{
				continue;
			}
			TStringBuilder<128> PlatformDirString;
			PlatformDirString.Appendf(TEXT("/%s/"), *CurrentPlatformName);
			for (const auto& SourceIni : Config->SourceIniHierarchy)
			{
				// Look for platform in the path, rating a full subdirectory name match (/Android/ or /Windows/) higher than a partial filename match (AndroidEngine.ini or WindowsEngine.ini)
				bool bFoundPlatformDir = UE::String::FindFirst(SourceIni.Value.Filename, PlatformDirString, ESearchCase::IgnoreCase) != INDEX_NONE;
				bool bFoundPlatformSubstring = UE::String::FindFirst(SourceIni.Value.Filename, CurrentPlatformName, ESearchCase::IgnoreCase) != INDEX_NONE;
				if (bFoundPlatformDir)
				{
					PlatformName = CurrentPlatformName;
					bFoundPlatformName = true;
					break;
				}
				else if (!bFoundPlatformGuess && bFoundPlatformSubstring)
				{
					PlatformName = CurrentPlatformName;
					bFoundPlatformGuess = true;
				}
			}
			if (bFoundPlatformName)
			{
				break;
			}
		}
		bFoundPlatformName = bFoundPlatformName || bFoundPlatformGuess;
	}

	TStringBuilder<128> ConfigName;
	if (bFoundPlatformName)
	{
		ConfigName << PlatformName;
		ConfigName << TEXT(".");
	}
	Config->Name.AppendString(ConfigName);
	const FName& ConfigFName = FName(ConfigName);
	TSet<FName> ProcessedValues;
	TCHAR PlainNameString[NAME_SIZE];
	TArray<const FConfigValue*> ValueArray;
	for ( auto& ConfigSection : *Config )
	{
		ProcessedValues.Reset();
		const FName SectionName = FName(*ConfigSection.Key);

		SectionName.GetPlainNameString(PlainNameString);
		if ( TCString<TCHAR>::Strstr(PlainNameString, TEXT(":")) )
		{
			UE_LOG(LogCook, Verbose, TEXT("Ignoring ini section checking for section name %s because it contains ':'"), PlainNameString);
			continue;
		}

		for ( auto& ConfigValue : ConfigSection.Value )
		{
			const FName& ValueName = ConfigValue.Key;
			if ( ProcessedValues.Contains(ValueName) )
				continue;

			ProcessedValues.Add(ValueName);

			ValueName.GetPlainNameString(PlainNameString);
			if (TCString<TCHAR>::Strstr(PlainNameString, TEXT(":")))
			{
				UE_LOG(LogCook, Verbose, TEXT("Ignoring ini section checking for section name %s because it contains ':'"), PlainNameString);
				continue;
			}

			
			ValueArray.Reset();
			ConfigSection.Value.MultiFindPointer( ValueName, ValueArray, true );

			bool bHasBeenAccessed = false;
			for (const FConfigValue* ValueArrayEntry : ValueArray)
			{
				if (ValueArrayEntry->HasBeenRead())
				{
					bHasBeenAccessed = true;
					break;
				}
			}

			if ( bHasBeenAccessed )
			{
				auto& AccessedConfig = OutAccessedIniStrings.FindOrAdd(ConfigFName);
				auto& AccessedSection = AccessedConfig.FindOrAdd(SectionName);
				auto& AccessedKey = AccessedSection.FindOrAdd(ValueName);
				AccessedKey.Empty(ValueArray.Num());
				for (const FConfigValue* ValueArrayEntry : ValueArray )
				{
					FString RemovedColon = ValueArrayEntry->GetSavedValue().Replace(TEXT(":"), TEXT(""));
					AccessedKey.Add(MoveTemp(RemovedColon));
				}
			}
			
		}
	}
}

bool UCookOnTheFlyServer::IniSettingsOutOfDate(const ITargetPlatform* TargetPlatform) const
{
	FScopeAssign<bool> A = FScopeAssign<bool>(IniSettingRecurse, true);

	FIniSettingContainer OldIniSettings;
	TMap<FString, FString> OldAdditionalSettings;
	if ( GetCookedIniVersionStrings(TargetPlatform, OldIniSettings, OldAdditionalSettings) == false)
	{
		UE_LOG(LogCook, Display, TEXT("Unable to read previous cook inisettings for platform %s invalidating cook"), *TargetPlatform->PlatformName());
		return true;
	}

	// compare against current settings
	TMap<FString, FString> CurrentAdditionalSettings;
	GetAdditionalCurrentIniVersionStrings(TargetPlatform, CurrentAdditionalSettings);

	for ( const auto& OldIniSetting : OldAdditionalSettings)
	{
		const FString* CurrentValue = CurrentAdditionalSettings.Find(OldIniSetting.Key);
		if ( !CurrentValue )
		{
			UE_LOG(LogCook, Display, TEXT("Previous cook had additional ini setting: %s current cook is missing this setting."), *OldIniSetting.Key);
			return true;
		}

		if ( *CurrentValue != OldIniSetting.Value )
		{
			UE_LOG(LogCook, Display, TEXT("Additional Setting from previous cook %s doesn't match %s %s"), *OldIniSetting.Key, **CurrentValue, *OldIniSetting.Value );
			return true;
		}
	}

	for (const auto& OldIniFile : OldIniSettings)
	{
		FName ConfigNameKey = OldIniFile.Key;

		TArray<FString> ConfigNameArray;
		ConfigNameKey.ToString().ParseIntoArray(ConfigNameArray, TEXT("."));
		FString Filename;
		FString PlatformName;
		// The input NameKey is of the form 
		//   Platform.ConfigName:Section:Key:ArrayIndex=Value
		// The Platform is optional and will not be present if the configfile was an editor config file rather than a platform-specific config file
		bool bFoundPlatformName = false;
		if (ConfigNameArray.Num() <= 1)
		{
			Filename = ConfigNameKey.ToString();
		}
		else if (ConfigNameArray.Num() == 2)
		{
			PlatformName = ConfigNameArray[0];
			Filename = ConfigNameArray[1];
			bFoundPlatformName = true;
		}
		else
		{
			UE_LOG(LogCook, Warning, TEXT("Found invalid file name in old ini settings file Filename %s settings file %s"), *ConfigNameKey.ToString(), *TargetPlatform->PlatformName());
			return true;
		}

		const FConfigFile* ConfigFile = nullptr;
		FConfigFile Temp;
		if (bFoundPlatformName)
		{
			// For the platform-specific old ini files, load them using LoadLocalIniFiles; this matches the assumption in SaveCurrentIniSettings
			// that the platform-specific ini files were loaded by LoadLocalIniFiles
			FConfigCacheIni::LoadLocalIniFile(Temp, *Filename, true, *PlatformName);
			ConfigFile = &Temp;
		}
		else
		{
			// For the platform-agnostic old ini files, read them from GConfig; this matches where we loaded them from in SaveCurrentIniSettings
			// The ini files may have been saved by fullpath or by shortname; search first for a fullpath match using FindConfigFile and
			// if that fails search for the shortname match by iterating over all files in GConfig
			ConfigFile = GConfig->FindConfigFile(Filename);
		}
		if (!ConfigFile)
		{
			FName FileFName = FName(*Filename);
			for( const auto& File : *GConfig )
			{
				if (File.Value.Name == FileFName)
				{
					ConfigFile = &File.Value;
					break;
				}
			}
			if (!ConfigFile)
			{
				UE_LOG(LogCook, Display, TEXT("Unable to find config file %s invalidating inisettings"), *FString::Printf(TEXT("%s %s"), *PlatformName, *Filename));
				return true;
			}
		}
		for ( const auto& OldIniSection : OldIniFile.Value )
		{
			const FName& SectionName = OldIniSection.Key;
			const FConfigSection* IniSection = ConfigFile->Find( SectionName.ToString() );
			const FString BlackListSetting = FString::Printf(TEXT("%s%s%s:%s"), *PlatformName, bFoundPlatformName ? TEXT(".") : TEXT(""), *Filename, *SectionName.ToString());

			if ( IniSection == nullptr )
			{
				UE_LOG(LogCook, Display, TEXT("Inisetting is different for %s, Current section doesn't exist"), 
					*FString::Printf(TEXT("%s %s %s"), *PlatformName, *Filename, *SectionName.ToString()));
				UE_LOG(LogCook, Display, TEXT("To avoid this add blacklist setting to DefaultEditor.ini [CookSettings] %s"), *BlackListSetting);
				return true;
			}

			for ( const auto& OldIniValue : OldIniSection.Value )
			{
				const FName& ValueName = OldIniValue.Key;

				TArray<FConfigValue> CurrentValues;
				IniSection->MultiFind( ValueName, CurrentValues, true );

				if ( CurrentValues.Num() != OldIniValue.Value.Num() )
				{
					UE_LOG(LogCook, Display, TEXT("Inisetting is different for %s, missmatched num array elements %d != %d "), *FString::Printf(TEXT("%s %s %s %s"),
						*PlatformName, *Filename, *SectionName.ToString(), *ValueName.ToString()), CurrentValues.Num(), OldIniValue.Value.Num());
					UE_LOG(LogCook, Display, TEXT("To avoid this add blacklist setting to DefaultEditor.ini [CookSettings] %s"), *BlackListSetting);
					return true;
				}
				for ( int Index = 0; Index < CurrentValues.Num(); ++Index )
				{
					const FString FilteredCurrentValue = CurrentValues[Index].GetSavedValue().Replace(TEXT(":"), TEXT(""));
					if ( FilteredCurrentValue != OldIniValue.Value[Index] )
					{
						UE_LOG(LogCook, Display, TEXT("Inisetting is different for %s, value %s != %s invalidating cook"),
							*FString::Printf(TEXT("%s %s %s %s %d"),*PlatformName, *Filename, *SectionName.ToString(), *ValueName.ToString(), Index),
							*CurrentValues[Index].GetSavedValue(), *OldIniValue.Value[Index] );
						UE_LOG(LogCook, Display, TEXT("To avoid this add blacklist setting to DefaultEditor.ini [CookSettings] %s"), *BlackListSetting);
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool UCookOnTheFlyServer::SaveCurrentIniSettings(const ITargetPlatform* TargetPlatform) const
{
	FScopeAssign<bool> S = FScopeAssign<bool>(IniSettingRecurse, true);

	TMap<FString, FString> AdditionalIniSettings;
	GetAdditionalCurrentIniVersionStrings(TargetPlatform, AdditionalIniSettings);

	FIniSettingContainer CurrentIniSettings;
	GetCurrentIniVersionStrings(TargetPlatform, CurrentIniSettings);

	const FString EditorIni = FPaths::ProjectDir() / TEXT("Metadata") / TEXT("CookedIniVersion.txt");
	const FString SandboxEditorIni = ConvertToFullSandboxPath(*EditorIni, true);


	const FString PlatformSandboxEditorIni = SandboxEditorIni.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());


	FConfigFile ConfigFile;
	// ConfigFile.Read(*PlatformSandboxEditorIni);

	ConfigFile.Dirty = true;
	const static FName NAME_UsedSettings(TEXT("UsedSettings"));
	ConfigFile.Remove(NAME_UsedSettings.ToString());
	FConfigSection& UsedSettings = ConfigFile.FindOrAdd(NAME_UsedSettings.ToString());


	{
		UE_SCOPED_HIERARCHICAL_COOKTIMER(ProcessingAccessedStrings)
		for (const auto& CurrentIniFilename : CurrentIniSettings)
		{
			const FName& Filename = CurrentIniFilename.Key;
			for ( const auto& CurrentSection : CurrentIniFilename.Value )
			{
				const FName& Section = CurrentSection.Key;
				for ( const auto& CurrentValue : CurrentSection.Value )
				{
					const FName& ValueName = CurrentValue.Key;
					const TArray<FString>& Values = CurrentValue.Value;

					for ( int Index = 0; Index < Values.Num(); ++Index )
					{
						FString NewKey = FString::Printf(TEXT("%s:%s:%s:%d"), *Filename.ToString(), *Section.ToString(), *ValueName.ToString(), Index);
						UsedSettings.Add(FName(*NewKey), Values[Index]);
					}
				}
			}
		}
	}


	const static FName NAME_AdditionalSettings(TEXT("AdditionalSettings"));
	ConfigFile.Remove(NAME_AdditionalSettings.ToString());
	FConfigSection& AdditionalSettings = ConfigFile.FindOrAdd(NAME_AdditionalSettings.ToString());

	for (const auto& AdditionalIniSetting : AdditionalIniSettings)
	{
		AdditionalSettings.Add( FName(*AdditionalIniSetting.Key), AdditionalIniSetting.Value );
	}

	ConfigFile.Write(PlatformSandboxEditorIni);


	return true;

}

FName UCookOnTheFlyServer::ConvertCookedPathToUncookedPath(
	const FString& SandboxRootDir, const FString& RelativeRootDir,
	const FString& SandboxProjectDir, const FString& RelativeProjectDir,
	const FString& CookedPath, FString& OutUncookedPath) const
{
	OutUncookedPath.Reset();

	// Check for remapped plugins' cooked content
	if (PluginsToRemap.Num() > 0 && CookedPath.Contains(REMAPPED_PLUGINS))
	{
		int32 RemappedIndex = CookedPath.Find(REMAPPED_PLUGINS);
		check(RemappedIndex >= 0);
		static uint32 RemappedPluginStrLen = FCString::Strlen(REMAPPED_PLUGINS);
		// Snip everything up through the RemappedPlugins/ off so we can find the plugin it corresponds to
		FString PluginPath = CookedPath.RightChop(RemappedIndex + RemappedPluginStrLen + 1);
		// Find the plugin that owns this content
		for (TSharedRef<IPlugin> Plugin : PluginsToRemap)
		{
			if (PluginPath.StartsWith(Plugin->GetName()))
			{
				OutUncookedPath = Plugin->GetContentDir();
				static uint32 ContentStrLen = FCString::Strlen(TEXT("Content/"));
				// Chop off the pluginName/Content since it's part of the full path
				OutUncookedPath /= PluginPath.RightChop(Plugin->GetName().Len() + ContentStrLen);
				break;
			}
		}

		if (OutUncookedPath.Len() > 0)
		{
			return FName(*OutUncookedPath);
		}
		// Otherwise fall through to sandbox handling
	}

	auto BuildUncookedPath = 
		[&OutUncookedPath](const FString& CookedPath, const FString& CookedRoot, const FString& UncookedRoot)
	{
		OutUncookedPath.AppendChars(*UncookedRoot, UncookedRoot.Len());
		OutUncookedPath.AppendChars(*CookedPath + CookedRoot.Len(), CookedPath.Len() - CookedRoot.Len());
	};

	if (CookedPath.StartsWith(SandboxRootDir))
	{
		// Optimized CookedPath.StartsWith(SandboxProjectDir) that does not compare all of SandboxRootDir again
		if (CookedPath.Len() >= SandboxProjectDir.Len() && 
			0 == FCString::Strnicmp(
				*CookedPath + SandboxRootDir.Len(),
				*SandboxProjectDir + SandboxRootDir.Len(),
				SandboxProjectDir.Len() - SandboxRootDir.Len()))
		{
			BuildUncookedPath(CookedPath, SandboxProjectDir, RelativeProjectDir);
		}
		else
		{
			BuildUncookedPath(CookedPath, SandboxRootDir, RelativeRootDir);
		}
	}
	else
	{
		FString FullCookedFilename = FPaths::ConvertRelativePathToFull(CookedPath);
		BuildUncookedPath(FullCookedFilename, SandboxRootDir, RelativeRootDir);
	}

	// Convert to a standard filename as required by FPackageNameCache where this path is used.
	FPaths::MakeStandardFilename(OutUncookedPath);

	return FName(*OutUncookedPath);
}

void UCookOnTheFlyServer::GetAllCookedFiles(TMap<FName, FName>& UncookedPathToCookedPath, const FString& SandboxRootDir)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::GetAllCookedFiles);

	TArray<FString> CookedFiles;
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		FPackageSearchVisitor PackageSearch(CookedFiles);
		PlatformFile.IterateDirectoryRecursively(*SandboxRootDir, PackageSearch);
	}

	const FString SandboxProjectDir = FPaths::Combine(*SandboxRootDir, FApp::GetProjectName()) + TEXT("/");
	const FString RelativeRootDir = FPaths::GetRelativePathToRoot();
	const FString RelativeProjectDir = FPaths::ProjectDir();
	FString UncookedFilename;
	UncookedFilename.Reserve(1024);

	for (const FString& CookedFile : CookedFiles)
	{
		const FName CookedFName(*CookedFile);
		const FName UncookedFName = ConvertCookedPathToUncookedPath(
			SandboxRootDir, RelativeRootDir,
			SandboxProjectDir, RelativeProjectDir,
			CookedFile, UncookedFilename);

		UncookedPathToCookedPath.Add(UncookedFName, CookedFName);
	}
}

void UCookOnTheFlyServer::DeleteSandboxDirectory(const FString& PlatformName)
{
	FString SandboxDirectory = GetSandboxDirectory(PlatformName);
	FPaths::NormalizeDirectoryName(SandboxDirectory);
	FString AsyncDeleteDirectory = GetAsyncDeleteDirectory(PlatformName, &SandboxDirectory);

	FAsyncIODelete& LocalAsyncIODelete = GetAsyncIODelete(PlatformName, &AsyncDeleteDirectory);
	LocalAsyncIODelete.DeleteDirectory(SandboxDirectory);

	// Part of Deleting the sandbox includes deleting the old AsyncDelete directory for the sandbox, in case a previous cooker crashed before cleaning it up.
	// The AsyncDelete directory is associated with a sandbox but is necessarily outside of it since it is used to delete the sandbox.
	// Note that for the Platform we used to create the AsyncIODelete, this Delete will fail because AsyncIODelete refuses to delete its own temproot; this is okay because it will delete the temproot on exit.
	LocalAsyncIODelete.DeleteDirectory(AsyncDeleteDirectory);

	// UE_DEPRECATED(4.25, "Delete the old location for AsyncDeleteDirectory until all users have cooked at least once")
	LocalAsyncIODelete.DeleteDirectory(SandboxDirectory + TEXT("AsyncDelete"));
}

FAsyncIODelete& UCookOnTheFlyServer::GetAsyncIODelete(const FString& PlatformName, const FString* AsyncDeleteDirectory)
{
	FAsyncIODelete* AsyncIODeletePtr = AsyncIODelete.Get();
	if (!AsyncIODeletePtr)
	{
		FString Buffer;
		if (!AsyncDeleteDirectory)
		{
			Buffer = GetAsyncDeleteDirectory(PlatformName);
			AsyncDeleteDirectory = &Buffer;
		}
		AsyncIODelete = MakeUnique<FAsyncIODelete>(*AsyncDeleteDirectory);
		AsyncIODeletePtr = AsyncIODelete.Get();
	}
	// If we have already created the AsyncIODelete, we ignore the input PlatformName and use the existing AsyncIODelete initialized from whatever platform we used before
	// The PlatformName is used only to construct a directory that we can be sure no other process is using (because a sandbox can only be cooked by one process at a time)
	return *AsyncIODeletePtr;
}

FString UCookOnTheFlyServer::GetAsyncDeleteDirectory(const FString& PlatformName, const FString* SandboxDirectory) const
{
	// The TempRoot we will delete into is a sibling of the the Platform-specific sandbox directory, with name [PlatformDir]AsyncDelete
	// Note that two UnrealEd-Cmd processes cooking to the same sandbox at the same time will therefore cause an error since FAsyncIODelete doesn't handle multiple processes sharing TempRoots.
	// That simultaneous-cook behavior is also not supported in other assumptions throughout the cooker.
	FString Buffer;
	if (!SandboxDirectory)
	{
		// Avoid recalculating SandboxDirectory if caller supplied it, but if they didn't, calculate it here
		Buffer = GetSandboxDirectory(PlatformName);
		FPaths::NormalizeDirectoryName(Buffer);
		SandboxDirectory = &Buffer;
	}
	return (*SandboxDirectory) + TEXT("_Del");
}

void UCookOnTheFlyServer::PopulateCookedPackagesFromDisk(const TArrayView<const ITargetPlatform* const>& Platforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::PopulateCookedPackagesFromDisk);

	// See what files are out of date in the sandbox folder
	for (int32 Index = 0; Index < Platforms.Num(); Index++)
	{
		TArray<FString> CookedPackagesToDelete;

		const ITargetPlatform* Target = Platforms[Index];
		UE::Cook::FPlatformData* PlatformData = PlatformManager->GetPlatformData(Target);
		FString SandboxPath = GetSandboxDirectory(Target->PlatformName());

		FString EngineSandboxPath = SandboxFile->ConvertToSandboxPath(*FPaths::EngineDir()) + TEXT("/");
		EngineSandboxPath.ReplaceInline(TEXT("[Platform]"), *Target->PlatformName());

		FString GameSandboxPath = SandboxFile->ConvertToSandboxPath(*(FPaths::ProjectDir() + TEXT("a.txt")));
		GameSandboxPath.ReplaceInline(TEXT("a.txt"), TEXT(""));
		GameSandboxPath.ReplaceInline(TEXT("[Platform]"), *Target->PlatformName());

		FString LocalGamePath = FPaths::ProjectDir();
		if (FPaths::IsProjectFilePathSet())
		{
			LocalGamePath = FPaths::GetPath(FPaths::GetProjectFilePath()) + TEXT("/");
		}

		FString LocalEnginePath = FPaths::EngineDir();

		// Registry generator already exists
		FAssetRegistryGenerator* PlatformAssetRegistry = PlatformData->RegistryGenerator.Get();
		check(PlatformAssetRegistry);

		// Load the platform cooked asset registry file
		const FString CookedAssetRegistry = FPaths::ProjectDir() / TEXT("Metadata") / GetDevelopmentAssetRegistryFilename();
		const FString SandboxCookedAssetRegistryFilename = ConvertToFullSandboxPath(*CookedAssetRegistry, true, Target->PlatformName());

		bool bIsIterateSharedBuild = IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild);

		if (bIsIterateSharedBuild)
		{
			// see if the shared build is newer then the current cooked content in the local directory
			FDateTime CurrentLocalCookedBuild = IFileManager::Get().GetTimeStamp(*SandboxCookedAssetRegistryFilename);

			// iterate on the shared build if the option is set
			FString SharedCookedAssetRegistry = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("SharedIterativeBuild"), *Target->PlatformName(), TEXT("Metadata"), GetDevelopmentAssetRegistryFilename());

			FDateTime CurrentIterativeCookedBuild = IFileManager::Get().GetTimeStamp(*SharedCookedAssetRegistry);

			if ( (CurrentIterativeCookedBuild >= CurrentLocalCookedBuild) && 
				(CurrentIterativeCookedBuild != FDateTime::MinValue()) )
			{
				// clean the sandbox
				ClearPlatformCookedData(Target);

				// SaveCurrentIniSettings(Target); // use this if we don't care about ini safty.
				// copy the ini settings from the shared cooked build. 
				const FString PlatformName = Target->PlatformName();
				const FString SharedCookedIniFile = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("SharedIterativeBuild"), *PlatformName, TEXT("Metadata"), TEXT("CookedIniVersion.txt"));
				const FString SandboxCookedIniFile = ConvertToFullSandboxPath(*(FPaths::ProjectDir() / TEXT("Metadata") / TEXT("CookedIniVersion.txt")), true).Replace(TEXT("[Platform]"), *PlatformName);

				IFileManager::Get().Copy(*SandboxCookedIniFile, *SharedCookedIniFile);

				bool bIniSettingsOutOfDate = IniSettingsOutOfDate(Target);
				if (bIniSettingsOutOfDate && !IsCookFlagSet(ECookInitializationFlags::IgnoreIniSettingsOutOfDate))
				{
					UE_LOG(LogCook, Display, TEXT("Shared iterative build ini settings out of date, not using shared cooked build"));
				}
				else
				{
					if (bIniSettingsOutOfDate)
					{
						UE_LOG(LogCook, Display, TEXT("Shared iterative build ini settings out of date, but we don't care"));
					}

					UE_LOG(LogCook, Display, TEXT("Shared iterative build is newer then local cooked build, iteratively cooking from shared build "));
					PlatformAssetRegistry->LoadPreviousAssetRegistry(SharedCookedAssetRegistry);
				}
			}
			else
			{
				UE_LOG(LogCook, Display, TEXT("Local cook is newer then shared cooked build, iterativly cooking from local build"));
				PlatformAssetRegistry->LoadPreviousAssetRegistry(SandboxCookedAssetRegistryFilename);
			}
		}
		else
		{
			PlatformAssetRegistry->LoadPreviousAssetRegistry(SandboxCookedAssetRegistryFilename);
		}

		// Get list of changed packages
		TSet<FName> ModifiedPackages, NewPackages, RemovedPackages, IdenticalCookedPackages, IdenticalUncookedPackages;

		// We recurse modifications up the reference chain because it is safer, if this ends up being a significant issue in some games we can add a command line flag
		bool bRecurseModifications = true;
		bool bRecurseScriptModifications = !IsCookFlagSet(ECookInitializationFlags::IgnoreScriptPackagesOutOfDate);
		PlatformAssetRegistry->ComputePackageDifferences(ModifiedPackages, NewPackages, RemovedPackages, IdenticalCookedPackages, IdenticalUncookedPackages, bRecurseModifications, bRecurseScriptModifications);

		// check the files on disk 
		TMap<FName, FName> UncookedPathToCookedPath;
		// get all the on disk cooked files
		GetAllCookedFiles(UncookedPathToCookedPath, SandboxPath);

		const static FName NAME_DummyCookedFilename(TEXT("DummyCookedFilename")); // pls never name a package dummycookedfilename otherwise shit might go wonky
		if (bIsIterateSharedBuild)
		{
			check(IFileManager::Get().FileExists(*NAME_DummyCookedFilename.ToString()) == false);

			TSet<FName> ExistingPackages = ModifiedPackages;
			ExistingPackages.Append(RemovedPackages);
			ExistingPackages.Append(IdenticalCookedPackages);
			ExistingPackages.Append(IdenticalUncookedPackages);

			// if we are iterating of a shared build the cooked files might not exist in the cooked directory because we assume they are packaged in the pak file (which we don't want to extract)
			for (FName PackageName : ExistingPackages)
			{
				FString Filename;
				if (FPackageName::DoesPackageExist(PackageName.ToString(), nullptr, &Filename))
				{
					UncookedPathToCookedPath.Add(FName(*Filename), NAME_DummyCookedFilename);
				}
			}
		}

		uint32 NumPackagesConsidered = UncookedPathToCookedPath.Num();
		uint32 NumPackagesUnableToFindCookedPackageInfo = 0;
		uint32 NumPackagesFileHashMismatch = 0;
		uint32 NumPackagesKept = 0;
		uint32 NumMarkedFailedSaveKept = 0;
		uint32 NumPackagesRemoved = 0;

		TArray<FName> KeptPackages;

		for (const auto& CookedPaths : UncookedPathToCookedPath)
		{
			const FName CookedFile = CookedPaths.Value;
			const FName UncookedFilename = CookedPaths.Key;
			const FName* FoundPackageName = GetPackageNameCache().GetCachedPackageNameFromStandardFileName(UncookedFilename);
			bool bShouldKeep = true;
			const FName SourcePackageName = FoundPackageName ? *FoundPackageName : NAME_None;
			if ( !FoundPackageName )
			{
				// Source file no longer exists
				++NumPackagesRemoved;
				bShouldKeep = false;
			}
			else
			{
				if (ModifiedPackages.Contains(SourcePackageName))
				{
					++NumPackagesFileHashMismatch;
					bShouldKeep = false;
				}
				else if (NewPackages.Contains(SourcePackageName) || RemovedPackages.Contains(SourcePackageName))
				{
					++NumPackagesUnableToFindCookedPackageInfo;
					bShouldKeep = false;
				}
				else if (IdenticalUncookedPackages.Contains(SourcePackageName))
				{
					// These are packages which failed to save the first time 
					// most likely because they are editor only packages
					bShouldKeep = false;
				}
			}

			TArray<const ITargetPlatform*> PlatformsForPackage;
			PlatformsForPackage.Add(Target);

			if (bShouldKeep)
			{
				// Mark this package as cooked so that we don't unnecessarily try to cook it again
				if (IdenticalCookedPackages.Contains(SourcePackageName))
				{
					UE::Cook::FPackageData* PackageData = PackageDatas->TryAddPackageDataByPackageName(SourcePackageName);
					if (PackageData)
					{
						PackageData->AddCookedPlatforms({ Target }, true /* bSucceeded */);
						KeptPackages.Add(SourcePackageName);
						++NumPackagesKept;
					}
				}
			}
			else
			{
				if (SourcePackageName != NAME_None && IsCookByTheBookMode()) // cook on the fly will requeue this package when it wants it 
				{
					// Force cook the modified file
					ExternalRequests->EnqueueUnique(UE::Cook::FFilePlatformRequest(UncookedFilename, PlatformsForPackage));
				}
				if (CookedFile != NAME_DummyCookedFilename)
				{
					// delete the old package 
					const FString CookedFullPath = FPaths::ConvertRelativePathToFull(CookedFile.ToString());
					UE_LOG(LogCook, Verbose, TEXT("Deleting cooked package %s failed filehash test"), *CookedFullPath);
					CookedPackagesToDelete.Add(CookedFullPath);
				}
				else
				{
					// the cooker should rebuild this package because it's not in the cooked package list
					// the new package will have higher priority then the package in the original shared cooked build
					const FString UncookedFilenameString = UncookedFilename.ToString();
					UE_LOG(LogCook, Verbose, TEXT("Shared cooked build: Detected package is out of date %s"), *UncookedFilenameString);
				}
			}
		}

		// Register identical uncooked packages from previous run
		for (FName UncookedPackage : IdenticalUncookedPackages)
		{
			UE::Cook::FPackageData* PackageData = PackageDatas->TryAddPackageDataByPackageName(UncookedPackage);
			if (PackageData)
			{
				ensure(!PackageData->HasAnyCookedPlatforms({ Target }, /* bIncludeFailed */ false));
				PackageData->AddCookedPlatforms({ Target }, false /* bSucceeded */);
				KeptPackages.Add(UncookedPackage);
				++NumMarkedFailedSaveKept;
			}
		}

		PlatformAssetRegistry->UpdateKeptPackages(KeptPackages);

		UE_LOG(LogCook, Display, TEXT("Iterative cooking summary for %s, \nConsidered: %d, \nFile Hash missmatch: %d, \nPackages Kept: %d, \nPackages failed save kept: %d, \nMissing Cooked Info(expected 0): %d"),
			*Target->PlatformName(),
			NumPackagesConsidered, NumPackagesFileHashMismatch,
			NumPackagesKept, NumMarkedFailedSaveKept,
			NumPackagesUnableToFindCookedPackageInfo);

		auto DeletePackageLambda = [&CookedPackagesToDelete](int32 PackageIndex)
		{
			const FString& CookedFullPath = CookedPackagesToDelete[PackageIndex];
			IFileManager::Get().Delete(*CookedFullPath, true, true, true);
		};
		ParallelFor(CookedPackagesToDelete.Num(), DeletePackageLambda);
	}
}

const FString ExtractPackageNameFromObjectPath( const FString ObjectPath )
{
	// get the path 
	int32 Beginning = ObjectPath.Find(TEXT("'"), ESearchCase::CaseSensitive);
	if ( Beginning == INDEX_NONE )
	{
		return ObjectPath;
	}
	int32 End = ObjectPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, Beginning + 1);
	if (End == INDEX_NONE )
	{
		End = ObjectPath.Find(TEXT("'"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Beginning + 1);
	}
	if ( End == INDEX_NONE )
	{
		// one more use case is that the path is "Class'Path" example "OrionBoostItemDefinition'/Game/Misc/Boosts/XP_1Win" dunno why but this is actually dumb
		if ( ObjectPath[Beginning+1] == '/' )
		{
			return ObjectPath.Mid(Beginning+1);
		}
		return ObjectPath;
	}
	return ObjectPath.Mid(Beginning + 1, End - Beginning - 1);
}

void DumpAssetRegistryForCooker(IAssetRegistry* AssetRegistry)
{
	FString DumpDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() + TEXT("Reports/AssetRegistryStatePages"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FAsyncIODelete DeleteReportDir(DumpDir + TEXT("_Del"));
	DeleteReportDir.DeleteDirectory(DumpDir);
	PlatformFile.CreateDirectoryTree(*DumpDir);
	TArray<FString> Pages;
	TArray<FString> Arguments({ TEXT("ObjectPath"),TEXT("PackageName"),TEXT("Path"),TEXT("Class"),TEXT("Tag"), TEXT("DependencyDetails"), TEXT("PackageData"), TEXT("LegacyDependencies") });
	AssetRegistry->GetAssetRegistryState()->Dump(Arguments, Pages, 10000 /* LinesPerPage */);
	int PageIndex = 0;
	TStringBuilder<256> FileName;
	for (FString& PageText : Pages)
	{
		FileName.Reset();
		FileName.Appendf(TEXT("%s_%05d.txt"), *(DumpDir / TEXT("Page")), PageIndex++);
		PageText.ToLowerInline();
		FFileHelper::SaveStringToFile(PageText, *FileName);
	}
}

void UCookOnTheFlyServer::GenerateAssetRegistry()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::GenerateAssetRegistry);

	// Cache asset registry for later
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistry = &AssetRegistryModule.Get();

	// Mark package as dirty for the last ones saved
	for (FName AssetFilename : ModifiedAssetFilenames)
	{
		const FString AssetPathOnDisk = AssetFilename.ToString();
		if (FPaths::FileExists(AssetPathOnDisk))
		{
			const FString PackageName = FPackageName::FilenameToLongPackageName(AssetPathOnDisk);
			FSoftObjectPath SoftPackage(PackageName);
			if (UPackage* Package = Cast<UPackage>(SoftPackage.ResolveObject()))
			{
				MarkPackageDirtyForCooker(Package, true);
			}
		}
	}

	if (!!(CookFlags & ECookInitializationFlags::GeneratedAssetRegistry))
	{
		UE_LOG(LogCook, Display, TEXT("Updating asset registry"));

		// Force a rescan of modified package files
		TArray<FString> ModifiedPackageFileList;

		for (FName ModifiedPackage : ModifiedAssetFilenames)
		{
			ModifiedPackageFileList.Add(ModifiedPackage.ToString());
		}

		AssetRegistry->ScanModifiedAssetFiles(ModifiedPackageFileList);
	}
	else
	{
		CookFlags |= ECookInitializationFlags::GeneratedAssetRegistry;
		UE_LOG(LogCook, Display, TEXT("Creating asset registry"));

		ModifiedAssetFilenames.Reset();

		// Perform a synchronous search of any .ini based asset paths (note that the per-game delegate may
		// have already scanned paths on its own)
		// We want the registry to be fully initialized when generating streaming manifests too.

		// editor will scan asset registry automagically 
		bool bCanDelayAssetregistryProcessing = IsRealtimeMode();

		// if we are running in the editor we need the asset registry to be finished loaded before we process any iterative cook requests
		bCanDelayAssetregistryProcessing &= !IsCookFlagSet(ECookInitializationFlags::Iterative);

		if (!bCanDelayAssetregistryProcessing)
		{
			TArray<FString> ScanPaths;
			if (ShouldPopulateFullAssetRegistry())
			{
				GConfig->GetArray(TEXT("AssetRegistry"), TEXT("PathsToScanForCook"), ScanPaths, GEngineIni);
			}
			else if (IsCookingDLC())
			{
				ScanPaths.Add(FString::Printf(TEXT("/%s/"), *CookByTheBookOptions->DlcName));
			}

			if (ScanPaths.Num() > 0 && !AssetRegistry->IsLoadingAssets())
			{
				AssetRegistry->ScanPathsSynchronous(ScanPaths);
			}
			else
			{
				// This will flush the background gather if we're in the editor
				AssetRegistry->SearchAllAssets(true);
			}

			if (FParse::Param(FCommandLine::Get(), TEXT("DumpAssetRegistry")))
			{
				DumpAssetRegistryForCooker(AssetRegistry);
			}
		}

		GetPackageNameCache().SetAssetRegistry(AssetRegistry);
	}
}

void UCookOnTheFlyServer::RefreshPlatformAssetRegistries(const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UCookOnTheFlyServer::RefreshPlatformAssetRegistries);

	for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
	{
		FName PlatformName = FName(*TargetPlatform->PlatformName());

		UE::Cook::FPlatformData* PlatformData = PlatformManager->GetPlatformData(TargetPlatform);
		FAssetRegistryGenerator* RegistryGenerator = PlatformData->RegistryGenerator.Get();
		if (!RegistryGenerator)
		{
			RegistryGenerator = new FAssetRegistryGenerator(TargetPlatform);
			PlatformData->RegistryGenerator = TUniquePtr<FAssetRegistryGenerator>(RegistryGenerator);
			RegistryGenerator->CleanManifestDirectories();
		}
		RegistryGenerator->Initialize(CookByTheBookOptions ? CookByTheBookOptions->StartupPackages : TArray<FName>());
	}
}

void UCookOnTheFlyServer::GenerateLongPackageNames(TArray<FName>& FilesInPath)
{
	TSet<FName> FilesInPathSet;
	TArray<FName> FilesInPathReverse;
	FilesInPathSet.Reserve(FilesInPath.Num());
	FilesInPathReverse.Reserve(FilesInPath.Num());

	for (int32 FileIndex = 0; FileIndex < FilesInPath.Num(); FileIndex++)
	{
		const FName& FileInPathFName = FilesInPath[FilesInPath.Num() - FileIndex - 1];
		const FString& FileInPath = FileInPathFName.ToString();
		if (FPackageName::IsValidLongPackageName(FileInPath))
		{
			bool bIsAlreadyAdded;
			FilesInPathSet.Add(FileInPathFName, &bIsAlreadyAdded);
			if (!bIsAlreadyAdded)
			{
				FilesInPathReverse.Add(FileInPathFName);
			}
		}
		else
		{
			FString LongPackageName;
			FString FailureReason;
			if (FPackageName::TryConvertFilenameToLongPackageName(FileInPath, LongPackageName, &FailureReason))
			{
				const FName LongPackageFName(*LongPackageName);
				bool bIsAlreadyAdded;
				FilesInPathSet.Add(LongPackageFName, &bIsAlreadyAdded);
				if (!bIsAlreadyAdded)
				{
					FilesInPathReverse.Add(LongPackageFName);
				}
			}
			else
			{
				LogCookerMessage(FString::Printf(TEXT("Unable to generate long package name for %s because %s"), *FileInPath, *FailureReason), EMessageSeverity::Warning);
			}
		}
	}
	FilesInPath.Empty(FilesInPathReverse.Num());
	FilesInPath.Append(FilesInPathReverse);
}

void UCookOnTheFlyServer::AddFileToCook( TArray<FName>& InOutFilesToCook, const FString &InFilename ) const
{ 
	if (!FPackageName::IsScriptPackage(InFilename) && !FPackageName::IsMemoryPackage(InFilename))
	{
		FName InFilenameName = FName(*InFilename );
		if ( InFilenameName == NAME_None)
		{
			return;
		}

		InOutFilesToCook.AddUnique(InFilenameName);
	}
}

void UCookOnTheFlyServer::CollectFilesToCook(TArray<FName>& FilesInPath, const TArray<FString>& CookMaps, const TArray<FString>& InCookDirectories,
	const TArray<FString> &IniMapSections, ECookByTheBookOptions FilesToCookFlags, const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
{
	UE_SCOPED_HIERARCHICAL_COOKTIMER(CollectFilesToCook);

	UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());

	bool bCookAll = (!!(FilesToCookFlags & ECookByTheBookOptions::CookAll)) || PackagingSettings->bCookAll;
	bool bMapsOnly = (!!(FilesToCookFlags & ECookByTheBookOptions::MapsOnly)) || PackagingSettings->bCookMapsOnly;
	bool bNoDev = !!(FilesToCookFlags & ECookByTheBookOptions::NoDevContent);

	TArray<FName> InitialPackages = FilesInPath;


	TArray<FString> CookDirectories = InCookDirectories;
	
	if (!IsCookingDLC() && 
		!(FilesToCookFlags & ECookByTheBookOptions::NoAlwaysCookMaps))
	{

		{
			TArray<FString> MapList;
			// Add the default map section
			GEditor->LoadMapListFromIni(TEXT("AlwaysCookMaps"), MapList);

			for (int32 MapIdx = 0; MapIdx < MapList.Num(); MapIdx++)
			{
				UE_LOG(LogCook, Verbose, TEXT("Maplist contains has %s "), *MapList[MapIdx]);
				AddFileToCook(FilesInPath, MapList[MapIdx]);
			}
		}


		bool bFoundMapsToCook = CookMaps.Num() > 0;

		{
			TArray<FString> MapList;
			for (const FString& IniMapSection : IniMapSections)
			{
				UE_LOG(LogCook, Verbose, TEXT("Loading map ini section %s"), *IniMapSection);
				GEditor->LoadMapListFromIni(*IniMapSection, MapList);
			}
			for (const FString& MapName : MapList)
			{
				UE_LOG(LogCook, Verbose, TEXT("Maplist contains %s"), *MapName);
				AddFileToCook(FilesInPath, MapName);
				bFoundMapsToCook = true;
			}
		}

		// If we didn't find any maps look in the project settings for maps
		if (bFoundMapsToCook == false)
		{
			for (const FFilePath& MapToCook : PackagingSettings->MapsToCook)
			{
				UE_LOG(LogCook, Verbose, TEXT("Maps to cook list contains %s"), *MapToCook.FilePath);
				FilesInPath.Add(FName(*MapToCook.FilePath));
				bFoundMapsToCook = true;
			}
		}

		// If we didn't find any maps, cook the AllMaps section
		if (bFoundMapsToCook == false)
		{
			UE_LOG(LogCook, Verbose, TEXT("Loading default map ini section AllMaps"));
			TArray<FString> AllMapsSection;
			GEditor->LoadMapListFromIni(TEXT("AllMaps"), AllMapsSection);
			for (const FString& MapName : AllMapsSection)
			{
				UE_LOG(LogCook, Verbose, TEXT("Maplist contains %s"), *MapName);
				AddFileToCook(FilesInPath, MapName);
			}
		}

		// Also append any cookdirs from the project ini files; these dirs are relative to the game content directory or start with a / root
		{
			for (const FDirectoryPath& DirToCook : PackagingSettings->DirectoriesToAlwaysCook)
			{
				FString LocalPath;
				if (FPackageName::TryConvertGameRelativePackagePathToLocalPath(DirToCook.Path, LocalPath))
				{
					UE_LOG(LogCook, Verbose, TEXT("Loading directory to always cook %s"), *DirToCook.Path);
					CookDirectories.Add(LocalPath);
				}
				else
				{
					UE_LOG(LogCook, Warning, TEXT("'ProjectSettings -> Directories to never cook -> Directories to always cook' has invalid element '%s'"), *DirToCook.Path);
				}
			}
		}
	}

	if (!(FilesToCookFlags & ECookByTheBookOptions::NoGameAlwaysCookPackages))
	{
		UE_SCOPED_HIERARCHICAL_COOKTIMER_AND_DURATION(CookModificationDelegate, DetailedCookStats::GameCookModificationDelegateTimeSec);
#define DEBUG_COOKMODIFICATIONDELEGATE 0
#if DEBUG_COOKMODIFICATIONDELEGATE
		TSet<UPackage*> LoadedPackages;
		for ( TObjectIterator<UPackage> It; It; ++It)
		{
			LoadedPackages.Add(*It);
		}
#endif

		// allow the game to fill out the asset registry, as well as get a list of objects to always cook
		TArray<FString> FilesInPathStrings;
		FGameDelegates::Get().GetCookModificationDelegate().ExecuteIfBound(FilesInPathStrings);

		for (const FString& FileString : FilesInPathStrings)
		{
			FilesInPath.Add(FName(*FileString));
		}

		if (UAssetManager::IsValid())
		{
			TArray<FName> PackagesToNeverCook;

			UAssetManager::Get().ModifyCook(FilesInPath, PackagesToNeverCook);

			for (FName NeverCookPackage : PackagesToNeverCook)
			{
				const FName StandardPackageFilename = GetPackageNameCache().GetCachedStandardFileName(NeverCookPackage);

				if (StandardPackageFilename != NAME_None)
				{
					PackageTracker->NeverCookPackageList.Add(StandardPackageFilename);
				}
			}
		}
#if DEBUG_COOKMODIFICATIONDELEGATE
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			if ( !LoadedPackages.Contains(*It) )
			{
				UE_LOG(LogCook, Display, TEXT("CookModificationDelegate loaded %s"), *It->GetName());
			}
		}
#endif

		if (UE_LOG_ACTIVE(LogCook, Verbose) )
		{
			for ( const FString& FileName : FilesInPathStrings )
			{
				UE_LOG(LogCook, Verbose, TEXT("Cook modification delegate requested package %s"), *FileName);
			}
		}
	}

	for ( const FString& CurrEntry : CookMaps )
	{
		UE_SCOPED_HIERARCHICAL_COOKTIMER(SearchForPackageOnDisk);
		if (FPackageName::IsShortPackageName(CurrEntry))
		{
			FString OutFilename;
			if (FPackageName::SearchForPackageOnDisk(CurrEntry, NULL, &OutFilename) == false)
			{
				LogCookerMessage( FString::Printf(TEXT("Unable to find package for map %s."), *CurrEntry), EMessageSeverity::Warning);
			}
			else
			{
				AddFileToCook( FilesInPath, OutFilename);
			}
		}
		else
		{
			AddFileToCook( FilesInPath,CurrEntry);
		}
	}
	if (IsCookingDLC())
	{
		TArray<FName> PackagesToNeverCook;
		UAssetManager::Get().ModifyDLCCook(CookByTheBookOptions->DlcName, FilesInPath, PackagesToNeverCook);

		for (FName NeverCookPackage : PackagesToNeverCook)
		{
			const FName* StandardPackageFilename = GetPackageNameCache().GetCachedPackageNameFromStandardFileName(NeverCookPackage);

			if (StandardPackageFilename && *StandardPackageFilename != NAME_None)
			{
				PackageTracker->NeverCookPackageList.Add(*StandardPackageFilename);
			}
		}
	}


	if (!(FilesToCookFlags & ECookByTheBookOptions::SkipSoftReferences))
	{
		const FString ExternalMountPointName(TEXT("/Game/"));
		for (const FString& CurrEntry : CookDirectories)
		{
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *CurrEntry, *(FString(TEXT("*")) + FPackageName::GetAssetPackageExtension()), true, false);
			for (int32 Index = 0; Index < Files.Num(); Index++)
			{
				FString StdFile = Files[Index];
				FPaths::MakeStandardFilename(StdFile);
				AddFileToCook(FilesInPath, StdFile);

				// this asset may not be in our currently mounted content directories, so try to mount a new one now
				FString LongPackageName;
				if (!FPackageName::IsValidLongPackageName(StdFile) && !FPackageName::TryConvertFilenameToLongPackageName(StdFile, LongPackageName))
				{
					FPackageName::RegisterMountPoint(ExternalMountPointName, CurrEntry);
				}
			}
		}

		// If no packages were explicitly added by command line or game callback, add all maps
		if (FilesInPath.Num() == InitialPackages.Num() || bCookAll)
		{
			TArray<FString> Tokens;
			Tokens.Empty(2);
			Tokens.Add(FString("*") + FPackageName::GetAssetPackageExtension());
			Tokens.Add(FString("*") + FPackageName::GetMapPackageExtension());

			uint8 PackageFilter = NORMALIZE_DefaultFlags | NORMALIZE_ExcludeEnginePackages | NORMALIZE_ExcludeLocalizedPackages;
			if (bMapsOnly)
			{
				PackageFilter |= NORMALIZE_ExcludeContentPackages;
			}

			if (bNoDev)
			{
				PackageFilter |= NORMALIZE_ExcludeDeveloperPackages;
			}

			// assume the first token is the map wildcard/pathname
			TArray<FString> Unused;
			for (int32 TokenIndex = 0; TokenIndex < Tokens.Num(); TokenIndex++)
			{
				TArray<FString> TokenFiles;
				if (!NormalizePackageNames(Unused, TokenFiles, Tokens[TokenIndex], PackageFilter))
				{
					UE_LOG(LogCook, Display, TEXT("No packages found for parameter %i: '%s'"), TokenIndex, *Tokens[TokenIndex]);
					continue;
				}

				for (int32 TokenFileIndex = 0; TokenFileIndex < TokenFiles.Num(); ++TokenFileIndex)
				{
					AddFileToCook(FilesInPath, TokenFiles[TokenFileIndex]);
				}
			}
		}
	}

	if (!(FilesToCookFlags & ECookByTheBookOptions::NoDefaultMaps))
	{
		// make sure we cook the default maps
		// Collect the default maps for all requested platforms.  Our additions are potentially wasteful if different platforms in the requested list have different default maps.
		// In that case we will wastefully cook maps for platforms that don't require them.
		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			// load the platform specific ini to get its DefaultMap
			FConfigFile PlatformEngineIni;
			FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true, *TargetPlatform->IniPlatformName());

			// get the server and game default maps and cook them
			FString Obj;
			if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), Obj))
			{
				if (Obj != FName(NAME_None).ToString())
				{
					AddFileToCook(FilesInPath, Obj);
				}
			}
			if (IsCookFlagSet(ECookInitializationFlags::IncludeServerMaps))
			{
				if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("ServerDefaultMap"), Obj))
				{
					if (Obj != FName(NAME_None).ToString())
					{
						AddFileToCook(FilesInPath, Obj);
					}
				}
			}
			if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), Obj))
			{
				if (Obj != FName(NAME_None).ToString())
				{
					AddFileToCook(FilesInPath, Obj);
				}
			}
			if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultServerGameMode"), Obj))
			{
				if (Obj != FName(NAME_None).ToString())
				{
					AddFileToCook(FilesInPath, Obj);
				}
			}
			if (PlatformEngineIni.GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameInstanceClass"), Obj))
			{
				if (Obj != FName(NAME_None).ToString())
				{
					AddFileToCook(FilesInPath, Obj);
				}
			}
		}
	}

	if (!(FilesToCookFlags & ECookByTheBookOptions::NoInputPackages))
	{
		// make sure we cook any extra assets for the default touch interface
		// @todo need a better approach to cooking assets which are dynamically loaded by engine code based on settings
		FConfigFile InputIni;
		FString InterfaceFile;
		FConfigCacheIni::LoadLocalIniFile(InputIni, TEXT("Input"), true);
		if (InputIni.GetString(TEXT("/Script/Engine.InputSettings"), TEXT("DefaultTouchInterface"), InterfaceFile))
		{
			if (InterfaceFile != TEXT("None") && InterfaceFile != TEXT(""))
			{
				AddFileToCook(FilesInPath, InterfaceFile);
			}
		}
	}
	//@todo SLATE: This is a hack to ensure all slate referenced assets get cooked.
	// Slate needs to be refactored to properly identify required assets at cook time.
	// Simply jamming everything in a given directory into the cook list is error-prone
	// on many levels - assets not required getting cooked/shipped; assets not put under 
	// the correct folder; etc.
	if ( !(FilesToCookFlags & ECookByTheBookOptions::NoSlatePackages))
	{
		TArray<FString> UIContentPaths;
		TSet <FName> ContentDirectoryAssets; 
		if (GConfig->GetArray(TEXT("UI"), TEXT("ContentDirectories"), UIContentPaths, GEditorIni) > 0)
		{
			for (int32 DirIdx = 0; DirIdx < UIContentPaths.Num(); DirIdx++)
			{
				FString ContentPath = FPackageName::LongPackageNameToFilename(UIContentPaths[DirIdx]);

				TArray<FString> Files;
				IFileManager::Get().FindFilesRecursive(Files, *ContentPath, *(FString(TEXT("*")) + FPackageName::GetAssetPackageExtension()), true, false);
				for (int32 Index = 0; Index < Files.Num(); Index++)
				{
					FString StdFile = Files[Index];
					FName PackageName = FName(*FPackageName::FilenameToLongPackageName(StdFile));
					ContentDirectoryAssets.Add(PackageName);
					FPaths::MakeStandardFilename(StdFile);
					AddFileToCook( FilesInPath, StdFile);
				}
			}
		}

		if (CookByTheBookOptions && CookByTheBookOptions->bGenerateDependenciesForMaps) 
		{
			for (auto& MapDependencyGraph : CookByTheBookOptions->MapDependencyGraphs)
			{
				MapDependencyGraph.Value.Add(FName(TEXT("ContentDirectoryAssets")), ContentDirectoryAssets);
			}
		}
	}
}

bool UCookOnTheFlyServer::IsCookByTheBookRunning() const
{
	return CookByTheBookOptions && CookByTheBookOptions->bRunning;
}


void UCookOnTheFlyServer::SaveGlobalShaderMapFiles(const TArrayView<const ITargetPlatform* const>& Platforms)
{
	// we don't support this behavior
	check( !IsCookingDLC() );
	for (int32 Index = 0; Index < Platforms.Num(); Index++)
	{
		// make sure global shaders are up to date!
		TArray<FString> Files;
		FShaderRecompileData RecompileData;
		RecompileData.PlatformName = Platforms[Index]->PlatformName();
		// Compile for all platforms
		RecompileData.ShaderPlatform = -1;
		RecompileData.ModifiedFiles = &Files;
		RecompileData.MeshMaterialMaps = NULL;

		check( IsInGameThread() );

		FString OutputDir = GetSandboxDirectory(RecompileData.PlatformName);

		RecompileShadersForRemote
			(RecompileData.PlatformName, 
			RecompileData.ShaderPlatform == -1 ? SP_NumPlatforms : (EShaderPlatform)RecompileData.ShaderPlatform, //-V547
			OutputDir, 
			RecompileData.MaterialsToLoad, 
			RecompileData.ShadersToRecompile,
			RecompileData.MeshMaterialMaps, 
			RecompileData.ModifiedFiles);
	}
}

FString UCookOnTheFlyServer::GetSandboxDirectory( const FString& PlatformName ) const
{
	FString Result;
	Result = SandboxFile->GetSandboxDirectory();

	Result.ReplaceInline(TEXT("[Platform]"), *PlatformName);

	/*if ( IsCookingDLC() )
	{
		check( IsCookByTheBookRunning() );
		Result.ReplaceInline(TEXT("/Cooked/"), *FString::Printf(TEXT("/CookedDLC_%s/"), *CookByTheBookOptions->DlcName) );
	}*/
	return Result;
}

FString UCookOnTheFlyServer::ConvertToFullSandboxPath( const FString &FileName, bool bForWrite ) const
{
	check( SandboxFile );

	FString Result;
	if (bForWrite)
	{
		// Ideally this would be in the Sandbox File but it can't access the project or plugin
		if (PluginsToRemap.Num() > 0)
		{
			// Handle remapping of plugins
			for (TSharedRef<IPlugin> Plugin : PluginsToRemap)
			{
				// If these match, then this content is part of plugin that gets remapped when packaged/staged
				if (FileName.StartsWith(Plugin->GetContentDir()))
				{
					FString SearchFor;
					SearchFor /= Plugin->GetName() / TEXT("Content");
					int32 FoundAt = FileName.Find(SearchFor, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					check(FoundAt != -1);
					// Strip off everything but <PluginName/Content/<remaing path to file>
					FString SnippedOffPath = FileName.RightChop(FoundAt);
					// Put this is in <sandbox path>/RemappedPlugins/<PluginName>/Content/<remaing path to file>
					FString RemappedPath = SandboxFile->GetSandboxDirectory();
					RemappedPath /= REMAPPED_PLUGINS;
					Result = RemappedPath / SnippedOffPath;
					return Result;
				}
			}
		}
		Result = SandboxFile->ConvertToAbsolutePathForExternalAppForWrite(*FileName);
	}
	else
	{
		Result = SandboxFile->ConvertToAbsolutePathForExternalAppForRead(*FileName);
	}

	/*if ( IsCookingDLC() )
	{
		check( IsCookByTheBookRunning() );
		Result.ReplaceInline(TEXT("/Cooked/"), *FString::Printf(TEXT("/CookedDLC_%s/"), *CookByTheBookOptions->DlcName) );
	}*/
	return Result;
}

FString UCookOnTheFlyServer::ConvertToFullSandboxPath( const FString &FileName, bool bForWrite, const FString& PlatformName ) const
{
	FString Result = ConvertToFullSandboxPath( FileName, bForWrite );
	Result.ReplaceInline(TEXT("[Platform]"), *PlatformName);
	return Result;
}

const FString UCookOnTheFlyServer::GetSandboxAssetRegistryFilename()
{
	static const FString RegistryFilename = FPaths::ProjectDir() / GetAssetRegistryFilename();

	if (IsCookingDLC())
	{
		check(IsCookByTheBookMode());
		const FString DLCRegistryFilename = FPaths::Combine(*GetBaseDirectoryForDLC(), GetAssetRegistryFilename());
		return ConvertToFullSandboxPath(*DLCRegistryFilename, true);
	}

	const FString SandboxRegistryFilename = ConvertToFullSandboxPath(*RegistryFilename, true);
	return SandboxRegistryFilename;
}

const FString UCookOnTheFlyServer::GetCookedAssetRegistryFilename(const FString& PlatformName )
{
	const FString CookedAssetRegistryFilename = GetSandboxAssetRegistryFilename().Replace(TEXT("[Platform]"), *PlatformName);
	return CookedAssetRegistryFilename;
}

void UCookOnTheFlyServer::InitShaderCodeLibrary(void)
{
    const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
    if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
    {
		FShaderLibraryCooker::InitForCooking(PackagingSettings->bSharedMaterialNativeLibraries);
        
		bool bAllPlatformsNeedStableKeys = false;
		// support setting without Hungarian prefix for the compatibility, but allow newer one to override
		GConfig->GetBool(TEXT("DevOptions.Shaders"), TEXT("NeedsShaderStableKeys"), bAllPlatformsNeedStableKeys, GEngineIni);
		GConfig->GetBool(TEXT("DevOptions.Shaders"), TEXT("bNeedsShaderStableKeys"), bAllPlatformsNeedStableKeys, GEngineIni);

        for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
        {
			// Find out if this platform requires stable shader keys, by reading the platform setting file.
			// Stable shader keys are needed if we are going to create a PSO cache.
			bool bNeedShaderStableKeys = bAllPlatformsNeedStableKeys;
			FConfigFile PlatformIniFile;
			FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Engine"), true, *TargetPlatform->IniPlatformName());
			PlatformIniFile.GetBool(TEXT("DevOptions.Shaders"), TEXT("NeedsShaderStableKeys"), bNeedShaderStableKeys);
			PlatformIniFile.GetBool(TEXT("DevOptions.Shaders"), TEXT("bNeedsShaderStableKeys"), bNeedShaderStableKeys);

			bool bNeedsDeterministicOrder = PackagingSettings->bDeterministicShaderCodeOrder;
			FConfigFile PlatformGameIniFile;
			FConfigCacheIni::LoadLocalIniFile(PlatformGameIniFile, TEXT("Game"), true, *TargetPlatform->IniPlatformName());
			PlatformGameIniFile.GetBool(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("bDeterministicShaderCodeOrder"), bNeedsDeterministicOrder);

            TArray<FName> ShaderFormats;
            TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
			TArray<FShaderLibraryCooker::FShaderFormatDescriptor> ShaderFormatsWithStableKeys;
			for (FName& Format : ShaderFormats)
			{
				FShaderLibraryCooker::FShaderFormatDescriptor NewDesc;
				NewDesc.ShaderFormat = Format;
				NewDesc.bNeedsStableKeys = bNeedShaderStableKeys;
				NewDesc.bNeedsDeterministicOrder = bNeedsDeterministicOrder;
				ShaderFormatsWithStableKeys.Push(NewDesc);
			}

            if (ShaderFormats.Num() > 0)
			{
				FShaderLibraryCooker::CookShaderFormats(ShaderFormatsWithStableKeys);
			}
        }
    }
}

static FString GenerateShaderCodeLibraryName(FString const& Name, bool bIsIterateSharedBuild)
{
	FString ActualName = (!bIsIterateSharedBuild) ? Name : Name + TEXT("_SC");
	return ActualName;
}

void UCookOnTheFlyServer::OpenGlobalShaderLibrary()
{
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
	if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
	{
		const TCHAR* GlobalShaderLibName = TEXT("Global");
		FString ActualName = GenerateShaderCodeLibraryName(GlobalShaderLibName, IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild));

		// The shader code library directory doesn't matter while cooking
		FShaderLibraryCooker::BeginCookingLibrary(ActualName);
	}
}

void UCookOnTheFlyServer::OpenShaderLibrary(FString const& Name)
{
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
	if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
	{
		FString ActualName = GenerateShaderCodeLibraryName(Name, IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild));

		// The shader code library directory doesn't matter while cooking
		FShaderLibraryCooker::BeginCookingLibrary(ActualName);
	}
}

void UCookOnTheFlyServer::CreatePipelineCache(const ITargetPlatform* TargetPlatform, const FString& LibraryName)
{
	// make sure we have a registry generated for all the platforms 
	const FString TargetPlatformName = TargetPlatform->PlatformName();
	TArray<FString>* SCLCSVPaths = OutSCLCSVPaths.Find(FName(TargetPlatformName));
	if (SCLCSVPaths && SCLCSVPaths->Num())
	{
		TArray<FName> ShaderFormats;
		TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
		for (FName ShaderFormat : ShaderFormats)
		{
			// *stablepc.csv or *stablepc.csv.compressed
			const FString Filename = FString::Printf(TEXT("*%s_%s.stablepc.csv"), *LibraryName, *ShaderFormat.ToString());
			const FString StablePCPath = FPaths::ProjectDir() / TEXT("Build") / TargetPlatform->IniPlatformName() / TEXT("PipelineCaches") / Filename;
			const FString StablePCPathCompressed = StablePCPath + TEXT(".compressed");

			TArray<FString> ExpandedFiles;
			IFileManager::Get().FindFilesRecursive(ExpandedFiles, *FPaths::GetPath(StablePCPath), *FPaths::GetCleanFilename(StablePCPath), true, false, false);
			IFileManager::Get().FindFilesRecursive(ExpandedFiles, *FPaths::GetPath(StablePCPathCompressed), *FPaths::GetCleanFilename(StablePCPathCompressed), true, false, false);
			if (!ExpandedFiles.Num())
			{
				UE_LOG(LogCook, Display, TEXT("---- NOT Running UShaderPipelineCacheToolsCommandlet for platform %s  shader format %s, no files found at %s"), *TargetPlatformName, *ShaderFormat.ToString(), *StablePCPath);
			}
			else
			{
				UE_LOG(LogCook, Display, TEXT("---- Running UShaderPipelineCacheToolsCommandlet for platform %s  shader format %s"), *TargetPlatformName, *ShaderFormat.ToString());

				const FString OutFilename = FString::Printf(TEXT("%s_%s.stable.upipelinecache"), *LibraryName, *ShaderFormat.ToString());
				const FString PCUncookedPath = FPaths::ProjectDir() / TEXT("Content") / TEXT("PipelineCaches") / TargetPlatform->IniPlatformName() / OutFilename;

				if (IFileManager::Get().FileExists(*PCUncookedPath))
				{
					UE_LOG(LogCook, Warning, TEXT("Deleting %s, cooked data doesn't belong here."), *PCUncookedPath);
					IFileManager::Get().Delete(*PCUncookedPath, false, true);
				}

				const FString PCCookedPath = ConvertToFullSandboxPath(*PCUncookedPath, true);
				const FString PCPath = PCCookedPath.Replace(TEXT("[Platform]"), *TargetPlatformName);


				FString Args(TEXT("build "));
				Args += TEXT("\"");
				Args += StablePCPath;
				Args += TEXT("\"");

				int32 NumMatched = 0;
				for (int32 Index = 0; Index < SCLCSVPaths->Num(); Index++)
				{
					if (!(*SCLCSVPaths)[Index].Contains(ShaderFormat.ToString()))
					{
						continue;
					}
					NumMatched++;
					Args += TEXT(" ");
					Args += TEXT("\"");
					Args += (*SCLCSVPaths)[Index];
					Args += TEXT("\"");
				}
				if (!NumMatched)
				{
					UE_LOG(LogCook, Warning, TEXT("Shader format %s for platform %s had this file %s, but no .scl.csv files."), *ShaderFormat.ToString(), *TargetPlatformName, *StablePCPath);
					for (int32 Index = 0; Index < SCLCSVPaths->Num(); Index++)
					{
						UE_LOG(LogCook, Warning, TEXT("    .scl.csv file: %s"), *((*SCLCSVPaths)[Index]));
					}							
					continue;
				}

				Args += TEXT(" ");
				Args += TEXT("\"");
				Args += PCPath;
				Args += TEXT("\"");
				UE_LOG(LogCook, Display, TEXT("  With Args: %s"), *Args);

				int32 Result = UShaderPipelineCacheToolsCommandlet::StaticMain(Args);

				if (Result)
				{
					LogCookerMessage(FString::Printf(TEXT("UShaderPipelineCacheToolsCommandlet failed %d"), Result), EMessageSeverity::Error);
				}
				else
				{
					UE_LOG(LogCook, Display, TEXT("---- Done running UShaderPipelineCacheToolsCommandlet for platform %s"), *TargetPlatformName);
				}
			}
		}
	}
}

void UCookOnTheFlyServer::SaveAndCloseGlobalShaderLibrary()
{
	const TCHAR* GlobalShaderLibName = TEXT("Global");
	FString ActualName = GenerateShaderCodeLibraryName(GlobalShaderLibName, IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild));

	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
	if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
	{
		// Save shader code map - cleaning directories is deliberately a separate loop here as we open the cache once per shader platform and we don't assume that they can't be shared across target platforms.
		for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
		{
			SaveShaderLibrary(TargetPlatform, GlobalShaderLibName);
		}

		FShaderLibraryCooker::EndCookingLibrary(ActualName);
	}
}

void UCookOnTheFlyServer::SaveShaderLibrary(const ITargetPlatform* TargetPlatform, FString const& Name)
{
	TArray<FName> ShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
	if (ShaderFormats.Num() > 0)
	{
		FString ActualName = GenerateShaderCodeLibraryName(Name, IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild));
		FString BasePath = !IsCookingDLC() ? FPaths::ProjectContentDir() : GetContentDirectoryForDLC();

		FString ShaderCodeDir = ConvertToFullSandboxPath(*BasePath, true, TargetPlatform->PlatformName());

		const FString RootMetaDataPath = FPaths::ProjectDir() / TEXT("Metadata") / TEXT("PipelineCaches");
		const FString MetaDataPathSB = ConvertToFullSandboxPath(*RootMetaDataPath, true);
		const FString MetaDataPath = MetaDataPathSB.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());

		TArray<FString>& PlatformSCLCSVPaths = OutSCLCSVPaths.FindOrAdd(FName(TargetPlatform->PlatformName()));
		const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
		FString ErrorString;
		if (!FShaderLibraryCooker::SaveShaderLibraryWithoutChunking(TargetPlatform, Name, ShaderCodeDir, MetaDataPath, PlatformSCLCSVPaths, ErrorString))
		{
			// This is fatal - In this case we should cancel any launch on device operation or package write but we don't want to assert and crash the editor
			LogCookerMessage(FString::Printf(TEXT("%s"), *ErrorString), EMessageSeverity::Error);
		}
		else
		{
			for (const FString& Item : PlatformSCLCSVPaths)
			{
				UE_LOG(LogCook, Display, TEXT("Saved scl.csv %s for platform %s, %d bytes"), *Item, *TargetPlatform->PlatformName(),
					IFileManager::Get().FileSize(*Item));
			}
		}
	}
}

void UCookOnTheFlyServer::CleanShaderCodeLibraries()
{
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
    bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	bool bIterativeCook = IsCookFlagSet(ECookInitializationFlags::Iterative) ||	PackageDatas->GetNumCooked() != 0;

	// If not iterative then clean up our temporary files
	if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode && !bIterativeCook)
	{
		for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
		{
			TArray<FName> ShaderFormats;
			TargetPlatform->GetAllTargetedShaderFormats(ShaderFormats);
			if (ShaderFormats.Num() > 0)
			{
				FShaderLibraryCooker::CleanDirectories(ShaderFormats);
			}
		}
	}
}

void UCookOnTheFlyServer::CookByTheBookFinished()
{
	check( IsInGameThread() );
	check( IsCookByTheBookMode() );
	check( CookByTheBookOptions->bRunning == true );
	check(PackageDatas->GetRequestQueue().IsEmpty());
	check(PackageDatas->GetLoadPrepareQueue().IsEmpty());
	check(PackageDatas->GetLoadReadyQueue().IsEmpty());
	check(PackageDatas->GetSaveQueue().IsEmpty());

	UE_LOG(LogCook, Display, TEXT("Finishing up..."));

	UPackage::WaitForAsyncFileWrites();
	
	FinalizePackageStore();

	GetDerivedDataCacheRef().WaitForQuiescence(true);
	
	UCookerSettings const* CookerSettings = GetDefault<UCookerSettings>();

	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	bool const bCacheShaderLibraries = IsUsingShaderCodeLibrary();
	FString LibraryName = !IsCookingDLC() ? FApp::GetProjectName() : CookByTheBookOptions->DlcName;

	{
		if (IBlueprintNativeCodeGenModule::IsNativeCodeGenModuleLoaded())
		{
			UE_SCOPED_HIERARCHICAL_COOKTIMER(GeneratingBlueprintAssets)
			IBlueprintNativeCodeGenModule& CodeGenModule = IBlueprintNativeCodeGenModule::Get();

			CodeGenModule.GenerateFullyConvertedClasses(); // While generating fully converted classes the list of necessary stubs is created.
			CodeGenModule.GenerateStubs();

			CodeGenModule.FinalizeManifest();

			// Unload the module as we only need it while cooking. This will also clear the current module's state in order to allow a new cooker pass to function properly.
			FModuleManager::Get().UnloadModule(CodeGenModule.GetModuleName());
		}

		// Save modified asset registry with all streaming chunk info generated during cook
		const FString& SandboxRegistryFilename = GetSandboxAssetRegistryFilename();

		// previously shader library was saved at this spot, but it's too early to know the chunk assignments, we need to BuildChunkManifest in the asset registry first

		{
			UE_SCOPED_HIERARCHICAL_COOKTIMER(SavingCurrentIniSettings)
			for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms() )
			{
				SaveCurrentIniSettings(TargetPlatform);
			}
		}

		{
			UE_SCOPED_HIERARCHICAL_COOKTIMER(SavingAssetRegistry);
			for (const ITargetPlatform* TargetPlatform : PlatformManager->GetSessionPlatforms())
			{
				UE::Cook::FPlatformData* PlatformData = PlatformManager->GetPlatformData(TargetPlatform);
				FAssetRegistryGenerator& Generator = *PlatformData->RegistryGenerator.Get();
				TArray<FName> CookedPackagesFilenames;
				TArray<FName> IgnorePackageFilenames;

				const FName& PlatformName = FName(*TargetPlatform->PlatformName());
				FString PlatformNameString = PlatformName.ToString();

				PackageDatas->GetCookedFileNamesForPlatform(TargetPlatform, CookedPackagesFilenames, false, /* include successful */ true);

				// ignore any packages which failed to cook
				PackageDatas->GetCookedFileNamesForPlatform(TargetPlatform, IgnorePackageFilenames, /* include failed */ true, false);

				bool bForceNoFilterAssetsFromAssetRegistry = false;

				if (IsCookingDLC())
				{
					TSet<FName> CookedPackagesSet(CookedPackagesFilenames);
					bForceNoFilterAssetsFromAssetRegistry = true;
					// remove the previous release cooked packages from the new asset registry, add to ignore list
					UE_SCOPED_HIERARCHICAL_COOKTIMER(RemovingOldManifestEntries);
					
					const TArray<FName>* PreviousReleaseCookedPackages = CookByTheBookOptions->BasedOnReleaseCookedPackages.Find(PlatformName);
					if (PreviousReleaseCookedPackages)
					{
						for (FName PreviousReleaseCookedPackage : *PreviousReleaseCookedPackages)
						{
							CookedPackagesSet.Remove(PreviousReleaseCookedPackage);
							IgnorePackageFilenames.Add(PreviousReleaseCookedPackage);
						}
					}
					CookedPackagesFilenames = CookedPackagesSet.Array();
				}

				// convert from filenames to package names
				TSet<FName> CookedPackageNames;
				for (FName PackageFilename : CookedPackagesFilenames)
				{
					const FName *FoundLongPackageFName = GetPackageNameCache().GetCachedPackageNameFromStandardFileName(PackageFilename);
					check(FoundLongPackageFName);
					CookedPackageNames.Add(*FoundLongPackageFName);
				}

				TSet<FName> IgnorePackageNames;
				for (FName PackageFilename : IgnorePackageFilenames)
				{
					const FName *FoundLongPackageFName = GetPackageNameCache().GetCachedPackageNameFromStandardFileName(PackageFilename);
					check(FoundLongPackageFName);
					IgnorePackageNames.Add(*FoundLongPackageFName);
				}

				// ignore packages that weren't cooked because they were only referenced by editor-only properties
				TSet<FName> UncookedEditorOnlyPackageNames;
				PackageTracker->UncookedEditorOnlyPackages.GetValues(UncookedEditorOnlyPackageNames);
				for (FName UncookedEditorOnlyPackage : UncookedEditorOnlyPackageNames)
				{
					IgnorePackageNames.Add(UncookedEditorOnlyPackage);
				}
				{
					Generator.PreSave(CookedPackageNames);
				}
				{
					UE_SCOPED_HIERARCHICAL_COOKTIMER(BuildChunkManifest);
					Generator.BuildChunkManifest(CookedPackageNames, IgnorePackageNames, SandboxFile.Get(), CookByTheBookOptions->bGenerateStreamingInstallManifests);
				}
				{
					UE_SCOPED_HIERARCHICAL_COOKTIMER(SaveManifests);
					// Always try to save the manifests, this is required to make the asset registry work, but doesn't necessarily write a file
					if (!Generator.SaveManifests(SandboxFile.Get()))
					{
						UE_LOG(LogCook, Warning, TEXT("Failed to save chunk manifest"));
					}

					int64 ExtraFlavorChunkSize;
					if (FParse::Value(FCommandLine::Get(), TEXT("ExtraFlavorChunkSize="), ExtraFlavorChunkSize))
					{
						if (ExtraFlavorChunkSize > 0)
						{
							if (!Generator.SaveManifests(SandboxFile.Get(), ExtraFlavorChunkSize))
							{
								UE_LOG(LogCook, Warning, TEXT("Failed to save chunk manifest"));
							}
						}
					}
				}
				{
					UE_SCOPED_HIERARCHICAL_COOKTIMER(SaveRealAssetRegistry);
					Generator.SaveAssetRegistry(SandboxRegistryFilename, true, bForceNoFilterAssetsFromAssetRegistry);
				}
				{
					Generator.PostSave();
				}
				{
					UE_SCOPED_HIERARCHICAL_COOKTIMER(WriteCookerOpenOrder);
					if (!IsCookFlagSet(ECookInitializationFlags::Iterative))
					{
						Generator.WriteCookerOpenOrder(SandboxFile.Get());
					}
				}
				// now that we have the asset registry and cooking open order, we have enough information to split the shader library
				// into parts for each chunk and (possibly) lay out the code in accordance with the file order
				if (bCacheShaderLibraries && PackagingSettings->bShareMaterialShaderCode)
				{
					// Save shader code map
					if (LibraryName.Len() > 0)
					{
						SaveShaderLibrary(TargetPlatform, LibraryName);

						CreatePipelineCache(TargetPlatform, LibraryName);
					}
				}
				{
					if (FParse::Param(FCommandLine::Get(), TEXT("fastcook")))
					{
						FFileHelper::SaveStringToFile(FString(), *(GetSandboxDirectory(PlatformNameString) / TEXT("fastcook.txt")));
					}
				}
				if (IsCreatingReleaseVersion())
				{
					const FString VersionedRegistryPath = GetCreateReleaseVersionAssetRegistryPath(CookByTheBookOptions->CreateReleaseVersion, PlatformNameString);
					IFileManager::Get().MakeDirectory(*VersionedRegistryPath, true);
					const FString VersionedRegistryFilename = VersionedRegistryPath / GetAssetRegistryFilename();
					const FString CookedAssetRegistryFilename = SandboxRegistryFilename.Replace(TEXT("[Platform]"), *PlatformNameString);
					IFileManager::Get().Copy(*VersionedRegistryFilename, *CookedAssetRegistryFilename, true, true);

					// Also copy development registry if it exists
					const FString DevVersionedRegistryFilename = VersionedRegistryFilename.Replace(TEXT("AssetRegistry.bin"), TEXT("Metadata/DevelopmentAssetRegistry.bin"));
					const FString DevCookedAssetRegistryFilename = CookedAssetRegistryFilename.Replace(TEXT("AssetRegistry.bin"), TEXT("Metadata/DevelopmentAssetRegistry.bin"));
					IFileManager::Get().Copy(*DevVersionedRegistryFilename, *DevCookedAssetRegistryFilename, true, true);
				}
			}
		}
	}

	FString ActualLibraryName = GenerateShaderCodeLibraryName(LibraryName, IsCookFlagSet(ECookInitializationFlags::IterateSharedBuild));
	FShaderLibraryCooker::EndCookingLibrary(ActualLibraryName);
	FShaderLibraryCooker::Shutdown();

	if (CookByTheBookOptions->bGenerateDependenciesForMaps)
	{
		UE_SCOPED_HIERARCHICAL_COOKTIMER(GenerateMapDependencies);
		for (auto& MapDependencyGraphIt : CookByTheBookOptions->MapDependencyGraphs)
		{
			BuildMapDependencyGraph(MapDependencyGraphIt.Key);
			WriteMapDependencyGraph(MapDependencyGraphIt.Key);
		}
	}

	CookByTheBookOptions->BasedOnReleaseCookedPackages.Empty();
	CookByTheBookOptions->bRunning = false;
	CookByTheBookOptions->bFullLoadAndSave = false;

	if (!IsCookingInEditor())
	{
		FCoreUObjectDelegates::PackageCreatedForLoad.RemoveAll(this);
	}
	PlatformManager->ClearSessionPlatforms();

	PrintFinishStats();

	OutputHierarchyTimers();
	ClearHierarchyTimers();

	UE_LOG(LogCook, Display, TEXT("Done!"));
}

void UCookOnTheFlyServer::PrintFinishStats()
{
	const float TotalCookTime = (float)(FPlatformTime::Seconds() - CookByTheBookOptions->CookStartTime);
	UE_LOG(LogCook, Display, TEXT("Cook by the book total time in tick %fs total time %f"), CookByTheBookOptions->CookTime, TotalCookTime);

	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	UE_LOG(LogCook, Display, TEXT("Peak Used virtual %u MiB Peak Used physical %u MiB"), MemStats.PeakUsedVirtual / 1024 / 1024, MemStats.PeakUsedPhysical / 1024 / 1024);
}

void UCookOnTheFlyServer::BuildMapDependencyGraph(const ITargetPlatform* TargetPlatform)
{
	auto& MapDependencyGraph = CookByTheBookOptions->MapDependencyGraphs.FindChecked(TargetPlatform);

	TArray<FName> PlatformCookedPackages;
	PackageDatas->GetCookedFileNamesForPlatform(TargetPlatform, PlatformCookedPackages, /* include failed */ true, /* include successful */ true);

	// assign chunks for all the map packages
	for (const FName& CookedPackage : PlatformCookedPackages)
	{
		TArray<FAssetData> PackageAssets;
		FName Name = FName(*FPackageName::FilenameToLongPackageName(CookedPackage.ToString()));

		if (!ContainsMap(Name))
		{
			continue;
		}

		TSet<FName> DependentPackages;
		TSet<FName> Roots; 

		Roots.Add(Name);

		GetDependentPackages(Roots, DependentPackages);

		MapDependencyGraph.Add(Name, DependentPackages);
	}
}

void UCookOnTheFlyServer::WriteMapDependencyGraph(const ITargetPlatform* TargetPlatform)
{
	auto& MapDependencyGraph = CookByTheBookOptions->MapDependencyGraphs.FindChecked(TargetPlatform);

	FString MapDependencyGraphFile = FPaths::ProjectDir() / TEXT("MapDependencyGraph.json");
	// dump dependency graph. 
	FString DependencyString;
	DependencyString += "{";
	for (auto& Ele : MapDependencyGraph)
	{
		TSet<FName>& Deps = Ele.Value;
		FName MapName = Ele.Key;
		DependencyString += TEXT("\t\"") + MapName.ToString() + TEXT("\" : \n\t[\n ");
		for (FName& Val : Deps)
		{
			DependencyString += TEXT("\t\t\"") + Val.ToString() + TEXT("\",\n");
		}
		DependencyString.RemoveFromEnd(TEXT(",\n"));
		DependencyString += TEXT("\n\t],\n");
	}
	DependencyString.RemoveFromEnd(TEXT(",\n"));
	DependencyString += "\n}";

	FString CookedMapDependencyGraphFilePlatform = ConvertToFullSandboxPath(MapDependencyGraphFile, true).Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());
	FFileHelper::SaveStringToFile(DependencyString, *CookedMapDependencyGraphFilePlatform, FFileHelper::EEncodingOptions::ForceUnicode);
}

void UCookOnTheFlyServer::QueueCancelCookByTheBook()
{
	if ( IsCookByTheBookMode() )
	{
		check( CookByTheBookOptions != NULL );
		CookByTheBookOptions->bCancel = true;
	}
}

void UCookOnTheFlyServer::CancelCookByTheBook()
{
	if ( IsCookByTheBookMode() && CookByTheBookOptions->bRunning )
	{
		check(CookByTheBookOptions);
		check( IsInGameThread() );

		CancelAllQueues();

		ClearPackageStoreContexts();

		CookByTheBookOptions->bRunning = false;
		SandboxFile = nullptr;

		PrintFinishStats();
	} 
}

void UCookOnTheFlyServer::StopAndClearCookedData()
{
	if ( IsCookByTheBookMode() )
	{
		check( CookByTheBookOptions != NULL );
		CancelCookByTheBook();
	}
	else
	{
		CancelAllQueues();
	}

	PackageTracker->RecompileRequests.Empty();
	PackageTracker->UnsolicitedCookedPackages.Empty();
	PackageDatas->ClearCookedPlatforms();
}

void UCookOnTheFlyServer::ClearAllCookedData()
{
	// if we are going to clear the cooked packages it is conceivable that we will recook the packages which we just cooked 
	// that means it's also conceivable that we will recook the same package which currently has an outstanding async write request
	UPackage::WaitForAsyncFileWrites();

	PackageTracker->UnsolicitedCookedPackages.Empty();
	PackageDatas->ClearCookedPlatforms();
}

void UCookOnTheFlyServer::CancelAllQueues()
{
	// Discard the external build requests, but execute any pending SchedulerCallbacks since these might have important teardowns
	TArray<UE::Cook::FSchedulerCallback> SchedulerCallbacks;
	TArray<UE::Cook::FFilePlatformRequest> UnusedRequests;
	ExternalRequests->DequeueAll(SchedulerCallbacks, UnusedRequests);
	for (UE::Cook::FSchedulerCallback& SchedulerCallback : SchedulerCallbacks)
	{
		SchedulerCallback();
	}

	using namespace UE::Cook;
	// Remove all elements from all Queues and send them to Idle
	FPackageDataQueue& SaveQueue = PackageDatas->GetSaveQueue();
	while (!SaveQueue.IsEmpty())
	{
		SaveQueue.PopFrontValue()->SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
	}
	FPackageDataQueue& LoadReadyQueue = PackageDatas->GetLoadReadyQueue();
	while (!LoadReadyQueue.IsEmpty())
	{
		LoadReadyQueue.PopFrontValue()->SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
	}
	FLoadPrepareQueue& LoadPrepareQueue = PackageDatas->GetLoadPrepareQueue();
	while (!LoadPrepareQueue.IsEmpty())
	{
		LoadPrepareQueue.PopFront()->SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
	}
	FRequestQueue& RequestQueue = PackageDatas->GetRequestQueue();
	while (!RequestQueue.IsEmpty())
	{
		RequestQueue.PopRequest()->SendToState(EPackageState::Idle, ESendFlags::QueueAdd);
	}

	bLoadBusy = false;
	bSaveBusy = false;
}


void UCookOnTheFlyServer::ClearPlatformCookedData(const ITargetPlatform* TargetPlatform)
{
	if (!TargetPlatform)
	{
		return;
	}

	// if we are going to clear the cooked packages it is conceivable that we will recook the packages which we just cooked 
	// that means it's also conceivable that we will recook the same package which currently has an outstanding async write request
	UPackage::WaitForAsyncFileWrites();

	for (UE::Cook::FPackageData* PackageData : *PackageDatas.Get())
	{
		PackageData->RemoveCookedPlatform(TargetPlatform);
	}

	TArray<FName> PackageNames;
	PackageTracker->UnsolicitedCookedPackages.GetPackagesForPlatformAndRemove(TargetPlatform, PackageNames);

	DeleteSandboxDirectory(TargetPlatform->PlatformName());
}

void UCookOnTheFlyServer::ClearPlatformCookedData(const FString& PlatformName)
{
	ClearPlatformCookedData(GetTargetPlatformManagerRef().FindTargetPlatform(PlatformName));
}

void UCookOnTheFlyServer::ClearCachedCookedPlatformDataForPlatform( const ITargetPlatform* TargetPlatform )
{
	if (TargetPlatform)
	{
		for ( TObjectIterator<UObject> It; It; ++It )
		{
			It->ClearCachedCookedPlatformData(TargetPlatform);
		}
	}
}

void UCookOnTheFlyServer::ClearCachedCookedPlatformDataForPlatform(const FName& PlatformName)
{
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	const ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(PlatformName.ToString());
	return ClearCachedCookedPlatformDataForPlatform(TargetPlatform);
}

void UCookOnTheFlyServer::OnTargetPlatformChangedSupportedFormats(const ITargetPlatform* TargetPlatform)
{
	for (TObjectIterator<UObject> It; It; ++It)
	{
		It->ClearCachedCookedPlatformData(TargetPlatform);
	}
}

void UCookOnTheFlyServer::CreateSandboxFile()
{
	// initialize the sandbox file after determining if we are cooking dlc
	// Local sandbox file wrapper. This will be used to handle path conversions,
	// but will not be used to actually write/read files so we can safely
	// use [Platform] token in the sandbox directory name and then replace it
	// with the actual platform name.
	check( SandboxFile == nullptr );
	SandboxFile = FSandboxPlatformFile::Create(false);

	// Output directory override.	
	FString OutputDirectory = GetOutputDirectoryOverride();

	// Use SandboxFile to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
	SandboxFile->Initialize(&FPlatformFileManager::Get().GetPlatformFile(), *FString::Printf(TEXT("-sandbox=\"%s\""), *OutputDirectory));
}

void UCookOnTheFlyServer::InitializeSandbox(const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
{
#if OUTPUT_COOKTIMING
	double CleanSandboxTime = 0.0;
#endif
	{
		UE_SCOPED_HIERARCHICAL_COOKTIMER_AND_DURATION(CleanSandbox, CleanSandboxTime);

		if (SandboxFile == nullptr)
		{
			CreateSandboxFile();
		}

		// before we can delete any cooked files we need to make sure that we have finished writing them
		UPackage::WaitForAsyncFileWrites();

		// Skip markup of packages for reload during sandbox initialization.  TODO: Is this no longer required because it is no longer possible to load packages in this function?
		bIsInitializingSandbox = true;
		ON_SCOPE_EXIT
		{
			bIsInitializingSandbox = false;
		};

		TSet<const ITargetPlatform*> AlreadyInitializedPlatforms;
		TArray<const ITargetPlatform*, TInlineAllocator<ExpectedMaxNumPlatforms>> RefreshPlatforms;
		const bool bIsDiffOnly = FParse::Param(FCommandLine::Get(), TEXT("DIFFONLY"));
		const bool bIsIterativeCook = IsCookFlagSet(ECookInitializationFlags::Iterative);

		for (const ITargetPlatform* Target : TargetPlatforms)
		{
			UE::Cook::FPlatformData* PlatformData = PlatformManager->GetPlatformData(Target);
			const bool bIsIniSettingsOutOfDate = IniSettingsOutOfDate(Target); // needs to be executed for side effects even if non-iterative

			bool bShouldClearCookedContent = true;
			if (bIsDiffOnly)
			{
				// When looking for deterministic cooking differences in cooked packages, don't delete the packages on disk
				bShouldClearCookedContent = false;
			}
			else if (bIsIterativeCook || PlatformData->bIsSandboxInitialized)
			{
				if (!bIsIniSettingsOutOfDate)
				{
					// We have constructed the sandbox in an earlier cook in this process (e.g. in the editor) and should not clear it again
					bShouldClearCookedContent = false;
				}
				else
				{
					if (!IsCookFlagSet(ECookInitializationFlags::IgnoreIniSettingsOutOfDate))
					{
						UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s ini settings don't match from last cook, clearing all cooked content"), *Target->PlatformName());
						bShouldClearCookedContent = true;
					}
					else
					{
						UE_LOG(LogCook, Display, TEXT("Inisettings were out of date for platform %s but we are going with it anyway because IgnoreIniSettingsOutOfDate is set"), *Target->PlatformName());
						bShouldClearCookedContent = false;
					}
				}
			}
			else
			{
				// In non-iterative cooks we will be replacing every cooked package and so should wipe the cooked directory
				UE_LOG(LogCook, Display, TEXT("Clearing all cooked content for platform %s"), *Target->PlatformName());
				bShouldClearCookedContent = true;
			}

			if (bShouldClearCookedContent)
			{
				ClearPlatformCookedData(Target);
				SaveCurrentIniSettings(Target);
			}
			else
			{
				RefreshPlatforms.Add(Target);
				if (PlatformData->bIsSandboxInitialized)
				{
					AlreadyInitializedPlatforms.Add(Target);
				}
			}

			PlatformData->bIsSandboxInitialized = true;
		}

		// Don't populate platforms that were already initialized; we already populated them when we first initialized them
		RefreshPlatforms.RemoveAllSwap([&AlreadyInitializedPlatforms](const ITargetPlatform* TargetPlatform) {
			return AlreadyInitializedPlatforms.Contains(TargetPlatform);
			});
		if (RefreshPlatforms.Num() != 0)
		{
			for (UE::Cook::FPackageData* PackageData : *PackageDatas.Get())
			{
				PackageData->RemoveCookedPlatforms(RefreshPlatforms);
			}
			// The asset registry makes populating cooked packages from disk fast, and populating is a performance benefit
			// Don't populate however if we are looking for deterministic cooking differences; start from an empty list of cooked packages
			if (!bIsDiffOnly) 
			{
				PopulateCookedPackagesFromDisk(RefreshPlatforms);
			}
		}
	}

#if OUTPUT_COOKTIMING
	FString PlatformNames;
	for (const ITargetPlatform* Target : TargetPlatforms)
	{
		PlatformNames += Target->PlatformName() + TEXT(" ");
	}
	PlatformNames.TrimEndInline();
	UE_LOG(LogCook, Display, TEXT("Sandbox cleanup took %5.3f seconds for platforms %s"), CleanSandboxTime, *PlatformNames);
#endif
}

void UCookOnTheFlyServer::InitializePackageStore(const TArrayView<const ITargetPlatform* const>& TargetPlatforms)
{
	const FString RootPath = FPaths::RootDir();
	const FString RootPathSandbox = ConvertToFullSandboxPath(*RootPath, true);

	const FString ProjectPath = FPaths::ProjectDir();
	const FString ProjectPathSandbox = ConvertToFullSandboxPath(*ProjectPath, true);

	const bool bIsDiffOnly = FParse::Param(FCommandLine::Get(), TEXT("DIFFONLY"));

	SavePackageContexts.Reserve(TargetPlatforms.Num());

	for (const ITargetPlatform* TargetPlatform: TargetPlatforms)
	{
		const FString PlatformString = TargetPlatform->PlatformName();

		const FString ResolvedRootPath = RootPathSandbox.Replace(TEXT("[Platform]"), *PlatformString);
		const FString ResolvedProjectPath = ProjectPathSandbox.Replace(TEXT("[Platform]"), *PlatformString);

		FPackageStoreBulkDataManifest* BulkDataManifest	= bIsDiffOnly == false ? new FPackageStoreBulkDataManifest(ResolvedProjectPath) : nullptr;
		FLooseFileWriter* LooseFileWriter				= IsUsingPackageStore() ? new FLooseFileWriter() : nullptr;

		FConfigFile PlatformEngineIni;
		FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true, *TargetPlatform->IniPlatformName());
		
		bool bLegacyBulkDataOffsets = false;
		PlatformEngineIni.GetBool(TEXT("Core.System"), TEXT("LegacyBulkDataOffsets"), bLegacyBulkDataOffsets);

		FSavePackageContext* SavePackageContext			= new FSavePackageContext(LooseFileWriter, BulkDataManifest, bLegacyBulkDataOffsets);
		SavePackageContexts.Add(SavePackageContext);
	}
}

void UCookOnTheFlyServer::FinalizePackageStore()
{
	UE_SCOPED_HIERARCHICAL_COOKTIMER(FinalizePackageStore);

	UE_LOG(LogCook, Display, TEXT("Saving BulkData manifest(s)..."));
	for (FSavePackageContext* PackageContext : SavePackageContexts)
	{
		if (PackageContext != nullptr && PackageContext->BulkDataManifest != nullptr)
		{
			PackageContext->BulkDataManifest->Save();
		}
	}
	UE_LOG(LogCook, Display, TEXT("Done saving BulkData manifest(s)"));

	ClearPackageStoreContexts();
}

void UCookOnTheFlyServer::ClearPackageStoreContexts()
{
	for (FSavePackageContext* Context : SavePackageContexts)
	{
		delete Context;
	}

	SavePackageContexts.Empty();
}

void UCookOnTheFlyServer::InitializeTargetPlatforms(const TArrayView<ITargetPlatform* const>& NewTargetPlatforms)
{
	//allow each platform to update its internals before cooking
	for (ITargetPlatform* TargetPlatform : NewTargetPlatforms)
	{
		TargetPlatform->RefreshSettings();
	}
}

void UCookOnTheFlyServer::DiscoverPlatformSpecificNeverCookPackages(
	const TArrayView<const ITargetPlatform* const>& TargetPlatforms, const TArray<FString>& UBTPlatformStrings)
{
	TArray<const ITargetPlatform*> PluginUnsupportedTargetPlatforms;
	TArray<FAssetData> PluginAssets;
	FARFilter PluginARFilter;
	FString PluginPackagePath;

	TArray<TSharedRef<IPlugin>> AllContentPlugins = IPluginManager::Get().GetEnabledPluginsWithContent();
	for (TSharedRef<IPlugin> Plugin : AllContentPlugins)
	{
		const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();

		// we are only interested in plugins that does not support all platforms
		if (Descriptor.SupportedTargetPlatforms.Num() == 0)
		{
			continue;
		}

		// find any unsupported target platforms for this plugin
		PluginUnsupportedTargetPlatforms.Reset();
		for (int32 I = 0, Count = TargetPlatforms.Num(); I < Count; ++I)
		{
			if (!Descriptor.SupportedTargetPlatforms.Contains(UBTPlatformStrings[I]))
			{
				PluginUnsupportedTargetPlatforms.Add(TargetPlatforms[I]);
			}
		}

		// if there are unsupported target platforms,
		// then add all packages for this plugin for these platforms to the PlatformSpecificNeverCookPackages map
		if (PluginUnsupportedTargetPlatforms.Num() > 0)
		{
			PluginPackagePath.Reset(127);
			PluginPackagePath.AppendChar(TEXT('/'));
			PluginPackagePath.Append(Plugin->GetName());

			PluginARFilter.bRecursivePaths = true;
			PluginARFilter.bIncludeOnlyOnDiskAssets = true;
			PluginARFilter.PackagePaths.Reset(1);
			PluginARFilter.PackagePaths.Emplace(*PluginPackagePath);

			PluginAssets.Reset();
			AssetRegistry->GetAssets(PluginARFilter, PluginAssets);

			for (const ITargetPlatform* TargetPlatform: PluginUnsupportedTargetPlatforms)
			{
				TSet<FName>& NeverCookPackages = PackageTracker->PlatformSpecificNeverCookPackages.FindOrAdd(TargetPlatform);
				for (const FAssetData& Asset : PluginAssets)
				{
					NeverCookPackages.Add(Asset.PackageName);
				}
			}
		}
	}
}

const FPackageNameCache& UCookOnTheFlyServer::GetPackageNameCache() const
{
	return PackageDatas->GetPackageNameCache();
}

void UCookOnTheFlyServer::TermSandbox()
{
	ClearAllCookedData();
	GetPackageNameCache().ClearPackageFileNameCache(nullptr);
	SandboxFile = nullptr;
}

void UCookOnTheFlyServer::StartCookByTheBook( const FCookByTheBookStartupOptions& CookByTheBookStartupOptions )
{
	UE_SCOPED_COOKTIMER(StartCookByTheBook);

	const TArray<FString>& CookMaps = CookByTheBookStartupOptions.CookMaps;
	const TArray<FString>& CookDirectories = CookByTheBookStartupOptions.CookDirectories;
	const TArray<FString>& IniMapSections = CookByTheBookStartupOptions.IniMapSections;
	const ECookByTheBookOptions& CookOptions = CookByTheBookStartupOptions.CookOptions;
	const FString& DLCName = CookByTheBookStartupOptions.DLCName;

	const FString& CreateReleaseVersion = CookByTheBookStartupOptions.CreateReleaseVersion;
	const FString& BasedOnReleaseVersion = CookByTheBookStartupOptions.BasedOnReleaseVersion;

	check( IsInGameThread() );
	check( IsCookByTheBookMode() );

	//force precache objects to refresh themselves before cooking anything
	LastUpdateTick = INT_MAX;

	CookByTheBookOptions->bCancel = false;
	CookByTheBookOptions->CookTime = 0.0f;
	CookByTheBookOptions->CookStartTime = FPlatformTime::Seconds();
	CookByTheBookOptions->bGenerateStreamingInstallManifests = CookByTheBookStartupOptions.bGenerateStreamingInstallManifests;
	CookByTheBookOptions->bGenerateDependenciesForMaps = CookByTheBookStartupOptions.bGenerateDependenciesForMaps;
	CookByTheBookOptions->CreateReleaseVersion = CreateReleaseVersion;
	CookByTheBookOptions->bSkipHardReferences = !!(CookOptions & ECookByTheBookOptions::SkipHardReferences);
	CookByTheBookOptions->bSkipSoftReferences = !!(CookOptions & ECookByTheBookOptions::SkipSoftReferences);
	CookByTheBookOptions->bFullLoadAndSave = !!(CookOptions & ECookByTheBookOptions::FullLoadAndSave);
	CookByTheBookOptions->bPackageStore = !!(CookOptions & ECookByTheBookOptions::PackageStore);
	CookByTheBookOptions->bCookAgainstFixedBase = !!(CookOptions & ECookByTheBookOptions::CookAgainstFixedBase);
	CookByTheBookOptions->bDlcLoadMainAssetRegistry = !!(CookOptions & ECookByTheBookOptions::DlcLoadMainAssetRegistry);
	CookByTheBookOptions->bErrorOnEngineContentUse = CookByTheBookStartupOptions.bErrorOnEngineContentUse;

	// if we are going to change the state of dlc, we need to clean out our package filename cache (the generated filename cache is dependent on this key). This has to happen later on, but we want to set the DLC State earlier.
	const bool bDlcStateChanged = CookByTheBookOptions->DlcName != DLCName;
	CookByTheBookOptions->DlcName = DLCName;
	if (CookByTheBookOptions->bSkipHardReferences && !CookByTheBookOptions->bSkipSoftReferences)
	{
		UE_LOG(LogCook, Warning, TEXT("Setting bSkipSoftReferences to true since bSkipHardReferences is true and skipping hard references requires skipping soft references."));
		CookByTheBookOptions->bSkipSoftReferences = true;
	}

	GenerateAssetRegistry();
	if (!IsCookingInEditor())
	{
		FCoreUObjectDelegates::PackageCreatedForLoad.AddUObject(this, &UCookOnTheFlyServer::MaybeMarkPackageAsAlreadyLoaded);
	}

	// SelectSessionPlatforms does not check for uniqueness and non-null, and we rely on those properties for performance, so ensure it here before calling SelectSessionPlatforms
	TArray<ITargetPlatform*> TargetPlatforms;
	TargetPlatforms.Reserve(CookByTheBookStartupOptions.TargetPlatforms.Num());
	for (ITargetPlatform* TargetPlatform : CookByTheBookStartupOptions.TargetPlatforms)
	{
		if (TargetPlatform)
		{
			TargetPlatforms.AddUnique(TargetPlatform);
		}
	}
	PlatformManager->SelectSessionPlatforms(TargetPlatforms);
	bPackageFilterDirty = true;
	check(PlatformManager->GetSessionPlatforms().Num() == TargetPlatforms.Num());

	// We want to set bRunning = true as early as possible, but it implies that session platforms have been selected so this is the earliest point we can set it
	CookByTheBookOptions->bRunning = true;

	RefreshPlatformAssetRegistries(TargetPlatforms);

	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();

	// Find all the localized packages and map them back to their source package
	{
		TArray<FString> AllCulturesToCook = CookByTheBookStartupOptions.CookCultures;
		for (const FString& CultureName : CookByTheBookStartupOptions.CookCultures)
		{
			const TArray<FString> PrioritizedCultureNames = FInternationalization::Get().GetPrioritizedCultureNames(CultureName);
			for (const FString& PrioritizedCultureName : PrioritizedCultureNames)
			{
				AllCulturesToCook.AddUnique(PrioritizedCultureName);
			}
		}
		AllCulturesToCook.Sort();

		UE_LOG(LogCook, Display, TEXT("Discovering localized assets for cultures: %s"), *FString::Join(AllCulturesToCook, TEXT(", ")));

		TArray<FString> RootPaths;
		FPackageName::QueryRootContentPaths(RootPaths);

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.bIncludeOnlyOnDiskAssets = false;
		Filter.PackagePaths.Reserve(AllCulturesToCook.Num() * RootPaths.Num());
		for (const FString& RootPath : RootPaths)
		{
			for (const FString& CultureName : AllCulturesToCook)
			{
				FString LocalizedPackagePath = RootPath / TEXT("L10N") / CultureName;
				Filter.PackagePaths.Add(*LocalizedPackagePath);
			}
		}

		TArray<FAssetData> AssetDataForCultures;
		AssetRegistry->GetAssets(Filter, AssetDataForCultures);

		for (const FAssetData& AssetData : AssetDataForCultures)
		{
			const FName LocalizedPackageName = AssetData.PackageName;
			const FName SourcePackageName = *FPackageName::GetSourcePackagePath(LocalizedPackageName.ToString());

			TArray<FName>& LocalizedPackageNames = CookByTheBookOptions->SourceToLocalizedPackageVariants.FindOrAdd(SourcePackageName);
			LocalizedPackageNames.AddUnique(LocalizedPackageName);
		}

		// Get the list of localization targets to chunk, and remove any targets that we've been asked not to stage
		TArray<FString> LocalizationTargetsToChunk = PackagingSettings->LocalizationTargetsToChunk;
		{
			TArray<FString> BlacklistLocalizationTargets;
			GConfig->GetArray(TEXT("Staging"), TEXT("BlacklistLocalizationTargets"), BlacklistLocalizationTargets, GGameIni);
			if (BlacklistLocalizationTargets.Num() > 0)
			{
				LocalizationTargetsToChunk.RemoveAll([&BlacklistLocalizationTargets](const FString& InLocalizationTarget)
				{
					return BlacklistLocalizationTargets.Contains(InLocalizationTarget);
				});
			}
		}

		if (LocalizationTargetsToChunk.Num() > 0 && AllCulturesToCook.Num() > 0)
		{
			for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
			{
				FAssetRegistryGenerator* RegistryGenerator = PlatformManager->GetPlatformData(TargetPlatform)->RegistryGenerator.Get();
				RegistryGenerator->RegisterChunkDataGenerator(MakeShared<FLocalizationChunkDataGenerator>(RegistryGenerator->GetPakchunkIndex(PackagingSettings->LocalizationTargetCatchAllChunkId), LocalizationTargetsToChunk, AllCulturesToCook));
			}
		}
	}

	PackageTracker->NeverCookPackageList.Empty();
	for (FName NeverCookPackage : GetNeverCookPackageFileNames(CookByTheBookStartupOptions.NeverCookDirectories))
	{
		PackageTracker->NeverCookPackageList.Add(NeverCookPackage);
	}

	// use temp list of UBT platform strings to discover PlatformSpecificNeverCookPackages
	{
		TArray<FString> UBTPlatformStrings;
		UBTPlatformStrings.Reserve(TargetPlatforms.Num());
		for (const ITargetPlatform* Platform : TargetPlatforms)
		{
			FString UBTPlatformName;
			Platform->GetPlatformInfo().UBTTargetId.ToString(UBTPlatformName);
			UBTPlatformStrings.Emplace(MoveTemp(UBTPlatformName));
		}

		DiscoverPlatformSpecificNeverCookPackages(TargetPlatforms, UBTPlatformStrings);
	}

	if (bDlcStateChanged)
	{
		// If we changed the DLC State earlier on, we must clear out the package name cache
		TermSandbox();
	}

	// This will either delete the sandbox or iteratively clean it
	InitializeSandbox(TargetPlatforms);
	InitializeTargetPlatforms(TargetPlatforms);

	InitializePackageStore(TargetPlatforms);

	if (CurrentCookMode == ECookMode::CookByTheBook && !IsCookFlagSet(ECookInitializationFlags::Iterative))
	{
		StartSavingEDLCookInfoForVerification();
	}

	// Note: Nativization only works with "cook by the book" mode and not from within the current editor process.
	if (CurrentCookMode == ECookMode::CookByTheBook
		&& PackagingSettings->BlueprintNativizationMethod != EProjectPackagingBlueprintNativizationMethod::Disabled)
	{
		FNativeCodeGenInitData CodeGenData;
		for (const ITargetPlatform* Entry : CookByTheBookStartupOptions.TargetPlatforms)
		{
			FPlatformNativizationDetails PlatformNativizationDetails;
			IBlueprintNativeCodeGenModule::Get().FillPlatformNativizationDetails(Entry, PlatformNativizationDetails);
			CodeGenData.CodegenTargets.Push(PlatformNativizationDetails);
		}
		CodeGenData.ManifestIdentifier = -1;
		IBlueprintNativeCodeGenModule::InitializeModule(CodeGenData);
	}

	{
		if (CookByTheBookOptions->bGenerateDependenciesForMaps)
		{
			for (const ITargetPlatform* Platform : TargetPlatforms)
			{
				CookByTheBookOptions->MapDependencyGraphs.Add(Platform);
			}
		}
	}
	
	// start shader code library cooking
	InitShaderCodeLibrary();
    CleanShaderCodeLibraries();
	
	if ( IsCookingDLC() )
	{
		const FPackageNameCache& PackageNameCache = GetPackageNameCache();
		IAssetRegistry* CacheAssetRegistry = PackageNameCache.GetAssetRegistry();
		if (CacheAssetRegistry == nullptr)
		{
			UE_LOG(LogCook, Log, TEXT("Temporarily Replacing PackageNameCache Asset Registry with the CookOnTheFlyServer's AssetRegistry to initialise Cache"));
			PackageNameCache.SetAssetRegistry(AssetRegistry);
		}

		// If we're cooking against a fixed base, we don't need to verify the packages exist on disk, we simply want to use the Release Data 
		const bool bVerifyPackagesExist = !IsCookingAgainstFixedBase();

		// if we are cooking dlc we must be based on a release version cook
		check( !BasedOnReleaseVersion.IsEmpty() );

		auto ReadDevelopmentAssetRegistry = [this, &BasedOnReleaseVersion, bVerifyPackagesExist](TArray<FName>& OutPackageList, const FString& InPlatformName)
		{
			FString OriginalSandboxRegistryFilename = GetBasedOnReleaseVersionAssetRegistryPath(BasedOnReleaseVersion, InPlatformName ) / TEXT("Metadata") / GetDevelopmentAssetRegistryFilename();

			// if this check fails probably because the asset registry can't be found or read
			bool bSucceeded = GetAllPackageFilenamesFromAssetRegistry(OriginalSandboxRegistryFilename, bVerifyPackagesExist, OutPackageList);
			if (!bSucceeded)
			{
				OriginalSandboxRegistryFilename = GetBasedOnReleaseVersionAssetRegistryPath(BasedOnReleaseVersion, InPlatformName) / GetAssetRegistryFilename();
				bSucceeded = GetAllPackageFilenamesFromAssetRegistry(OriginalSandboxRegistryFilename, bVerifyPackagesExist, OutPackageList);
			}

			if (!bSucceeded)
			{
				using namespace PlatformInfo;
				// Check all possible flavors 
				// For example release version could be cooked as Android_ASTC flavor, but DLC can be made as Android_ETC2
				FVanillaPlatformEntry VanillaPlatfromEntry = BuildPlatformHierarchy(*InPlatformName, EPlatformFilter::CookFlavor);
				for (const FPlatformInfo* PlatformFlavorInfo : VanillaPlatfromEntry.PlatformFlavors)
				{
					OriginalSandboxRegistryFilename = GetBasedOnReleaseVersionAssetRegistryPath(BasedOnReleaseVersion, PlatformFlavorInfo->PlatformInfoName.ToString()) / GetAssetRegistryFilename();
					bSucceeded = GetAllPackageFilenamesFromAssetRegistry(OriginalSandboxRegistryFilename, bVerifyPackagesExist, OutPackageList);
					if (bSucceeded)
					{
						break;
					}
				}
			}

			checkf(bSucceeded, TEXT("Failed to load DevelopmentAssetRegistry for platform %s"), *InPlatformName);
		};

		TArray<FName> OverridePackageList;
		FString DevelopmentAssetRegistryPlatformOverride;
		const bool bUsingDevRegistryOverride = FParse::Value(FCommandLine::Get(), TEXT("DevelopmentAssetRegistryPlatformOverride="), DevelopmentAssetRegistryPlatformOverride);
		if (bUsingDevRegistryOverride)
		{
			// Read the contents of the asset registry for the overriden platform. We'll use this for all requested platforms so we can just keep one copy of it here
			ReadDevelopmentAssetRegistry(OverridePackageList, *DevelopmentAssetRegistryPlatformOverride);
			checkf(OverridePackageList.Num() != 0, TEXT("DevelopmentAssetRegistry platform override is empty! An override is expected to exist and contain some valid data"));
		}

		for ( const ITargetPlatform* TargetPlatform: TargetPlatforms )
		{
			TArray<FName> PackageList;
			FString PlatformNameString = TargetPlatform->PlatformName();
			FName PlatformName(*PlatformNameString);

			if (!bUsingDevRegistryOverride)
			{
				ReadDevelopmentAssetRegistry(PackageList, PlatformNameString);
			}

			TArray<FName>& ActivePackageList = OverridePackageList.Num() > 0 ? OverridePackageList : PackageList;
			if (ActivePackageList.Num() > 0)
			{
				TArray<const ITargetPlatform*> ResultPlatforms;
				ResultPlatforms.Add(TargetPlatform);
				TArray<bool> Succeeded;
				Succeeded.Add(true);
				for (const FName& PackageFilename : ActivePackageList)
				{
					UE::Cook::FPackageData* PackageData = PackageDatas->TryAddPackageDataByFileName(PackageFilename);
					if (PackageData)
					{
						PackageData->AddCookedPlatforms({ TargetPlatform }, true /* Succeeded */);
					}
				}
			}

			if (OverridePackageList.Num() > 0)
			{
				// This is the override list, so we can't give the memory away because we will need it for the other platforms
				CookByTheBookOptions->BasedOnReleaseCookedPackages.Add(PlatformName, OverridePackageList);
			}
			else
			{
				CookByTheBookOptions->BasedOnReleaseCookedPackages.Add(PlatformName, MoveTemp(PackageList));
			}
		}

		PackageNameCache.SetAssetRegistry(CacheAssetRegistry);
	}
	
	// add shader library chunkers
	if (PackagingSettings->bShareMaterialShaderCode)
	{
		for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
		{
			FAssetRegistryGenerator* RegistryGenerator = PlatformManager->GetPlatformData(TargetPlatform)->RegistryGenerator.Get();
			RegistryGenerator->RegisterChunkDataGenerator(MakeShared<FShaderLibraryChunkDataGenerator>(TargetPlatform));
		}
	}

	// don't resave the global shader map files in dlc
	if (!IsCookingDLC() && !(CookByTheBookStartupOptions.CookOptions & ECookByTheBookOptions::ForceDisableSaveGlobalShaders))
	{
		OpenGlobalShaderLibrary();

		SaveGlobalShaderMapFiles(TargetPlatforms);

		SaveAndCloseGlobalShaderLibrary();
	}
	
	// Open the shader code library for the current project or the current DLC pack, depending on which we are cooking
    {
		FString LibraryName = !IsCookingDLC() ? FApp::GetProjectName() : CookByTheBookOptions->DlcName;
		if (LibraryName.Len() > 0)
		{
			OpenShaderLibrary(LibraryName);
		}
	}

	TArray<FName> FilesInPath;
	TSet<FName> StartupSoftObjectPackages;
	if (!IsCookByTheBookMode() || !CookByTheBookOptions->bSkipSoftReferences)
	{
		// Get the list of soft references, for both empty package and all startup packages
		GRedirectCollector.ProcessSoftObjectPathPackageList(NAME_None, false, StartupSoftObjectPackages);

		for (const FName& StartupPackage : CookByTheBookOptions->StartupPackages)
		{
			GRedirectCollector.ProcessSoftObjectPathPackageList(StartupPackage, false, StartupSoftObjectPackages);
		}
	}

	CollectFilesToCook(FilesInPath, CookMaps, CookDirectories, IniMapSections, CookOptions, TargetPlatforms);

	// Add string asset packages after collecting files, to avoid accidentally activating the behavior to cook all maps if none are specified
	for (FName SoftObjectPackage : StartupSoftObjectPackages)
	{
		TMap<FName, FName> RedirectedPaths;

		// If this is a redirector, extract destination from asset registry
		if (ContainsRedirector(SoftObjectPackage, RedirectedPaths))
		{
			for (TPair<FName, FName>& RedirectedPath : RedirectedPaths)
			{
				GRedirectCollector.AddAssetPathRedirection(RedirectedPath.Key, RedirectedPath.Value);
			}
		}

		if (!CookByTheBookOptions->bSkipSoftReferences)
		{
			AddFileToCook(FilesInPath, SoftObjectPackage.ToString());
		}
	}
	
	if (FilesInPath.Num() == 0)
	{
		LogCookerMessage(FString::Printf(TEXT("No files found to cook.")), EMessageSeverity::Warning);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("RANDOMPACKAGEORDER")) || 
		(FParse::Param(FCommandLine::Get(), TEXT("DIFFONLY")) && !FParse::Param(FCommandLine::Get(), TEXT("DIFFNORANDCOOK"))))
	{
		UE_LOG(LogCook, Log, TEXT("Randomizing package order."));
		//randomize the array, taking the Array_Shuffle approach, in order to help bring cooking determinism issues to the surface.
		for (int32 FileIndex = 0; FileIndex < FilesInPath.Num(); ++FileIndex)
		{
			FilesInPath.Swap(FileIndex, FMath::RandRange(FileIndex, FilesInPath.Num() - 1));
		}
	}

	{
		UE_SCOPED_HIERARCHICAL_COOKTIMER(GenerateLongPackageName);
		GenerateLongPackageNames(FilesInPath);
	}
	// add all the files for the requested platform to the cook list
	for ( const FName& FileFName : FilesInPath )
	{
		if (FileFName == NAME_None)
		{
			continue;
		}

		const FName PackageFileFName = GetPackageNameCache().GetCachedStandardFileName(FileFName);
		
		if (PackageFileFName != NAME_None)
		{
			ExternalRequests->EnqueueUnique( UE::Cook::FFilePlatformRequest( PackageFileFName, TargetPlatforms ) );
		}
		else if (!FLinkerLoad::IsKnownMissingPackage(FileFName))
		{
			const FString FileName = FileFName.ToString();
			LogCookerMessage( FString::Printf(TEXT("Unable to find package for cooking %s"), *FileName), EMessageSeverity::Warning );
		}	
	}


	if (!IsCookingDLC())
	{
		// if we are not cooking dlc then basedOnRelease version just needs to make sure that we cook all the packages which are in the previous release (as well as the new ones)
		if ( !BasedOnReleaseVersion.IsEmpty() )
		{
			// if we are based of a release and we are not cooking dlc then we should always be creating a new one (note that we could be creating the same one we are based of).
			// note that we might erroneously enter here if we are generating a patch instead and we accidentally passed in BasedOnReleaseVersion to the cooker instead of to unrealpak
			check( !CreateReleaseVersion.IsEmpty() );

			for ( const ITargetPlatform* TargetPlatform : TargetPlatforms )
			{
				// if we are based of a cook and we are creating a new one we need to make sure that at least all the old packages are cooked as well as the new ones
				FString OriginalAssetRegistryPath = GetBasedOnReleaseVersionAssetRegistryPath( BasedOnReleaseVersion, TargetPlatform->PlatformName() ) / GetAssetRegistryFilename();

				TArray<FName> PackageFiles;
				verify( GetAllPackageFilenamesFromAssetRegistry(OriginalAssetRegistryPath, true, PackageFiles) );

				TArray<const ITargetPlatform*, TInlineAllocator<1>> RequestPlatforms;
				RequestPlatforms.Add(TargetPlatform);
				for ( const FName& PackageFilename : PackageFiles )
				{
					ExternalRequests->EnqueueUnique( UE::Cook::FFilePlatformRequest( PackageFilename, RequestPlatforms) );
				}
			}
		}
	}
}

TArray<FName> UCookOnTheFlyServer::GetNeverCookPackageFileNames(TArrayView<const FString> ExtraNeverCookDirectories)
{
	TArray<FString> NeverCookDirectories(ExtraNeverCookDirectories);

	auto AddDirectoryPathArray = [&NeverCookDirectories](const TArray<FDirectoryPath>& DirectoriesToNeverCook, const TCHAR* SettingName)
	{
		for (const FDirectoryPath& DirToNotCook : DirectoriesToNeverCook)
		{
			FString LocalPath;
			if (FPackageName::TryConvertGameRelativePackagePathToLocalPath(DirToNotCook.Path, LocalPath))
			{
				NeverCookDirectories.Add(LocalPath);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("'%s' has invalid element '%s'"), SettingName, *DirToNotCook.Path);
			}
		}

	};
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	AddDirectoryPathArray(PackagingSettings->DirectoriesToNeverCook, TEXT("ProjectSettings -> Project -> Packaging -> Directories to never cook"));
	AddDirectoryPathArray(PackagingSettings->TestDirectoriesToNotSearch, TEXT("ProjectSettings -> Project -> Packaging -> Test directories to not search"));

	TArray<FString> NeverCookPackagesPaths;
	FPackageName::FindPackagesInDirectories(NeverCookPackagesPaths, NeverCookDirectories);

	TArray<FName> NeverCookNormalizedFileNames;
	for (const FString& NeverCookPackagePath : NeverCookPackagesPaths)
	{
		NeverCookNormalizedFileNames.Add(FPackageNameCache::GetStandardFileName(NeverCookPackagePath));
	}
	return NeverCookNormalizedFileNames;
}

bool UCookOnTheFlyServer::RecompileChangedShaders(const TArray<const ITargetPlatform*>& TargetPlatforms)
{
	bool bShadersRecompiled = false;
	for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
	{
		bShadersRecompiled |= RecompileChangedShadersForPlatform(TargetPlatform->PlatformName());
	}
	return bShadersRecompiled;
}

bool UCookOnTheFlyServer::RecompileChangedShaders(const TArray<FName>& TargetPlatformNames)
{
	bool bShadersRecompiled = false;
	for (const FName& TargetPlatformName : TargetPlatformNames)
	{
		bShadersRecompiled |= RecompileChangedShadersForPlatform(TargetPlatformName.ToString());
	}
	return bShadersRecompiled;
}

/* UCookOnTheFlyServer callbacks
 *****************************************************************************/

void UCookOnTheFlyServer::MaybeMarkPackageAsAlreadyLoaded(UPackage *Package)
{
	// can't use this optimization while cooking in editor
	check(IsCookingInEditor()==false);
	check(IsCookByTheBookMode());

	if (bIsInitializingSandbox)
	{
		return;
	}

	// if the package is already fully loaded then we are not going to mark it up anyway
	if ( Package->IsFullyLoaded() )
	{
		return;
	}

	FName StandardName = GetPackageNameCache().GetCachedStandardFileName(Package);

	// UE_LOG(LogCook, Display, TEXT("Loading package %s"), *StandardName.ToString());

	bool bShouldMarkAsAlreadyProcessed = false;

	TArray<const ITargetPlatform*> CookedPlatforms;
	UE::Cook::FPackageData* PackageData = PackageDatas->FindPackageDataByPackageName(Package->GetFName());
	if (PackageData && PackageData->HasAnyCookedPlatform())
	{
		bShouldMarkAsAlreadyProcessed = PackageData->HasAllCookedPlatforms(PlatformManager->GetSessionPlatforms(), true /* bIncludeFailed */);

		FString Platforms;
		for (const ITargetPlatform* CookedPlatform : PackageData->GetCookedPlatforms())
		{
			Platforms += TEXT(" ");
			Platforms += CookedPlatform->PlatformName();
		}
		if (IsCookFlagSet(ECookInitializationFlags::LogDebugInfo))
		{
			if (!bShouldMarkAsAlreadyProcessed)
			{
				UE_LOG(LogCook, Display, TEXT("Reloading package %s slowly because it wasn't cooked for all platforms %s."), *StandardName.ToString(), *Platforms);
			}
			else
			{
				UE_LOG(LogCook, Display, TEXT("Marking %s as reloading for cooker because it's been cooked for platforms %s."), *StandardName.ToString(), *Platforms);
			}
		}
	}

	check(IsInGameThread());
	if (PackageTracker->NeverCookPackageList.Contains(StandardName))
	{
		bShouldMarkAsAlreadyProcessed = true;
		UE_LOG(LogCook, Verbose, TEXT("Marking %s as reloading for cooker because it was requested as never cook package."), *StandardName.ToString());
	}

	if (bShouldMarkAsAlreadyProcessed)
	{
		if (Package->IsFullyLoaded() == false)
		{
			Package->SetPackageFlags(PKG_ReloadingForCooker);
		}
	}
}


bool UCookOnTheFlyServer::HandleNetworkFileServerNewConnection(const FString& VersionInfo, const FString& PlatformName)
{
	const uint32 CL = FEngineVersion::CompatibleWith().GetChangelist();
	const FString Branch = FEngineVersion::CompatibleWith().GetBranch();

	const FString LocalVersionInfo = FString::Printf(TEXT("%s %d"), *Branch, CL);

	{
		UE::Cook::FPlatformManager::FReadScopeLock PlatformScopeLock(PlatformManager->ReadLockPlatforms());
		if (!AddCookOnTheFlyPlatform(PlatformName))
		{
			UE_LOG(LogCook, Warning, TEXT("Unrecognized PlatformName '%s', CookOnTheFly requests for this platform will fail."), *PlatformName);
			return false;
		}
	}

	UE_LOG(LogCook, Display, TEXT("Connection received of version %s local version %s"), *VersionInfo, *LocalVersionInfo);

	if (LocalVersionInfo != VersionInfo)
	{
		UE_LOG(LogCook, Warning, TEXT("Connection tried to connect with incompatible version"));
		// return false;
	}
	return true;
}

static void AppendExistingPackageSidecarFiles(const FString& PackageSandboxFilename, const FString& PackageStandardFilename, TArray<FString>& OutPackageSidecarFiles)
{
	const TCHAR* const PackageSidecarExtensions[] =
	{
		TEXT(".uexp"),
		// TODO: re-enable this once the client-side of the NetworkPlatformFile isn't prone to becoming overwhelmed by slow writing of unsolicited files
		//TEXT(".ubulk"),
		//TEXT(".uptnl"),
		//TEXT(".m.ubulk")
	};

	for (const TCHAR* PackageSidecarExtension : PackageSidecarExtensions)
	{
		const FString SidecarSandboxFilename = FPathViews::ChangeExtension(PackageSandboxFilename, PackageSidecarExtension);
		if (IFileManager::Get().FileExists(*SidecarSandboxFilename))
		{
			OutPackageSidecarFiles.Add(FPathViews::ChangeExtension(PackageStandardFilename, PackageSidecarExtension));
		}
	}
}

void UCookOnTheFlyServer::GetCookOnTheFlyUnsolicitedFiles(const ITargetPlatform* TargetPlatform, const FString& PlatformName, TArray<FString>& UnsolicitedFiles, const FString& Filename, bool bIsCookable)
{
	UPackage::WaitForAsyncFileWrites();

	if (bIsCookable)
		AppendExistingPackageSidecarFiles(ConvertToFullSandboxPath(*Filename, true, PlatformName), Filename, UnsolicitedFiles);

	TArray<FName> UnsolicitedFilenames;
	PackageTracker->UnsolicitedCookedPackages.GetPackagesForPlatformAndRemove(TargetPlatform, UnsolicitedFilenames);

	for (const FName& UnsolicitedFile : UnsolicitedFilenames)
	{
		FString StandardFilename = UnsolicitedFile.ToString();
		FPaths::MakeStandardFilename(StandardFilename);

		// check that the sandboxed file exists... if it doesn't then don't send it back
		// this can happen if the package was saved but the async writer thread hasn't finished writing it to disk yet

		FString SandboxFilename = ConvertToFullSandboxPath(*StandardFilename, true, PlatformName);
		if (IFileManager::Get().FileExists(*SandboxFilename))
		{
			UnsolicitedFiles.Add(StandardFilename);
			if (FPackageName::IsPackageExtension(*FPaths::GetExtension(StandardFilename, true)))
				AppendExistingPackageSidecarFiles(SandboxFilename, StandardFilename, UnsolicitedFiles);
		}
		else
		{
			UE_LOG(LogCook, Warning, TEXT("Unsolicited file doesn't exist in sandbox, ignoring %s"), *StandardFilename);
		}
	}
}

void UCookOnTheFlyServer::HandleNetworkFileServerFileRequest(FString& Filename, const FString& PlatformNameString, TArray<FString>& UnsolicitedFiles)
{
	check(IsCookOnTheFlyMode());

	FName PlatformName(*PlatformNameString);
	const bool bIsCookable = FPackageName::IsPackageExtension(*FPaths::GetExtension(Filename, true));
	if (!bIsCookable)
	{
		while (true)
		{
			{
				UE::Cook::FPlatformManager::FReadScopeLock PlatformsScopeLock(PlatformManager->ReadLockPlatforms());
				const ITargetPlatform* TargetPlatform = AddCookOnTheFlyPlatform(PlatformNameString);
				if (!TargetPlatform)
				{
					break;
				}
				if (PlatformManager->IsPlatformInitialized(TargetPlatform))
				{
					GetCookOnTheFlyUnsolicitedFiles(TargetPlatform, PlatformNameString, UnsolicitedFiles, Filename, bIsCookable);
					break;
				}
			}
			// Wait for the Platform to be added if this is the first time; it is not legal to call GetCookOnTheFlyUnsolicitedFiles until after the platform has been added
			FPlatformProcess::Sleep(0.001f);
		}
		return;
	}

	FString StandardFileName = Filename;
	FPackageName::FindPackageFileWithoutExtension(FPaths::ChangeExtension(Filename, TEXT("")), Filename);
	FPaths::MakeStandardFilename( StandardFileName );
	FName StandardFileFname = FName(*StandardFileName);

#if PROFILE_NETWORK
	double StartTime = FPlatformTime::Seconds();
	check(NetworkRequestEvent);
	NetworkRequestEvent->Reset();
#endif
	
	UE_LOG(LogCook, Display, TEXT("Requesting file from cooker %s"), *StandardFileName);
	TAtomic<bool> bCookComplete(false);
	UE::Cook::FCompletionCallback OnCookComplete = [&bCookComplete]()
	{
		bCookComplete = true;
	};

	{
		// This lock guards us from having the TargetPlatform pointer invalidated as a key until after we have stored it in ExternalRequests
		// Once it is in ExternalRequests we are safe because TargetPlatform pointers in ExternalRequests are updated whenever the TargetPlatform pointer changes.
		// Note that we can not dereference the TargetPlatform pointer in this function as it could be invalidated at any time; we can only use it as a key value for identifying the platform in the external request.
		UE::Cook::FPlatformManager::FReadScopeLock PlatformsScopeLock(PlatformManager->ReadLockPlatforms());

		const ITargetPlatform* TargetPlatform = AddCookOnTheFlyPlatform(PlatformNameString);
		if (!TargetPlatform)
		{
			UE_LOG(LogCook, Warning, TEXT("Unrecognized PlatformName '%s', CookOnTheFly FileServerRequest requests for this platform will fail."), *PlatformNameString);
			return;
		}
		PlatformManager->AddRefCookOnTheFlyPlatform(PlatformName, *this);

		UE::Cook::FFilePlatformRequest FileRequest(StandardFileFname, TargetPlatform, MoveTemp(OnCookComplete));
		ExternalRequests->EnqueueUnique(MoveTemp(FileRequest), true);
	}
	
	if (ExternalRequests->CookRequestEvent)
	{
		ExternalRequests->CookRequestEvent->Trigger();
	}

#if PROFILE_NETWORK
	bool bFoundNetworkEventWait = true;
	while (NetworkRequestEvent->Wait(1) == false)
	{
		// for some reason we missed the stat
		if (bCookComplete)
		{
			double DeltaTimeTillRequestForfilled = FPlatformTime::Seconds() - StartTime;
			TimeTillRequestForfilled += DeltaTimeTillRequestForfilled;
			TimeTillRequestForfilledError += DeltaTimeTillRequestForfilled;
			StartTime = FPlatformTime::Seconds();
			bFoundNetworkEventWait = false;
			break;
		}
	}

	// wait for tick entry here
	TimeTillRequestStarted += FPlatformTime::Seconds() - StartTime;
	StartTime = FPlatformTime::Seconds();
#endif

	while (!bCookComplete)
	{
		FPlatformProcess::Sleep(0.001f);
	}


	{
		UE::Cook::FPlatformManager::FReadScopeLock PlatformsScopeLock(PlatformManager->ReadLockPlatforms());
		const ITargetPlatform* TargetPlatform = AddCookOnTheFlyPlatform(PlatformNameString);
		PlatformManager->ReleaseCookOnTheFlyPlatform(PlatformName);
		if (TargetPlatform)
		{
			GetCookOnTheFlyUnsolicitedFiles(TargetPlatform, PlatformNameString, UnsolicitedFiles, Filename, bIsCookable);
		}
	}


#if PROFILE_NETWORK
	if ( bFoundNetworkEventWait )
	{
		TimeTillRequestForfilled += FPlatformTime::Seconds() - StartTime;
		StartTime = FPlatformTime::Seconds();
	}
	UE_LOG( LogCook, Display, TEXT("Cook complete %s"), *StandardFileFname.ToString());
	WaitForAsyncFilesWrites += FPlatformTime::Seconds() - StartTime;
	StartTime = FPlatformTime::Seconds();
#endif
#if DEBUG_COOKONTHEFLY
	UE_LOG( LogCook, Display, TEXT("Processed file request %s"), *Filename );
#endif

}


FString UCookOnTheFlyServer::HandleNetworkGetSandboxPath()
{
	return SandboxFile->GetSandboxDirectory();
}

void UCookOnTheFlyServer::HandleNetworkGetPrecookedList(const FString& PlatformName, TMap<FString, FDateTime>& PrecookedFileList)
{
	ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
	const ITargetPlatform* TargetPlatform = TPM.FindTargetPlatform(PlatformName);
	if (!TargetPlatform)
	{
		UE_LOG(LogCook, Warning, TEXT("Unrecognized PlatformName '%s' in HandleNetworkGetPrrequests, returning 0 files."), *PlatformName);
		return;
	}

	TArray<FName> CookedPlatformFiles;
	PackageDatas->GetCookedFileNamesForPlatform(TargetPlatform, CookedPlatformFiles, /* include failed */ true, /* include successful */ true);


	for ( const FName& CookedFile : CookedPlatformFiles)
	{
		const FString SandboxFilename = ConvertToFullSandboxPath(CookedFile.ToString(), true, PlatformName);
		if (IFileManager::Get().FileExists(*SandboxFilename))
		{
			continue;
		}

		PrecookedFileList.Add(CookedFile.ToString(),FDateTime::MinValue());
	}
}

void UCookOnTheFlyServer::HandleNetworkFileServerRecompileShaders(const FShaderRecompileData& RecompileData)
{
	// shouldn't receive network requests unless we are in cook on the fly mode
	check( IsCookOnTheFlyMode() );
	check( !IsCookingDLC() );
	// if we aren't in the game thread, we need to push this over to the game thread and wait for it to finish
	if (!IsInGameThread())
	{
		UE_LOG(LogCook, Display, TEXT("Got a recompile request on non-game thread"));

		// make a new request
		UE::Cook::FRecompileRequest* Request = new UE::Cook::FRecompileRequest;
		Request->RecompileData = RecompileData;
		Request->bComplete = false;

		// push the request for the game thread to process
		PackageTracker->RecompileRequests.Enqueue(Request);

		// wait for it to complete (the game thread will pull it out of the TArray, but I will delete it)
		while (!Request->bComplete)
		{
			FPlatformProcess::Sleep(0);
		}
		delete Request;
		UE_LOG(LogCook, Display, TEXT("Completed recompile..."));

		// at this point, we are done on the game thread, and ModifiedFiles will have been filled out
		return;
	}

	FString OutputDir = GetSandboxDirectory(RecompileData.PlatformName);

	RecompileShadersForRemote
		(RecompileData.PlatformName, 
		RecompileData.ShaderPlatform == -1 ? SP_NumPlatforms : (EShaderPlatform)RecompileData.ShaderPlatform,
		OutputDir, 
		RecompileData.MaterialsToLoad, 
		RecompileData.ShadersToRecompile,
		RecompileData.MeshMaterialMaps, 
		RecompileData.ModifiedFiles,
		RecompileData.bCompileChangedShaders);
}

bool UCookOnTheFlyServer::GetAllPackageFilenamesFromAssetRegistry( const FString& AssetRegistryPath, bool bVerifyPackagesExist, TArray<FName>& OutPackageFilenames ) const
{
	UE_SCOPED_COOKTIMER(GetAllPackageFilenamesFromAssetRegistry);
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*AssetRegistryPath));
	if (Reader)
	{
		FAssetRegistryState TempState;
		TempState.Serialize(*Reader.Get(), FAssetRegistrySerializationOptions());

		const TMap<FName, const FAssetData*>& RegistryDataMap = TempState.GetObjectPathToAssetDataMap();

		check(OutPackageFilenames.Num() == 0);
		OutPackageFilenames.SetNum(RegistryDataMap.Num());

		TArray<const FAssetData*> Packages;
		Packages.Reserve(RegistryDataMap.Num());

		for (const TPair<FName, const FAssetData*>& RegistryData : RegistryDataMap)
		{
			int32 AddedIndex = Packages.Add(RegistryData.Value);
			if (GetPackageNameCache().ContainsPackageName(Packages.Last()->PackageName))
			{
				OutPackageFilenames[AddedIndex] = GetPackageNameCache().GetCachedStandardFileName(Packages.Last()->PackageName);
			}
		}

		TArray<TTuple<FName, FString>> PackageToStandardFileNames;
		PackageToStandardFileNames.SetNum(RegistryDataMap.Num());

		ParallelFor(Packages.Num(), [&AssetRegistryPath, &OutPackageFilenames, &PackageToStandardFileNames, &Packages, this, bVerifyPackagesExist](int32 AssetIndex)
			{
				if (!OutPackageFilenames[AssetIndex].IsNone())
				{
					return;
				}

				const FName PackageName = Packages[AssetIndex]->PackageName;

				FString StandardFilename;
				if (!GetPackageNameCache().CalculateCacheData(PackageName, StandardFilename, OutPackageFilenames[AssetIndex]))
				{
					if (bVerifyPackagesExist)
					{
						UE_LOG(LogCook, Warning, TEXT("Could not resolve package %s from %s"), *PackageName.ToString(), *AssetRegistryPath);
					}
					else
					{
						const bool bContainsMap = Packages[AssetIndex]->PackageFlags & PKG_ContainsMap;
						FString PackageNameStr = PackageName.ToString();

						if (FPackageName::TryConvertLongPackageNameToFilename(PackageNameStr, StandardFilename, bContainsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()))
						{
							OutPackageFilenames[AssetIndex] = FPackageNameCache::GetStandardFileName(StandardFilename);
							StandardFilename = OutPackageFilenames[AssetIndex].ToString();
						}
					}
				}
				
				PackageToStandardFileNames[AssetIndex] = TTuple<FName, FString>(PackageName, MoveTemp(StandardFilename));
			});

		for (int32 Idx = OutPackageFilenames.Num() - 1; Idx >= 0; --Idx)
		{
			if (OutPackageFilenames[Idx] == NAME_None)
			{
				OutPackageFilenames.RemoveAtSwap(Idx);
			}
		}

		GetPackageNameCache().AppendCacheResults(MoveTemp(PackageToStandardFileNames));
		return true;
	}

	return false;
}

uint32 UCookOnTheFlyServer::FullLoadAndSave(uint32& CookedPackageCount)
{
	UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave);
	check(CurrentCookMode == ECookMode::CookByTheBook);
	check(CookByTheBookOptions);
	check(IsInGameThread());

	uint32 Result = 0;

	const TArray<const ITargetPlatform*>& TargetPlatforms = PlatformManager->GetSessionPlatforms();

	{
		UE_LOG(LogCook, Display, TEXT("Loading requested packages..."));
		UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_RequestedLoads);
		while (ExternalRequests->HasRequests())
		{
			UE::Cook::FFilePlatformRequest ToBuild;
			TArray<UE::Cook::FSchedulerCallback> SchedulerCallbacks;
			UE::Cook::EExternalRequestType RequestType = ExternalRequests->DequeueRequest(SchedulerCallbacks, /* out */ ToBuild);
			if (RequestType == UE::Cook::EExternalRequestType::Callback)
			{
				for (UE::Cook::FSchedulerCallback& SchedulerCallback : SchedulerCallbacks)
				{
					SchedulerCallback();
				}
				continue;
			}
			check(RequestType == UE::Cook::EExternalRequestType::Cook && ToBuild.IsValid());

			const FName BuildFilenameFName = ToBuild.GetFilename();
			if (!PackageTracker->NeverCookPackageList.Contains(BuildFilenameFName))
			{
				const FString BuildFilename = BuildFilenameFName.ToString();
				GIsCookerLoadingPackage = true;
				UE_SCOPED_HIERARCHICAL_COOKTIMER(LoadPackage);
				LoadPackage(nullptr, *BuildFilename, LOAD_None);
				if (GShaderCompilingManager)
				{
					GShaderCompilingManager->ProcessAsyncResults(true, false);
				}
				GIsCookerLoadingPackage = false;
			}
		}
	}

	const bool bSaveConcurrent = FParse::Param(FCommandLine::Get(), TEXT("ConcurrentSave"));
	uint32 SaveFlags = SAVE_KeepGUID | SAVE_Async | SAVE_ComputeHash | (IsCookFlagSet(ECookInitializationFlags::Unversioned) ? SAVE_Unversioned : 0);
	if (bSaveConcurrent)
	{
		SaveFlags |= SAVE_Concurrent;
	}
	TArray<UE::Cook::FPackageData*> PackagesToSave;
	PackagesToSave.Reserve(65536);

	TSet<UPackage*> ProcessedPackages;
	ProcessedPackages.Reserve(65536);

	TMap<UWorld*, bool> WorldsToPostSaveRoot;
	WorldsToPostSaveRoot.Reserve(1024);

	TArray<UObject*> ObjectsToWaitForCookedPlatformData;
	ObjectsToWaitForCookedPlatformData.Reserve(65536);

	TArray<FString> PackagesToLoad;
	do
	{
		PackagesToLoad.Reset();

		{
			UE_LOG(LogCook, Display, TEXT("Caching platform data and discovering string referenced assets..."));
			UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_CachePlatformDataAndDiscoverNewAssets);
			for (TObjectIterator<UPackage> It; It; ++It)
			{
				UPackage* Package = *It;
				check(Package);

				if (ProcessedPackages.Contains(Package))
				{
					continue;
				}

				ProcessedPackages.Add(Package);

				if (Package->HasAnyPackageFlags(PKG_CompiledIn | PKG_ForDiffing | PKG_EditorOnly | PKG_Compiling | PKG_PlayInEditor | PKG_ContainsScript | PKG_ReloadingForCooker))
				{
					continue;
				}

				if (Package == GetTransientPackage())
				{
					continue;
				}

				FName PackageName = Package->GetFName();
				if (PackageTracker->NeverCookPackageList.Contains(GetPackageNameCache().GetCachedStandardFileName(PackageName)))
				{
					// refuse to save this package
					continue;
				}

				if (!FPackageName::IsValidLongPackageName(PackageName.ToString()))
				{
					continue;
				}

				if (Package->GetOuter() != nullptr)
				{
					UE_LOG(LogCook, Warning, TEXT("Skipping package %s with outermost %s"), *Package->GetName(), *Package->GetOutermost()->GetName());
					continue;
				}

				UE::Cook::FPackageData* PackageData = PackageDatas->TryAddPackageDataByPackageName(PackageName);
				// Legacy behavior: if TryAddPackageDataByPackageName failed, we will still try to load the Package, but we will not try to save it.
				if (PackageData)
				{
					PackageData->SetPackage(Package);
					PackagesToSave.Add(PackageData);
				}


				{
					UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_PerObjectLogic);
					TSet<UObject*> ProcessedObjects;
					ProcessedObjects.Reserve(64);
					bool bObjectsMayHaveBeenCreated = false;
					do
					{
						bObjectsMayHaveBeenCreated = false;
						TArray<UObject*> ObjsInPackage;
						{
							UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_GetObjectsWithOuter);
							GetObjectsWithOuter(Package, ObjsInPackage, true);
						}
						for (UObject* Obj : ObjsInPackage)
						{
							if (Obj->HasAnyFlags(RF_Transient))
							{
								continue;
							}

							if (ProcessedObjects.Contains(Obj))
							{
								continue;
							}

							bObjectsMayHaveBeenCreated = true;
							ProcessedObjects.Add(Obj);

							UWorld* World = Cast<UWorld>(Obj);
							bool bInitializedPhysicsSceneForSave = false;
							bool bForceInitializedWorld = false;
							if (World && bSaveConcurrent)
							{
								UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_SettingUpWorlds);
								// We need a physics scene at save time in case code does traces during onsave events.
								bInitializedPhysicsSceneForSave = GEditor->InitializePhysicsSceneForSaveIfNecessary(World, bForceInitializedWorld);

								GIsCookerLoadingPackage = true;
								{
									UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_PreSaveWorld);
									GEditor->OnPreSaveWorld(SaveFlags, World);
								}
								{
									UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_PreSaveRoot);
									bool bCleanupIsRequired = World->PreSaveRoot(TEXT(""));
									WorldsToPostSaveRoot.Add(World, bCleanupIsRequired);
								}
								GIsCookerLoadingPackage = false;
							}

							bool bAllPlatformDataLoaded = true;
							bool bIsTexture = Obj->IsA(UTexture::StaticClass());
							for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
							{
								if (bSaveConcurrent)
								{
									GIsCookerLoadingPackage = true;
									{
										UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_PreSave);
										Obj->PreSave(TargetPlatform);
									}
									GIsCookerLoadingPackage = false;
								}

								if (!bIsTexture || bSaveConcurrent)
								{
									UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_BeginCache);
									Obj->BeginCacheForCookedPlatformData(TargetPlatform);
									if (!Obj->IsCachedCookedPlatformDataLoaded(TargetPlatform))
									{
										bAllPlatformDataLoaded = false;
									}
								}
							}

							if (!bAllPlatformDataLoaded)
							{
								ObjectsToWaitForCookedPlatformData.Add(Obj);
							}

							if (World && bInitializedPhysicsSceneForSave)
							{
								UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_CleaningUpWorlds);
								GEditor->CleanupPhysicsSceneThatWasInitializedForSave(World, bForceInitializedWorld);
							}
						}
					} while (bObjectsMayHaveBeenCreated);

					if (bSaveConcurrent)
					{
						UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_MiscPrep);
						// Precache the metadata so we don't risk rehashing the map in the parallelfor below
						Package->GetMetaData();
					}
				}

				if (!IsCookByTheBookMode() || !CookByTheBookOptions->bSkipSoftReferences)
				{
					UE_SCOPED_HIERARCHICAL_COOKTIMER(ResolveStringReferences);
					TSet<FName> StringAssetPackages;
					GRedirectCollector.ProcessSoftObjectPathPackageList(PackageName, false, StringAssetPackages);

					for (FName StringAssetPackage : StringAssetPackages)
					{
						TMap<FName, FName> RedirectedPaths;

						// If this is a redirector, extract destination from asset registry
						if (ContainsRedirector(StringAssetPackage, RedirectedPaths))
						{
							for (TPair<FName, FName>& RedirectedPath : RedirectedPaths)
							{
								GRedirectCollector.AddAssetPathRedirection(RedirectedPath.Key, RedirectedPath.Value);
								PackagesToLoad.Add(FPackageName::ObjectPathToPackageName(RedirectedPath.Value.ToString()));
							}
						}
						else
						{
							PackagesToLoad.Add(StringAssetPackage.ToString());
						}
					}
				}
			}
		}

		{
			UE_LOG(LogCook, Display, TEXT("Loading string referenced assets..."));
			UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_LoadStringReferencedAssets);
			GIsCookerLoadingPackage = true;
			for (const FString& ToLoad : PackagesToLoad)
			{
				FName BuildFilenameFName = GetPackageNameCache().GetCachedStandardFileName(FName(*ToLoad));
				if (!PackageTracker->NeverCookPackageList.Contains(BuildFilenameFName))
				{
					LoadPackage(nullptr, *ToLoad, LOAD_None);
					if (GShaderCompilingManager)
					{
						GShaderCompilingManager->ProcessAsyncResults(true, false);
					}
				}
			}
			GIsCookerLoadingPackage = false;
		}
	} while (PackagesToLoad.Num() > 0);

	ProcessedPackages.Empty();

	// When saving concurrently, flush async loading since that is normally done internally in SavePackage
	if (bSaveConcurrent)
	{
		UE_LOG(LogCook, Display, TEXT("Flushing async loading..."));
		UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_FlushAsyncLoading);
		FlushAsyncLoading();
	}

	if (bSaveConcurrent)
	{
		UE_LOG(LogCook, Display, TEXT("Waiting for async tasks..."));
		UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_ProcessThreadUntilIdle);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
	}

	// Wait for all shaders to finish compiling
	if (GShaderCompilingManager)
	{
		UE_LOG(LogCook, Display, TEXT("Waiting for shader compilation..."));
		UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_WaitForShaderCompilation);
		while(GShaderCompilingManager->IsCompiling())
		{
			GShaderCompilingManager->ProcessAsyncResults(false, false);
			FPlatformProcess::Sleep(0.5f);
		}

		// One last process to get the shaders that were compiled at the very end
		GShaderCompilingManager->ProcessAsyncResults(false, false);
	}

	if (GDistanceFieldAsyncQueue)
	{
		UE_LOG(LogCook, Display, TEXT("Waiting for distance field async operations..."));
		UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_WaitForDistanceField);
		GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete();
	}

	// Wait for all platform data to be loaded
	{
		UE_LOG(LogCook, Display, TEXT("Waiting for cooked platform data..."));
		UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_WaitForCookedPlatformData);
		while (ObjectsToWaitForCookedPlatformData.Num() > 0)
		{
			for (int32 ObjIdx = ObjectsToWaitForCookedPlatformData.Num() - 1; ObjIdx >= 0; --ObjIdx)
			{
				UObject* Obj = ObjectsToWaitForCookedPlatformData[ObjIdx];
				bool bAllPlatformDataLoaded = true;
				for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
				{
					if (!Obj->IsCachedCookedPlatformDataLoaded(TargetPlatform))
					{
						bAllPlatformDataLoaded = false;
						break;
					}
				}

				if (bAllPlatformDataLoaded)
				{
					ObjectsToWaitForCookedPlatformData.RemoveAtSwap(ObjIdx, 1, false);
				}
			}

			FPlatformProcess::Sleep(0.001f);
		}

		ObjectsToWaitForCookedPlatformData.Empty();
	}

	{
		UE_LOG(LogCook, Display, TEXT("Saving packages..."));
		UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_Save);
		check(bIsSavingPackage == false);
		bIsSavingPackage = true;

		if (bSaveConcurrent)
		{
			GIsSavingPackage = true;
		}

		int64 ParallelSavedPackages = 0;
		ParallelFor(PackagesToSave.Num(), [this, &PackagesToSave, &TargetPlatforms ,&ParallelSavedPackages, SaveFlags, bSaveConcurrent](int32 PackageIdx)
		{
			UE::Cook::FPackageData& PackageData = *PackagesToSave[PackageIdx];
			UPackage* Package = PackageData.GetPackage();
			check(Package);

			// when concurrent saving is supported, precaching will need to be refactored for concurrency
			if (!bSaveConcurrent)
			{
				// precache texture platform data ahead of save
				const int32 PrecacheOffset = 512;
				UPackage* PrecachePackage = PackageIdx + PrecacheOffset < PackagesToSave.Num() ? PackagesToSave[PackageIdx + PrecacheOffset]->GetPackage() : nullptr;
				if (PrecachePackage)
				{
					TArray<UObject*> ObjsInPackage;
					{
						GetObjectsWithOuter(PrecachePackage, ObjsInPackage, false);
					}

					for (UObject* Obj : ObjsInPackage)
					{
						if (Obj->HasAnyFlags(RF_Transient) || !Obj->IsA(UTexture::StaticClass()))
						{
							continue;
						}

						for (const ITargetPlatform* TargetPlatform : TargetPlatforms)
						{
							Obj->BeginCacheForCookedPlatformData(TargetPlatform);
						}
					}
				}
			}

			const FName& PackageFileName = PackageData.GetFileName();
			if (!PackageFileName.IsNone())
			{
				// Use SandboxFile to do path conversion to properly handle sandbox paths (outside of standard paths in particular).
				FString Filename = ConvertToFullSandboxPath(PackageFileName.ToString(), true);

				// look for a world object in the package (if there is one, there's a map)
				EObjectFlags FlagsToCook = RF_Public;
				TArray<UObject*> ObjsInPackage;
				UWorld* World = nullptr;
				{
					//UE_SCOPED_HIERARCHICAL_COOKTIMER(SaveCookedPackage_FindWorldInPackage);
					GetObjectsWithOuter(Package, ObjsInPackage, false);
					for (UObject* Obj : ObjsInPackage)
					{
						World = Cast<UWorld>(Obj);
						if (World)
						{
							FlagsToCook = RF_NoFlags;
							break;
						}
					}
				}

				const FName& PackageName = PackageData.GetPackageName();
				FString PackageNameStr = PackageName.ToString();
				bool bExcludeFromNonEditorTargets = IsCookFlagSet(ECookInitializationFlags::SkipEditorContent) && (PackageNameStr.StartsWith(TEXT("/Engine/Editor")) || PackageNameStr.StartsWith(TEXT("/Engine/VREditor")));

				uint32 OriginalPackageFlags = Package->GetPackageFlags();

				TArray<bool> SavePackageSuccessPerPlatform;
				SavePackageSuccessPerPlatform.SetNum(TargetPlatforms.Num());
				for (int32 PlatformIndex = 0; PlatformIndex < TargetPlatforms.Num(); ++PlatformIndex)
				{
					const ITargetPlatform* Target = TargetPlatforms[PlatformIndex];

					// don't save Editor resources from the Engine if the target doesn't have editoronly data
					bool bCookPackage = (!bExcludeFromNonEditorTargets || Target->HasEditorOnlyData());
					if (UAssetManager::IsValid() && !UAssetManager::Get().ShouldCookForPlatform(Package, Target))
					{
						bCookPackage = false;
					}

					if (bCookPackage)
					{
						FString PlatFilename = Filename.Replace(TEXT("[Platform]"), *Target->PlatformName());

						UE_CLOG(GCookProgressDisplay & (int32)ECookProgressDisplayMode::PackageNames, LogCook, Display, TEXT("Cooking %s -> %s"), *Package->GetName(), *PlatFilename);

						bool bSwap = (!Target->IsLittleEndian()) ^ (!PLATFORM_LITTLE_ENDIAN);
						if (!Target->HasEditorOnlyData())
						{
							Package->SetPackageFlags(PKG_FilterEditorOnly);
						}
						else
						{
							Package->ClearPackageFlags(PKG_FilterEditorOnly);
						}
								
						GIsCookerLoadingPackage = true;
						FSavePackageContext* const SavePackageContext = (IsCookByTheBookMode() && SavePackageContexts.Num() > 0) ? SavePackageContexts[PlatformIndex] : nullptr;
						FSavePackageResultStruct SaveResult = GEditor->Save(Package, World, FlagsToCook, *PlatFilename, GError, NULL, bSwap, false, SaveFlags, Target, FDateTime::MinValue(), 
																			false, /*DiffMap*/ nullptr,	SavePackageContext);
						GIsCookerLoadingPackage = false;

						if (SaveResult == ESavePackageResult::Success && UAssetManager::IsValid())
						{
							if (!UAssetManager::Get().VerifyCanCookPackage(Package->GetFName()))
							{
								SaveResult = ESavePackageResult::Error;
							}
						}

						const bool bSucceededSavePackage = (SaveResult == ESavePackageResult::Success || SaveResult == ESavePackageResult::GenerateStub || SaveResult == ESavePackageResult::ReplaceCompletely);
						if (bSucceededSavePackage)
						{
							{
								UE::Cook::FPlatformManager::FReadScopeLock PlatformScopeLock(PlatformManager->ReadLockPlatforms());
								FAssetRegistryGenerator* Generator = PlatformManager->GetPlatformData(Target)->RegistryGenerator.Get();
								UpdateAssetRegistryPackageData(Generator, *Package, SaveResult);
							}

							FPlatformAtomics::InterlockedIncrement(&ParallelSavedPackages);
						}

						if (SaveResult != ESavePackageResult::ReferencedOnlyByEditorOnlyData)
						{
							SavePackageSuccessPerPlatform[PlatformIndex] = true;
						}
						else
						{
							SavePackageSuccessPerPlatform[PlatformIndex] = false;
						}
					}
					else
					{
						SavePackageSuccessPerPlatform[PlatformIndex] = false;
					}
				}

				for (int n = 0; n < TargetPlatforms.Num(); ++n)
				{
					PackageData.AddCookedPlatforms({ TargetPlatforms[n] }, SavePackageSuccessPerPlatform[n]);
				}

				if (SavePackageSuccessPerPlatform.Contains(false))
				{
					PackageTracker->UncookedEditorOnlyPackages.Add(PackageName);
				}

				Package->SetPackageFlagsTo(OriginalPackageFlags);
			}
		}, !bSaveConcurrent);

		if (bSaveConcurrent)
		{
			GIsSavingPackage = false;
		}

		CookedPackageCount += ParallelSavedPackages;
		if (ParallelSavedPackages > 0)
		{
			Result |= COSR_CookedPackage;
		}

		check(bIsSavingPackage == true);
		bIsSavingPackage = false;
	}

	if (bSaveConcurrent)
	{
		UE_LOG(LogCook, Display, TEXT("Calling PostSaveRoot on worlds..."));
		UE_SCOPED_HIERARCHICAL_COOKTIMER(FullLoadAndSave_PostSaveRoot);
		for (auto WorldIt = WorldsToPostSaveRoot.CreateConstIterator(); WorldIt; ++WorldIt)
		{
			UWorld* World = WorldIt.Key();
			check(World);
			World->PostSaveRoot(WorldIt.Value());
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
