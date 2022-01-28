// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/AsyncPackageLoader.h"
#include "HAL/IConsoleManager.h"
#include "Serialization/AsyncLoadingThread.h"
#include "Serialization/AsyncLoading2.h"
#include "Serialization/EditorPackageLoader.h"
#include "UObject/GCObject.h"
#include "UObject/LinkerLoad.h"
#include "IO/PackageId.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/PathViews.h"
#include "IO/IoDispatcher.h"
#include "HAL/IConsoleManager.h"

#define DO_TRACK_ASYNC_LOAD_REQUESTS (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

#if DO_TRACK_ASYNC_LOAD_REQUESTS
#include "Containers/StackTracker.h"
#include "Misc/OutputDeviceFile.h"
#include "ProfilingDebugging/CsvProfiler.h"
#endif

volatile int32 GIsLoaderCreated;
TUniquePtr<IAsyncPackageLoader> GPackageLoader;
bool GAsyncLoadingAllowed = true;

FThreadSafeCounter IAsyncPackageLoader::NextPackageRequestId;

int32 IAsyncPackageLoader::GetNextRequestId()
{
	 return NextPackageRequestId.Increment();
}

#if !UE_BUILD_SHIPPING
static void LoadPackageCommand(const TArray<FString>& Args)
{
	for (const FString& PackageName : Args)
	{
		UE_LOG(LogStreaming, Display, TEXT("LoadPackageCommand: %s - Requested"), *PackageName);
		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		UE_LOG(LogStreaming, Display, TEXT("LoadPackageCommand: %s - %s"),
			*PackageName, (Package != nullptr) ? TEXT("Loaded") : TEXT("Failed"));
	}
}

static void LoadPackageAsyncCommand(const TArray<FString>& Args)
{
	for (const FString& PackageName : Args)
	{
		UE_LOG(LogStreaming, Display, TEXT("LoadPackageAsyncCommand: %s - Requested"), *PackageName);
		LoadPackageAsync(PackageName, FLoadPackageAsyncDelegate::CreateLambda(
			[](const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result)
		{
			UE_LOG(LogStreaming, Display, TEXT("LoadPackageAsyncCommand: %s - %s"),
				*PackageName.ToString(), (Package != nullptr) ? TEXT("Loaded") : TEXT("Failed"));
		}
		));
	}
}

static FAutoConsoleCommand CVar_LoadPackageCommand(
	TEXT("LoadPackage"),
	TEXT("Loads packages by names. Usage: LoadPackage <package name> [<package name> ...]"),
	FConsoleCommandWithArgsDelegate::CreateStatic(LoadPackageCommand));

static FAutoConsoleCommand CVar_LoadPackageAsyncCommand(
	TEXT("LoadPackageAsync"),
	TEXT("Loads packages async by names. Usage: LoadPackageAsync <package name> [<package name> ...]"),
	FConsoleCommandWithArgsDelegate::CreateStatic(LoadPackageAsyncCommand));
#endif

const FName PrestreamPackageClassNameLoad = FName("PrestreamPackage");

struct FEDLBootObjectState
{
	ENotifyRegistrationType NotifyRegistrationType;
	ENotifyRegistrationPhase LastNotifyRegistrationPhase;
	UObject *(*Register)();
	bool bDynamic;
};

struct FEDLBootWaitingPackage
{
	void* Package;
	FPackageIndex Import;
};

struct FEDLBootNotificationManager
	: public IEDLBootNotificationManager
{
	TMap<FName, FEDLBootObjectState> PathToState;
	TMultiMap<FName, FEDLBootWaitingPackage> PathToWaitingPackageNodes;
	TArray<FName> PathsToFire;
	TArray<UClass*> CDORecursiveStack;
	TArray<UClass*> CDORecursives;
	FCriticalSection EDLBootNotificationManagerLock;
	bool bEnabled = true;

	void Disable()
	{
		PathToState.Empty();
		PathsToFire.Empty();
		bEnabled = false;
	}

	// return true if you are waiting for this compiled in object
	bool AddWaitingPackage(void* Pkg, FName PackageName, FName ObjectName, FPackageIndex Import, bool bIgnoreMissingPackage) override
	{
		if (PackageName == GLongCoreUObjectPackageName)
		{
			return false; // We assume nothing in coreuobject ever loads assets in a constructor
		}
		FScopeLock Lock(&EDLBootNotificationManagerLock);
		check(GIsInitialLoad);
		check(Import.IsImport()); // compiled in exports make no sense
		FString ObjectNameString = ObjectName.ToString();
		FName LongFName(*(PackageName.ToString() / ObjectNameString));
		check(LongFName != NAME_None);
		FName WaitName = LongFName;
		FEDLBootObjectState* ExistingState = PathToState.Find(LongFName);
		if (!ExistingState)
		{
			//if (ObjectName.ToString().EndsWith(HEADER_GENERATED_DELEGATE_SIGNATURE_SUFFIX))
			// there are also some arg structs and other things that are just part of the package with no registration
			{
				ExistingState = PathToState.Find(PackageName);
				WaitName = PackageName;
			}
			if (!ExistingState)
			{
				UE_CLOG(!bIgnoreMissingPackage, LogStreaming, Fatal, TEXT("Compiled in export %s not found; it was never registered."), *LongFName.ToString());
				return false;
			}
		}
		if (ExistingState->LastNotifyRegistrationPhase == ENotifyRegistrationPhase::NRP_Finished)
		{
			return false;
		}
		FEDLBootWaitingPackage WaitingPackage;
		WaitingPackage.Package = Pkg;
		WaitingPackage.Import = Import;

		PathToWaitingPackageNodes.Add(WaitName, WaitingPackage);

		return true;
	}

	void NotifyRegistrationEvent(const TCHAR* PackageName, const TCHAR* Name, ENotifyRegistrationType NotifyRegistrationType, ENotifyRegistrationPhase NotifyRegistrationPhase, UObject *(*InRegister)(), bool InbDynamic)
	{
		if (!bEnabled || !GIsInitialLoad)
		{
			return;
		}
		static FName LongCoreUObjectPackageName(TEXT("/Script/CoreUObject")); // can't use the global here because it may not be initialized yet
		FName PackageFName(PackageName);
		if (PackageFName == LongCoreUObjectPackageName)
		{
			return; // We assume nothing in coreuobject ever loads assets in a constructor
		}

		TStringBuilder<256> LongNameBuilder;
		LongNameBuilder << PackageName;
		FPathViews::Append(LongNameBuilder, Name);
		FName LongFName(LongNameBuilder.ToView());

		FScopeLock Lock(&EDLBootNotificationManagerLock);

		//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("NotifyRegistrationEvent %s %d %d\r\n"), *LongFName.ToString(), int32(NotifyRegistrationType), int32(NotifyRegistrationPhase));

		// some things, like delegate signatures, are not registered; rather they are part of the package singleton, so we track the package state as being the max of any member of that package
		FEDLBootObjectState* ExistingPackageState = PathToState.Find(PackageFName);
		FEDLBootObjectState* ExistingState = PathToState.Find(LongFName);

		if (!ExistingState)
		{
			if (NotifyRegistrationPhase != ENotifyRegistrationPhase::NRP_Added)
			{
				UE_LOG(LogStreaming, Fatal, TEXT("Attempt to process %s before it has been added."), *LongFName.ToString());
			}
			FEDLBootObjectState NewState;
			NewState.LastNotifyRegistrationPhase = NotifyRegistrationPhase;
			NewState.NotifyRegistrationType = NotifyRegistrationType;
			NewState.Register = InRegister;
			NewState.bDynamic = InbDynamic;
			PathToState.Add(LongFName, NewState);

			if (!ExistingPackageState)
			{
				NewState.NotifyRegistrationType = ENotifyRegistrationType::NRT_Package;
				PathToState.Add(PackageFName, NewState);
			}
		}
		else
		{
			if (int32(ExistingState->LastNotifyRegistrationPhase) + 1 != int32(NotifyRegistrationPhase))
			{
				UE_CLOG(GEventDrivenLoaderEnabled, LogStreaming, Fatal, TEXT("Invalid state transition %d %d with %s when it has already been processed."), int32(ExistingState->LastNotifyRegistrationPhase), int32(NotifyRegistrationPhase), *LongFName.ToString());
			}
			if (ExistingState->NotifyRegistrationType != (NotifyRegistrationType))
			{
				UE_CLOG(GEventDrivenLoaderEnabled, LogStreaming, Fatal, TEXT("Multiple types %d %d with %s when it has already been processed."), int32(ExistingState->NotifyRegistrationType), int32(NotifyRegistrationType), *LongFName.ToString());
			}
			ExistingState->LastNotifyRegistrationPhase = NotifyRegistrationPhase;
			if (NotifyRegistrationPhase == ENotifyRegistrationPhase::NRP_Finished)
			{
				ExistingState->Register = nullptr; // we don't need to do this in ConstructWaitingBootObjects
				PathsToFire.Add(LongFName);
			}
			check(ExistingPackageState); // if we have an existing state for the thing, we should also have a 
			if (ExistingPackageState && int32(NotifyRegistrationPhase) > int32(ExistingPackageState->LastNotifyRegistrationPhase))
			{
				ExistingPackageState->LastNotifyRegistrationPhase = NotifyRegistrationPhase;
				if (NotifyRegistrationPhase == ENotifyRegistrationPhase::NRP_Finished)
				{
					//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fired package %s %d %d\r\n"), *PackageFName.ToString(), int32(NotifyRegistrationType), int32(NotifyRegistrationPhase));
					PathsToFire.Add(PackageFName);
				}
			}
		}
	}

	void NotifyRegistrationComplete()
	{
		if (!bEnabled)
		{
			return;
		}
#if USE_EVENT_DRIVEN_ASYNC_LOAD_AT_BOOT_TIME
		FireCompletedCompiledInImports(true);
		FlushAsyncLoading();
#endif
#if !HACK_HEADER_GENERATOR
		check(!GIsInitialLoad && IsInGameThread());
		FScopeLock Lock(&EDLBootNotificationManagerLock);
		for (auto& Pair : PathToState)
		{
			if (Pair.Value.LastNotifyRegistrationPhase != ENotifyRegistrationPhase::NRP_Finished && !Pair.Value.bDynamic)
			{
#if USE_EVENT_DRIVEN_ASYNC_LOAD_AT_BOOT_TIME
				UE_CLOG(GEventDrivenLoaderEnabled, LogStreaming, Fatal, TEXT("%s (%d) was not complete (%d) after registration was complete."), *Pair.Key.ToString(), int32(Pair.Value.NotifyRegistrationType), int32(Pair.Value.LastNotifyRegistrationPhase));
#else
				UE_LOG(LogStreaming, Warning, TEXT("%s was not complete (%d) after registration was complete."), *Pair.Key.ToString(), int32(Pair.Value.LastNotifyRegistrationPhase));
#endif
			}
		}
		if (PathToWaitingPackageNodes.Num())
		{
			UE_LOG(LogStreaming, Fatal, TEXT("Initial load is complete, but we still have %d waiting packages."), PathToWaitingPackageNodes.Num());
		}
		if (GEventDrivenLoaderEnabled && PathsToFire.Num() && USE_EVENT_DRIVEN_ASYNC_LOAD_AT_BOOT_TIME)
		{
			for (FName Path : PathsToFire)
			{
				UE_LOG(LogStreaming, Error, TEXT("%s was not fired."), *Path.ToString());
			}
			UE_LOG(LogStreaming, Fatal, TEXT("Initial load is complete, but we still have %d imports to fire (listed above)."), PathsToFire.Num());
		}
#endif
		Disable();
	}

	bool ConstructWaitingBootObjects() override
	{
		static struct FFixedBootOrder
		{
			TArray<FName> Array;
			FFixedBootOrder()
			{
				// look for any packages that we want to force preload at startup
				FConfigSection* BootObjects = GConfig->GetSectionPrivate(TEXT("/Script/Engine.StreamingSettings"), false, true, GEngineIni);
				if (BootObjects)
				{
					// go through list and add to the array
					for (FConfigSectionMap::TIterator It(*BootObjects); It; ++It)
					{
						if (It.Key() == TEXT("FixedBootOrder"))
						{
							// add this package to the list to be fully loaded later
							Array.Add(FName(*It.Value().GetValue()));
						}
					}
				}
			}

		} FixedBootOrder;

		check(GIsInitialLoad && IsInGameThread());
		UObject *(*BootObjectRegister)() = nullptr;
		UObject *(*BootPackageObjectRegister)() = nullptr;
		FName WaitingPackage;
		bool bIsCDO = false;

		while (FixedBootOrder.Array.Num())
		{
			FName ThisItem = FixedBootOrder.Array.Pop();
			FScopeLock Lock(&EDLBootNotificationManagerLock);
			FEDLBootObjectState* ExistingState = PathToState.Find(ThisItem);

			if (!ExistingState)
			{
				UE_LOG(LogStreaming, Fatal, TEXT("%s was listed as a fixed load order but was not found,"), *ThisItem.ToString());
			}
			else if (!ExistingState->Register)
			{
				UE_LOG(LogStreaming, Log, TEXT("%s was listed as a fixed load order but was already processed"), *ThisItem.ToString());
			}
			else
			{
				//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Booting Fixed %s %d\r\n"), *ThisItem.ToString(), int32(ExistingState->NotifyRegistrationType));
				BootObjectRegister = ExistingState->Register;
				ExistingState->Register = nullptr; // we don't need to do this more than once
				bIsCDO = ExistingState->NotifyRegistrationType == ENotifyRegistrationType::NRT_ClassCDO;
				break;
			}
		}

		if (!BootObjectRegister)
		{
			FScopeLock Lock(&EDLBootNotificationManagerLock);
			for (auto& Pair : PathToWaitingPackageNodes)
			{
				FEDLBootObjectState* ExistingState = PathToState.Find(Pair.Key);
				if (ExistingState)
				{
					if (ExistingState->Register)
					{
						//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Booting %s %d\r\n"), *Pair.Key.ToString(), int32(ExistingState->NotifyRegistrationType));
						BootObjectRegister = ExistingState->Register;
						ExistingState->Register = nullptr; // we don't need to do this more than once
						bIsCDO = ExistingState->NotifyRegistrationType == ENotifyRegistrationType::NRT_ClassCDO;
						break;
					}
				}
			}
		}
		if (BootObjectRegister)
		{
			UObject* BootObject = BootObjectRegister();
			check(BootObject);
			UObjectForceRegistration(BootObject);
			if (bIsCDO)
			{
				UClass* Class = CastChecked<UClass>(BootObject);
				bool bAnyParentOnStack = false;
				UClass* Super = Class;
				while (Super)
				{
					if (CDORecursiveStack.Contains(Super))
					{
						bAnyParentOnStack = true;
						break;
					}
					Super = Super->GetSuperClass();
				}

				if (!bAnyParentOnStack)
				{
					CDORecursiveStack.Push(Class);
					//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Create CDO %s\r\n"), *BootObject->GetName());
					Class->GetDefaultObject();
					verify(CDORecursiveStack.Pop() == Class);
				}
				else
				{
					//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Recursive Deferred %s\r\n"), *BootObject->GetName());
					CDORecursives.Add(Class);
				}
			}
			return true;
		}
		if (CDORecursives.Num())
		{
			UClass* OkToRun = nullptr;
			for (UClass* Class : CDORecursives)
			{
				bool bAnyParentOnStack = false;
				UClass* Super = Class;
				while (Super)
				{
					if (CDORecursiveStack.Contains(Super))
					{
						bAnyParentOnStack = true;
						break;
					}
					Super = Super->GetSuperClass();
				}
				if (!bAnyParentOnStack)
				{
					OkToRun = Class;
					break;
				}
			}
			if (OkToRun)
			{
				//FPlatformMisc::LowLevelOutputDebugStringf(TEXT("CDORecursives %s\r\n"), *OkToRun->GetName());
				CDORecursives.Remove(OkToRun);
				CDORecursiveStack.Push(OkToRun);
				OkToRun->GetDefaultObject();
				verify(CDORecursiveStack.Pop() == OkToRun);
			}
			else
			{
				FPlatformProcess::Sleep(.001f);
			}
			return true; // even if we didn't do anything we need to return true to avoid checking for cycles
		}
		return false;
	}

	bool IsWaitingForSomething() override
	{
		FScopeLock Lock(&EDLBootNotificationManagerLock);
		return PathToWaitingPackageNodes.Num() > 0;
	}

	bool IsObjComplete(UObject* Obj)
	{
		static FName LongCoreUObjectPackageName(TEXT("/Script/CoreUObject")); // can't use the global here because it may not be initialized yet
		FName PackageName = Obj->GetOutermost()->GetFName();
		if (PackageName == LongCoreUObjectPackageName)
		{
			return true; // We assume nothing in coreuobject ever loads assets in a constructor, therefore it can be considered complete
		}
		FScopeLock Lock(&EDLBootNotificationManagerLock);
		FName LongFName(*(PackageName.ToString() / Obj->GetName()));

		FEDLBootObjectState* ExistingState = PathToState.Find(LongFName);

		if (!ExistingState || ExistingState->LastNotifyRegistrationPhase == ENotifyRegistrationPhase::NRP_Finished)
		{
			return true;
		}
		return false;
	}

	bool FireCompletedCompiledInImports(bool bFinalRun) override
	{
#if USE_EVENT_DRIVEN_ASYNC_LOAD_AT_BOOT_TIME
		FScopeLock Lock(&EDLBootNotificationManagerLock);
		check(bFinalRun || GIsInitialLoad);
		bool bResult = !!PathsToFire.Num();
		for (FName LongName : PathsToFire)
		{
			for (auto It = PathToWaitingPackageNodes.CreateKeyIterator(LongName); It; ++It)
			{
				FEDLBootWaitingPackage& WaitingPackage = It.Value();
				GPackageLoader->FireCompletedCompiledInImport(WaitingPackage.Package, WaitingPackage.Import);
			}
			PathToWaitingPackageNodes.Remove(LongName);
		}
		PathsToFire.Empty();
		return bResult;
#else
		return false;
#endif
	}
};

static FEDLBootNotificationManager& GetGEDLBootNotificationManager()
{
	static FEDLBootNotificationManager Singleton;
	return Singleton;
}

FAsyncLoadingThreadSettings::FAsyncLoadingThreadSettings()
{
#if THREADSAFE_UOBJECTS
	if (FPlatformProperties::RequiresCookedData())
	{
		check(GConfig);

		bool bConfigValue = true;
		GConfig->GetBool(TEXT("/Script/Engine.StreamingSettings"), TEXT("s.AsyncLoadingThreadEnabled"), bConfigValue, GEngineIni);
		bool bCommandLineDisable = FParse::Param(FCommandLine::Get(), TEXT("NoAsyncLoadingThread"));
		bool bCommandLineEnable = FParse::Param(FCommandLine::Get(), TEXT("AsyncLoadingThread"));
		bAsyncLoadingThreadEnabled = bCommandLineEnable || (bConfigValue && FApp::ShouldUseThreadingForPerformance() && !bCommandLineDisable);

		bConfigValue = true;
		GConfig->GetBool(TEXT("/Script/Engine.StreamingSettings"), TEXT("s.AsyncPostLoadEnabled"), bConfigValue, GEngineIni);
		bCommandLineDisable = FParse::Param(FCommandLine::Get(), TEXT("NoAsyncPostLoad"));
		bCommandLineEnable = FParse::Param(FCommandLine::Get(), TEXT("AsyncPostLoad"));
		bAsyncPostLoadEnabled = bCommandLineEnable || (bConfigValue && FApp::ShouldUseThreadingForPerformance() && !bCommandLineDisable);
	}
	else
#endif
	{
		bAsyncLoadingThreadEnabled = false;
		bAsyncPostLoadEnabled = false;
	}
}

FAsyncLoadingThreadSettings& FAsyncLoadingThreadSettings::Get()
{
	static FAsyncLoadingThreadSettings Settings;
	return Settings;
}

bool IsFullyLoadedObj(UObject* Obj)
{
	if (!Obj)
	{
		return false;
	}
	if (Obj->HasAllFlags(RF_WasLoaded | RF_LoadCompleted)
		|| Obj->IsA(UPackage::StaticClass())) // packages are never really loaded, so if it exists, it is loaded
	{
		return true;
	}
	if (Obj->HasAnyFlags(RF_WasLoaded | RF_NeedLoad | RF_WillBeLoaded))
	{
		return false;
	}
	if (GIsInitialLoad && Obj->GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn))
	{
		return GetGEDLBootNotificationManager().IsObjComplete(Obj);
	}
	
	return true;
}

bool IsNativeCodePackage(UPackage* Package)
{
	return (Package && Package->HasAnyPackageFlags(PKG_CompiledIn));
}

/** Checks if the object can have PostLoad called on the Async Loading Thread */
bool CanPostLoadOnAsyncLoadingThread(UObject* Object)
{
	if (Object->IsPostLoadThreadSafe())
	{
		bool bCanPostLoad = true;
		// All outers should also be safe to call PostLoad on ALT
		for (UObject* Outer = Object->GetOuter(); Outer && bCanPostLoad; Outer = Outer->GetOuter())
		{
			bCanPostLoad = !Outer->HasAnyFlags(RF_NeedPostLoad) || Outer->IsPostLoadThreadSafe();
		}
		return bCanPostLoad;
	}
	return false;
}

IAsyncPackageLoader& GetAsyncPackageLoader()
{
	check(GPackageLoader.Get());
	return *GPackageLoader;
}

void SetAsyncLoadingAllowed(bool bAllowAsyncLoading)
{
	GAsyncLoadingAllowed = bAllowAsyncLoading;
}

void InitAsyncThread()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
#if WITH_ASYNCLOADING2
	if (FIoDispatcher::IsInitialized())
	{
		FIoDispatcher& IoDispatcher = FIoDispatcher::Get();
		bool bHasScriptObjectsChunk = IoDispatcher.DoesChunkExist(CreateIoChunkId(0, 0, EIoChunkType::ScriptObjects));
		bool bHasUseIoStoreParamInEditor = WITH_EDITOR && FParse::Param(FCommandLine::Get(), TEXT("UseIoStore"));
		if (bHasScriptObjectsChunk || bHasUseIoStoreParamInEditor)
		{
			GetGEDLBootNotificationManager().Disable();
#if WITH_EDITOR
			GPackageLoader = MakeEditorPackageLoader(IoDispatcher, GetGEDLBootNotificationManager());
#else
			GPackageLoader.Reset(MakeAsyncPackageLoader2(IoDispatcher));
#endif
		}
	}
#endif
	if (!GPackageLoader.IsValid())
	{
		GPackageLoader = MakeUnique<FAsyncLoadingThread>(/** ThreadIndex = */ 0, GetGEDLBootNotificationManager());
	}

	FPlatformAtomics::InterlockedIncrement(&GIsLoaderCreated);

	FCoreDelegates::OnSyncLoadPackage.AddStatic([](const FString&) { GSyncLoadCount++; });
 
	GPackageLoader->InitializeLoading();
}

void ShutdownAsyncThread()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	if (GPackageLoader)
	{
		GPackageLoader->ShutdownLoading();
		GPackageLoader.Reset(nullptr);
	}
}

bool IsInAsyncLoadingThreadCoreUObjectInternal()
{
	if (GPackageLoader)
	{
		return GPackageLoader->IsInAsyncLoadThread();
	}
	else
	{
		return false;
	}
}

void FlushAsyncLoading(int32 PackageID /* = INDEX_NONE */)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FlushAsyncLoading);

#if defined(WITH_CODE_GUARD_HANDLER) && WITH_CODE_GUARD_HANDLER
	void CheckImageIntegrityAtRuntime();
	CheckImageIntegrityAtRuntime();
#endif
	LLM_SCOPE(ELLMTag::AsyncLoading);
	checkf(IsInGameThread(), TEXT("Unable to FlushAsyncLoading from any thread other than the game thread."));
	if (GPackageLoader)
	{
#if NO_LOGGING == 0
		if (IsAsyncLoading())
		{
			// Log the flush, but only display once per frame to avoid log spam.
			static uint64 LastFrameNumber = -1;
			if (LastFrameNumber != GFrameNumber)
			{
				UE_LOG(LogStreaming, Display, TEXT("FlushAsyncLoading: %d QueuedPackages, %d AsyncPackages"), GPackageLoader->GetNumQueuedPackages(), GPackageLoader->GetNumAsyncPackages());
			}
			else
			{
				UE_LOG(LogStreaming, Log, TEXT("FlushAsyncLoading: %d QueuedPackages, %d AsyncPackages"), GPackageLoader->GetNumQueuedPackages(), GPackageLoader->GetNumAsyncPackages());
			}
			LastFrameNumber = GFrameNumber;
		}
#endif
		GPackageLoader->FlushLoading(PackageID);
	}
}

EAsyncPackageState::Type ProcessAsyncLoadingUntilComplete(TFunctionRef<bool()> CompletionPredicate, double TimeLimit)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	return GetAsyncPackageLoader().ProcessLoadingUntilComplete(CompletionPredicate, TimeLimit);
}

int32 GetNumAsyncPackages()
{
	return GetAsyncPackageLoader().GetNumAsyncPackages();
}

EAsyncPackageState::Type ProcessAsyncLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, double TimeLimit)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessAsyncLoading);
	return GetAsyncPackageLoader().ProcessLoading(bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
}

bool IsAsyncLoadingCoreUObjectInternal()
{
	// GIsInitialLoad guards the async loading thread from being created too early
	return GetAsyncPackageLoader().IsAsyncLoadingPackages();
}

bool IsAsyncLoadingMultithreadedCoreUObjectInternal()
{
	// GIsInitialLoad guards the async loading thread from being created too early
	return GetAsyncPackageLoader().IsMultithreaded();
}

void SuspendAsyncLoadingInternal()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	check(IsInGameThread() && !IsInSlateThread());
	GetAsyncPackageLoader().SuspendLoading();
}

void ResumeAsyncLoadingInternal()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	check(IsInGameThread() && !IsInSlateThread());
	GetAsyncPackageLoader().ResumeLoading();
}

bool IsAsyncLoadingSuspendedInternal()
{
	return GetAsyncPackageLoader().IsAsyncLoadingSuspended();
}

#if DO_TRACK_ASYNC_LOAD_REQUESTS
static TAutoConsoleVariable<int32> CVarTrackAsyncLoadRequests_Enable(
	TEXT("TrackAsyncLoadRequests.Enable"),
	0,
	TEXT("If > 0 then remove aliases from the counting process. This essentialy merges addresses that have the same human readable string. It is slower.")
);

static TAutoConsoleVariable<int32> CVarTrackAsyncLoadRequests_Dedupe(
	TEXT("TrackAsyncLoadRequests.Dedupe"),
	0,
	TEXT("If > 0 then deduplicate requests to async load the same package in the report.")
);

static TAutoConsoleVariable<int32> CVarTrackAsyncLoadRequests_RemoveAliases(
	TEXT("TrackAsyncLoadRequests.RemoveAliases"),
	1,
	TEXT("If > 0 then remove aliases from the counting process. This essentialy merges addresses that have the same human readable string. It is slower.")
);

static TAutoConsoleVariable<int32> CVarTrackAsyncLoadRequests_StackIgnore(
	TEXT("TrackAsyncLoadRequests.StackIgnore"),
	5,
	TEXT("Number of items to discard from the top of a stack frame."));

static TAutoConsoleVariable<int32> CVarTrackAsyncLoadRequests_StackLen(
	TEXT("TrackAsyncLoadRequests.StackLen"),
	12,
	TEXT("Maximum number of stack frame items to keep. This improves aggregation because calls that originate from multiple places but end up in the same place will be accounted together."));

static TAutoConsoleVariable<int32> CVarTrackAsyncLoadRequests_Threshhold(
	TEXT("TrackAsyncLoadRequests.Threshhold"),
	0,
	TEXT("Minimum number of hits to include in the report."));

static TAutoConsoleVariable<int32> CVarTrackAsyncLoadRequests_DumpAfterCsvProfiling(
	TEXT("TrackAsyncLoadRequests.DumpAfterCsvProfiling"),
	1,
	TEXT("If > 0, dumps tracked async load requests to a file when csv profiling ends."));


struct FTrackAsyncLoadRequests
{
	FStackTracker StackTracker;
	FCriticalSection CritSec;

	struct FUserData
	{
		struct FLoadRequest
		{
			FString RequestName;
			int32 Priority;

			FLoadRequest(FString InName, int32 InPriority)
				: RequestName(MoveTemp(InName))
				, Priority(InPriority)
			{
			}
		};
		TArray<FLoadRequest> Requests;
	};

	static FTrackAsyncLoadRequests& Get()
	{
		static FTrackAsyncLoadRequests Instance;
		return Instance;
	}

	FTrackAsyncLoadRequests()
		: StackTracker(&UpdateStack, &ReportStack, &DeleteUserData, true)
	{
#if CSV_PROFILER
		FCsvProfiler::Get()->OnCSVProfileEnd().AddRaw(this, &FTrackAsyncLoadRequests::DumpRequestsAfterCsvProfiling);
#endif
	}

	static void UpdateStack(const FStackTracker::FCallStack& CallStack, void* InUserData)
	{
		FUserData* NewUserData = (FUserData*)InUserData;
		FUserData* OldUserData = (FUserData*)CallStack.UserData;

		OldUserData->Requests.Append(NewUserData->Requests);
	}

	static void ReportStack(const FStackTracker::FCallStack& CallStack, uint64 TotalStackCount, FOutputDevice& Ar)
	{
		FUserData* UserData = (FUserData*)CallStack.UserData;
		bool bOldSuppress = Ar.GetSuppressEventTag();
		Ar.SetSuppressEventTag(true);

		if (CVarTrackAsyncLoadRequests_Dedupe->GetInt() > 0)
		{
			Ar.Logf(TEXT("Requested package names (Deduped):"));
			Ar.Logf(TEXT("===================="));
			TSet<FString> Seen;
			for (auto& Request : UserData->Requests)
			{
				if (!Seen.Contains(Request.RequestName))
				{
					Ar.Logf(TEXT("%d %s"), Request.Priority, *Request.RequestName);
					Seen.Add(Request.RequestName);
				}
			}
		}
		else
		{
			Ar.Logf(TEXT("Requested package names:"));
			Ar.Logf(TEXT("===================="));
			for (const auto& Request : UserData->Requests)
			{
				Ar.Logf(TEXT("%d %s"), Request.Priority, *Request.RequestName);
			}
		}
		Ar.Logf(TEXT("===================="));
		Ar.SetSuppressEventTag(bOldSuppress);
	}
	
	static void DeleteUserData(void* InUserData)
	{
		FUserData* UserData = (FUserData*)InUserData;
		delete UserData;
	}

	void TrackRequest(const FString& InName, const TCHAR* InPackageToLoadFrom, int32 InPriority)
	{
		if (CVarTrackAsyncLoadRequests_Enable->GetInt() == 0)
		{
			return;
		}

		FUserData* UserData = new FUserData;
		UserData->Requests.Emplace(InPackageToLoadFrom ? FString(InPackageToLoadFrom) : InName, InPriority);

		FScopeLock Lock(&CritSec);
		StackTracker.CaptureStackTrace(CVarTrackAsyncLoadRequests_StackIgnore->GetInt(), (void*)UserData, CVarTrackAsyncLoadRequests_StackLen->GetInt(), CVarTrackAsyncLoadRequests_Dedupe->GetBool());
	}

	void Reset()
	{
		FScopeLock Lock(&CritSec);
		StackTracker.ResetTracking();
	}

	void DumpRequests(bool bReset)
	{
		FScopeLock Lock(&CritSec);
		StackTracker.DumpStackTraces(CVarTrackAsyncLoadRequests_Threshhold->GetInt(), *GLog);
		if (bReset)
		{
			StackTracker.ResetTracking();
		}
	}

	void DumpRequestsToFile(bool bReset)
	{
		FString Filename = FPaths::ProfilingDir() / FString::Printf(TEXT("AsyncLoadRequests_%s.log"), *FDateTime::Now().ToString());
		FOutputDeviceFile Out(*Filename, true);
		Out.SetSuppressEventTag(true);

		UE_LOG(LogStreaming, Display, TEXT("Dumping async load requests & callstacks to %s"), *Filename);

		FScopeLock Lock(&CritSec);
		StackTracker.DumpStackTraces(CVarTrackAsyncLoadRequests_Threshhold->GetInt(), Out);
		if (bReset)
		{
			StackTracker.ResetTracking();
		}
	}

#if CSV_PROFILER
	void DumpRequestsAfterCsvProfiling()
	{
		if (CVarTrackAsyncLoadRequests_DumpAfterCsvProfiling->GetInt() > 0)
		{
			DumpRequestsToFile(false);
		}
	}
#endif
};

static FAutoConsoleCommand TrackAsyncLoadRequests_Reset
(
	TEXT("TrackAsyncLoadRequests.Reset"),
	TEXT("Reset tracked async load requests"),
	FConsoleCommandDelegate::CreateLambda([]() { FTrackAsyncLoadRequests::Get().Reset(); })
);

static FAutoConsoleCommand TrackAsyncLoadRequests_Dump
(
	TEXT("TrackAsyncLoadRequests.Dump"),
	TEXT("Dump tracked async load requests and reset tracking"),
	FConsoleCommandDelegate::CreateLambda([]() { FTrackAsyncLoadRequests::Get().DumpRequests(true); })
);

static FAutoConsoleCommand TrackAsyncLoadRequests_DumpToFile
(
	TEXT("TrackAsyncLoadRequests.DumpToFile"),
	TEXT("Dump tracked async load requests and reset tracking"),
	FConsoleCommandDelegate::CreateLambda([]() { FTrackAsyncLoadRequests::Get().DumpRequestsToFile(true); })
);
#endif

static FPackagePath GetLoadPackageAsyncPackagePath(FStringView InPackageNameOrFilePath)
{
	FPackagePath PackagePath;
	if (!FPackagePath::TryFromMountedName(InPackageNameOrFilePath, PackagePath))
	{
		// Legacy behavior from FAsyncLoadingThread::LoadPackage: handle asset strings with class references: ClassName'PackageName'. 
		FStringView ExportTextPackagePath;
		if (FPackageName::ParseExportTextPath(InPackageNameOrFilePath, nullptr /* OutClassName */, &ExportTextPackagePath))
		{
			if (FPackagePath::TryFromMountedName(ExportTextPackagePath, PackagePath))
			{
				UE_LOG(LogStreaming, Warning, TEXT("Deprecation warning: calling LoadPackage with the export text format of a package name (ClassName'PackageName') is deprecated and will be removed in a future release."));
			}
		}
	}

	// If PackagePath is empty at this point, then we are going to fail because its not mounted. But pass the input name 
	// into PackagePath (preferably as a packagename) to provide to the caller's CompletionCallback
	if (PackagePath.IsEmpty())
	{
		if (!FPackagePath::TryFromPackageName(InPackageNameOrFilePath, PackagePath))
		{
			PackagePath = FPackagePath::FromLocalPath(InPackageNameOrFilePath);
		}
	}

	return PackagePath;
}

int32 IAsyncPackageLoader::LoadPackage(
	const FString& InPackageName,
	const FGuid* InGuid,
	const TCHAR* InPackageToLoadFrom,
	FLoadPackageAsyncDelegate InCompletionDelegate,
	EPackageFlags InPackageFlags,
	int32 InPIEInstanceID,
	int32 InPackagePriority,
	const FLinkerInstancingContext* InstancingContext)
{
	FPackagePath PackagePath = GetLoadPackageAsyncPackagePath(InPackageToLoadFrom ? FStringView(InPackageToLoadFrom) : FStringView(InPackageName));
	return LoadPackage(PackagePath, FName(InPackageName), InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority, InstancingContext);
}

bool ShouldAlwaysLoadPackageAsync(const FPackagePath& InPackagePath)
{
	if (!GPackageLoader)
	{
		return false;
	}
	return GPackageLoader->ShouldAlwaysLoadPackageAsync(InPackagePath);
}

int32 LoadPackageAsync(const FPackagePath& InPackagePath, FName InPackageNameToCreate /* = NAME_None*/, FLoadPackageAsyncDelegate InCompletionDelegate /*= FLoadPackageAsyncDelegate()*/, EPackageFlags InPackageFlags /*= PKG_None*/, int32 InPIEInstanceID /*= INDEX_NONE*/, int32 InPackagePriority /*= 0*/, const FLinkerInstancingContext* InstancingContext /*=nullptr*/)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	UE_CLOG(!GAsyncLoadingAllowed && !IsInAsyncLoadingThread(), LogStreaming, Fatal, TEXT("Requesting async load of \"%s\" when async loading is not allowed (after shutdown). Please fix higher level code."), *InPackagePath.GetDebugName());
#if DO_TRACK_ASYNC_LOAD_REQUESTS
	FTrackAsyncLoadRequests::Get().TrackRequest(InPackagePath.GetDebugName(), nullptr, InPackagePriority);
#endif
	return GetAsyncPackageLoader().LoadPackage(InPackagePath, InPackageNameToCreate, InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority, InstancingContext);
}

int32 LoadPackageAsync(const FString& InName, const FGuid* InGuid /* nullptr*/)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	FPackagePath PackagePath = GetLoadPackageAsyncPackagePath(InName);
	return LoadPackageAsync(PackagePath, NAME_None /* InPackageNameToCreate */, FLoadPackageAsyncDelegate(), PKG_None, INDEX_NONE, 0, nullptr);
}

int32 LoadPackageAsync(const FString& InName, FLoadPackageAsyncDelegate CompletionDelegate, int32 InPackagePriority /*= 0*/, EPackageFlags InPackageFlags /*= PKG_None*/, int32 InPIEInstanceID /*= INDEX_NONE*/)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	FPackagePath PackagePath = GetLoadPackageAsyncPackagePath(InName);
	return LoadPackageAsync(PackagePath, NAME_None /* InPackageNameToCreate */, CompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority, nullptr);
}

int32 LoadPackageAsync(const FString& InName, const FGuid* InGuid /*= nullptr*/, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate /*= FLoadPackageAsyncDelegate()*/, EPackageFlags InPackageFlags /*= PKG_None*/, int32 InPIEInstanceID /*= INDEX_NONE*/, int32 InPackagePriority /*= 0*/, const FLinkerInstancingContext* InstancingContext /*=nullptr*/)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	FPackagePath PackagePath = GetLoadPackageAsyncPackagePath(InPackageToLoadFrom ? FStringView(InPackageToLoadFrom) : FStringView(InName));
	FName InPackageNameToCreate;
	if (InPackageToLoadFrom)
	{
		// We're loading from PackageToLoadFrom. If InName is different, it is the PackageNameToCreate. InName might be a filepath, so use FPackagePath to convert it to a PackagePath
		FPackagePath PackagePathToCreate;
		if (FPackagePath::TryFromMountedName(InName, PackagePathToCreate))
		{
			InPackageNameToCreate = PackagePathToCreate.GetPackageFName();
		}
	}
	return LoadPackageAsync(PackagePath, InPackageNameToCreate, InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority, InstancingContext);
}

void CancelAsyncLoading()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	// Cancelling async loading while loading is suspend will result in infinite stall
	UE_CLOG(GetAsyncPackageLoader().IsAsyncLoadingSuspended(), LogStreaming, Fatal, TEXT("Cannot Cancel Async Loading while async loading is suspended."));
	GetAsyncPackageLoader().CancelLoading();

	if (!IsEngineExitRequested())
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
	}

	const EInternalObjectFlags AsyncFlags = EInternalObjectFlags::Async | EInternalObjectFlags::AsyncLoading;
	for (int32 ObjectIndex = 0; ObjectIndex < GUObjectArray.GetObjectArrayNum(); ++ObjectIndex)
	{
		FUObjectItem* ObjectItem = &GUObjectArray.GetObjectItemArrayUnsafe()[ObjectIndex];
		if (UObject* Obj = static_cast<UObject*>(ObjectItem->Object))
		{
			check(!Obj->HasAnyInternalFlags(AsyncFlags));
		}
	}
}

float GetAsyncLoadPercentage(const FName& PackageName)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	return GetAsyncPackageLoader().GetAsyncLoadPercentage(PackageName);
}

void NotifyRegistrationEvent(const TCHAR* PackageName, const TCHAR* Name, ENotifyRegistrationType NotifyRegistrationType, ENotifyRegistrationPhase NotifyRegistrationPhase, UObject *(*InRegister)(), bool InbDynamic)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	GetGEDLBootNotificationManager().NotifyRegistrationEvent(PackageName, Name, NotifyRegistrationType, NotifyRegistrationPhase, InRegister, InbDynamic);
}

void NotifyRegistrationComplete()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	GetGEDLBootNotificationManager().NotifyRegistrationComplete();
	FlushAsyncLoading();
	GPackageLoader->StartThread();
}

void NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObject)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	GetAsyncPackageLoader().NotifyConstructedDuringAsyncLoading(Object, bSubObject);
}

void NotifyUnreachableObjects(const TArrayView<FUObjectItem*>& UnreachableObjects)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	GetAsyncPackageLoader().NotifyUnreachableObjects(UnreachableObjects);
}

double GFlushAsyncLoadingTime = 0.0;
uint32 GFlushAsyncLoadingCount = 0;
uint32 GSyncLoadCount = 0;

void ResetAsyncLoadingStats()
{
	check(IsInGameThread());
	GFlushAsyncLoadingTime = 0.0;
	GFlushAsyncLoadingCount = 0;
	GSyncLoadCount = 0;
}

int32 GWarnIfTimeLimitExceeded = 0;
static FAutoConsoleVariableRef CVarWarnIfTimeLimitExceeded(
	TEXT("s.WarnIfTimeLimitExceeded"),
	GWarnIfTimeLimitExceeded,
	TEXT("Enables log warning if time limit for time-sliced package streaming has been exceeded."),
	ECVF_Default
);

float GTimeLimitExceededMultiplier = 1.5f;
static FAutoConsoleVariableRef CVarTimeLimitExceededMultiplier(
	TEXT("s.TimeLimitExceededMultiplier"),
	GTimeLimitExceededMultiplier,
	TEXT("Multiplier for time limit exceeded warning time threshold."),
	ECVF_Default
);

float GTimeLimitExceededMinTime = 0.005f;
static FAutoConsoleVariableRef CVarTimeLimitExceededMinTime(
	TEXT("s.TimeLimitExceededMinTime"),
	GTimeLimitExceededMinTime,
	TEXT("Minimum time the time limit exceeded warning will be triggered by."),
	ECVF_Default
);

void IsTimeLimitExceededPrint(
	double InTickStartTime,
	double CurrentTime,
	double LastTestTime,
	float InTimeLimit, 
	const TCHAR* InLastTypeOfWorkPerformed,
	UObject* InLastObjectWorkWasPerformedOn)
{
	static double LastPrintStartTime = -1.0;
	// Log single operations that take longer than time limit (but only in cooked builds)
	if (LastPrintStartTime != InTickStartTime &&
		(CurrentTime - InTickStartTime) > GTimeLimitExceededMinTime &&
		(CurrentTime - InTickStartTime) > (GTimeLimitExceededMultiplier * InTimeLimit))
	{
		double EstimatedTimeForThisStep = (CurrentTime - InTickStartTime) * 1000.0;
		if (LastTestTime > InTickStartTime)
		{
			EstimatedTimeForThisStep = (CurrentTime - LastTestTime) * 1000.0;
		}
		LastPrintStartTime = InTickStartTime;
		UE_LOG(LogStreaming, Warning, TEXT("IsTimeLimitExceeded: %s %s Load Time %5.2fms   Last Step Time %5.2fms"),
			InLastTypeOfWorkPerformed ? InLastTypeOfWorkPerformed : TEXT("unknown"),
			InLastObjectWorkWasPerformedOn ? *InLastObjectWorkWasPerformedOn->GetFullName() : TEXT("nullptr"),
			(CurrentTime - InTickStartTime) * 1000,
			EstimatedTimeForThisStep);
	}
}
