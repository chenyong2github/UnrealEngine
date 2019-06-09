// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertClientWorkspace.h"
#include "ConcertClientTransactionManager.h"
#include "ConcertClientPackageManager.h"
#include "ConcertClientLockManager.h"
#include "IConcertClientPackageBridge.h"
#include "IConcertClient.h"
#include "IConcertModule.h"
#include "IConcertSession.h"
#include "ConcertSyncClientLiveSession.h"
#include "ConcertSyncSessionDatabase.h"
#include "ConcertSyncSettings.h"
#include "ConcertSyncClientUtil.h"
#include "ConcertLogGlobal.h"
#include "ConcertWorkspaceData.h"
#include "ConcertClientDataStore.h"
#include "ConcertClientLiveTransactionAuthors.h"

#include "Containers/Ticker.h"
#include "Containers/ArrayBuilder.h"
#include "UObject/Package.h"
#include "UObject/Linker.h"
#include "UObject/LinkerLoad.h"
#include "UObject/StructOnScope.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/FeedbackContext.h"
#include "RenderingThread.h"
#include "Modules/ModuleManager.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "Backends/JsonStructDeserializerBackend.h"
#include "Backends/JsonStructSerializerBackend.h"

#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#if WITH_EDITOR
	#include "UnrealEdGlobals.h"
	#include "UnrealEdMisc.h"
	#include "Editor/EditorEngine.h"
	#include "Editor/UnrealEdEngine.h"
	#include "Editor/TransBuffer.h"
	#include "FileHelpers.h"
	#include "GameMapsSettings.h"
#endif

#define LOCTEXT_NAMESPACE "ConcertClientWorkspace"

FConcertClientWorkspace::FConcertClientWorkspace(TSharedRef<FConcertSyncClientLiveSession> InLiveSession, IConcertClientPackageBridge* InPackageBridge, IConcertClientTransactionBridge* InTransactionBridge)
{
	BindSession(InLiveSession, InPackageBridge, InTransactionBridge);
}

FConcertClientWorkspace::~FConcertClientWorkspace()
{
	UnbindSession();
}

IConcertClientSession& FConcertClientWorkspace::GetSession() const
{
	return LiveSession->GetSession();
}

FGuid FConcertClientWorkspace::GetWorkspaceLockId() const
{
	return LockManager ? LockManager->GetWorkspaceLockId() : FGuid();
}

FGuid FConcertClientWorkspace::GetResourceLockId(const FName InResourceName) const
{
	return LockManager ? LockManager->GetResourceLockId(InResourceName) : FGuid();
}

bool FConcertClientWorkspace::AreResourcesLockedBy(TArrayView<const FName> ResourceNames, const FGuid& ClientId)
{
	return !LockManager || LockManager->AreResourcesLockedBy(ResourceNames, ClientId);
}

TFuture<FConcertResourceLockResponse> FConcertClientWorkspace::LockResources(TArray<FName> InResourceNames)
{
	if (LockManager)
	{
		return LockManager->LockResources(InResourceNames);
	}

	FConcertResourceLockResponse DummyResponse;
	DummyResponse.LockType = EConcertResourceLockType::Lock;
	return MakeFulfilledPromise<FConcertResourceLockResponse>(MoveTemp(DummyResponse)).GetFuture();
}

TFuture<FConcertResourceLockResponse> FConcertClientWorkspace::UnlockResources(TArray<FName> InResourceNames)
{
	if (LockManager)
	{
		return LockManager->UnlockResources(InResourceNames);
	}

	FConcertResourceLockResponse DummyResponse;
	DummyResponse.LockType = EConcertResourceLockType::Unlock;
	return MakeFulfilledPromise<FConcertResourceLockResponse>(MoveTemp(DummyResponse)).GetFuture();
}

bool FConcertClientWorkspace::HasSessionChanges() const
{
	return (TransactionManager && TransactionManager->HasSessionChanges()) || (PackageManager && PackageManager->HasSessionChanges());
}

TArray<FString> FConcertClientWorkspace::GatherSessionChanges()
{
	TArray<FString> SessionChanges;
#if WITH_EDITOR
	// Save live transactions to packages so we can properly report those changes.
	SaveLiveTransactionsToPackages();
#endif
	// Persist the sandbox state over the real content directory
	// This will also check things out from source control and make them ready to be submitted
	if (PackageManager)
	{
		SessionChanges = PackageManager->GatherSessionChanges();
	}
	return SessionChanges;
}

bool FConcertClientWorkspace::PersistSessionChanges(TArrayView<const FString> InFilesToPersist, ISourceControlProvider* SourceControlProvider, TArray<FText>* OutFailureReasons)
{
#if WITH_EDITOR
	if (PackageManager)
	{
		return PackageManager->PersistSessionChanges(InFilesToPersist, SourceControlProvider, OutFailureReasons);
	}
#endif
	return false;
}

bool FConcertClientWorkspace::HasLiveTransactionSupport(UPackage* InPackage) const
{
	return TransactionManager && TransactionManager->HasLiveTransactionSupport(InPackage);
}

bool FConcertClientWorkspace::ShouldIgnorePackageDirtyEvent(class UPackage* InPackage) const
{
	return PackageManager && PackageManager->ShouldIgnorePackageDirtyEvent(InPackage);
}

bool FConcertClientWorkspace::FindTransactionEvent(const int64 TransactionEventId, FConcertSyncTransactionEvent& OutTransactionEvent, const bool bMetaDataOnly) const
{
	return LiveSession->GetSessionDatabase().GetTransactionEvent(TransactionEventId, OutTransactionEvent, bMetaDataOnly);
}

bool FConcertClientWorkspace::FindPackageEvent(const int64 PackageEventId, FConcertSyncPackageEvent& OutPackageEvent, const bool bMetaDataOnly) const
{
	return LiveSession->GetSessionDatabase().GetPackageEvent(PackageEventId, OutPackageEvent, bMetaDataOnly);
}

void FConcertClientWorkspace::GetActivities(const int64 FirstActivityIdToFetch, const int64 MaxNumActivities, TMap<FGuid, FConcertClientInfo>& OutEndpointClientInfoMap, TArray<FConcertClientSessionActivity>& OutActivities) const
{
	OutEndpointClientInfoMap.Reset();
	OutActivities.Reset();
	LiveSession->GetSessionDatabase().EnumerateActivitiesInRange(FirstActivityIdToFetch, MaxNumActivities, [this, &OutEndpointClientInfoMap, &OutActivities](FConcertSyncActivity&& InActivity)
	{
		if (!OutEndpointClientInfoMap.Contains(InActivity.EndpointId))
		{
			FConcertSyncEndpointData EndpointData;
			if (LiveSession->GetSessionDatabase().GetEndpoint(InActivity.EndpointId, EndpointData))
			{
				OutEndpointClientInfoMap.Add(InActivity.EndpointId, EndpointData.ClientInfo);
			}
		}

		FStructOnScope ActivitySummary;
		if (InActivity.EventSummary.GetPayload(ActivitySummary))
		{
			OutActivities.Emplace(MoveTemp(InActivity), MoveTemp(ActivitySummary));
		}

		return true;
	});
}

int64 FConcertClientWorkspace::GetLastActivityId() const
{
	int64 ActivityMaxId = 0;
	LiveSession->GetSessionDatabase().GetActivityMaxId(ActivityMaxId);
	return ActivityMaxId;
}

FOnActivityAddedOrUpdated& FConcertClientWorkspace::OnActivityAddedOrUpdated()
{
	return OnActivityAddedOrUpdatedDelegate;
}

FOnWorkspaceSynchronized& FConcertClientWorkspace::OnWorkspaceSynchronized()
{
	return OnWorkspaceSyncedDelegate;
}

IConcertClientDataStore& FConcertClientWorkspace::GetDataStore()
{
	return *DataStore;
}

void FConcertClientWorkspace::BindSession(TSharedPtr<FConcertSyncClientLiveSession> InLiveSession, IConcertClientPackageBridge* InPackageBridge, IConcertClientTransactionBridge* InTransactionBridge)
{
	check(InLiveSession->IsValidSession());
	check(InPackageBridge);
	check(InTransactionBridge);

	UnbindSession();
	LiveSession = InLiveSession;
	PackageBridge = InPackageBridge;

	LoadSessionData();

	bHasSyncedWorkspace = false;
	bFinalizeWorkspaceSyncRequested = false;

	// Provide access to the data store (shared by session clients) maintained by the server.
	DataStore = MakeUnique<FConcertClientDataStore>(LiveSession.ToSharedRef());

	// Create Transaction Manager
	if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableTransactions))
	{
		TransactionManager = MakeUnique<FConcertClientTransactionManager>(LiveSession.ToSharedRef(), InTransactionBridge);
	}

	// Create Package Manager
	if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnablePackages))
	{
		PackageManager = MakeUnique<FConcertClientPackageManager>(LiveSession.ToSharedRef(), InPackageBridge);
	}

	// Create Lock Manager
	if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableLocking))
	{
		LockManager = MakeUnique<FConcertClientLockManager>(LiveSession.ToSharedRef());
	}

	// Register Session events
	LiveSession->GetSession().OnConnectionChanged().AddRaw(this, &FConcertClientWorkspace::HandleConnectionChanged);

#if WITH_EDITOR
	if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableTransactions))
	{
		// Register Asset Load Events
		FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FConcertClientWorkspace::HandleAssetLoaded);

		if (EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::ShouldDiscardTransactionsOnPackageUnload))
		{
			// Register Package Discarded Events
			PackageBridge->OnLocalPackageDiscarded().AddRaw(this, &FConcertClientWorkspace::HandlePackageDiscarded);
		}
	}

	// Register PIE/SIE Events
	FEditorDelegates::PostPIEStarted.AddRaw(this, &FConcertClientWorkspace::HandlePostPIEStarted);
	FEditorDelegates::OnSwitchBeginPIEAndSIE.AddRaw(this, &FConcertClientWorkspace::HandleSwitchBeginPIEAndSIE);
	FEditorDelegates::EndPIE.AddRaw(this, &FConcertClientWorkspace::HandleEndPIE);
#endif

	// Register OnEndFrame events
	FCoreDelegates::OnEndFrame.AddRaw(this, &FConcertClientWorkspace::OnEndFrame);

	// Register workspace event
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertWorkspaceSyncEndpointEvent>(this, &FConcertClientWorkspace::HandleWorkspaceSyncEndpointEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertWorkspaceSyncActivityEvent>(this, &FConcertClientWorkspace::HandleWorkspaceSyncActivityEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertWorkspaceSyncLockEvent>(this, &FConcertClientWorkspace::HandleWorkspaceSyncLockEvent);
	LiveSession->GetSession().RegisterCustomEventHandler<FConcertWorkspaceSyncCompletedEvent>(this, &FConcertClientWorkspace::HandleWorkspaceSyncCompletedEvent);
}

void FConcertClientWorkspace::UnbindSession()
{
	if (LiveSession)
	{
		SaveSessionData();

		// Destroy Transaction Authors
		LiveTransactionAuthors.Reset();

		// Destroy Lock Manager
		LockManager.Reset();

		// Destroy Package Manager
		PackageManager.Reset();

		// Destroy Transaction Manager
		TransactionManager.Reset();

		// Unregister Session events
		LiveSession->GetSession().OnConnectionChanged().RemoveAll(this);

#if WITH_EDITOR
		// Unregister Asset Load Events
		FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);

		// Unregister Package Discarded Events
		PackageBridge->OnLocalPackageDiscarded().RemoveAll(this);

		// Unregister PIE/SIE Events
		FEditorDelegates::PostPIEStarted.RemoveAll(this);
		FEditorDelegates::OnSwitchBeginPIEAndSIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);
#endif

		// Unregister OnEndFrame events
		FCoreDelegates::OnEndFrame.RemoveAll(this);

		// Unregister workspace event
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertWorkspaceSyncEndpointEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertWorkspaceSyncActivityEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertWorkspaceSyncLockEvent>(this);
		LiveSession->GetSession().UnregisterCustomEventHandler<FConcertWorkspaceSyncCompletedEvent>(this);

		DataStore.Reset();
		LiveSession.Reset();
		PackageBridge = nullptr;
	}
}

void FConcertClientWorkspace::LoadSessionData()
{
	FString ClientWorkspaceDataPath = LiveSession->GetSession().GetSessionWorkingDirectory() / TEXT("WorkspaceData.json");
	if (TUniquePtr<FArchive> FileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*ClientWorkspaceDataPath)))
	{
		FJsonStructDeserializerBackend Backend(*FileReader);
		FStructDeserializer::Deserialize<FConcertClientWorkspaceData>(SessionData, Backend);
		FileReader->Close();
	}
	// if the loaded session data doesn't match the session clear everything
	if (SessionData.SessionIdentifier != LiveSession->GetSession().GetSessionServerEndpointId())
	{
		SessionData.SessionIdentifier.Invalidate();
		SessionData.PersistedFiles.Empty();
	}
}

void FConcertClientWorkspace::SaveSessionData()
{
	SessionData.SessionIdentifier = LiveSession->GetSession().GetSessionServerEndpointId();
	if (PackageManager)
	{
		SessionData.PersistedFiles = PackageManager->GetPersistedFiles();
	}
	
	FString ClientWorkspaceDataPath = LiveSession->GetSession().GetSessionWorkingDirectory() / TEXT("WorkspaceData.json");
	if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*ClientWorkspaceDataPath)))
	{
		FJsonStructSerializerBackend Backend(*FileWriter, EStructSerializerBackendFlags::Default);
		FStructSerializer::Serialize<FConcertClientWorkspaceData>(SessionData, Backend);
		FileWriter->Close();
	}
}

void FConcertClientWorkspace::HandleConnectionChanged(IConcertClientSession& InSession, EConcertConnectionStatus Status)
{
	check(&LiveSession->GetSession() == &InSession);

	if (Status == EConcertConnectionStatus::Connected)
	{
		bHasSyncedWorkspace = false;
		bFinalizeWorkspaceSyncRequested = false;
		InitialSyncSlowTask = MakeUnique<FScopedSlowTask>(1.0f, LOCTEXT("SynchronizingSession", "Synchronizing Session..."));
		InitialSyncSlowTask->MakeDialog();

		// Request our initial workspace sync for any new activity since we last joined
		{
			FConcertWorkspaceSyncRequestedEvent SyncRequestedEvent;
			LiveSession->GetSessionDatabase().GetActivityMaxId(SyncRequestedEvent.FirstActivityIdToSync);
			SyncRequestedEvent.FirstActivityIdToSync++;
			SyncRequestedEvent.bEnableLiveSync = EnumHasAnyFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableLiveSync);
			LiveSession->GetSession().SendCustomEvent(SyncRequestedEvent, LiveSession->GetSession().GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
		}

#if WITH_EDITOR
		if (GUnrealEd)
		{
			if (FWorldContext* PIEWorldContext = GUnrealEd->GetPIEWorldContext())
			{
				if (UWorld* PIEWorld = PIEWorldContext->World())
				{
					// Track open PIE/SIE sessions so the server can discard them once everyone leaves
					FConcertPlaySessionEvent PlaySessionEvent;
					PlaySessionEvent.EventType = EConcertPlaySessionEventType::BeginPlay;
					PlaySessionEvent.PlayEndpointId = LiveSession->GetSession().GetSessionClientEndpointId();
					PlaySessionEvent.PlayPackageName = PIEWorld->GetOutermost()->GetFName();
					PlaySessionEvent.bIsSimulating = GUnrealEd->bIsSimulatingInEditor;
					LiveSession->GetSession().SendCustomEvent(PlaySessionEvent, LiveSession->GetSession().GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
				}
			}
		}
#endif
	}
	else if (Status == EConcertConnectionStatus::Disconnected)
	{
		bHasSyncedWorkspace = false;
		bFinalizeWorkspaceSyncRequested = false;
		InitialSyncSlowTask.Reset();
	}
}

#if WITH_EDITOR

void FConcertClientWorkspace::SaveLiveTransactionsToPackages()
{
	// Save any packages that have live transactions
	if (GEditor && TransactionManager)
	{
		// Ignore these package saves as the other clients should already be in-sync
		IConcertClientPackageBridge::FScopedIgnoreLocalSave IgnorePackageSaveScope(*PackageBridge);
		LiveSession->GetSessionDatabase().EnumeratePackageNamesWithLiveTransactions([this](const FName PackageName)
		{
			const FString PackageNameStr = PackageName.ToString();
			UPackage* Package = LoadPackage(nullptr, *PackageNameStr, LOAD_None);
			if (Package)
			{
				UWorld* World = UWorld::FindWorldInPackage(Package);
				FString PackageFilename;
				if (!FPackageName::DoesPackageExist(PackageNameStr, nullptr, &PackageFilename))
				{
					PackageFilename = FPackageName::LongPackageNameToFilename(PackageNameStr, World ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
				}

				if (GEditor->SavePackage(Package, World, RF_Standalone, *PackageFilename, GWarn))
				{
					// Add a dummy package entry to trim the live transaction for the saved package but ONLY if we're tracking package saves (ie, we have a package manager)
					// This is added ONLY on this client, and will be CLOBBERED by any future saves of this package from the server!
					if (PackageManager)
					{
						int64 PackageEventId = 0;
						LiveSession->GetSessionDatabase().AddDummyPackageEvent(PackageName, PackageEventId);
					}
				}
				else
				{
					UE_LOG(LogConcert, Warning, TEXT("Failed to save package '%s' when persiting sandbox state!"), *PackageNameStr);
				}
			}
			return true;
		});
	}
}

void FConcertClientWorkspace::HandleAssetLoaded(UObject* InAsset)
{
	if (TransactionManager && bHasSyncedWorkspace)
	{
		const FName LoadedPackageName = InAsset->GetOutermost()->GetFName();
		TransactionManager->ReplayTransactions(LoadedPackageName);
	}
}

void FConcertClientWorkspace::HandlePackageDiscarded(UPackage* InPackage)
{
	if (bHasSyncedWorkspace && EnumHasAllFlags(LiveSession->GetSessionFlags(), EConcertSyncSessionFlags::EnableTransactions | EConcertSyncSessionFlags::ShouldDiscardTransactionsOnPackageUnload))
	{
		const FName PackageName = InPackage->GetFName();

		// Add a dummy package entry to trim the live transaction for the discarded world
		// This is added ONLY on this client, and will be CLOBBERED by any future saves of this package from the server!
		// We always do this, even if the client is tracking package changes, as we may be in the middle of an action that 
		// needs to fence transactions immediately and can't wait for the activity to be returned from the server
		int64 PackageEventId = 0;
		LiveSession->GetSessionDatabase().AddDummyPackageEvent(PackageName, PackageEventId);

		// Client is tracking package events, so also discard the changes made to this package for everyone in the session
		if (PackageManager)
		{
			PackageManager->HandlePackageDiscarded(InPackage);
		}
	}
}

void FConcertClientWorkspace::HandlePostPIEStarted(const bool InIsSimulating)
{
	if (FWorldContext* PIEWorldContext = GUnrealEd->GetPIEWorldContext())
	{
		if (UWorld* PIEWorld = PIEWorldContext->World())
		{
			// Track open PIE/SIE sessions so the server can discard them once everyone leaves
			FConcertPlaySessionEvent PlaySessionEvent;
			PlaySessionEvent.EventType = EConcertPlaySessionEventType::BeginPlay;
			PlaySessionEvent.PlayEndpointId = LiveSession->GetSession().GetSessionClientEndpointId();
			PlaySessionEvent.PlayPackageName = PIEWorld->GetOutermost()->GetFName();
			PlaySessionEvent.bIsSimulating = InIsSimulating;
			LiveSession->GetSession().SendCustomEvent(PlaySessionEvent, LiveSession->GetSession().GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);

			// Apply transactions to the PIE/SIE world
			HandleAssetLoaded(PIEWorld);
		}
	}
}

void FConcertClientWorkspace::HandleSwitchBeginPIEAndSIE(const bool InIsSimulating)
{
	if (FWorldContext* PIEWorldContext = GUnrealEd->GetPIEWorldContext())
	{
		if (UWorld* PIEWorld = PIEWorldContext->World())
		{
			// Track open PIE/SIE sessions so the server can discard them once everyone leaves
			FConcertPlaySessionEvent PlaySessionEvent;
			PlaySessionEvent.EventType = EConcertPlaySessionEventType::SwitchPlay;
			PlaySessionEvent.PlayEndpointId = LiveSession->GetSession().GetSessionClientEndpointId();
			PlaySessionEvent.PlayPackageName = PIEWorld->GetOutermost()->GetFName();
			PlaySessionEvent.bIsSimulating = InIsSimulating;
			LiveSession->GetSession().SendCustomEvent(PlaySessionEvent, LiveSession->GetSession().GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
		}
	}
}

void FConcertClientWorkspace::HandleEndPIE(const bool InIsSimulating)
{
	if (FWorldContext* PIEWorldContext = GUnrealEd->GetPIEWorldContext())
	{
		if (UWorld* PIEWorld = PIEWorldContext->World())
		{
			// Track open PIE/SIE sessions so the server can discard them once everyone leaves
			FConcertPlaySessionEvent PlaySessionEvent;
			PlaySessionEvent.EventType = EConcertPlaySessionEventType::EndPlay;
			PlaySessionEvent.PlayEndpointId = LiveSession->GetSession().GetSessionClientEndpointId();
			PlaySessionEvent.PlayPackageName = PIEWorld->GetOutermost()->GetFName();
			PlaySessionEvent.bIsSimulating = InIsSimulating;
			LiveSession->GetSession().SendCustomEvent(PlaySessionEvent, LiveSession->GetSession().GetSessionServerEndpointId(), EConcertMessageFlags::ReliableOrdered);
		}
	}
}

#endif	// WITH_EDITOR

void FConcertClientWorkspace::OnEndFrame()
{
	if (bFinalizeWorkspaceSyncRequested)
	{
		bFinalizeWorkspaceSyncRequested = false;

		// Start tracking changes made by other users
		check(!LiveTransactionAuthors);
		LiveTransactionAuthors = MakeUnique<FConcertClientLiveTransactionAuthors>(LiveSession.ToSharedRef());

		// Make sure any new packages are loaded
		if (InitialSyncSlowTask.IsValid())
		{
			InitialSyncSlowTask->EnterProgressFrame(0.0f, LOCTEXT("ApplyingSynchronizedPackages", "Applying Synchronized Packages..."));
		}
		if (PackageManager)
		{
			PackageManager->SynchronizePersistedFiles(SessionData.PersistedFiles);
			PackageManager->ApplyAllHeadPackageData();
			PackageManager->SynchronizeInMemoryPackages();
		}

		// Replay any "live" transactions
		if (InitialSyncSlowTask.IsValid())
		{
			InitialSyncSlowTask->EnterProgressFrame(0.0f, LOCTEXT("ApplyingSynchronizedTransactions", "Applying Synchronized Transactions..."));
		}
		if (TransactionManager)
		{
			TransactionManager->ReplayAllTransactions();

			// We process all pending transactions we just replayed before finalizing the sync to prevent package being loaded as a result to trigger replaying transactions again
			TransactionManager->ProcessPending();
		}

		// Finalize the sync
		bHasSyncedWorkspace = true;
		InitialSyncSlowTask.Reset();
	}

	if (bHasSyncedWorkspace)
	{
		if (PackageManager)
		{
			PackageManager->SynchronizeInMemoryPackages();
		}

		if (TransactionManager)
		{
			TransactionManager->ProcessPending();
		}
	}
}

void FConcertClientWorkspace::HandleWorkspaceSyncEndpointEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncEndpointEvent& Event)
{
	// Update slow task dialog
	if (InitialSyncSlowTask.IsValid())
	{
		InitialSyncSlowTask->TotalAmountOfWork = InitialSyncSlowTask->CompletedWork + Event.NumRemainingSyncEvents + 1;
		InitialSyncSlowTask->EnterProgressFrame(FMath::Min<float>(Event.NumRemainingSyncEvents, 1.0f), FText::Format(LOCTEXT("SynchronizedEndpointFmt", "Synchronized User {0}..."), FText::AsCultureInvariant(Event.Endpoint.EndpointData.ClientInfo.DisplayName)));
	}

	// Set endpoint in database
	SetEndpoint(Event.Endpoint.EndpointId, Event.Endpoint.EndpointData);
}

void FConcertClientWorkspace::HandleWorkspaceSyncActivityEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncActivityEvent& Event)
{
	FStructOnScope ActivityPayload;
	Event.Activity.GetPayload(ActivityPayload);

	check(ActivityPayload.IsValid() && ActivityPayload.GetStruct()->IsChildOf(FConcertSyncActivity::StaticStruct()));
	FConcertSyncActivity* Activity = (FConcertSyncActivity*)ActivityPayload.GetStructMemory();

	// Update slow task dialog
	if (InitialSyncSlowTask.IsValid())
	{
		InitialSyncSlowTask->TotalAmountOfWork = InitialSyncSlowTask->CompletedWork + Event.NumRemainingSyncEvents + 1;
		InitialSyncSlowTask->EnterProgressFrame(FMath::Min<float>(Event.NumRemainingSyncEvents, 1.0f), FText::Format(LOCTEXT("SynchronizedActivityFmt", "Synchronized Activity {0}..."), Activity->ActivityId));
	}

	// Handle the activity correctly
	switch (Activity->EventType)
	{
	case EConcertSyncActivityEventType::Connection:
		check(ActivityPayload.GetStruct()->IsChildOf(FConcertSyncConnectionActivity::StaticStruct()));
		SetConnectionActivity(*(FConcertSyncConnectionActivity*)Activity);
		break;

	case EConcertSyncActivityEventType::Lock:
		check(ActivityPayload.GetStruct()->IsChildOf(FConcertSyncLockActivity::StaticStruct()));
		SetLockActivity(*(FConcertSyncLockActivity*)Activity);
		break;

	case EConcertSyncActivityEventType::Transaction:
		check(ActivityPayload.GetStruct()->IsChildOf(FConcertSyncTransactionActivity::StaticStruct()));
		SetTransactionActivity(*(FConcertSyncTransactionActivity*)Activity);
		break;

	case EConcertSyncActivityEventType::Package:
		check(ActivityPayload.GetStruct()->IsChildOf(FConcertSyncPackageActivity::StaticStruct()));
		SetPackageActivity(*(FConcertSyncPackageActivity*)Activity);
		break;

	default:
		checkf(false, TEXT("Unhandled EConcertSyncActivityEventType when syncing session activity"));
		break;
	}
}

void FConcertClientWorkspace::HandleWorkspaceSyncLockEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncLockEvent& Event)
{
	// Initial sync of the locked resources
	if (LockManager)
	{
		LockManager->SetLockedResources(Event.LockedResources);
	}
}

void FConcertClientWorkspace::HandleWorkspaceSyncCompletedEvent(const FConcertSessionContext& Context, const FConcertWorkspaceSyncCompletedEvent& Event)
{
	// Request the sync to finalize at the end of the next frame
	bFinalizeWorkspaceSyncRequested = true;
	OnWorkspaceSyncedDelegate.Broadcast();
}

bool FConcertClientWorkspace::IsAssetModifiedByOtherClients(const FName& AssetName, int32* OutOtherClientsWithModifNum, TArray<FConcertClientInfo>* OutOtherClientsWithModifInfo, int32 OtherClientsWithModifMaxFetchNum) const
{
	return LiveTransactionAuthors && LiveTransactionAuthors->IsPackageAuthoredByOtherClients(AssetName, OutOtherClientsWithModifNum, OutOtherClientsWithModifInfo, OtherClientsWithModifMaxFetchNum);
}

void FConcertClientWorkspace::SetEndpoint(const FGuid& InEndpointId, const FConcertSyncEndpointData& InEndpointData)
{
	// Update this endpoint
	if (!LiveSession->GetSessionDatabase().SetEndpoint(InEndpointId, InEndpointData))
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set endpoint '%s' on live session '%s': %s"), *InEndpointId.ToString(), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertClientWorkspace::SetConnectionActivity(const FConcertSyncConnectionActivity& InConnectionActivity)
{
	// Update this activity
	if (LiveSession->GetSessionDatabase().SetConnectionActivity(InConnectionActivity))
	{
		PostActivityUpdated(InConnectionActivity);
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set connection activity '%s' on live session '%s': %s"), *LexToString(InConnectionActivity.ActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertClientWorkspace::SetLockActivity(const FConcertSyncLockActivity& InLockActivity)
{
	// Update this activity
	if (LiveSession->GetSessionDatabase().SetLockActivity(InLockActivity))
	{
		PostActivityUpdated(InLockActivity);
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set lock activity '%s' on live session '%s': %s"), *LexToString(InLockActivity.ActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertClientWorkspace::SetTransactionActivity(const FConcertSyncTransactionActivity& InTransactionActivity)
{
	// Update this activity
	if (LiveSession->GetSessionDatabase().SetTransactionActivity(InTransactionActivity))
	{
		PostActivityUpdated(InTransactionActivity);
		if (TransactionManager)
		{
			TransactionManager->HandleRemoteTransaction(InTransactionActivity.EndpointId, InTransactionActivity.EventId, bHasSyncedWorkspace);
		}
		if (LiveTransactionAuthors)
		{
			LiveTransactionAuthors->AddLiveTransactionActivity(InTransactionActivity.EndpointId, InTransactionActivity.EventData.Transaction.ModifiedPackages);
		}
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set transaction activity '%s' on live session '%s': %s"), *LexToString(InTransactionActivity.ActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertClientWorkspace::SetPackageActivity(const FConcertSyncPackageActivity& InPackageActivity)
{
	// Update this activity
	if (LiveSession->GetSessionDatabase().SetPackageActivity(InPackageActivity))
	{
		PostActivityUpdated(InPackageActivity);
		if (PackageManager)
		{
			PackageManager->HandleRemotePackage(InPackageActivity.EndpointId, InPackageActivity.EventId, bHasSyncedWorkspace);
		}
		if (LiveTransactionAuthors)
		{
			LiveTransactionAuthors->ResolveLiveTransactionAuthorsForPackage(InPackageActivity.EventData.Package.Info.PackageName);
		}
	}
	else
	{
		UE_LOG(LogConcert, Error, TEXT("Failed to set package activity '%s' on live session '%s': %s"), *LexToString(InPackageActivity.ActivityId), *LiveSession->GetSession().GetName(), *LiveSession->GetSessionDatabase().GetLastError());
	}
}

void FConcertClientWorkspace::PostActivityUpdated(const FConcertSyncActivity& InActivity)
{
	FConcertSyncPackageActivity Activity;
	if (LiveSession->GetSessionDatabase().GetActivity(InActivity.ActivityId, Activity))
	{
		FConcertSyncEndpointData EndpointData;
		if (LiveSession->GetSessionDatabase().GetEndpoint(InActivity.EndpointId, EndpointData))
		{
			FStructOnScope ActivitySummary;
			if (Activity.EventSummary.GetPayload(ActivitySummary))
			{
				check(ActivitySummary.GetStruct()->IsChildOf(FConcertSyncActivitySummary::StaticStruct()));
				const FConcertSyncActivitySummary* ActivitySummaryPtr = (FConcertSyncActivitySummary*)ActivitySummary.GetStructMemory();
				UE_LOG(LogConcert, Display, TEXT("Synced activity '%s' produced by endpoint '%s': %s"), *LexToString(InActivity.ActivityId), *InActivity.EndpointId.ToString(), *ActivitySummaryPtr->ToDisplayText(FText::AsCultureInvariant(EndpointData.ClientInfo.DisplayName)).ToString());
				OnActivityAddedOrUpdatedDelegate.Broadcast(EndpointData.ClientInfo, Activity, ActivitySummary);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
