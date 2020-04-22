// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnAsyncLoading.cpp: Unreal async loading code.
=============================================================================*/

#include "Serialization/AsyncLoading2.h"
#include "Serialization/AsyncPackageLoader.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "HAL/Event.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "Stats/StatsMisc.h"
#include "Misc/CoreStats.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectResource.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/NameBatchSerialization.h"
#include "Serialization/DeferredMessageLog.h"
#include "UObject/UObjectThreadContext.h"
#include "Misc/Paths.h"
#include "Misc/ExclusiveLoadPackageTimeTracker.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/ExceptionHandling.h"
#include "UObject/UObjectHash.h"
#include "Templates/UniquePtr.h"
#include "Serialization/BufferReader.h"
#include "Async/TaskGraphInterfaces.h"
#include "Blueprint/BlueprintSupport.h"
#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UObject/UObjectArchetypeInternal.h"
#include "UObject/GarbageCollectionInternal.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Serialization/LoadTimeTracePrivate.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Serialization/AsyncPackage.h"
#include "Serialization/UnversionedPropertySerialization.h"
#include "Serialization/Zenaphore.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectRedirector.h"
#include "Serialization/BulkData.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/MemoryReader.h"
#include "UObject/UObjectClusters.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Async/Async.h"
#include "HAL/LowLevelMemStats.h"

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
PRAGMA_DISABLE_OPTIMIZATION
#endif

FArchive& operator<<(FArchive& Ar, FMappedName& MappedName)
{
	Ar << MappedName.Index << MappedName.Number;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FContainerHeader& ContainerHeader)
{
	Ar << ContainerHeader.ContainerId;
	Ar << ContainerHeader.Names;
	Ar << ContainerHeader.NameHashes;
	Ar << ContainerHeader.PackageIds;
	Ar << ContainerHeader.PackageNames;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FExportBundleEntry& ExportBundleEntry)
{
	Ar << ExportBundleEntry.LocalExportIndex;
	Ar << ExportBundleEntry.CommandType;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FExportBundleHeader& ExportBundleHeader)
{
	Ar << ExportBundleHeader.FirstEntryIndex;
	Ar << ExportBundleHeader.EntryCount;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FExportBundleMetaEntry& ExportBundleMetaEntry)
{
	Ar << ExportBundleMetaEntry.LoadOrder;
	Ar << ExportBundleMetaEntry.PayloadSize;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FScriptObjectEntry& ScriptObjectEntry)
{
	Ar << ScriptObjectEntry.ObjectName.Index << ScriptObjectEntry.ObjectName.Number;
	Ar << ScriptObjectEntry.OuterIndex;
	Ar << ScriptObjectEntry.CDOClassIndex;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FExportMapEntry& ExportMapEntry)
{
	Ar << ExportMapEntry.CookedSerialOffset;
	Ar << ExportMapEntry.CookedSerialSize;
	Ar << ExportMapEntry.ObjectName;
	Ar << ExportMapEntry.OuterIndex;
	Ar << ExportMapEntry.ClassIndex;
	Ar << ExportMapEntry.SuperIndex;
	Ar << ExportMapEntry.TemplateIndex;
	Ar << ExportMapEntry.GlobalImportIndex;

	uint32 ObjectFlags = uint32(ExportMapEntry.ObjectFlags);
	Ar << ObjectFlags;
	
	if (Ar.IsLoading())
	{
		ExportMapEntry.ObjectFlags = EObjectFlags(ObjectFlags);
	}

	uint8 FilterFlags = uint8(ExportMapEntry.FilterFlags);
	Ar << FilterFlags;

	if (Ar.IsLoading())
	{
		ExportMapEntry.FilterFlags = EExportFilterFlags(FilterFlags);
	}

	uint64 Pad = 0;
	Ar.Serialize(&Pad, 7);

	return Ar;
}

#if WITH_ASYNCLOADING2

#ifndef ALT2_VERIFY_ASYNC_FLAGS
#define ALT2_VERIFY_ASYNC_FLAGS DO_CHECK
#endif

#ifndef ALT2_VERIFY_RECURSIVE_LOADS
#define ALT2_VERIFY_RECURSIVE_LOADS DO_CHECK
#endif

#ifndef ALT2_LOG_VERBOSE
#define ALT2_LOG_VERBOSE DO_CHECK
#endif

#define UE_ASYNC_PACKAGE_LOG(Verbosity, PackageDesc, LogDesc, Format, ...) \
if ((PackageDesc).Name != (PackageDesc).NameToLoad) \
{ \
	UE_LOG(LogStreaming, Verbosity, LogDesc TEXT(": %s (%d) %s (%d) - ") Format, \
		*(PackageDesc).Name.ToString(), \
		(PackageDesc).PackageId.ToIndexForDebugging(), \
		*(PackageDesc).NameToLoad.ToString(), \
		(PackageDesc).PackageIdToLoad.ToIndexForDebugging(), \
		##__VA_ARGS__); \
} \
else \
{ \
	UE_LOG(LogStreaming, Verbosity, LogDesc TEXT(": %s (%d) - ") Format, \
		*(PackageDesc).Name.ToString(), \
		(PackageDesc).PackageId.ToIndexForDebugging(), \
		##__VA_ARGS__); \
}

#define UE_ASYNC_PACKAGE_CLOG(Condition, Verbosity, PackageDesc, LogDesc, Format, ...) \
if ((Condition)) \
{ \
	UE_ASYNC_PACKAGE_LOG(Verbosity, PackageDesc, LogDesc, Format, ##__VA_ARGS__); \
}

#if ALT2_LOG_VERBOSE
#define UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbosity, PackageDesc, LogDesc, Format, ...) \
	UE_ASYNC_PACKAGE_LOG(Verbosity, PackageDesc, LogDesc, Format, ##__VA_ARGS__)
#define UE_ASYNC_PACKAGE_CLOG_VERBOSE(Condition, Verbosity, PackageDesc, LogDesc, Format, ...) \
	UE_ASYNC_PACKAGE_CLOG(Condition, Verbosity, PackageDesc, LogDesc, Format, ##__VA_ARGS__)
#else
#define UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbosity, PackageDesc, LogDesc, Format, ...)
#define UE_ASYNC_PACKAGE_CLOG_VERBOSE(Condition, Verbosity, PackageDesc, LogDesc, Format, ...)
#endif

TRACE_DECLARE_INT_COUNTER(PendingBundleIoRequests, TEXT("AsyncLoading/PendingBundleIoRequests"));

struct FAsyncPackage2;
class FAsyncLoadingThread2;

class FSimpleArchive final
	: public FArchive
{
public:
	FSimpleArchive(const uint8* BufferPtr, uint64 BufferSize)
	{
		ActiveFPLB->OriginalFastPathLoadBuffer = BufferPtr;
		ActiveFPLB->StartFastPathLoadBuffer = BufferPtr;
		ActiveFPLB->EndFastPathLoadBuffer = BufferPtr + BufferSize;
	}

	int64 TotalSize() override
	{
		return ActiveFPLB->EndFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer;
	}

	int64 Tell() override
	{
		return ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer;
	}

	void Seek(int64 Position) override
	{
		ActiveFPLB->StartFastPathLoadBuffer = ActiveFPLB->OriginalFastPathLoadBuffer + Position;
		check(ActiveFPLB->StartFastPathLoadBuffer <= ActiveFPLB->EndFastPathLoadBuffer);
	}

	void Serialize(void* Data, int64 Length) override
	{
		if (!Length || IsError())
		{
			return;
		}
		check(ActiveFPLB->StartFastPathLoadBuffer + Length <= ActiveFPLB->EndFastPathLoadBuffer);
		FMemory::Memcpy(Data, ActiveFPLB->StartFastPathLoadBuffer, Length);
		ActiveFPLB->StartFastPathLoadBuffer += Length;
	}
};

struct FExportObject
{
	UObject* Object = nullptr;
	bool bFiltered = false;
	bool bExportLoadFailed = false;
};

struct FAsyncPackageDesc2
{
	// A unique request id for each external call to LoadPackage
	int32 RequestID;
	// The PackageId is used as a key in AsyncPackageLookup to track active load requests,
	// which in turn is used for looking up packages for setting up serialized arcs (mostly post load dependencies).
	// The PackageId always corresponds to either Name or NameToLoad.
	// - For normal packages it is the same as PackageIdToLoad.
	// - For temp packages it is a "fake" unique package id for that specific temporary name.
	// - For localized packages it is the same as PackageIdToLoad,
	//   source package id cannot be used here, then serialized arcs would be wrong.
	FPackageId PackageId; 
	// The package id to load always represents an actual serialized package on disc.
	FPackageId PackageIdToLoad; 
	// The UPackage name used by the engine and game code for in memory and external communication.
	// - For normal packages it is the same as NameToLoad.
	// - For temp packages it is a temp name provided in the call to LoadPackage.
	// - For localized packages it is the source name of the localized package,
	//   so clients running different languages all use the same name.
	FName Name;
	// The package name of the actual serialized package on disc. Always corresponds to PackageIdToLoad.
	FName NameToLoad;
	/** Delegate called on completion of loading. This delegate can only be created and consumed on the game thread */
	TUniquePtr<FLoadPackageAsyncDelegate> PackageLoadedDelegate;

	FAsyncPackageDesc2(
		int32 InRequestID,
		FPackageId InPackageId,
		FPackageId InPackageIdToLoad,
		const FName& InName,
		const FName& InNameToLoad,
		TUniquePtr<FLoadPackageAsyncDelegate>&& InCompletionDelegate = TUniquePtr<FLoadPackageAsyncDelegate>())
		: RequestID(InRequestID)
		, PackageId(InPackageId)
		, PackageIdToLoad(InPackageIdToLoad)
		, Name(InName)
		, NameToLoad(InNameToLoad)
		, PackageLoadedDelegate(MoveTemp(InCompletionDelegate))
	{
	}

	/** This constructor does not modify the package loaded delegate as this is not safe outside the game thread */
	FAsyncPackageDesc2(const FAsyncPackageDesc2& OldPackage)
		: RequestID(OldPackage.RequestID)
		, PackageId(OldPackage.PackageId)
		, PackageIdToLoad(OldPackage.PackageIdToLoad)
		, Name(OldPackage.Name)
		, NameToLoad(OldPackage.NameToLoad)
	{
	}

	/** This constructor will explicitly copy the package loaded delegate and invalidate the old one */
	FAsyncPackageDesc2(const FAsyncPackageDesc2& OldPackage, TUniquePtr<FLoadPackageAsyncDelegate>&& InPackageLoadedDelegate)
		: FAsyncPackageDesc2(OldPackage)
	{
		PackageLoadedDelegate = MoveTemp(InPackageLoadedDelegate);
	}

#if DO_GUARD_SLOW
	~FAsyncPackageDesc2()
	{
		checkSlow(!PackageLoadedDelegate.IsValid() || IsInGameThread());
	}
#endif
};

using FExportObjects = TArray<FExportObject>;

class FGlobalImport
{
private:
	union
	{
		UObject* Object = nullptr;
		struct
		{
			int32 ObjectIndex;
			int32 SerialNumber;
		} WeakPointer;
	};

	TAtomic<int32> RefCount { 0 };
	bool bIsWeakPointer = false;

public:
	inline void AddRef()
	{
		++RefCount;
	}

	inline void ReleaseRef()
	{
		--RefCount;
	}

	inline int32 GetRefCount()
	{
		return RefCount;
	}

	UObject* GetObject()
	{
		if (bIsWeakPointer)
		{
			InternalMakeRaw();
		}
		return Object;
	}

	UObject* GetObjectIfRawPointer()
	{
		return bIsWeakPointer ? nullptr : Object;
	}

	void SetObject(UObject* InObject)
	{
#if DO_CHECK
		if (bIsWeakPointer)
		{
			InternalMakeRaw();
		}
		check(!Object || Object == InObject);
#endif
		Object = InObject;
		bIsWeakPointer = false;
	}

	void MakeWeak()
	{
		check(RefCount == 0);
		check(!bIsWeakPointer);
		int32 ObjectIndex = GUObjectArray.ObjectToIndex((UObjectBase*)Object);
		int32 SerialNumber = GUObjectArray.AllocateSerialNumber(ObjectIndex);
		WeakPointer.ObjectIndex = ObjectIndex;
		WeakPointer.SerialNumber = SerialNumber;
		bIsWeakPointer = true;
	}

private:
	void InternalMakeRaw()
	{
		check(bIsWeakPointer);
		const bool bEvenIfPendingKill = false;
		FUObjectItem* ObjectItem = GUObjectArray.IndexToValidObject(WeakPointer.ObjectIndex, bEvenIfPendingKill);
		int32 ActualSerialNumber = GUObjectArray.GetSerialNumber(WeakPointer.ObjectIndex);
		Object = (ObjectItem && ActualSerialNumber == WeakPointer.SerialNumber) ? (UObject*)ObjectItem->Object : nullptr;
		check(!Object || (!Object->IsUnreachable() && !Object->IsPendingKill()));
		bIsWeakPointer = false;
	}
};

class FNameMap
{
public:
	void LoadGlobal(FIoDispatcher& IoDispatcher)
	{
		check(NameEntries.Num() == 0);

		FIoChunkId NamesId = CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNames);
		FIoChunkId HashesId = CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNameHashes);

		FIoBatch Batch = IoDispatcher.NewBatch();
		FIoRequest NameRequest = Batch.Read(NamesId, FIoReadOptions());
		FIoRequest HashRequest = Batch.Read(HashesId, FIoReadOptions());
		Batch.Issue();

		ReserveNameBatch(	IoDispatcher.GetSizeForChunk(NamesId).ValueOrDie(),
							IoDispatcher.GetSizeForChunk(HashesId).ValueOrDie());

		Batch.Wait();

		FIoBuffer NameBuffer = NameRequest.GetResult().ConsumeValueOrDie();
		FIoBuffer HashBuffer = HashRequest.GetResult().ConsumeValueOrDie();

		Load(MakeArrayView(NameBuffer.Data(), NameBuffer.DataSize()), MakeArrayView(HashBuffer.Data(), HashBuffer.DataSize()), FMappedName::EType::Global);

		IoDispatcher.FreeBatch(Batch);
	}

	void Load(TArrayView<const uint8> NameBuffer, TArrayView<const uint8> HashBuffer, FMappedName::EType InNameMapType)
	{
		LoadNameBatch(NameEntries, NameBuffer, HashBuffer);
		NameMapType = InNameMapType;
	}

	FName GetName(const FMappedName& MappedName) const
	{
		check(MappedName.GetType() == NameMapType);
		FNameEntryId NameEntry = NameEntries[MappedName.GetIndex()];
		return FName::CreateFromDisplayId(NameEntry, MappedName.GetNumber());
	}

	FMinimalName GetMinimalName(const FMappedName& MappedName) const
	{
		check(MappedName.GetType() == NameMapType);
		FNameEntryId NameEntry = NameEntries[MappedName.GetIndex()];
		return FMinimalName(NameEntry, MappedName.GetNumber());
	}

	const TArray<FNameEntryId>& GetNameEntries() const
	{
		return NameEntries;
	}

private:
	TArray<FNameEntryId> NameEntries;
	FMappedName::EType NameMapType = FMappedName::EType::Global;
};

struct FGlobalImportStore
{
	TArray<UObject*> ScriptObjects;
	TArray<FGlobalImport> PublicExports;
	TArray<FScriptObjectEntry> ScriptObjectEntries;

	inline FName GetName(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsImport());

		return GlobalIndex.IsScriptImport() && ScriptObjectEntries.Num() > 0
			? MinimalNameToName(ScriptObjectEntries[GlobalIndex.GetIndex()].ObjectName)
			: NAME_None;
	}

	inline FGlobalImport& GetImport(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsPackageImport());
		FGlobalImport& Import = PublicExports[GlobalIndex.GetIndex()];
		return Import;
	}

	inline UObject* GetImportObject(FPackageObjectIndex GlobalIndex)
	{
		if (GlobalIndex.IsScriptImport())
		{
			return ScriptObjects[GlobalIndex.GetIndex()];
		}
		if (GlobalIndex.IsPackageImport())
		{
			return PublicExports[GlobalIndex.GetIndex()].GetObject();
		}
		check(false);
		return nullptr;
	}

	UObject* FindScriptImportObjectFromIndex(int32 ScriptImportIndex);

	FORCENOINLINE UObject* FindOrCreateScriptObject(int32 ScriptImportIndex)
	{
		UObject* Object = FindScriptImportObjectFromIndex(ScriptImportIndex);
		if (!Object)
		{
			const FScriptObjectEntry& Entry = ScriptObjectEntries[ScriptImportIndex];
			const FPackageObjectIndex& CDOClassIndex = Entry.CDOClassIndex;
			if (CDOClassIndex.IsScriptImport())
			{
				UObject* CDOClassObject = FindScriptImportObjectFromIndex(CDOClassIndex.GetIndex());
				if (CDOClassObject)
				{
					// UObjectLoadAllCompiledInDefaultProperties is creating CDOs from a flat list.
					// One CDO may call LoadObject which may depend on a CDO later in the list, then just create it here.
					// Recursive loads may be a problem, PostCDOConstruct exists as a workaround.
					UClass* Class = CastChecked<UClass>(CDOClassObject);
					UObject* CDO = Class->GetDefaultObject();
					(void)CDO;
					Object = FindScriptImportObjectFromIndex(ScriptImportIndex);
				}
			}
		}
		return Object;
	}

	inline UObject* FindOrGetImportObject(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsImport());
		UObject* Object = GetImportObject(GlobalIndex);
		if (!Object && GlobalIndex.IsScriptImport() && GIsInitialLoad)
		{
			return FindOrCreateScriptObject(GlobalIndex.GetIndex());
		}
		return Object;
	}

	void StoreGlobalObject(FPackageObjectIndex GlobalIndex, UObject* InObject)
	{
		if (GlobalIndex.IsScriptImport())
		{
			UObject*& Object = ScriptObjects[GlobalIndex.GetIndex()];
			check(!Object || Object == InObject);
			Object = InObject;
		}
		else if (GlobalIndex.IsPackageImport())
		{
			FGlobalImport& Import = PublicExports[GlobalIndex.GetIndex()];
			Import.SetObject(InObject);
		}
		else
		{
			check(GlobalIndex.IsNull());
		}
	}

	/** Reference tracking for GC management */
	TArray<UObject*> KeepAliveObjects;
	bool bNeedToHandleGarbageCollect = false;

	void OnPreGarbageCollect(bool bInIsLoadingPackages);
	void OnPostGarbageCollect();

	void FindAllScriptObjects();
};

class FPackageStore
{
public:
	FPackageStore(FIoDispatcher& InIoDispatcher, FNameMap& InGlobalNameMap)
		: IoDispatcher(InIoDispatcher)
		, GlobalNameMap(InGlobalNameMap) { }
	
	struct FLoadedContainer
	{
		TUniquePtr<FNameMap> ContainerNameMap;
		int32 Order;
		bool bValid = false;
	};

	FIoDispatcher& IoDispatcher;
	FNameMap& GlobalNameMap;
	TArray<FLoadedContainer> LoadedContainers;

	FCriticalSection PackageNameToPackageIdCritical;
	TMap<FName, FPackageId> PackageNameToPackageId;

	FCulturePackageMap CulturePackageMap;
	FSourceToLocalizedPackageIdMap* LocalizedPackageIdMap = nullptr;

	FGlobalImportStore ImportStore;
	FPackageStoreEntry* StoreEntries = nullptr;
	int32 PackageCount = 0;
	int32 ScriptArcsCount = 0;

public:
	void Load()
	{
		FIoBuffer IoBuffer;
		FEvent* Event = FPlatformProcess::GetSynchEventFromPool();

		FIoBuffer InitialLoadIoBuffer;
		FEvent* InitialLoadEvent = FPlatformProcess::GetSynchEventFromPool();

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreTocIo);

			IoDispatcher.ReadWithCallback(
				CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalMeta),
				FIoReadOptions(),
				[Event, &IoBuffer](TIoStatusOr<FIoBuffer> Result)
				{
			 		IoBuffer = Result.ConsumeValueOrDie();
					Event->Trigger();
				});

			IoDispatcher.ReadWithCallback(
				CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta),
				FIoReadOptions(),
				[InitialLoadEvent, &InitialLoadIoBuffer](TIoStatusOr<FIoBuffer> Result)
				{
					InitialLoadIoBuffer = Result.ConsumeValueOrDie();
					InitialLoadEvent->Trigger();
				});

			Event->Wait();
		}
		
		FLargeMemoryReader GlobalMetaAr(IoBuffer.Data(), IoBuffer.DataSize());
		
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreTocFixup);

			int32 PackageByteCount = 0;
			int32 PackageImportCount = 0;
			GlobalMetaAr << PackageCount;
			GlobalMetaAr << PackageImportCount;
			GlobalMetaAr << PackageByteCount;

			StoreEntries = reinterpret_cast<FPackageStoreEntry*>(FMemory::Malloc(PackageByteCount));

			// In-place loading
			GlobalMetaAr.Serialize(StoreEntries, PackageByteCount);

			ImportStore.PublicExports.AddDefaulted(PackageImportCount);

			// add 10% slack for temp package names
			{
				FScopeLock Lock(&PackageNameToPackageIdCritical);
				PackageNameToPackageId.Reserve(PackageCount + PackageCount / 10);
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreLocalization);

			GlobalMetaAr << CulturePackageMap;

			FString CurrentCultureName = FInternationalization::Get().GetCurrentCulture()->GetName();
			FParse::Value(FCommandLine::Get(), TEXT("CULTURE="), CurrentCultureName);

			LocalizedPackageIdMap = CulturePackageMap.Find(CurrentCultureName);
		}

		// Load initial loading meta data
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreInitialLoadIo);
			InitialLoadEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(InitialLoadEvent);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreInitialLoadFixup);
			FLargeMemoryReader InitialLoadArchive(InitialLoadIoBuffer.Data(), InitialLoadIoBuffer.DataSize());
			int32 NumScriptObjects = 0;
			InitialLoadArchive << NumScriptObjects;
			ImportStore.ScriptObjectEntries = MakeArrayView(reinterpret_cast<const FScriptObjectEntry*>(InitialLoadIoBuffer.Data() + InitialLoadArchive.Tell()), NumScriptObjects);

			for (FScriptObjectEntry& ScriptObjectEntry : ImportStore.ScriptObjectEntries)
			{
				const FMappedName& MappedName = FMappedName::FromMinimalName(ScriptObjectEntry.ObjectName);
				check(MappedName.IsGlobal());
				ScriptObjectEntry.ObjectName = GlobalNameMap.GetMinimalName(MappedName);
			}

			ImportStore.ScriptObjects.AddDefaulted(ImportStore.ScriptObjectEntries.Num());
		}

		FPackageName::DoesPackageExistOverride().BindLambda([this](FName InPackageName)
		{
			FScopeLock Lock(&PackageNameToPackageIdCritical);
			return PackageNameToPackageId.Contains(InPackageName);
		});

		LoadContainers(IoDispatcher.GetMountedContainers());
		IoDispatcher.OnContainerMounted().AddRaw(this, &FPackageStore::OnContainerMounted);
	}

	void LoadContainers(TArrayView<const FIoDispatcherMountedContainer> Containers)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadContainers);

		int32 ContainersToLoad = 0;

		int32 MaxContainerIndex = LoadedContainers.Num();
		for (const FIoDispatcherMountedContainer& Container : Containers)
		{
			const int32 ContainerIndex = Container.ContainerId.ToIndex();
			if (ContainerIndex != 0)
			{
				++ContainersToLoad;
				MaxContainerIndex = FMath::Max(MaxContainerIndex, ContainerIndex);
			}
		}

		if (!ContainersToLoad)
		{
			return;
		}

		const int32 NewContainerMapSize = MaxContainerIndex + 1;
		if (NewContainerMapSize > LoadedContainers.Num())
		{
			LoadedContainers.SetNum(NewContainerMapSize, false);
		}

		TAtomic<int32> Remaining(ContainersToLoad);

		FEvent* Event = FPlatformProcess::GetSynchEventFromPool();

		for (const FIoDispatcherMountedContainer& Container : Containers)
		{
			FIoContainerId ContainerId = Container.ContainerId;
			if (ContainerId.ToIndex() == 0)
			{
				continue;
			}

			FLoadedContainer& LoadedContainer = LoadedContainers[ContainerId.ToIndex()];
			if (LoadedContainer.bValid && LoadedContainer.Order >= Container.Environment.GetOrder())
			{
				UE_LOG(LogStreaming, Log, TEXT("Skipping loading mounted container ID '%d', already loaded with higher order"), ContainerId.ToIndex());
				if (--Remaining == 0)
				{
					Event->Trigger();
				}
				continue;
			}

			UE_LOG(LogStreaming, Log, TEXT("Loading mounted container ID '%d'"), ContainerId.ToIndex());
			LoadedContainer.bValid = true;
			LoadedContainer.Order = Container.Environment.GetOrder();

			FIoChunkId HeaderChunkId = CreateIoChunkId(0, ContainerId.ToIndex(), EIoChunkType::ContainerHeader);
			IoDispatcher.ReadWithCallback(HeaderChunkId, FIoReadOptions(), [this, &Remaining, Event, &LoadedContainer](TIoStatusOr<FIoBuffer> Result)
			{
				// Execution method Thread will run the async block synchronously when multithreading is NOT supported
				const EAsyncExecution ExecutionMethod = FPlatformProcess::SupportsMultithreading() ? EAsyncExecution::TaskGraph : EAsyncExecution::Thread;

				Async(ExecutionMethod, [this, &Remaining, Event, IoBuffer = Result.ConsumeValueOrDie(), &LoadedContainer]()
				{
					FMemoryReaderView Ar(MakeArrayView(IoBuffer.Data(), IoBuffer.DataSize()));

					FContainerHeader ContainerHeader;
					Ar << ContainerHeader;

					const bool bHasContainerLocalNameMap = ContainerHeader.Names.Num() > 0;
					if (bHasContainerLocalNameMap)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(LoadContainerNameMap);
						LoadedContainer.ContainerNameMap.Reset(new FNameMap());
						LoadedContainer.ContainerNameMap->Load(ContainerHeader.Names, ContainerHeader.NameHashes, FMappedName::EType::Container);
					}

					FNameMap& NameMap = bHasContainerLocalNameMap ? *LoadedContainer.ContainerNameMap : GlobalNameMap;

					{
						TRACE_CPUPROFILER_EVENT_SCOPE(AddPackages);
						FScopeLock Lock(&PackageNameToPackageIdCritical);

						int32 NameIndex = 0;
						for (FPackageId PackageId : ContainerHeader.PackageIds)
						{
							FName PackageName = NameMap.GetName(ContainerHeader.PackageNames[NameIndex++]);
							FPackageId& Id = PackageNameToPackageId.FindOrAdd(PackageName);
							if (!Id.IsValid())
							{
								Id = PackageId;
								StoreEntries[PackageId.ToIndex()].Name = NameToMinimalName(PackageName);
							}
						}
					}

					if (--Remaining == 0)
					{
						Event->Trigger();
					}
				});
			});
		}

		Event->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Event);

		ApplyLocalization();
	}

	void OnContainerMounted(const FIoDispatcherMountedContainer& Container)
	{
		LoadContainers(MakeArrayView(&Container, 1));
	}

	void ApplyLocalization()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApplyLocalization);

		if (!LocalizedPackageIdMap)
		{
			return;
		}

		// Mark already localized entries if this becomes a perfromance problem

		for (auto It = LocalizedPackageIdMap->CreateIterator(); It; ++It)
		{
			const FPackageStoreEntry& PackageEntry = StoreEntries[It.Key().ToIndex()];

			if (!FMappedName::IsResolvedToMinimalName(PackageEntry.Name))
			{
				// Not mounted yet
				continue;
			}

			const FName SourceName = MinimalNameToName(PackageEntry.Name);
			FScopeLock Lock(&PackageNameToPackageIdCritical);
			FPackageId* FoundPackageId = PackageNameToPackageId.Find(SourceName);

			UE_CLOG(!FoundPackageId, LogStreaming, Warning,
				TEXT("Skip remapping for localized package %s (%d) since the source package %s (%d) is unknown."),
				It.Value().ToIndex(),
				*FMappedName::SafeMinimalNameToName(StoreEntries[It.Value().ToIndex()].Name).ToString(),
				It.Key().ToIndex(),
				*SourceName.ToString());

			if (FoundPackageId)
			{
				const FPackageId LocalizedPackageId = It.Value();
				*FoundPackageId = LocalizedPackageId;

#if ALT2_LOG_VERBOSE
				UE_LOG(LogStreaming, Verbose, TEXT("LoadPackageStore: RemapLocalizedPackage: %s (%d) %s (%d)."),
					*FMappedName::SafeMinimalNameToName(StoreEntries[It.Key().ToIndex()].Name).ToString(),
					It.Key().ToIndex(),
					*FMappedName::SafeMinimalNameToName(StoreEntries[It.Value().ToIndex()].Name).ToString(),
					It.Value().ToIndex());
#endif
			}
		}

		for (int I = 0; I < PackageCount; ++I)
		{
			FPackageStoreEntry& StoreEntry = StoreEntries[I];

			if (!FMappedName::IsResolvedToMinimalName(StoreEntry.Name))
			{
				// Not mounted yet
				continue;
			}

			for (FPackageId& ImportedPackageIndex : StoreEntry.ImportedPackages)
			{
				if (FPackageId* Value = LocalizedPackageIdMap->Find(ImportedPackageIndex))
				{
					ImportedPackageIndex = *Value;
				}
			}
		}
	}

	void FinalizeInitialLoad()
	{
		ImportStore.FindAllScriptObjects();

		UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - InitialLoad Finalized: Script Objects: %d"),
			ImportStore.ScriptObjects.Num());
	}

	inline FGlobalImportStore& GetGlobalImportStore()
	{
		return ImportStore;
	}

	inline FPackageId FindPackageId(FName Name)
	{
		FScopeLock Lock(&PackageNameToPackageIdCritical);
		FPackageId* Id = PackageNameToPackageId.Find(Name);
		return Id ? *Id : FPackageId();
	}

	inline FPackageId FindOrAddPackageId(FName Name)
	{
		FScopeLock Lock(&PackageNameToPackageIdCritical);
		if (FPackageId* Id = PackageNameToPackageId.Find(Name))
		{
			return *Id;
		}

		FPackageId NewId = FPackageId::FromIndex(PackageNameToPackageId.Num());
		PackageNameToPackageId.Add(Name, NewId);
		return NewId;
	}

	inline FPackageStoreEntry* GetGlobalStoreEntries()
	{
		return StoreEntries;
	}

	inline const FPackageStoreEntry& GetStoreEntry(FPackageId PackageId) const
	{
		return StoreEntries[PackageId.ToIndex()];
	}

	inline const FNameMap& GetPackageNameMap(const uint16 NameMapIndex) const
	{
		return NameMapIndex > 0 ? *LoadedContainers[NameMapIndex].ContainerNameMap : GlobalNameMap;
	}
};

struct FPackageImportStore
{
	FPackageStore& GlobalPackageStore;
	FGlobalImportStore& GlobalImportStore;
	const FPackageStoreEntry& StoreEntry;
	const FPackageObjectIndex* ImportMap = nullptr;
	int32 ImportMapCount = 0;

	FPackageImportStore(FPackageStore& InGlobalPackageStore, const FPackageStoreEntry& InStoreEntry)
		: GlobalPackageStore(InGlobalPackageStore)
		, GlobalImportStore(GlobalPackageStore.ImportStore)
		, StoreEntry(InStoreEntry)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NewPackageImportStore);
		AddGlobalImportObjectReferences();
	}

	~FPackageImportStore()
	{
		check(!ImportMap);
	}

	inline UObject* GetImportObject(FPackageObjectIndex GlobalIndex)
	{
		return GlobalImportStore.GetImportObject(GlobalIndex);
	}

	inline bool IsValidLocalImportIndex(FPackageIndex LocalIndex)
	{
		check(ImportMap);
		return LocalIndex.IsImport() && LocalIndex.ToImport() < ImportMapCount;
	}

	inline UObject* FindOrGetImportObjectFromLocalIndex(FPackageIndex LocalIndex)
	{
		check(LocalIndex.IsImport());
		check(ImportMap);
		const int32 LocalImportIndex = LocalIndex.ToImport();
		check(LocalImportIndex < ImportMapCount);
		const FPackageObjectIndex GlobalIndex = ImportMap[LocalIndex.ToImport()];
		check(GlobalIndex.IsImport());
		return GlobalImportStore.FindOrGetImportObject(GlobalIndex);
	}

	inline UObject* FindOrGetImportObject(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsImport());
		return GlobalImportStore.FindOrGetImportObject(GlobalIndex);
	}

	inline FName GetNameFromLocalIndex(FPackageIndex LocalIndex)
	{
		check(LocalIndex.IsImport());
		check(ImportMap);
		const int32 LocalImportIndex = LocalIndex.ToImport();
		if (LocalImportIndex < ImportMapCount)
		{
			const FPackageObjectIndex GlobalIndex = ImportMap[LocalImportIndex];
			return GlobalImportStore.GetName(GlobalIndex);
		}
		return FName();
	}

	void StoreGlobalObject(FPackageObjectIndex GlobalIndex, UObject* InObject)
	{
		GlobalImportStore.StoreGlobalObject(GlobalIndex, InObject);
	}

	void ClearReferences()
	{
		ReleaseGlobalImportObjectReferences();
	}

private:
	void AddGlobalImportObjectReferences()
	{
		for (const FPackageId& GlobalPackageId : StoreEntry.ImportedPackages)
		{
			const FPackageStoreEntry& ImportEntry = GlobalPackageStore.GetStoreEntry(GlobalPackageId);
			for (const FPackageObjectIndex& GlobalImportIndex : ImportEntry.PublicExports)
			{
				GlobalImportStore.GetImport(GlobalImportIndex).AddRef();
			}
		}
		// Add ref counts to own imports to speed up OnPreGarbageCollect
		for (const FPackageObjectIndex& GlobalImportIndex : StoreEntry.PublicExports)
		{
			GlobalImportStore.GetImport(GlobalImportIndex).AddRef();
		}
	}

	void ReleaseGlobalImportObjectReferences()
	{
		for (const FPackageId& GlobalPackageId : StoreEntry.ImportedPackages)
		{
			const FPackageStoreEntry& ImportEntry = GlobalPackageStore.GetStoreEntry(GlobalPackageId);
			for (const FPackageObjectIndex& GlobalImportIndex : ImportEntry.PublicExports)
			{
				GlobalImportStore.GetImport(GlobalImportIndex).ReleaseRef();
			}
		}
		// Release ref counts to own imports to speed up OnPreGarbageCollect
		for (const FPackageObjectIndex& GlobalImportIndex : StoreEntry.PublicExports)
		{
			GlobalImportStore.GetImport(GlobalImportIndex).ReleaseRef();
		}
	}
};
	
class FExportArchive final : public FArchive
{
public:
	FExportArchive(const uint8* AllExportDataPtr, const uint8* CurrentExportPtr, uint64 AllExportDataSize)
	{
		ActiveFPLB->OriginalFastPathLoadBuffer = AllExportDataPtr;
		ActiveFPLB->StartFastPathLoadBuffer = CurrentExportPtr;
		ActiveFPLB->EndFastPathLoadBuffer = AllExportDataPtr + AllExportDataSize;
	}

	void ExportBufferBegin(uint64 InExportCookedFileSerialOffset, uint64 InExportSerialSize)
	{
		CookedSerialOffset = InExportCookedFileSerialOffset;
		BufferSerialOffset = (ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer);
		CookedSerialSize = InExportSerialSize;
	}

	void ExportBufferEnd()
	{
		CookedSerialOffset = 0;
		BufferSerialOffset = 0;
		CookedSerialSize = 0;
	}

	void CheckBufferPosition(const TCHAR* Text, uint64 Offset = 0)
	{
#if DO_CHECK
		const uint64 BufferPosition = (ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer) + Offset;
		const bool bIsInsideExportBuffer =
			(BufferSerialOffset <= BufferPosition) && (BufferPosition <= BufferSerialOffset + CookedSerialSize);

		UE_ASYNC_PACKAGE_CLOG(
			!bIsInsideExportBuffer,
			Error, *PackageDesc, TEXT("FExportArchive::InvalidPosition"),
			TEXT("%s: Position %llu is outside of the current export buffer (%lld,%lld)."),
			Text,
			BufferPosition,
			BufferSerialOffset, BufferSerialOffset + CookedSerialSize);
#endif
	}

	void Skip(int64 InBytes)
	{
		CheckBufferPosition(TEXT("InvalidSkip"), InBytes);
		ActiveFPLB->StartFastPathLoadBuffer += InBytes;
	}

	virtual int64 TotalSize() override
	{
		return CookedHeaderSize + (ActiveFPLB->EndFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer);
	}

	virtual int64 Tell() override
	{
		int64 CookedFilePosition = (ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer);
		CookedFilePosition -= BufferSerialOffset;
		CookedFilePosition += CookedSerialOffset;
		return CookedFilePosition;
	}

	virtual void Seek(int64 Position) override
	{
		uint64 BufferPosition = (uint64)Position;
		BufferPosition -= CookedSerialOffset;
		BufferPosition += BufferSerialOffset;
		ActiveFPLB->StartFastPathLoadBuffer = ActiveFPLB->OriginalFastPathLoadBuffer + BufferPosition;
		CheckBufferPosition(TEXT("InvalidSeek"));
	}

	virtual void Serialize(void* Data, int64 Length) override
	{
		if (!Length || ArIsError)
		{
			return;
		}
		CheckBufferPosition(TEXT("InvalidSerialize"), (uint64)Length);
		FMemory::Memcpy(Data, ActiveFPLB->StartFastPathLoadBuffer, Length);
		ActiveFPLB->StartFastPathLoadBuffer += Length;
	}

	void UsingCustomVersion(const FGuid& Key) override {};
	using FArchive::operator<<; // For visibility of the overloads we don't override

	//~ Begin FArchive::FArchiveUObject Interface
	virtual FArchive& operator<<(FSoftObjectPath& Value) override { return FArchiveUObject::SerializeSoftObjectPath(*this, Value); }
	virtual FArchive& operator<<(FWeakObjectPtr& Value) override { return FArchiveUObject::SerializeWeakObjectPtr(*this, Value); }
	//~ End FArchive::FArchiveUObject Interface

	//~ Begin FArchive::FLinkerLoad Interface
	UObject* GetArchetypeFromLoader(const UObject* Obj) { return TemplateForGetArchetypeFromLoader; }

	virtual bool AttachExternalReadDependency(FExternalReadCallback& ReadCallback) override
	{
		ExternalReadDependencies->Add(ReadCallback);
		return true;
	}

	FORCENOINLINE void HandleBadExportIndex(int32 ExportIndex, UObject*& Object)
	{
		UE_ASYNC_PACKAGE_LOG(Error, *PackageDesc, TEXT("HandleBadExportIndex"),
			TEXT("Index: %d/%d"), ExportIndex, ExportCount);

		Object = nullptr;
	}

	FORCENOINLINE void HandleBadImportIndex(int32 ImportIndex, UObject*& Object)
	{
		UE_ASYNC_PACKAGE_LOG(Error, *PackageDesc, TEXT("HandleBadImportIndex"),
			TEXT("ImportIndex: %d/%d"), ImportIndex, ImportStore->ImportMapCount);

		Object = nullptr;
	}

	virtual FArchive& operator<<( UObject*& Object ) override
	{
		FPackageIndex Index;
		FArchive& Ar = *this;
		Ar << Index;

		if (Index.IsNull())
		{
			Object = nullptr;
		}
		else if (Index.IsExport())
		{
			const int32 ExportIndex = Index.ToExport();
			if (ExportIndex < ExportCount)
			{
				Object = (*Exports)[ExportIndex].Object;

#if ALT2_LOG_VERBOSE
				const FExportMapEntry& Export = ExportMap[ExportIndex];
				FNameEntryId NameEntry = (*NameMap)[Export.ObjectName.GetIndex()];
				FName ObjectName = FName::CreateFromDisplayId(NameEntry, Export.ObjectName.GetNumber());
				UE_ASYNC_PACKAGE_CLOG_VERBOSE(!Object, VeryVerbose, *PackageDesc,
					TEXT("FExportArchive: Object"), TEXT("Export %s at index %d is null."),
					*ObjectName.ToString(), 
					ExportIndex);
#endif
			}
			else
			{
				HandleBadExportIndex(ExportIndex, Object);
			}
		}
		else
		{
			if (ImportStore->IsValidLocalImportIndex(Index))
			{
				Object = ImportStore->FindOrGetImportObjectFromLocalIndex(Index);

				UE_ASYNC_PACKAGE_CLOG_VERBOSE(!Object, Log, *PackageDesc,
					TEXT("FExportArchive: Object"), TEXT("Import %s at index %d is null"),
					*ImportStore->GetNameFromLocalIndex(Index).ToString(),
					Index.ToImport());
			}
			else
			{
				HandleBadImportIndex(Index.ToImport(), Object);
			}
		}
		return *this;
	}

	inline virtual FArchive& operator<<(FLazyObjectPtr& LazyObjectPtr) override
	{
		FArchive& Ar = *this;
		FUniqueObjectGuid ID;
		Ar << ID;
		LazyObjectPtr = ID;
		return Ar;
	}

	inline virtual FArchive& operator<<(FSoftObjectPtr& Value) override
	{
		FArchive& Ar = *this;
		FSoftObjectPath ID;
		ID.Serialize(Ar);
		Value = ID;
		return Ar;
	}

	FORCENOINLINE void HandleBadNameIndex(int32 NameIndex, FName& Name)
	{
		UE_ASYNC_PACKAGE_LOG(Error, *PackageDesc, TEXT("HandleBadNameIndex"),
			TEXT("Index: %d/%d"), NameIndex, NameMap->Num());

		Name = FName();
		SetCriticalError();
	}

	inline virtual FArchive& operator<<(FName& Name) override
	{
		FArchive& Ar = *this;
		int32 NameIndex;
		Ar << NameIndex;
		int32 Number = 0;
		Ar << Number;

		NameIndex = PackageNameMap[NameIndex];

		if (NameMap->IsValidIndex(NameIndex))
		{
			// if the name wasn't loaded (because it wasn't valid in this context)
			FNameEntryId MappedName = (*NameMap)[NameIndex];

			// simply create the name from the NameMap's name and the serialized instance number
			Name = FName::CreateFromDisplayId(MappedName, Number);
		}
		else
		{
			HandleBadNameIndex(NameIndex, Name);
		}
		return *this;
	}
	//~ End FArchive::FLinkerLoad Interface

private:
	friend FAsyncPackage2;

	UObject* TemplateForGetArchetypeFromLoader = nullptr;

	FAsyncPackageDesc2* PackageDesc = nullptr;
	FPackageImportStore* ImportStore = nullptr;
	TArray<FExternalReadCallback>* ExternalReadDependencies;
	const int32* PackageNameMap = nullptr;
	const TArray<FNameEntryId>* NameMap = nullptr;
	const FExportObjects* Exports = nullptr;
	const FExportMapEntry* ExportMap = nullptr;
	int32 ExportCount = 0;
	uint32 CookedHeaderSize = 0;
	uint64 CookedSerialOffset = 0;
	uint64 CookedSerialSize = 0;
	uint64 BufferSerialOffset = 0;
};

enum class EAsyncPackageLoadingState2 : uint8
{
	NewPackage,
	WaitingForSummary,
	ProcessNewImportsAndExports,
	PostLoad_Etc,
	PackageComplete,
};

class FEventLoadGraphAllocator;
struct FAsyncLoadEventSpec;
struct FAsyncLoadingThreadState2;

/** [EDL] Event Load Node */
class FEventLoadNode2
{
public:
	FEventLoadNode2(const FAsyncLoadEventSpec* InSpec, FAsyncPackage2* InPackage, int32 InImportOrExportIndex);
	void DependsOn(FEventLoadNode2* Other);
	void AddBarrier();
	void AddBarrier(int32 Count);
	void ReleaseBarrier();
	void Execute(FAsyncLoadingThreadState2& ThreadState);

	int32 GetBarrierCount()
	{
		return BarrierCount.Load();
	}

	bool IsDone()
	{
		return !!bDone.Load();
	}

private:
	void ProcessDependencies(FAsyncLoadingThreadState2& ThreadState);
	void Fire();

	union
	{
		FEventLoadNode2* SingleDependent;
		FEventLoadNode2** MultipleDependents;
	};
	uint32 DependenciesCount = 0;
	uint32 DependenciesCapacity = 0;
	TAtomic<int32> BarrierCount { 0 };
	TAtomic<uint8> DependencyWriterCount { 0 };
	TAtomic<uint8> bDone { 0 };
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	TAtomic<uint8> bFired { 0 };
#endif

	const FAsyncLoadEventSpec* Spec;
	FAsyncPackage2* Package;
	int32 ImportOrExportIndex;
};

class FAsyncLoadEventGraphAllocator
{
public:
	FEventLoadNode2* AllocNodes(uint32 Count)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(AllocNodes);
		SIZE_T Size = Count * sizeof(FEventLoadNode2);
		TotalNodeCount += Count;
		TotalAllocated += Size;
		return reinterpret_cast<FEventLoadNode2*>(FMemory::Malloc(Size));
	}

	void FreeNodes(FEventLoadNode2* Nodes, uint32 Count)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(FreeNodes);
		FMemory::Free(Nodes);
		SIZE_T Size = Count * sizeof(FEventLoadNode2);
		TotalAllocated -= Size;
		TotalNodeCount -= Count;
	}

	FEventLoadNode2** AllocArcs(uint32 Count)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(AllocArcs);
		SIZE_T Size = Count * sizeof(FEventLoadNode2*);
		TotalArcCount += Count;
		TotalAllocated += Size;
		return reinterpret_cast<FEventLoadNode2**>(FMemory::Malloc(Size));
	}

	void FreeArcs(FEventLoadNode2** Arcs, uint32 Count)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(FreeArcs);
		FMemory::Free(Arcs);
		SIZE_T Size = Count * sizeof(FEventLoadNode2*);
		TotalAllocated -= Size;
		TotalArcCount -= Count;
	}

	TAtomic<int64> TotalNodeCount { 0 };
	TAtomic<int64> TotalArcCount { 0 };
	TAtomic<int64> TotalAllocated { 0 };
};

class FAsyncLoadEventQueue2
{
public:
	FAsyncLoadEventQueue2();
	~FAsyncLoadEventQueue2();

	void SetZenaphore(FZenaphore* InZenaphore)
	{
		Zenaphore = InZenaphore;
	}

	bool PopAndExecute(FAsyncLoadingThreadState2& ThreadState);
	void Push(FEventLoadNode2* Node);

private:
	FZenaphore* Zenaphore = nullptr;
	TAtomic<uint64> Head { 0 };
	TAtomic<uint64> Tail { 0 };
	TAtomic<FEventLoadNode2*> Entries[524288];
};

struct FAsyncLoadEventSpec
{
	typedef EAsyncPackageState::Type(*FAsyncLoadEventFunc)(FAsyncPackage2*, int32);
	FAsyncLoadEventFunc Func = nullptr;
	FAsyncLoadEventQueue2* EventQueue = nullptr;
	bool bExecuteImmediately = false;
};

struct FAsyncLoadingThreadState2
	: public FTlsAutoCleanup
{
	static FAsyncLoadingThreadState2* Create(FAsyncLoadEventGraphAllocator& GraphAllocator, FIoDispatcher& IoDispatcher)
	{
		check(TlsSlot != 0);
		check(!FPlatformTLS::GetTlsValue(TlsSlot));
		FAsyncLoadingThreadState2* State = new FAsyncLoadingThreadState2(GraphAllocator, IoDispatcher);
		State->Register();
		FPlatformTLS::SetTlsValue(TlsSlot, State);
		return State;
	}

	static FAsyncLoadingThreadState2* Get()
	{
		check(TlsSlot != 0);
		return static_cast<FAsyncLoadingThreadState2*>(FPlatformTLS::GetTlsValue(TlsSlot));
	}

	FAsyncLoadingThreadState2(FAsyncLoadEventGraphAllocator& InGraphAllocator, FIoDispatcher& InIoDispatcher)
		: GraphAllocator(InGraphAllocator)
	{

	}

	void ProcessDeferredFrees()
	{
		if (DeferredFreeNodes.Num() || DeferredFreeArcs.Num())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessDeferredFrees);
			for (TTuple<FEventLoadNode2*, uint32>& DeferredFreeNode : DeferredFreeNodes)
			{
				GraphAllocator.FreeNodes(DeferredFreeNode.Get<0>(), DeferredFreeNode.Get<1>());
			}
			DeferredFreeNodes.Reset();
			for (TTuple<FEventLoadNode2**, uint32>& DeferredFreeArc : DeferredFreeArcs)
			{
				GraphAllocator.FreeArcs(DeferredFreeArc.Get<0>(), DeferredFreeArc.Get<1>());
			}
			DeferredFreeArcs.Reset();
		}
	}

	void SetTimeLimit(bool bInUseTimeLimit, double InTimeLimit)
	{
		bUseTimeLimit = bInUseTimeLimit;
		TimeLimit = InTimeLimit;
		StartTime = FPlatformTime::Seconds();
	}

	bool IsTimeLimitExceeded(const TCHAR* InLastTypeOfWorkPerformed = nullptr, UObject* InLastObjectWorkWasPerformedOn = nullptr)
	{
		bool bTimeLimitExceeded = false;

		if (bUseTimeLimit)
		{
			double CurrentTime = FPlatformTime::Seconds();
			bTimeLimitExceeded = CurrentTime - StartTime > TimeLimit;

			if (bTimeLimitExceeded && GWarnIfTimeLimitExceeded)
			{
				IsTimeLimitExceededPrint(StartTime, CurrentTime, LastTestTime, TimeLimit, InLastTypeOfWorkPerformed, InLastObjectWorkWasPerformedOn);
			}

			LastTestTime = CurrentTime;
		}

		if (!bTimeLimitExceeded)
		{
			bTimeLimitExceeded = IsGarbageCollectionWaiting();
			UE_CLOG(bTimeLimitExceeded, LogStreaming, Verbose, TEXT("Timing out async loading due to Garbage Collection request"));
		}

		return bTimeLimitExceeded;
	}

	bool UseTimeLimit()
	{
		return bUseTimeLimit;
	}

	FAsyncLoadEventGraphAllocator& GraphAllocator;
	TArray<TTuple<FEventLoadNode2*, uint32>> DeferredFreeNodes;
	TArray<TTuple<FEventLoadNode2**, uint32>> DeferredFreeArcs;
	TArray<FEventLoadNode2*> NodesToFire;
	FEventLoadNode2* CurrentEventNode = nullptr;
	bool bShouldFireNodes = true;
	bool bUseTimeLimit = false;
	double TimeLimit = 0.0;
	double StartTime = 0.0;
	double LastTestTime = -1.0;
	static uint32 TlsSlot;
};

uint32 FAsyncLoadingThreadState2::TlsSlot;

/**
* Structure containing intermediate data required for async loading of all exports of a package.
*/

struct FAsyncPackage2
{
	friend struct FScopedAsyncPackageEvent2;
	friend class FAsyncLoadingThread2;

	FAsyncPackage2(const FAsyncPackageDesc2& InDesc,
		FAsyncLoadingThread2& InAsyncLoadingThread,
		FAsyncLoadEventGraphAllocator& InGraphAllocator,
		const FAsyncLoadEventSpec* EventSpecs);
	virtual ~FAsyncPackage2();


	bool bAddedForDelete = false;

	void AddRef()
	{
		++RefCount;
	}

	void ReleaseRef()
	{
		check(RefCount > 0);
		if (--RefCount == 0)
		{
			GetPackageNode(EEventLoadNode2::Package_Delete)->ReleaseBarrier();
		}
	}

	void ClearImportedPackages();

	/** Marks a specific request as complete */
	void MarkRequestIDsAsComplete();

	/**
	 * @return Estimated load completion percentage.
	 */
	FORCEINLINE float GetLoadPercentage() const
	{
		return LoadPercentage;
	}

	/**
	 * @return Time load begun. This is NOT the time the load was requested in the case of other pending requests.
	 */
	double GetLoadStartTime() const;

	/**
	 * Returns the name of the package to load.
	 */
	FORCEINLINE const FName& GetPackageName() const
	{
		return Desc.Name;
	}

	FORCEINLINE FPackageId GetPackageId() const
	{
		return Desc.PackageId;
	}

	void AddCompletionCallback(TUniquePtr<FLoadPackageAsyncDelegate>&& Callback);

	FORCEINLINE UPackage* GetLinkerRoot() const
	{
		return LinkerRoot;
	}

	/** Returns true if the package has finished loading. */
	FORCEINLINE bool HasFinishedLoading() const
	{
		return bLoadHasFinished;
	}

	/** Returns true if loading has failed */
	FORCEINLINE bool HasLoadFailed() const
	{
		return bLoadHasFailed;
	}

	/** Adds new request ID to the existing package */
	void AddRequestID(int32 Id);

	/**
	* Cancel loading this package.
	*/
	void Cancel();

	void AddOwnedObjectFromCallback(UObject* Object, bool bSubObject)
	{
		if (bSubObject)
		{
			if (!OwnedObjects.Contains(Object))
			{
				OwnedObjects.Add(Object);
			}
		}
		else
		{
			check(!OwnedObjects.Contains(Object));
			OwnedObjects.Add(Object);
		}
	}

	void AddOwnedObject(UObject* Object, bool bForceAdd)
	{
		if (bForceAdd || !IsInAsyncLoadingThread())
		{
			check(!OwnedObjects.Contains(Object));
			OwnedObjects.Add(Object);
		}
		check(OwnedObjects.Contains(Object));
	}

	void AddOwnedObjectWithAsyncFlag(UObject* Object, bool bForceAdd)
	{
		AddOwnedObject(Object, bForceAdd);
		if (bForceAdd || IsInGameThread())
		{
			check(!Object->HasAnyInternalFlags(EInternalObjectFlags::Async));
			Object->SetInternalFlags(EInternalObjectFlags::Async);
		}
		check(Object->HasAnyInternalFlags(EInternalObjectFlags::Async));
	}

	void ClearOwnedObjects();

	/** Returns the UPackage wrapped by this, if it is valid */
	UPackage* GetLoadedPackage();

	/** Checks if all dependencies (imported packages) of this package have been fully loaded */
	bool AreAllDependenciesFullyLoaded(TSet<FPackageId>& VisitedPackages);

	/** Returs true if this package loaded objects that can create GC clusters */
	bool HasClusterObjects() const
	{
		return bHasClusterObjects;
	}

	/** Creates GC clusters from loaded objects */
	EAsyncPackageState::Type CreateClusters();

	void ImportPackagesRecursive();
	void StartLoading();

private:

	/** Checks if all dependencies (imported packages) of this package have been fully loaded */
	bool AreAllDependenciesFullyLoadedInternal(FAsyncPackage2* Package, TSet<FPackageId>& VisitedPackages, FPackageId& OutPackageId);

	TAtomic<int32> RefCount{ 0 };

	/** Basic information associated with this package */
	FAsyncPackageDesc2 Desc;
	/** Package store information associated with this package */
	const FPackageStoreEntry& StoreEntry;
	/** Package which is going to have its exports and imports loaded									*/
	UPackage*				LinkerRoot;
	/** Call backs called when we finished loading this package											*/
	using FCompletionCallback = TUniquePtr<FLoadPackageAsyncDelegate>;
	TArray<FCompletionCallback, TInlineAllocator<2>> CompletionCallbacks;
	/** Current bundle entry index in the current export bundle */
	int32						ExportBundleEntryIndex = 0;
	/** Current index into ExternalReadDependencies array used to spread wating for external reads over several frames			*/
	int32						ExternalReadIndex = 0;
	/** Current index into ObjLoaded array used to spread routing PostLoad over several frames			*/
	int32							PostLoadIndex;
	/** Current index into DeferredPostLoadObjects array used to spread routing PostLoad over several frames			*/
	int32						DeferredPostLoadIndex;
	/** Current index into DeferredFinalizeObjects array used to spread routing PostLoad over several frames			*/
	int32						DeferredFinalizeIndex;
	/** Current index into DeferredClusterObjects array used to spread routing CreateClusters over several frames			*/
	int32						DeferredClusterIndex;
	/** True if any export can be a cluster root */
	bool						bHasClusterObjects;
	/** True if our load has failed */
	bool						bLoadHasFailed;
	/** True if our load has finished */
	bool						bLoadHasFinished;
	/** True if this package was created by this async package */
	bool						bCreatedLinkerRoot;
	/** Time load begun. This is NOT the time the load was requested in the case of pending requests.	*/
	double						LoadStartTime;
	/** Estimated load percentage.																		*/
	float						LoadPercentage;

	/** List of all request handles */
	TArray<int32, TInlineAllocator<2>> RequestIDs;
	/** Number of times we recursed to load this package. */
	int32 ReentryCount;
	TArray<FAsyncPackage2*> ImportedAsyncPackages;
	/** List of OwnedObjects = Exports + UPackage + ObjectsCreatedFromExports */
	TArray<UObject*> OwnedObjects;
	/** Cached async loading thread object this package was created by */
	FAsyncLoadingThread2& AsyncLoadingThread;
	FAsyncLoadEventGraphAllocator& GraphAllocator;

	FEventLoadNode2* PackageNodes = nullptr;
	FEventLoadNode2* ExportBundleNodes = nullptr;
	uint32 ExportBundleNodeCount = 0;

	FIoBuffer IoBuffer;
	const uint8* CurrentExportDataPtr = nullptr;
	const uint8* AllExportDataPtr = nullptr;
	uint32 CookedHeaderSize = 0;

	// FZenLinkerLoad
	TArray<FExternalReadCallback> ExternalReadDependencies;
	int32 ExportCount = 0;
	const FExportMapEntry* ExportMap = nullptr;
	const int32* PackageNameMap = nullptr;
	FExportObjects Exports;
	FPackageImportStore ImportStore;
	const FNameMap* NameMap = nullptr;

	int32 ExportBundleCount = 0;
	const FExportBundleHeader* ExportBundles = nullptr;
	const FExportBundleEntry* ExportBundleEntries = nullptr;
public:

	FAsyncLoadingThread2& GetAsyncLoadingThread()
	{
		return AsyncLoadingThread;
	}

	FAsyncLoadEventGraphAllocator& GetGraphAllocator()
	{
		return GraphAllocator;
	}

	/** [EDL] Begin Event driven loader specific stuff */

	EAsyncPackageLoadingState2 AsyncPackageLoadingState;

	bool bHasImportedPackagesRecursive = false;

	bool bAllExportsSerialized;

	static EAsyncPackageState::Type Event_ProcessExportBundle(FAsyncPackage2* Package, int32 ExportBundleIndex);
	static EAsyncPackageState::Type Event_ExportsDone(FAsyncPackage2* Package, int32);
	static EAsyncPackageState::Type Event_PostLoad(FAsyncPackage2* Package, int32);
	static EAsyncPackageState::Type Event_Delete(FAsyncPackage2* Package, int32);

	void EventDrivenCreateExport(int32 LocalExportIndex);
	void EventDrivenSerializeExport(int32 LocalExportIndex, FExportArchive& Ar);

	UObject* EventDrivenIndexToObject(FPackageObjectIndex Index, bool bCheckSerialized);
	template<class T>
	T* CastEventDrivenIndexToObject(FPackageObjectIndex Index, bool bCheckSerialized)
	{
		UObject* Result = EventDrivenIndexToObject(Index, bCheckSerialized);
		if (!Result)
		{
			return nullptr;
		}
		return CastChecked<T>(Result);
	}

	FEventLoadNode2* GetPackageNode(EEventLoadNode2 Phase);
	FEventLoadNode2* GetExportBundleNode(EEventLoadNode2 Phase, uint32 ExportBundleIndex);
	FEventLoadNode2* GetNode(int32 NodeIndex);

	/** [EDL] End Event driven loader specific stuff */

	void CallCompletionCallbacks(EAsyncLoadingResult::Type LoadingResult);

	/**
	* Route PostLoad to deferred objects.
	*
	* @return true if we finished calling PostLoad on all loaded objects and no new ones were created, false otherwise
	*/
	EAsyncPackageState::Type PostLoadDeferredObjects();

private:
	void CreateNodes(const FAsyncLoadEventSpec* EventSpecs);
	void SetupSerializedArcs(const uint8* GraphData, uint64 GraphDataSize);

	/**
	 * Begin async loading process. Simulates parts of BeginLoad.
	 *
	 * Objects created during BeginAsyncLoad and EndAsyncLoad will have EInternalObjectFlags::AsyncLoading set
	 */
	void BeginAsyncLoad();
	/**
	 * End async loading process. Simulates parts of EndLoad(). FinishObjects
	 * simulates some further parts once we're fully done loading the package.
	 */
	void EndAsyncLoad();
	/**
	 * Create UPackage
	 *
	 * @return true
	 */
	void CreateUPackage(const FPackageSummary* PackageSummary);

	/**
	 * Route PostLoad to all loaded objects. This might load further objects!
	 *
	 * @return true if we finished calling PostLoad on all loaded objects and no new ones were created, false otherwise
	 */
	EAsyncPackageState::Type PostLoadObjects();
	/**
	 * Finish up objects and state, which means clearing the EInternalObjectFlags::AsyncLoading flag on newly created ones
	 *
	 * @return true
	 */
	EAsyncPackageState::Type FinishObjects();

	/**
	 * Finalizes external dependencies till time limit is exceeded
	 *
	 * @return Complete if all dependencies are finished, TimeOut otherwise
	 */
	enum EExternalReadAction { ExternalReadAction_Poll, ExternalReadAction_Wait };
	EAsyncPackageState::Type ProcessExternalReads(EExternalReadAction Action);

	/**
	* Updates load percentage stat
	*/
	void UpdateLoadPercentage();

public:

	/** Serialization context for this package */
	FUObjectSerializeContext* GetSerializeContext();
};

struct FScopedAsyncPackageEvent2
{
	/** Current scope package */
	FAsyncPackage2* Package;
	/** Outer scope package */
	FAsyncPackage2* PreviousPackage;

	FScopedAsyncPackageEvent2(FAsyncPackage2* InPackage);
	~FScopedAsyncPackageEvent2();
};

class FAsyncLoadingThreadWorker : private FRunnable
{
public:
	FAsyncLoadingThreadWorker(FAsyncLoadEventGraphAllocator& InGraphAllocator, FAsyncLoadEventQueue2& InEventQueue, FIoDispatcher& InIoDispatcher, FZenaphore& InZenaphore, TAtomic<int32>& InActiveWorkersCount)
		: Zenaphore(InZenaphore)
		, EventQueue(InEventQueue)
		, GraphAllocator(InGraphAllocator)
		, IoDispatcher(InIoDispatcher)
		, ActiveWorkersCount(InActiveWorkersCount)
	{
	}

	void StartThread();
	
	void StopThread()
	{
		bStopRequested = true;
		bSuspendRequested = true;
		Zenaphore.NotifyAll();
	}
	
	void SuspendThread()
	{
		bSuspendRequested = true;
		Zenaphore.NotifyAll();
	}
	
	void ResumeThread()
	{
		bSuspendRequested = false;
	}
	
	int32 GetThreadId() const
	{
		return ThreadId;
	}

private:
	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	virtual void Stop() override {};

	FZenaphore& Zenaphore;
	FAsyncLoadEventQueue2& EventQueue;
	FAsyncLoadEventGraphAllocator& GraphAllocator;
	FIoDispatcher& IoDispatcher;
	TAtomic<int32>& ActiveWorkersCount;
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStopRequested { false };
	TAtomic<bool> bSuspendRequested { false };
	int32 ThreadId = 0;
};

class FAsyncLoadingThread2 final
	: public FRunnable
	, public IAsyncPackageLoader
{
	friend struct FAsyncPackage2;
public:
	FAsyncLoadingThread2(FIoDispatcher& IoDispatcher);
	virtual ~FAsyncLoadingThread2();

private:
	/** Thread to run the worker FRunnable on */
	FRunnableThread* Thread;
	TAtomic<bool> bStopRequested { false };
	TAtomic<bool> bSuspendRequested { false };
	TArray<FAsyncLoadingThreadWorker> Workers;
	TAtomic<int32> ActiveWorkersCount { 0 };
	bool bWorkersSuspended = false;

	/** [ASYNC/GAME THREAD] true if the async thread is actually started. We don't start it until after we boot because the boot process on the game thread can create objects that are also being created by the loader */
	bool bThreadStarted = false;

#if ALT2_VERIFY_RECURSIVE_LOADS
	int32 LoadRecursionLevel = 0;
#endif

	/** [ASYNC/GAME THREAD] Event used to signal loading should be cancelled */
	FEvent* CancelLoadingEvent;
	/** [ASYNC/GAME THREAD] Event used to signal that the async loading thread should be suspended */
	FEvent* ThreadSuspendedEvent;
	/** [ASYNC/GAME THREAD] Event used to signal that the async loading thread has resumed */
	FEvent* ThreadResumedEvent;
	/** [ASYNC/GAME THREAD] List of queued packages to stream */
	TArray<FAsyncPackageDesc2*> QueuedPackages;
	/** [ASYNC/GAME THREAD] Package queue critical section */
	FCriticalSection QueueCritical;
	/** [ASYNC/GAME THREAD] Event used to signal there's queued packages to stream */
	TArray<FAsyncPackage2*> LoadedPackages;
	/** [ASYNC/GAME THREAD] Critical section for LoadedPackages list */
	FCriticalSection LoadedPackagesCritical;
	TArray<FAsyncPackage2*> LoadedPackagesToProcess;
	TArray<FAsyncPackage2*> PackagesToDelete;
	
	struct FQueuedFailedPackageCallback
	{
		FName PackageName;
		TUniquePtr<FLoadPackageAsyncDelegate> Callback;
	};
	TArray<FQueuedFailedPackageCallback> QueuedFailedPackageCallbacks;

	FCriticalSection AsyncPackagesCritical;
	TMap<FPackageId, FAsyncPackage2*> AsyncPackageLookup;

	TQueue<FAsyncPackage2*, EQueueMode::Mpsc> ExternalReadQueue;
	FThreadSafeCounter WaitingForIoBundleCounter;
	FThreadSafeCounter WaitingForPostLoadCounter;

	/** List of all pending package requests */
	TSet<int32> PendingRequests;
	/** Synchronization object for PendingRequests list */
	FCriticalSection PendingRequestsCritical;

	/** [ASYNC/GAME THREAD] Number of package load requests in the async loading queue */
	TAtomic<uint32> QueuedPackagesCounter { 0 };
	/** [ASYNC/GAME THREAD] Number of packages being loaded on the async thread and post loaded on the game thread */
	FThreadSafeCounter ExistingAsyncPackagesCounter;

	FThreadSafeCounter AsyncThreadReady;

	/** When cancelling async loading: list of package requests to cancel */
	TArray<FAsyncPackageDesc2*> QueuedPackagesToCancel;
	/** When cancelling async loading: list of packages to cancel */
	TSet<FAsyncPackage2*> PackagesToCancel;

	/** Async loading thread ID */
	uint32 AsyncLoadingThreadID;

	FThreadSafeCounter PackageRequestID;

	/** I/O Dispatcher */
	FIoDispatcher& IoDispatcher;

	FNameMap GlobalNameMap;
	FPackageStore GlobalPackageStore;

	struct FBundleIoRequest
	{
		bool operator<(const FBundleIoRequest& Other) const
		{
			return BundeOrder < Other.BundeOrder;
		}

		FAsyncPackage2* Package;
		uint32 BundeOrder;
		uint32 BundleSize;
	};
	TArray<FBundleIoRequest> WaitingIoRequests;
	uint64 PendingBundleIoRequestsTotalSize = 0;

public:

	//~ Begin FRunnable Interface.
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	//~ End FRunnable Interface

	/** Start the async loading thread */
	virtual void StartThread() override;

	/** [GC] Management of global import objects */
	void OnPreGarbageCollect();
	void OnPostGarbageCollect();

	/** [EDL] Event queue */
	FZenaphore AltZenaphore;
	TArray<FZenaphore> WorkerZenaphores;
	FAsyncLoadEventGraphAllocator GraphAllocator;
	FAsyncLoadEventQueue2 EventQueue;
	FAsyncLoadEventQueue2 AsyncEventQueue;
	FAsyncLoadEventQueue2 ProcessExportBundlesEventQueue;
	TArray<FAsyncLoadEventQueue2*> AltEventQueues;
	TArray<FAsyncLoadEventSpec> EventSpecs;

	/** True if multithreaded async loading is currently being used. */
	inline virtual bool IsMultithreaded() override
	{
		return bThreadStarted;
	}

	/** Sets the current state of async loading */
	void EnterAsyncLoadingTick()
	{
		AsyncLoadingTickCounter++;
	}

	void LeaveAsyncLoadingTick()
	{
		AsyncLoadingTickCounter--;
		check(AsyncLoadingTickCounter >= 0);
	}

	/** Gets the current state of async loading */
	bool GetIsInAsyncLoadingTick() const
	{
		return !!AsyncLoadingTickCounter;
	}

	/** Returns true if packages are currently being loaded on the async thread */
	inline virtual bool IsAsyncLoadingPackages() override
	{
		FPlatformMisc::MemoryBarrier();
		return QueuedPackagesCounter != 0 || ExistingAsyncPackagesCounter.GetValue() != 0;
	}

	/** Returns true this codes runs on the async loading thread */
	virtual bool IsInAsyncLoadThread() override
	{
		if (IsMultithreaded())
		{
			// We still need to report we're in async loading thread even if 
			// we're on game thread but inside of async loading code (PostLoad mostly)
			// to make it behave exactly like the non-threaded version
			uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
			if (CurrentThreadId == AsyncLoadingThreadID ||
				(IsInGameThread() && GetIsInAsyncLoadingTick()))
			{
				return true;
			}
			else
			{
				for (const FAsyncLoadingThreadWorker& Worker : Workers)
				{
					if (CurrentThreadId == Worker.GetThreadId())
					{
						return true;
					}
				}
			}
			return false;
		}
		else
		{
			return IsInGameThread() && GetIsInAsyncLoadingTick();
		}
	}

	/** Returns true if async loading is suspended */
	inline virtual bool IsAsyncLoadingSuspended() override
	{
		return bSuspendRequested;
	}

	virtual void NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObject) override;

	virtual void FireCompletedCompiledInImport(void* AsyncPackage, FPackageIndex Import) override;

	/**
	* [ASYNC THREAD] Finds an existing async package in the AsyncPackages by its name.
	*
	* @param PackageName async package name.
	* @return Pointer to the package or nullptr if not found
	*/
	FORCEINLINE FAsyncPackage2* FindAsyncPackage(const FName& PackageName)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FindAsyncPackage);
		FPackageId PackageId = GlobalPackageStore.FindPackageId(PackageName);
		if (PackageId.IsValid())
		{
			FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
			//checkSlow(IsInAsyncLoadThread());
			return AsyncPackageLookup.FindRef(PackageId);
		}
		return nullptr;
	}

	FORCEINLINE FAsyncPackage2* GetAsyncPackage(const FPackageId& PackageId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetAsyncPackage);
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
		//checkSlow(IsInAsyncLoadThread());
		return AsyncPackageLookup.FindRef(PackageId);
	}

	/**
	* [ASYNC THREAD] Inserts package to queue according to priority.
	*
	* @param PackageName - async package name.
	* @param InsertMode - Insert mode, describing how we insert this package into the request list
	*/
	void InsertPackage(FAsyncPackage2* Package, bool bReinsert = false);

	FAsyncPackage2* FindOrInsertPackage(FAsyncPackageDesc2* InDesc, bool& bInserted);

	/**
	* [ASYNC/GAME THREAD] Queues a package for streaming.
	*
	* @param Package package descriptor.
	*/
	void QueuePackage(FAsyncPackageDesc2& Package);

	/**
	* [ASYNC* THREAD] Loads all packages
	*
	* @param OutPackagesProcessed Number of packages processed in this call.
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type ProcessAsyncLoadingFromGameThread(int32& OutPackagesProcessed);

	/**
	* [GAME THREAD] Ticks game thread side of async loading.
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type TickAsyncLoadingFromGameThread(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, int32 FlushRequestID = INDEX_NONE);

	/**
	* [ASYNC THREAD] Main thread loop
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	*/
	EAsyncPackageState::Type TickAsyncThreadFromGameThread(bool& bDidSomething);

	/** Initializes async loading thread */
	virtual void InitializeLoading() override;

	virtual void ShutdownLoading() override;

	virtual int32 LoadPackage(
		const FString& InPackageName,
		const FGuid* InGuid,
		const TCHAR* InPackageToLoadFrom,
		FLoadPackageAsyncDelegate InCompletionDelegate,
		EPackageFlags InPackageFlags,
		int32 InPIEInstanceID,
		int32 InPackagePriority) override;

	EAsyncPackageState::Type ProcessLoadingFromGameThread(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit);

	inline virtual EAsyncPackageState::Type ProcessLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit) override
	{
		return ProcessLoadingFromGameThread(bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
	}

	EAsyncPackageState::Type ProcessLoadingUntilCompleteFromGameThread(TFunctionRef<bool()> CompletionPredicate, float TimeLimit);

	inline virtual EAsyncPackageState::Type ProcessLoadingUntilComplete(TFunctionRef<bool()> CompletionPredicate, float TimeLimit) override
	{
		return ProcessLoadingUntilCompleteFromGameThread(CompletionPredicate, TimeLimit);
	}

	virtual void CancelLoading() override;

	virtual void SuspendLoading() override;

	virtual void ResumeLoading() override;

	virtual void FlushLoading(int32 PackageId) override;

	virtual int32 GetNumAsyncPackages() override
	{
		FPlatformMisc::MemoryBarrier();
		return ExistingAsyncPackagesCounter.GetValue();
	}

	/**
	 * [GAME THREAD] Gets the load percentage of the specified package
	 * @param PackageName Name of the package to return async load percentage for
	 * @return Percentage (0-100) of the async package load or -1 of package has not been found
	 */
	virtual float GetAsyncLoadPercentage(const FName& PackageName) override;

	/**
	 * [ASYNC/GAME THREAD] Checks if a request ID already is added to the loading queue
	 */
	bool ContainsRequestID(int32 RequestID)
	{
		FScopeLock Lock(&PendingRequestsCritical);
		return PendingRequests.Contains(RequestID);
	}

	/**
	 * [ASYNC/GAME THREAD] Adds a request ID to the list of pending requests
	 */
	void AddPendingRequest(int32 RequestID)
	{
		FScopeLock Lock(&PendingRequestsCritical);
		PendingRequests.Add(RequestID);
	}

	/**
	 * [ASYNC/GAME THREAD] Removes a request ID from the list of pending requests
	 */
	void RemovePendingRequests(TArray<int32, TInlineAllocator<2>>& RequestIDs)
	{
		FScopeLock Lock(&PendingRequestsCritical);
		for (int32 ID : RequestIDs)
		{
			PendingRequests.Remove(ID);
			TRACE_LOADTIME_END_REQUEST(ID);
		}
	}

private:

	void SuspendWorkers();
	void ResumeWorkers();

	/**
	* [GAME THREAD] Performs game-thread specific operations on loaded packages (not-thread-safe PostLoad, callbacks)
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type ProcessLoadedPackagesFromGameThread(bool& bDidSomething, int32 FlushRequestID = INDEX_NONE);

	bool CreateAsyncPackagesFromQueue();
	void AddBundleIoRequest(FAsyncPackage2* Package, const FExportBundleMetaEntry& BundleMetaEntry);
	void BundleIoRequestCompleted(const FExportBundleMetaEntry& BundleMetaEntry);
	void StartBundleIoRequests();

	FAsyncPackage2* CreateAsyncPackage(const FAsyncPackageDesc2& Desc)
	{
		if (Desc.PackageIdToLoad.ToIndex() < (uint32)GlobalPackageStore.PackageCount)
		{
			return new FAsyncPackage2(Desc, *this, GraphAllocator, EventSpecs.GetData());
		}
		else
		{
			return nullptr;
		}
	}

	/**
	* [ASYNC THREAD] Adds a package to a list of packages that have finished loading on the async thread
	*/
	void AddToLoadedPackages(FAsyncPackage2* Package);

	/** Number of times we re-entered the async loading tick, mostly used by singlethreaded ticking. Debug purposes only. */
	int32 AsyncLoadingTickCounter;
};

/**
 * Updates FUObjectThreadContext with the current package when processing it.
 * FUObjectThreadContext::AsyncPackage is used by NotifyConstructedDuringAsyncLoading.
 */
struct FAsyncPackageScope2
{
	/** Outer scope package */
	void* PreviousPackage;
	/** Cached ThreadContext so we don't have to access it again */
	FUObjectThreadContext& ThreadContext;

	FAsyncPackageScope2(void* InPackage)
		: ThreadContext(FUObjectThreadContext::Get())
	{
		PreviousPackage = ThreadContext.AsyncPackage;
		ThreadContext.AsyncPackage = InPackage;
	}
	~FAsyncPackageScope2()
	{
		ThreadContext.AsyncPackage = PreviousPackage;
	}
};

/** Just like TGuardValue for FAsyncLoadingThread::AsyncLoadingTickCounter but only works for the game thread */
struct FAsyncLoadingTickScope2
{
	FAsyncLoadingThread2& AsyncLoadingThread;
	bool bNeedsToLeaveAsyncTick;

	FAsyncLoadingTickScope2(FAsyncLoadingThread2& InAsyncLoadingThread)
		: AsyncLoadingThread(InAsyncLoadingThread)
		, bNeedsToLeaveAsyncTick(false)
	{
		if (IsInGameThread())
		{
			AsyncLoadingThread.EnterAsyncLoadingTick();
			bNeedsToLeaveAsyncTick = true;
		}
	}
	~FAsyncLoadingTickScope2()
	{
		if (bNeedsToLeaveAsyncTick)
		{
			AsyncLoadingThread.LeaveAsyncLoadingTick();
		}
	}
};

void FAsyncLoadingThread2::InitializeLoading()
{
#if USE_NEW_BULKDATA
	FBulkDataBase::SetIoDispatcher(&IoDispatcher);
#endif

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadGlobalNameMap);
		GlobalNameMap.LoadGlobal(IoDispatcher);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStore);
		GlobalPackageStore.Load();
	}

	AsyncThreadReady.Increment();

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - Initialized: Packages: %d, PublicExports: %d, ScriptObjects %d, FNames: %d"),
		GlobalPackageStore.PackageCount,
		GlobalPackageStore.ImportStore.PublicExports.Num(),
		GlobalPackageStore.ImportStore.ScriptObjects.Num(),
		GlobalNameMap.GetNameEntries().Num());
}

void FAsyncLoadingThread2::QueuePackage(FAsyncPackageDesc2& Package)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(QueuePackage);
	{
		FScopeLock QueueLock(&QueueCritical);
		++QueuedPackagesCounter;
		QueuedPackages.Add(new FAsyncPackageDesc2(Package, MoveTemp(Package.PackageLoadedDelegate)));
	}
	AltZenaphore.NotifyOne();
}

FAsyncPackage2* FAsyncLoadingThread2::FindOrInsertPackage(FAsyncPackageDesc2* Desc, bool& bInserted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindOrInsertPackage);
	FAsyncPackage2* Package = nullptr;
	bInserted = false;
	{
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
		Package = AsyncPackageLookup.FindRef(Desc->PackageId);
		if (!Package)
		{
			Package = CreateAsyncPackage(*Desc);
			if (!Package)
			{
				return nullptr;
			}
			Package->AddRef();
			ExistingAsyncPackagesCounter.Increment();
			AsyncPackageLookup.Add(Desc->PackageId, Package);
			bInserted = true;
		}
		else if (Desc->RequestID > 0)
		{
			Package->AddRequestID(Desc->RequestID);
		}
		if (Desc->PackageLoadedDelegate.IsValid())
		{
			Package->AddCompletionCallback(MoveTemp(Desc->PackageLoadedDelegate));
		}
	}
	return Package;
}

bool FAsyncLoadingThread2::CreateAsyncPackagesFromQueue()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateAsyncPackagesFromQueue);
	
	FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
	bool bPackagesCreated = false;
	const int32 TimeSliceGranularity = ThreadState.UseTimeLimit() ? 4 : MAX_int32;
	TArray<FAsyncPackageDesc2*> QueueCopy;

	do
	{
		{
			QueueCopy.Reset();
			FScopeLock QueueLock(&QueueCritical);

			const int32 NumPackagesToCopy = FMath::Min(TimeSliceGranularity, QueuedPackages.Num());
			if (NumPackagesToCopy > 0)
			{
				QueueCopy.Append(QueuedPackages.GetData(), NumPackagesToCopy);
				QueuedPackages.RemoveAt(0, NumPackagesToCopy, false);
			}
			else
			{
				break;
			}
		}

		for (FAsyncPackageDesc2* PackageDesc : QueueCopy)
		{
			bool bInserted;
			FAsyncPackage2* Package = FindOrInsertPackage(PackageDesc, bInserted);

			if (bInserted)
			{
				UE_ASYNC_PACKAGE_LOG(Verbose, *PackageDesc, TEXT("CreateAsyncPackages: AddPackage"),
					TEXT("Start loading package."));
			}
			else if (!Package)
			{
				UE_ASYNC_PACKAGE_LOG(Warning, *PackageDesc, TEXT("CreateAsyncPackages: SkipPackage"),
					TEXT("Skipping unknown package, probably a temp package that has already been completely loaded"));
			}
			else
			{
				UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbose, *PackageDesc, TEXT("CreateAsyncPackages: UpdatePackage"),
					TEXT("Package is alreay being loaded."));
			}

			--QueuedPackagesCounter;
			if (Package)
			{
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(ImportPackages);
					Package->ImportPackagesRecursive();
				}

				if (bInserted)
				{
					Package->StartLoading();
				}

				StartBundleIoRequests();
			}
			delete PackageDesc;
		}

		bPackagesCreated |= QueueCopy.Num() > 0;
	} while (!ThreadState.IsTimeLimitExceeded(TEXT("CreateAsyncPackagesFromQueue")));

	return bPackagesCreated;
}

void FAsyncLoadingThread2::AddBundleIoRequest(FAsyncPackage2* Package, const FExportBundleMetaEntry& BundleMetaEntry)
{
	WaitingForIoBundleCounter.Increment();
	WaitingIoRequests.HeapPush({ Package, BundleMetaEntry.LoadOrder, BundleMetaEntry.PayloadSize });
}

void FAsyncLoadingThread2::BundleIoRequestCompleted(const FExportBundleMetaEntry& BundleMetaEntry)
{
	check(PendingBundleIoRequestsTotalSize >= BundleMetaEntry.PayloadSize)
	PendingBundleIoRequestsTotalSize -= BundleMetaEntry.PayloadSize;
	if (WaitingIoRequests.Num())
	{
		StartBundleIoRequests();
	}
}

void FAsyncLoadingThread2::StartBundleIoRequests()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StartBundleIoRequests);
	constexpr uint64 MaxPendingRequestsSize = 256 << 20;
	FAsyncPackage2* PreviousPackage = nullptr;
	while (WaitingIoRequests.Num())
	{
		FBundleIoRequest& BundleIoRequest = WaitingIoRequests.HeapTop();
		FAsyncPackage2* Package = BundleIoRequest.Package;
		check(Package);
		if (PendingBundleIoRequestsTotalSize > 0 && PendingBundleIoRequestsTotalSize + BundleIoRequest.BundleSize > MaxPendingRequestsSize)
		{
			break;
		}
		PendingBundleIoRequestsTotalSize += BundleIoRequest.BundleSize;
		WaitingIoRequests.HeapPop(BundleIoRequest, false);

		if (GIsInitialLoad && PreviousPackage)
		{
			Package->GetExportBundleNode(ExportBundle_Process, 0)->DependsOn(PreviousPackage->GetExportBundleNode(ExportBundle_Process, 0));
		}
		PreviousPackage = Package;

		FIoReadOptions ReadOptions;
		IoDispatcher.ReadWithCallback(CreateIoChunkId(Package->Desc.PackageIdToLoad.ToIndex(), 0, EIoChunkType::ExportBundleData),
			ReadOptions,
			[Package](TIoStatusOr<FIoBuffer> Result)
		{
			if (Result.IsOk())
			{
				Package->IoBuffer = Result.ConsumeValueOrDie();
			}
			else
			{
				UE_LOG(LogStreaming, Error, TEXT("Failed reading chunk for package %s [%s]"), *Package->Desc.NameToLoad.ToString(), *Result.Status().ToString());
				Package->bLoadHasFailed = true;
			}
			Package->GetExportBundleNode(EEventLoadNode2::ExportBundle_Process, 0)->ReleaseBarrier();
			Package->AsyncLoadingThread.WaitingForIoBundleCounter.Decrement();
		});
		TRACE_COUNTER_DECREMENT(PendingBundleIoRequests);
	}
}

FEventLoadNode2::FEventLoadNode2(const FAsyncLoadEventSpec* InSpec, FAsyncPackage2* InPackage, int32 InImportOrExportIndex)
	: Spec(InSpec)
	, Package(InPackage)
	, ImportOrExportIndex(InImportOrExportIndex)
{
	check(Spec);
	check(Package);
}

void FEventLoadNode2::DependsOn(FEventLoadNode2* Other)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DependsOn);
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	check(!bDone);
	check(!bFired);
#endif
	uint8 Expected = 0;
	while (!Other->DependencyWriterCount.CompareExchange(Expected, 1))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DependsOnContested);
		check(Expected == 1);
		Expected = 0;
	}
	if (!Other->bDone.Load())
	{
		++BarrierCount;
		if (Other->DependenciesCount == 0)
		{
			Other->SingleDependent = this;
			Other->DependenciesCount = 1;
		}
		else
		{
			if (Other->DependenciesCount == 1)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(DependsOnAlloc);
				FEventLoadNode2* FirstDependency = Other->SingleDependent;
				uint32 NewDependenciesCapacity = 4;
				Other->DependenciesCapacity = NewDependenciesCapacity;
				Other->MultipleDependents = Package->GetGraphAllocator().AllocArcs(NewDependenciesCapacity);
				Other->MultipleDependents[0] = FirstDependency;
			}
			else if (Other->DependenciesCount == Other->DependenciesCapacity)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(DependsOnRealloc);
				FEventLoadNode2** OriginalDependents = Other->MultipleDependents;
				uint32 OldDependenciesCapcity = Other->DependenciesCapacity;
				SIZE_T OldDependenciesSize = OldDependenciesCapcity * sizeof(FEventLoadNode2*);
				uint32 NewDependenciesCapacity = OldDependenciesCapcity * 2;
				Other->DependenciesCapacity = NewDependenciesCapacity;
				Other->MultipleDependents = Package->GetGraphAllocator().AllocArcs(NewDependenciesCapacity);
				FMemory::Memcpy(Other->MultipleDependents, OriginalDependents, OldDependenciesSize);
				Package->GetGraphAllocator().FreeArcs(OriginalDependents, OldDependenciesCapcity);
			}
			Other->MultipleDependents[Other->DependenciesCount++] = this;
		}
	}
	Other->DependencyWriterCount.Store(0);
}

void FEventLoadNode2::AddBarrier()
{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	check(!bDone);
	check(!bFired);
#endif
	++BarrierCount;
}

void FEventLoadNode2::AddBarrier(int32 Count)
{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	check(!bDone);
	check(!bFired);
#endif
	BarrierCount += Count;
}

void FEventLoadNode2::ReleaseBarrier()
{
	check(BarrierCount > 0);
	if (--BarrierCount == 0)
	{
		Fire();
	}
}

void FEventLoadNode2::Fire()
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(Fire);

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	bFired.Store(1);
#endif

	FAsyncLoadingThreadState2* ThreadState = FAsyncLoadingThreadState2::Get();
	if (Spec->bExecuteImmediately && ThreadState && !ThreadState->CurrentEventNode)
	{
		Execute(*ThreadState);
	}
	else
	{
		Spec->EventQueue->Push(this);
	}
}

void FEventLoadNode2::Execute(FAsyncLoadingThreadState2& ThreadState)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteEvent);
	check(BarrierCount.Load() == 0);
	check(!ThreadState.CurrentEventNode || ThreadState.CurrentEventNode == this);

	ThreadState.CurrentEventNode = this;
	EAsyncPackageState::Type State = Spec->Func(Package, ImportOrExportIndex);
	if (State == EAsyncPackageState::Complete)
	{
		ThreadState.CurrentEventNode = nullptr;
		bDone.Store(1);
		ProcessDependencies(ThreadState);
	}
}

void FEventLoadNode2::ProcessDependencies(FAsyncLoadingThreadState2& ThreadState)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessDependencies);
	if (DependencyWriterCount.Load() != 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ConcurrentWriter);
		do
		{
			FPlatformProcess::Sleep(0);
		} while (DependencyWriterCount.Load() != 0);
	}

	if (DependenciesCount == 1)
	{
		check(SingleDependent->BarrierCount > 0);
		if (--SingleDependent->BarrierCount == 0)
		{
			ThreadState.NodesToFire.Push(SingleDependent);
		}
	}
	else if (DependenciesCount != 0)
	{
		FEventLoadNode2** Current = MultipleDependents;
		FEventLoadNode2** End = MultipleDependents + DependenciesCount;
		for (; Current < End; ++Current)
		{
			FEventLoadNode2* Dependent = *Current;
			check(Dependent->BarrierCount > 0);
			if (--Dependent->BarrierCount == 0)
			{
				ThreadState.NodesToFire.Push(Dependent);
			}
		}
		ThreadState.DeferredFreeArcs.Add(MakeTuple(MultipleDependents, DependenciesCapacity));
	}
	if (ThreadState.bShouldFireNodes)
	{
		ThreadState.bShouldFireNodes = false;
		while (ThreadState.NodesToFire.Num())
		{
			ThreadState.NodesToFire.Pop(false)->Fire();
		}
		ThreadState.bShouldFireNodes = true;
	}
}

FAsyncLoadEventQueue2::FAsyncLoadEventQueue2()
{
	FMemory::Memzero(Entries, sizeof(Entries));
}

FAsyncLoadEventQueue2::~FAsyncLoadEventQueue2()
{
}

void FAsyncLoadEventQueue2::Push(FEventLoadNode2* Node)
{
	uint64 LocalHead = Head.IncrementExchange();
	FEventLoadNode2* Expected = nullptr;
	if (!Entries[LocalHead % UE_ARRAY_COUNT(Entries)].CompareExchange(Expected, Node))
	{
		*(volatile int*)0 = 0; // queue is full: TODO
	}
	if (Zenaphore)
	{
		Zenaphore->NotifyOne();
	}
}

bool FAsyncLoadEventQueue2::PopAndExecute(FAsyncLoadingThreadState2& ThreadState)
{
	if (ThreadState.CurrentEventNode)
	{
		check(!ThreadState.CurrentEventNode->IsDone());
		ThreadState.CurrentEventNode->Execute(ThreadState);
		return true;
	}

	FEventLoadNode2* Node = nullptr;
	{
		uint64 LocalHead = Head.Load();
		uint64 LocalTail = Tail.Load();
		for (;;)
		{
			if (LocalTail >= LocalHead)
			{
				break;
			}
			if (Tail.CompareExchange(LocalTail, LocalTail + 1))
			{
				while (!Node)
				{
					Node = Entries[LocalTail % UE_ARRAY_COUNT(Entries)].Exchange(nullptr);
				}
				break;
			}
		}
	}

	if (Node)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(Execute);
		Node->Execute(ThreadState);
		return true;
	}
	else
	{
		return false;
	}
}

FScopedAsyncPackageEvent2::FScopedAsyncPackageEvent2(FAsyncPackage2* InPackage)
	:Package(InPackage)
{
	check(Package);

	// Update the thread context with the current package. This is used by NotifyConstructedDuringAsyncLoading.
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	PreviousPackage = static_cast<FAsyncPackage2*>(ThreadContext.AsyncPackage);
	ThreadContext.AsyncPackage = Package;

	Package->BeginAsyncLoad();
}

FScopedAsyncPackageEvent2::~FScopedAsyncPackageEvent2()
{
	Package->EndAsyncLoad();

	// Restore the package from the outer scope
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	ThreadContext.AsyncPackage = PreviousPackage;
}

void FAsyncLoadingThreadWorker::StartThread()
{
	Thread = FRunnableThread::Create(this, TEXT("FAsyncLoadingThreadWorker"), 0, TPri_Normal);
	ThreadId = Thread->GetThreadID();
	TRACE_SET_THREAD_GROUP(ThreadId, "AsyncLoading");
}

uint32 FAsyncLoadingThreadWorker::Run()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);

	FPlatformProcess::SetThreadAffinityMask(FPlatformAffinity::GetAsyncLoadingThreadMask());
	FMemory::SetupTLSCachesOnCurrentThread();

	FAsyncLoadingThreadState2::Create(GraphAllocator, IoDispatcher);

	FZenaphoreWaiter Waiter(Zenaphore, TEXT("WaitForEvents"));

	FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();

	bool bSuspended = false;
	while (!bStopRequested)
	{
		if (bSuspended)
		{
			if (!bSuspendRequested.Load(EMemoryOrder::SequentiallyConsistent))
			{
				bSuspended = false;
			}
			else
			{
				FPlatformProcess::Sleep(0.001f);
			}
		}
		else
		{
			bool bDidSomething = false;
			{
				FGCScopeGuard GCGuard;
				TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
				++ActiveWorkersCount;
				do
				{
					bDidSomething = EventQueue.PopAndExecute(ThreadState);
					
					if (bSuspendRequested.Load(EMemoryOrder::Relaxed))
					{
						bSuspended = true;
						bDidSomething = true;
						break;
					}
				} while (bDidSomething);
				--ActiveWorkersCount;
			}
			if (!bDidSomething)
			{
				ThreadState.ProcessDeferredFrees();
				Waiter.Wait();
			}
		}
	}
	return 0;
}

FUObjectSerializeContext* FAsyncPackage2::GetSerializeContext()
{
	return FUObjectThreadContext::Get().GetSerializeContext();
}

void FAsyncPackage2::SetupSerializedArcs(const uint8* GraphData, uint64 GraphDataSize)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SetupSerializedArcs);

	LLM_SCOPE(ELLMTag::AsyncLoading);

	FSimpleArchive GraphArchive(GraphData, GraphDataSize);
	int32 InternalArcCount;
	GraphArchive << InternalArcCount;
	for (int32 InternalArcIndex = 0; InternalArcIndex < InternalArcCount; ++InternalArcIndex)
	{
		int32 FromNodeIndex;
		int32 ToNodeIndex;
		GraphArchive << FromNodeIndex;
		GraphArchive << ToNodeIndex;
		PackageNodes[ToNodeIndex].DependsOn(PackageNodes + FromNodeIndex);
	}
	int32 ImportedPackagesCount;
	GraphArchive << ImportedPackagesCount;
	for (int32 ImportedPackageIndex = 0; ImportedPackageIndex < ImportedPackagesCount; ++ImportedPackageIndex)
	{
		FPackageId ImportedPackageId;
		int32 ExternalArcCount;
		GraphArchive << ImportedPackageId;
		GraphArchive << ExternalArcCount;

		FAsyncPackage2* ImportedPackage = AsyncLoadingThread.GetAsyncPackage(ImportedPackageId);
		for (int32 ExternalArcIndex = 0; ExternalArcIndex < ExternalArcCount; ++ExternalArcIndex)
		{
			int32 FromNodeIndex;
			int32 ToNodeIndex;
			GraphArchive << FromNodeIndex;
			GraphArchive << ToNodeIndex;
			if (ImportedPackage)
			{
				PackageNodes[ToNodeIndex].DependsOn(ImportedPackage->PackageNodes + FromNodeIndex);
			}
		}
	}
}

static UObject* GFindExistingScriptImport(int32 GlobalImportIndex,
	TArray<UObject*>& ScriptObjects,
	const TArray<FScriptObjectEntry>& ScriptObjectEntries)
{
	UObject*& Object = ScriptObjects[GlobalImportIndex];
	if (!Object)
	{
		const FScriptObjectEntry& Entry = ScriptObjectEntries[GlobalImportIndex];
		if (Entry.OuterIndex.IsNull())
		{
			Object = StaticFindObjectFast(UPackage::StaticClass(), nullptr, MinimalNameToName(Entry.ObjectName), true);
		}
		else
		{
			UObject* Outer = GFindExistingScriptImport(Entry.OuterIndex.GetIndex(), ScriptObjects, ScriptObjectEntries);
			if (Outer)
			{
				Object = StaticFindObjectFast(UObject::StaticClass(), Outer, MinimalNameToName(Entry.ObjectName), false, true);
			}
		}
	}
	return Object;
}

UObject* FGlobalImportStore::FindScriptImportObjectFromIndex(int32 GlobalImportIndex)
{
	check(ScriptObjectEntries.Num() > 0);
	return GFindExistingScriptImport(GlobalImportIndex, ScriptObjects, ScriptObjectEntries);
}

void FGlobalImportStore::FindAllScriptObjects()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindAllScriptObjects);
	const int32 ScriptImportCount = ScriptObjectEntries.Num();
	check(ScriptImportCount > 0);
	for (int32 GlobalImportIndex = 0; GlobalImportIndex < ScriptImportCount; ++GlobalImportIndex)
	{
		UObject* Object = GFindExistingScriptImport(
			GlobalImportIndex, ScriptObjects, ScriptObjectEntries);
#if DO_CHECK
		if (!Object)
		{
			FScriptObjectEntry& Entry = ScriptObjectEntries[GlobalImportIndex];
			if (Entry.OuterIndex.IsNull())
			{
				UE_LOG(LogStreaming, Warning,
					TEXT("AsyncLoading2 - Failed to find import script package after initial load: %s"),
					*MinimalNameToName(ScriptObjectEntries[GlobalImportIndex].ObjectName).ToString());
			}
			else
			{
				UE_LOG(LogStreaming, Warning,
					TEXT("AsyncLoading2 - Failed to find import script object after initial load: %s (%d) in outer %s (%d)"),
					*MinimalNameToName(ScriptObjectEntries[GlobalImportIndex].ObjectName).ToString(),
					GlobalImportIndex,
					*MinimalNameToName(ScriptObjectEntries[Entry.OuterIndex.GetIndex()].ObjectName).ToString(),
					*MinimalNameToName(ScriptObjectEntries[GlobalImportIndex].ObjectName).ToString());
			}
		}
#endif
	}
	ScriptObjectEntries.Empty();
}

void FAsyncPackage2::ImportPackagesRecursive()
{
	if (bHasImportedPackagesRecursive)
	{
		return;
	}
	bHasImportedPackagesRecursive = true;

	FPackageStore& GlobalPackageStore = AsyncLoadingThread.GlobalPackageStore;
	int32 ImportedPackageCount = StoreEntry.ImportedPackages.Num();
	if (!ImportedPackageCount)
	{
		return;
	}

	for (const FPackageId& ImportedPackageId : StoreEntry.ImportedPackages)
	{
		// AreAllImportsInImportPackageAlreadyLoaded?
		const FPackageStoreEntry& ImportEntry = GlobalPackageStore.GetStoreEntry(ImportedPackageId);
#if DO_CHECK
		UE_CLOG(!FMappedName::IsResolvedToMinimalName(ImportEntry.Name), LogStreaming, Warning,
			TEXT("Package '%s' is trying to import non mounted package ID '%d'"),
			*Desc.Name.ToString(),
			ImportedPackageId.ToIndex());
#endif
		bool bNeedToLoadPackage = false;
		for (const FPackageObjectIndex& GlobalImportIndex : ImportEntry.PublicExports)
		{
			UObject* ImportedObject = ImportStore.GetImportObject(GlobalImportIndex);
			if (!ImportedObject || !ImportedObject->HasAllFlags(RF_WasLoaded | RF_LoadCompleted))
			{
				bNeedToLoadPackage = true;
				break;
			}
		}

		if (bNeedToLoadPackage)
		{
			const FPackageId PackageIdToLoad = ImportedPackageId;
			const FName NameToLoad = MinimalNameToName(ImportEntry.Name);
			const FName SourceName =
				ImportEntry.SourcePackageId.IsValid() ?
				MinimalNameToName(GlobalPackageStore.StoreEntries[ImportEntry.SourcePackageId.ToIndex()].Name) :
				NameToLoad;

			FAsyncPackageDesc2 PackageDesc(INDEX_NONE, PackageIdToLoad, PackageIdToLoad, SourceName, NameToLoad);
			bool bInserted;
			FAsyncPackage2* ImportedPackage = AsyncLoadingThread.FindOrInsertPackage(&PackageDesc, bInserted);
			if (ImportedPackage)
			{
				TRACE_LOADTIME_ASYNC_PACKAGE_IMPORT_DEPENDENCY(this, ImportedPackage);
				if (bInserted)
				{
					UE_ASYNC_PACKAGE_LOG(Verbose, PackageDesc, TEXT("ImportPackages: AddPackage"),
						TEXT("Start loading imported package."));
				}
				else
				{
					UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, PackageDesc, TEXT("ImportPackages: UpdatePackage"),
						TEXT("Imported package is already being loaded."));
				}
				ImportedPackage->AddRef();
				ImportedAsyncPackages.Reserve(ImportedPackageCount);
				ImportedAsyncPackages.Add(ImportedPackage);
				if (bInserted)
				{
					ImportedPackage->ImportPackagesRecursive();
					ImportedPackage->StartLoading();
				}
			}
			else
			{
				UE_ASYNC_PACKAGE_LOG(Error, PackageDesc, TEXT("ImportPackages: SkipPackage"),
					TEXT("Skipping unknown imported package, but this should not happen here"));
			}
		}
	}
	UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("ImportPackages: ImportsDone"),
		TEXT("All imported packages are now being loaded."));
}

void FAsyncPackage2::StartLoading()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StartLoading);
	TRACE_LOADTIME_BEGIN_LOAD_ASYNC_PACKAGE(this);
	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::NewPackage);
	AsyncPackageLoadingState = EAsyncPackageLoadingState2::WaitingForSummary;

	LoadStartTime = FPlatformTime::Seconds();

	check(ExportBundleCount > 0);
	const FExportBundleMetaEntry& BundleMetaEntry = StoreEntry.ExportBundles[0];
	AsyncLoadingThread.AddBundleIoRequest(this, BundleMetaEntry);
}

EAsyncPackageState::Type FAsyncPackage2::Event_ProcessExportBundle(FAsyncPackage2* Package, int32 ExportBundleIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_ProcessExportBundle);

	FScopedAsyncPackageEvent2 Scope(Package);

	auto FilterExport = [](const EExportFilterFlags FilterFlags) -> bool
	{
#if UE_SERVER
		return !!(static_cast<uint32>(FilterFlags) & static_cast<uint32>(EExportFilterFlags::NotForServer));
#elif !WITH_SERVER_CODE
		return !!(static_cast<uint32>(FilterFlags) & static_cast<uint32>(EExportFilterFlags::NotForClient));
#else
		static const bool bIsDedicatedServer = !GIsClient && GIsServer;
		static const bool bIsClientOnly = GIsClient && !GIsServer;

		if (bIsDedicatedServer && static_cast<uint32>(FilterFlags) & static_cast<uint32>(EExportFilterFlags::NotForServer))
		{
			return true;
		}

		if (bIsClientOnly && static_cast<uint32>(FilterFlags) & static_cast<uint32>(EExportFilterFlags::NotForClient))
		{
			return true;
		}

		return false;
#endif
	};

	check(ExportBundleIndex < Package->ExportBundleCount);
	
	if (!Package->bLoadHasFailed)
	{
		if (Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::WaitingForSummary)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessPackageSummary);

			check(ExportBundleIndex == 0);
			check(Package->ExportBundleEntryIndex == 0);

			const uint8* PackageSummaryData = Package->IoBuffer.Data();
			const FPackageSummary* PackageSummary = reinterpret_cast<const FPackageSummary*>(PackageSummaryData);
			const uint8* GraphData = PackageSummaryData + PackageSummary->GraphDataOffset;
			const uint64 PackageSummarySize = GraphData + PackageSummary->GraphDataSize - PackageSummaryData;

			Package->CookedHeaderSize = PackageSummary->CookedHeaderSize;
			Package->NameMap = &Package->AsyncLoadingThread.GlobalPackageStore.GetPackageNameMap(PackageSummary->NameMapIndex);
			Package->PackageNameMap = reinterpret_cast<const int32*>(PackageSummaryData + PackageSummary->NameMapOffset);
			Package->ImportStore.ImportMap = reinterpret_cast<const FPackageObjectIndex*>(PackageSummaryData + PackageSummary->ImportMapOffset);
			Package->ImportStore.ImportMapCount = (PackageSummary->ExportMapOffset - PackageSummary->ImportMapOffset) / sizeof(int32);
			Package->ExportMap = reinterpret_cast<const FExportMapEntry*>(PackageSummaryData + PackageSummary->ExportMapOffset);
			Package->ExportBundles = reinterpret_cast<const FExportBundleHeader*>(PackageSummaryData + PackageSummary->ExportBundlesOffset);
			Package->ExportBundleEntries = reinterpret_cast<const FExportBundleEntry*>(Package->ExportBundles + Package->ExportBundleCount);

			Package->CreateUPackage(PackageSummary);
			Package->SetupSerializedArcs(GraphData, PackageSummary->GraphDataSize);

			Package->AllExportDataPtr = PackageSummaryData + PackageSummarySize;
			Package->CurrentExportDataPtr = Package->AllExportDataPtr;
			Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::ProcessNewImportsAndExports;

			TRACE_LOADTIME_PACKAGE_SUMMARY(Package, PackageSummarySize, Package->ImportStore.ImportMapCount, Package->ExportCount);
		}

		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessNewImportsAndExports);

		const uint64 AllExportDataSize = Package->IoBuffer.DataSize() - (Package->AllExportDataPtr - Package->IoBuffer.Data());
		FExportArchive Ar(Package->AllExportDataPtr, Package->CurrentExportDataPtr, AllExportDataSize);
		{
			Ar.SetUE4Ver(Package->LinkerRoot->LinkerPackageVersion);
			Ar.SetLicenseeUE4Ver(Package->LinkerRoot->LinkerLicenseeVersion);
			// Ar.SetEngineVer(Summary.SavedByEngineVersion); // very old versioning scheme
			// Ar.SetCustomVersions(LinkerRoot->LinkerCustomVersion); // only if not cooking with -unversioned
			Ar.SetUseUnversionedPropertySerialization(CanUseUnversionedPropertySerialization());
			Ar.SetIsLoading(true);
			Ar.SetIsPersistent(true);
			if (Package->LinkerRoot->GetPackageFlags() & PKG_FilterEditorOnly)
			{
				Ar.SetFilterEditorOnly(true);
			}
			Ar.ArAllowLazyLoading = true;

			// FExportArchive special fields
			Ar.CookedHeaderSize = Package->CookedHeaderSize;
			Ar.PackageDesc = &Package->Desc;
			Ar.PackageNameMap = Package->PackageNameMap;
			Ar.NameMap = &Package->NameMap->GetNameEntries();
			Ar.ImportStore = &Package->ImportStore;
			Ar.Exports = &Package->Exports;
			Ar.ExportMap = Package->ExportMap;
			Ar.ExportCount = Package->ExportCount;
			Ar.ExternalReadDependencies = &Package->ExternalReadDependencies;
		}
		const FExportBundleHeader* ExportBundle = Package->ExportBundles + ExportBundleIndex;
		const FExportBundleEntry* BundleEntries = Package->ExportBundleEntries + ExportBundle->FirstEntryIndex;
		const FExportBundleEntry* BundleEntry = BundleEntries + Package->ExportBundleEntryIndex;
		const FExportBundleEntry* BundleEntryEnd = BundleEntries + ExportBundle->EntryCount;
		check(BundleEntry <= BundleEntryEnd);
		while (BundleEntry < BundleEntryEnd)
		{
			if (FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded(TEXT("Event_ProcessExportBundle")))
			{
				return EAsyncPackageState::TimeOut;
			}
			const FExportMapEntry& ExportMapEntry = Package->ExportMap[BundleEntry->LocalExportIndex];
			FExportObject& Export = Package->Exports[BundleEntry->LocalExportIndex];
			Export.bFiltered = FilterExport(ExportMapEntry.FilterFlags);

			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Create)
			{
				if (!(Export.bFiltered | Export.bExportLoadFailed))
				{
					Package->EventDrivenCreateExport(BundleEntry->LocalExportIndex);
				}
			}
			else
			{
				check(BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize);

				const uint64 CookedSerialSize = ExportMapEntry.CookedSerialSize;
				UObject* Object = Export.Object;

				check(Package->CurrentExportDataPtr + CookedSerialSize <= Package->IoBuffer.Data() + Package->IoBuffer.DataSize());
				check(Object || Export.bFiltered || Export.bExportLoadFailed);

				Ar.ExportBufferBegin(ExportMapEntry.CookedSerialOffset, ExportMapEntry.CookedSerialSize);
				if (!(Export.bFiltered | Export.bExportLoadFailed) && Object->HasAnyFlags(RF_NeedLoad))
				{
					TRACE_LOADTIME_SERIALIZE_EXPORT_SCOPE(Object, CookedSerialSize);
					const int64 Pos = Ar.Tell();
					check(CookedSerialSize <= uint64(Ar.TotalSize() - Pos));

					Package->EventDrivenSerializeExport(BundleEntry->LocalExportIndex, Ar);
					checkf(CookedSerialSize == uint64(Ar.Tell() - Pos), TEXT("Expect read size: %llu - Actual read size: %llu"), CookedSerialSize, uint64(Ar.Tell() - Pos));
				}
				else
				{
					Ar.Skip(CookedSerialSize);
				}
				Ar.ExportBufferEnd();

				check((Object && !Object->HasAnyFlags(RF_NeedLoad)) || Export.bFiltered || Export.bExportLoadFailed);

				Package->CurrentExportDataPtr += CookedSerialSize;
			}
			++BundleEntry;
			++Package->ExportBundleEntryIndex;
		}
	}
	
	Package->ExportBundleEntryIndex = 0;

	if (ExportBundleIndex + 1 < Package->ExportBundleCount)
	{
		Package->GetExportBundleNode(ExportBundle_Process, ExportBundleIndex + 1)->ReleaseBarrier();
	}
	else
	{
		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessNewImportsAndExports);
		Package->ImportStore.ImportMap = nullptr;
		Package->ImportStore.ImportMapCount = 0;
		Package->bAllExportsSerialized = true;
		Package->IoBuffer = FIoBuffer();
		Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::PostLoad_Etc;

		if (Package->ExternalReadDependencies.Num() == 0)
		{
			Package->GetNode(Package_ExportsSerialized)->ReleaseBarrier();
		}
		else
		{
			Package->AsyncLoadingThread.ExternalReadQueue.Enqueue(Package);
		}
	}

	if (ExportBundleIndex == 0)
	{
		check(Package->ExportBundleCount > 0);
		const FExportBundleMetaEntry& BundleMetaEntry = Package->StoreEntry.ExportBundles[0];
		Package->AsyncLoadingThread.BundleIoRequestCompleted(BundleMetaEntry);
	}

	return EAsyncPackageState::Complete;
}

UObject* FAsyncPackage2::EventDrivenIndexToObject(FPackageObjectIndex Index, bool bCheckSerialized)
{
	UObject* Result = nullptr;
	if (Index.IsNull())
	{
		return Result;
	}
	if (Index.IsExport())
	{
		Result = Exports[Index.ToExport()].Object;
	}
	else if (Index.IsImport())
	{
		Result = ImportStore.FindOrGetImportObject(Index);
		UE_CLOG(!Result, LogStreaming, Warning, TEXT("Missing import for package %s (ScriptImport: %d, Index: %d)"), *Desc.Name.ToString(), Index.IsScriptImport(), Index.IsScriptImport() ? Index.ToScriptImport() : Index.ToPackageImport());
	}
#if DO_CHECK
	if (bCheckSerialized && !IsFullyLoadedObj(Result))
	{
		/*FEventLoadNode2* MyDependentNode = GetExportNode(EEventLoadNode2::Export_Serialize, Index.ToExport());
		if (!Result)
		{
			UE_LOG(LogStreaming, Error, TEXT("Missing Dependency, request for %s but it hasn't been created yet."), *Linker->GetPathName(Index));
		}
		else if (!MyDependentNode || MyDependentNode->GetBarrierCount() > 0)
		{
			UE_LOG(LogStreaming, Fatal, TEXT("Missing Dependency, request for %s but it was still waiting for serialization."), *Linker->GetPathName(Index));
		}
		else
		{
			UE_LOG(LogStreaming, Fatal, TEXT("Missing Dependency, request for %s but it was still has RF_NeedLoad."), *Linker->GetPathName(Index));
		}*/
		UE_LOG(LogStreaming, Warning, TEXT("Missing Dependency"));
	}
	if (Result)
	{
		UE_CLOG(Result->HasAnyInternalFlags(EInternalObjectFlags::Unreachable), LogStreaming, Fatal, TEXT("Returning an object  (%s) from EventDrivenIndexToObject that is unreachable."), *Result->GetFullName());
	}
#endif
	return Result;
}


void FAsyncPackage2::EventDrivenCreateExport(int32 LocalExportIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateExport);

	const FExportMapEntry& Export = ExportMap[LocalExportIndex];
	UObject*& Object = Exports[LocalExportIndex].Object;
	check(!Object);

	FName ObjectName;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ObjectNameFixup);
		ObjectName = NameMap->GetName(Export.ObjectName);
	}

	TRACE_LOADTIME_CREATE_EXPORT_SCOPE(this, &Object);

	LLM_SCOPE(ELLMTag::AsyncLoading);
	LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(GetLinkerRoot(), ELLMTagSet::Assets);
	// LLM_SCOPED_TAG_WITH_OBJECT_IN_SET((Export.DynamicType == FObjectExport::EDynamicType::DynamicType) ? UDynamicClass::StaticClass() : 
	// 	CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, false), ELLMTagSet::AssetClasses);

	bool bIsCompleteyLoaded = false;
	UClass* LoadClass = Export.ClassIndex.IsNull() ? UClass::StaticClass() : CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, true);
	UObject* ThisParent = Export.OuterIndex.IsNull() ? LinkerRoot : EventDrivenIndexToObject(Export.OuterIndex, false);

	if (!LoadClass)
	{
		UE_LOG(LogStreaming, Error, TEXT("Could not find class object for %s in %s"), *ObjectName.ToString(), *Desc.NameToLoad.ToString());
		Exports[LocalExportIndex].bExportLoadFailed = true;
		return;
	}
	if (!ThisParent)
	{
		UE_LOG(LogStreaming, Error, TEXT("Could not find outer object for %s in %s"), *ObjectName.ToString(), *Desc.NameToLoad.ToString());
		Exports[LocalExportIndex].bExportLoadFailed = true;
		return;
	}
	check(!dynamic_cast<UObjectRedirector*>(ThisParent));

	// Try to find existing object first as we cannot in-place replace objects, could have been created by other export in this package
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FindExport);
		Object = StaticFindObjectFastInternal(NULL, ThisParent, ObjectName, true);
	}

	// Object is found in memory.
	if (Object)
	{
		// If this object was allocated but never loaded (components created by a constructor, CDOs, etc) make sure it gets loaded
		// Do this for all subobjects created in the native constructor.
		const EObjectFlags ObjectFlags = Object->GetFlags();
		bIsCompleteyLoaded = !!(ObjectFlags & RF_LoadCompleted);
		if (!bIsCompleteyLoaded)
		{
			UE_LOG(LogStreaming, VeryVerbose, TEXT("Note2: %s was constructed during load and is an export and so needs loading."), *Object->GetFullName());
			check(!(ObjectFlags & (RF_NeedLoad | RF_WasLoaded))); // If export exist but is not completed, it is expected to have been created from a native constructor and not from EventDrivenCreateExport, but who knows...?
			if (ObjectFlags & RF_ClassDefaultObject)
			{
				// never call PostLoadSubobjects on class default objects, this matches the behavior of the old linker where
				// StaticAllocateObject prevents setting of RF_NeedPostLoad and RF_NeedPostLoadSubobjects, but FLinkerLoad::Preload
				// assigns RF_NeedPostLoad for blueprint CDOs:
				Object->SetFlags(RF_NeedLoad | RF_NeedPostLoad | RF_WasLoaded);
			}
			else
			{
				Object->SetFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_WasLoaded);
			}
			AddOwnedObjectWithAsyncFlag(Object, /*bForceAdd*/ false);
		}
		else
		{
			AddOwnedObjectWithAsyncFlag(Object, /*bForceAdd*/ true);
		}
	}
	else
	{
		// Find the Archetype object for the one we are loading.
		check(!Export.TemplateIndex.IsNull());
		UObject* Template = EventDrivenIndexToObject(Export.TemplateIndex, true);
		if (!Template)
		{
			UE_LOG(LogStreaming, Error, TEXT("Could not find template for %s in %s"), *ObjectName.ToString(), *Desc.NameToLoad.ToString());
			Exports[LocalExportIndex].bExportLoadFailed = true;
			return;
		}
		// we also need to ensure that the template has set up any instances
		Template->ConditionalPostLoadSubobjects();

		check(!GVerifyObjectReferencesOnly); // not supported with the event driven loader
		// Create the export object, marking it with the appropriate flags to
		// indicate that the object's data still needs to be loaded.
		EObjectFlags ObjectLoadFlags = Export.ObjectFlags;
		ObjectLoadFlags = EObjectFlags(ObjectLoadFlags | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_WasLoaded);

		// If we are about to create a CDO, we need to ensure that all parent sub-objects are loaded
		// to get default value initialization to work.
#if DO_CHECK
		if ((ObjectLoadFlags & RF_ClassDefaultObject) != 0)
		{
			UClass* SuperClass = LoadClass->GetSuperClass();
			UObject* SuperCDO = SuperClass ? SuperClass->GetDefaultObject() : nullptr;
			check(!SuperCDO || Template == SuperCDO); // the template for a CDO is the CDO of the super
			if (SuperClass && !SuperClass->IsNative())
			{
				check(SuperCDO);
				if (SuperClass->HasAnyFlags(RF_NeedLoad))
				{
					UE_LOG(LogStreaming, Fatal, TEXT("Super %s had RF_NeedLoad while creating %s"), *SuperClass->GetFullName(), *ObjectName.ToString());
					return;
				}
				if (SuperCDO->HasAnyFlags(RF_NeedLoad))
				{
					UE_LOG(LogStreaming, Fatal, TEXT("Super CDO %s had RF_NeedLoad while creating %s"), *SuperCDO->GetFullName(), *ObjectName.ToString());
					return;
				}
				TArray<UObject*> SuperSubObjects;
				GetObjectsWithOuter(SuperCDO, SuperSubObjects, /*bIncludeNestedObjects=*/ false, /*ExclusionFlags=*/ RF_NoFlags, /*InternalExclusionFlags=*/ EInternalObjectFlags::Native);

				for (UObject* SubObject : SuperSubObjects)
				{
					if (SubObject->HasAnyFlags(RF_NeedLoad))
					{
						UE_LOG(LogStreaming, Fatal, TEXT("Super CDO subobject %s had RF_NeedLoad while creating %s"), *SubObject->GetFullName(), *ObjectName.ToString());
						return;
					}
				}
			}
			else
			{
				check(Template->IsA(LoadClass));
			}
		}
#endif
		checkf(!LoadClass->HasAnyFlags(RF_NeedLoad),
			TEXT("LoadClass %s had RF_NeedLoad while creating %s"), *LoadClass->GetFullName(), *ObjectName.ToString());
		checkf(!(LoadClass->GetDefaultObject() && LoadClass->GetDefaultObject()->HasAnyFlags(RF_NeedLoad)), 
			TEXT("Class CDO %s had RF_NeedLoad while creating %s"), *LoadClass->GetDefaultObject()->GetFullName(), *ObjectName.ToString());
		checkf(!Template->HasAnyFlags(RF_NeedLoad),
			TEXT("Template %s had RF_NeedLoad while creating %s"), *Template->GetFullName(), *ObjectName.ToString());

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ConstructObject);
			Object = StaticConstructObject_Internal
				(
				 LoadClass,
				 ThisParent,
				 ObjectName,
				 ObjectLoadFlags,
				 EInternalObjectFlags::None,
				 Template,
				 false,
				 nullptr,
				 true
				);
		}

		if (GIsInitialLoad || GUObjectArray.IsOpenForDisregardForGC())
		{
			Object->AddToRoot();
		}

		AddOwnedObjectWithAsyncFlag(Object, /*bForceAdd*/ false);
		check(Object->GetClass() == LoadClass);
		check(Object->GetFName() == ObjectName);
	}

	check(Object);
	ImportStore.StoreGlobalObject(Export.GlobalImportIndex, Object);
}

void FAsyncPackage2::EventDrivenSerializeExport(int32 LocalExportIndex, FExportArchive& Ar)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SerializeExport);

	const FExportMapEntry& Export = ExportMap[LocalExportIndex];
	UObject* Object = Exports[LocalExportIndex].Object;
	check(Object);

	LLM_SCOPE(ELLMTag::UObject);
	LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(GetLinkerRoot(), ELLMTagSet::Assets);
	// LLM_SCOPED_TAG_WITH_OBJECT_IN_SET((Export.DynamicType == FObjectExport::EDynamicType::DynamicType) ? UDynamicClass::StaticClass() :
	// 	CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, false), ELLMTagSet::AssetClasses);

	// If this is a struct, make sure that its parent struct is completely loaded
	if (UStruct* Struct = dynamic_cast<UStruct*>(Object))
	{
		if (!Export.SuperIndex.IsNull())
		{
			UStruct* SuperStruct = CastEventDrivenIndexToObject<UStruct>(Export.SuperIndex, true);
			if (!SuperStruct)
			{
				UE_LOG(LogStreaming, Fatal, TEXT("Could not find SuperStruct for %s"), *Object->GetFullName());
				Exports[LocalExportIndex].bExportLoadFailed = true;
				return;
			}
			Struct->SetSuperStruct(SuperStruct);
			if (UClass* ClassObject = dynamic_cast<UClass*>(Object))
			{
				ClassObject->Bind();
			}
		}
	}

	// cache archetype
	// prevents GetArchetype from hitting the expensive GetArchetypeFromRequiredInfoImpl
	check(!Export.TemplateIndex.IsNull());
	UObject* Template = EventDrivenIndexToObject(Export.TemplateIndex, true);
	check(Template);
	CacheArchetypeForObject(Object, Template);

	Object->ClearFlags(RF_NeedLoad);

	FUObjectSerializeContext* LoadContext = GetSerializeContext();
	UObject* PrevSerializedObject = LoadContext->SerializedObject;
	LoadContext->SerializedObject = Object;

	Ar.TemplateForGetArchetypeFromLoader = Template;

	if (Object->HasAnyFlags(RF_ClassDefaultObject))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SerializeDefaultObject);
		Object->GetClass()->SerializeDefaultObject(Object, Ar);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SerializeObject);
		Object->Serialize(Ar);
	}
	Ar.TemplateForGetArchetypeFromLoader = nullptr;

	Object->SetFlags(RF_LoadCompleted);
	LoadContext->SerializedObject = PrevSerializedObject;

#if DO_CHECK
	if (Object->HasAnyFlags(RF_ClassDefaultObject) && Object->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		check(Object->HasAllFlags(RF_NeedPostLoad | RF_WasLoaded));
		//Object->SetFlags(RF_NeedPostLoad | RF_WasLoaded);
	}
#endif

	// push stats so that we don't overflow number of tags per thread during blocking loading
	LLM_PUSH_STATS_FOR_ASSET_TAGS();
}

EAsyncPackageState::Type FAsyncPackage2::Event_ExportsDone(FAsyncPackage2* Package, int32)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_ExportsDone);

	Package->GetNode(EEventLoadNode2::Package_PostLoad)->ReleaseBarrier();
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::Event_PostLoad(FAsyncPackage2* Package, int32)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_PostLoad);

	check(!Package->HasFinishedLoading());
	check(Package->ExternalReadDependencies.Num() == 0);
	
	FAsyncPackageScope2 PackageScope(Package);

	// Make sure we finish our work if there's no time limit. The loop is required as PostLoad
	// might cause more objects to be loaded in which case we need to Preload them again.
	EAsyncPackageState::Type LoadingState = EAsyncPackageState::Complete;

	// Begin async loading, simulates BeginLoad
	Package->BeginAsyncLoad();

	LoadingState = Package->PostLoadObjects();

	// End async loading, simulates EndLoad
	Package->EndAsyncLoad();

	// Finish objects (removing EInternalObjectFlags::AsyncLoading, dissociate imports and forced exports, 
	// call completion callback, ...
	// If the load has failed, perform completion callbacks and then quit
	if (LoadingState == EAsyncPackageState::Complete || Package->bLoadHasFailed)
	{
		LoadingState = Package->FinishObjects();
	}

	// Mark this package as loaded if everything completed.
	Package->bLoadHasFinished = (LoadingState == EAsyncPackageState::Complete);

	if (Package->bLoadHasFinished)
	{
		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::PostLoad_Etc);
		Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::PackageComplete;
	}

	if (Package->LinkerRoot && LoadingState == EAsyncPackageState::Complete)
	{
		UE_ASYNC_PACKAGE_LOG(Verbose, Package->Desc, TEXT("AsyncThread: FullyLoaded"),
			TEXT("Async loading of package is done, and UPackage is marked as fully loaded."))
		Package->LinkerRoot->MarkAsFullyLoaded();
	}

	if (LoadingState == EAsyncPackageState::TimeOut)
	{
		return EAsyncPackageState::TimeOut;
	}
	check(LoadingState == EAsyncPackageState::Complete);
	// We're done, at least on this thread, so we can remove the package now.
	Package->AsyncLoadingThread.AddToLoadedPackages(Package);
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::Event_Delete(FAsyncPackage2* Package, int32)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_Delete);
	delete Package;
	return EAsyncPackageState::Complete;
}


FEventLoadNode2* FAsyncPackage2::GetPackageNode(EEventLoadNode2 Phase)
{
	check(Phase < EEventLoadNode2::Package_NumPhases);
	return PackageNodes + Phase;
}

FEventLoadNode2* FAsyncPackage2::GetExportBundleNode(EEventLoadNode2 Phase, uint32 ExportBundleIndex)
{
	check(ExportBundleIndex < uint32(ExportBundleCount));
	uint32 ExportBundleNodeIndex = ExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases + Phase;
	return ExportBundleNodes + ExportBundleNodeIndex;
}

FEventLoadNode2* FAsyncPackage2::GetNode(int32 NodeIndex)
{ 
	check(uint32(NodeIndex) < EEventLoadNode2::Package_NumPhases + ExportBundleNodeCount);
	return &PackageNodes[NodeIndex];
}

void FAsyncLoadingThread2::AddToLoadedPackages(FAsyncPackage2* Package)
{
	WaitingForPostLoadCounter.Increment();
	FScopeLock LoadedLock(&LoadedPackagesCritical);
	check(!LoadedPackages.Contains(Package));
	LoadedPackages.Add(Package);
}

#if ALT2_VERIFY_RECURSIVE_LOADS 
struct FScopedLoadRecursionVerifier
{
	int32& Level;
	FScopedLoadRecursionVerifier(int32& InLevel) : Level(InLevel)
	{
		UE_CLOG(Level > 0, LogStreaming, Error, TEXT("Entering recursive load level: %d"), Level);
		++Level;
		check(Level == 1);
	}
	~FScopedLoadRecursionVerifier()
	{
		--Level;
		UE_CLOG(Level > 0, LogStreaming, Error, TEXT("Leaving recursive load level: %d"), Level);
		check(Level == 0);
	}
};
#endif

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessAsyncLoadingFromGameThread(int32& OutPackagesProcessed)
{
	SCOPED_LOADTIMER(AsyncLoadingTime);

	check(IsInGameThread());

	// If we're not multithreaded and flushing async loading, update the thread heartbeat
	const bool bNeedsHeartbeatTick = !FAsyncLoadingThread2::IsMultithreaded();
	OutPackagesProcessed = 0;

#if ALT2_VERIFY_RECURSIVE_LOADS 
	FScopedLoadRecursionVerifier LoadRecursionVerifier(this->LoadRecursionLevel);
#endif
	FAsyncLoadingTickScope2 InAsyncLoadingTick(*this);
	uint32 LoopIterations = 0;

	FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();

	while (true)
	{
		do 
		{
			GlobalPackageStore.GetGlobalImportStore().bNeedToHandleGarbageCollect |= IsAsyncLoadingPackages();

			ThreadState.ProcessDeferredFrees();

			if (bNeedsHeartbeatTick && (++LoopIterations) % 32 == 31)
			{
				// Update heartbeat after 32 events
				FThreadHeartBeat::Get().HeartBeat();
			}

			if (ThreadState.IsTimeLimitExceeded(TEXT("ProcessAsyncLoadingFromGameThread")))
			{
				return EAsyncPackageState::TimeOut;
			}

			if (IsAsyncLoadingSuspended())
			{
				return EAsyncPackageState::TimeOut;
			}

			if (!ExternalReadQueue.IsEmpty())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ProcessExternalReads);

				FAsyncPackage2* Package = nullptr;
				ExternalReadQueue.Dequeue(Package);

				EAsyncPackageState::Type Result = Package->ProcessExternalReads(FAsyncPackage2::ExternalReadAction_Wait);
				check(Result == EAsyncPackageState::Complete);

				OutPackagesProcessed++;
				break;
			}

			if (QueuedPackagesCounter)
			{
				CreateAsyncPackagesFromQueue();
				OutPackagesProcessed++;
				break;
			}

			bool bPopped = false;
			for (FAsyncLoadEventQueue2* Queue : AltEventQueues)
			{
				if (Queue->PopAndExecute(ThreadState))
				{
					bPopped = true;
					break;
				}
			}
			if (bPopped)
			{
				OutPackagesProcessed++;
				break;
			}

			return EAsyncPackageState::Complete;
		} while (false);
	}
	check(false);
	return EAsyncPackageState::Complete;
}

bool FAsyncPackage2::AreAllDependenciesFullyLoadedInternal(FAsyncPackage2* Package, TSet<FPackageId>& VisitedPackages, FPackageId& OutPackageId)
{
	for (const FPackageId& ImportedPackageId : Package->StoreEntry.ImportedPackages)
	{
		if (VisitedPackages.Contains(ImportedPackageId))
		{
			continue;
		}
		VisitedPackages.Add(ImportedPackageId);

		FAsyncPackage2* AsyncRoot = AsyncLoadingThread.GetAsyncPackage(ImportedPackageId);
		if (AsyncRoot)
		{
			if (AsyncRoot->DeferredPostLoadIndex < AsyncRoot->ExportCount)
			{
				OutPackageId = ImportedPackageId;
				return false;
			}

			if (!AreAllDependenciesFullyLoadedInternal(AsyncRoot, VisitedPackages, OutPackageId))
			{
				return false;
			}
		}
	}
	return true;
}

bool FAsyncPackage2::AreAllDependenciesFullyLoaded(TSet<FPackageId>& VisitedPackages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AreAllDependenciesFullyLoaded);
	VisitedPackages.Reset();
	FPackageId PackageId;
	const bool bLoaded = AreAllDependenciesFullyLoadedInternal(this, VisitedPackages, PackageId);
	if (!bLoaded)
	{
		FAsyncPackage2* AsyncRoot = AsyncLoadingThread.GetAsyncPackage(PackageId);
		TCHAR PackageName[FName::StringBufferSize];
		AsyncRoot->GetPackageName().ToString(PackageName);
		UE_LOG(LogStreaming, Verbose, TEXT("AreAllDependenciesFullyLoaded: '%s' doesn't have all exports processed by DeferredPostLoad"), PackageName);
	}
	return bLoaded;
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadedPackagesFromGameThread(bool& bDidSomething, int32 FlushRequestID)
{
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;

	// This is for debugging purposes only. @todo remove
	volatile int32 CurrentAsyncLoadingCounter = AsyncLoadingTickCounter;

	{
		FScopeLock LoadedPackagesLock(&LoadedPackagesCritical);
		if (LoadedPackages.Num() != 0)
		{
			LoadedPackagesToProcess.Append(LoadedPackages);
			LoadedPackages.Reset();
		}
	}
	if (IsMultithreaded() &&
		ENamedThreads::GetRenderThread() == ENamedThreads::GameThread &&
		!FTaskGraphInterface::Get().IsThreadProcessingTasks(ENamedThreads::GameThread)) // render thread tasks are actually being sent to the game thread.
	{
		// The async loading thread might have queued some render thread tasks (we don't have a render thread yet, so these are actually sent to the game thread)
		// We need to process them now before we do any postloads.
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		if (FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded(TEXT("ProcessLoadedPackagesFromGameThread")))
		{
			return EAsyncPackageState::TimeOut;
		}
	}

	bDidSomething = LoadedPackagesToProcess.Num() > 0 || QueuedFailedPackageCallbacks.Num() > 0;
	for (FQueuedFailedPackageCallback& QueuedFailedPackageCallback : QueuedFailedPackageCallbacks)
	{
		QueuedFailedPackageCallback.Callback->ExecuteIfBound(QueuedFailedPackageCallback.PackageName, nullptr, EAsyncLoadingResult::Failed);
	}
	QueuedFailedPackageCallbacks.Empty();
	FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
	for (int32 PackageIndex = 0; PackageIndex < LoadedPackagesToProcess.Num() && !IsAsyncLoadingSuspended(); ++PackageIndex)
	{
		FAsyncPackage2* Package = LoadedPackagesToProcess[PackageIndex];
		SCOPED_LOADTIMER(ProcessLoadedPackagesTime);

		Result = Package->PostLoadDeferredObjects();
		if (Result == EAsyncPackageState::Complete)
		{
			{
				FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
				AsyncPackageLookup.Remove(Package->GetPackageId());
				Package->ClearOwnedObjects();
			}

			// Remove the package from the list before we trigger the callbacks, 
			// this is to ensure we can re-enter FlushAsyncLoading from any of the callbacks
			LoadedPackagesToProcess.RemoveAt(PackageIndex--);

			// Incremented on the Async Thread, now decrement as we're done with this package				
			const int32 NewExistingAsyncPackagesCounterValue = ExistingAsyncPackagesCounter.Decrement();

			UE_CLOG(NewExistingAsyncPackagesCounterValue < 0, LogStreaming, Fatal, TEXT("ExistingAsyncPackagesCounter is negative, this means we loaded more packages then requested so there must be a bug in async loading code."));

			TRACE_LOADTIME_END_LOAD_ASYNC_PACKAGE(Package);

			// Call external callbacks
			const EAsyncLoadingResult::Type LoadingResult = Package->HasLoadFailed() ? EAsyncLoadingResult::Failed : EAsyncLoadingResult::Succeeded;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PackageCompletionCallbacks);
				Package->CallCompletionCallbacks(LoadingResult);
			}

			// We don't need the package anymore
			check(!Package->bAddedForDelete);
			check(!PackagesToDelete.Contains(Package));
			PackagesToDelete.Add(Package);
			Package->bAddedForDelete = true;
			Package->MarkRequestIDsAsComplete();

			UE_ASYNC_PACKAGE_LOG(Verbose, Package->Desc, TEXT("GameThread: LoadCompleted"),
				TEXT("All loading of package is done, and the async package and load request will be deleted."));

			if (ThreadState.IsTimeLimitExceeded(TEXT("ProcessAsyncLoadingFromGameThread")) || (FlushRequestID != INDEX_NONE && !ContainsRequestID(FlushRequestID)))
			{
				// The only package we care about has finished loading, so we're good to exit
				break;
			}
		}
		else
		{
			break;
		}
	}
	bDidSomething = bDidSomething || PackagesToDelete.Num() > 0;

	// Delete packages we're done processing and are no longer dependencies of anything else
	if (Result != EAsyncPackageState::TimeOut)
	{
		// For performance reasons this set is created here and reset inside of AreAllDependenciesFullyLoaded
		TSet<FPackageId> VisistedPackages;

		for (int32 PackageIndex = 0; PackageIndex < PackagesToDelete.Num(); ++PackageIndex)
		{
			FAsyncPackage2* Package = PackagesToDelete[PackageIndex];
			{
				bool bSafeToDelete = false;
				if (Package->HasClusterObjects())
				{
					// This package will create GC clusters but first check if all dependencies of this package have been fully loaded
					if (Package->AreAllDependenciesFullyLoaded(VisistedPackages))
					{
						if (Package->CreateClusters() == EAsyncPackageState::Complete)
						{
							// All clusters created, it's safe to delete the package
							bSafeToDelete = true;
						}
						else
						{
							// Cluster creation timed out
							Result = EAsyncPackageState::TimeOut;
							break;
						}
					}
				}
				else
				{
					// No clusters to create so it's safe to delete
					bSafeToDelete = true;
				}

				if (bSafeToDelete)
				{
					PackagesToDelete.RemoveAtSwap(PackageIndex--);
					Package->ClearImportedPackages();
					Package->AsyncLoadingThread.WaitingForPostLoadCounter.Decrement();
					Package->ReleaseRef();
				}
			}

			// push stats so that we don't overflow number of tags per thread during blocking loading
			LLM_PUSH_STATS_FOR_ASSET_TAGS();
		}
	}

	if (Result == EAsyncPackageState::Complete)
	{
		// We're not done until all packages have been deleted
		Result = PackagesToDelete.Num() ? EAsyncPackageState::PendingImports : EAsyncPackageState::Complete;
	}

	return Result;
}

EAsyncPackageState::Type FAsyncLoadingThread2::TickAsyncLoadingFromGameThread(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, int32 FlushRequestID)
{
	LLM_SCOPE(ELLMTag::AsyncLoading);

	//TRACE_INT_VALUE(QueuedPackagesCounter, QueuedPackagesCounter);
	//TRACE_INT_VALUE(GraphNodeCount, GraphAllocator.TotalNodeCount);
	//TRACE_INT_VALUE(GraphArcCount, GraphAllocator.TotalArcCount);
	//TRACE_MEMORY_VALUE(GraphMemory, GraphAllocator.TotalAllocated);


	check(IsInGameThread());
	check(!IsGarbageCollecting());

	const bool bLoadingSuspended = IsAsyncLoadingSuspended();
	EAsyncPackageState::Type Result = bLoadingSuspended ? EAsyncPackageState::PendingImports : EAsyncPackageState::Complete;

	if (!bLoadingSuspended)
	{
		FAsyncLoadingThreadState2::Get()->SetTimeLimit(bUseTimeLimit, TimeLimit);

		// First make sure there's no objects pending to be unhashed. This is important in uncooked builds since we don't 
		// detach linkers immediately there and we may end up in getting unreachable objects from Linkers in CreateImports
		if (FPlatformProperties::RequiresCookedData() == false && IsIncrementalUnhashPending() && IsAsyncLoadingPackages())
		{
			// Call ConditionalBeginDestroy on all pending objects. CBD is where linkers get detached from objects.
			UnhashUnreachableObjects(false);
		}

		const bool bIsMultithreaded = FAsyncLoadingThread2::IsMultithreaded();
		double TickStartTime = FPlatformTime::Seconds();

		bool bDidSomething = false;
		{
			Result = ProcessLoadedPackagesFromGameThread(bDidSomething, FlushRequestID);
			double TimeLimitUsedForProcessLoaded = FPlatformTime::Seconds() - TickStartTime;
			UE_CLOG(!GIsEditor && bUseTimeLimit && TimeLimitUsedForProcessLoaded > .1f, LogStreaming, Warning, TEXT("Took %6.2fms to ProcessLoadedPackages"), float(TimeLimitUsedForProcessLoaded) * 1000.0f);
		}

		if (!bIsMultithreaded && Result != EAsyncPackageState::TimeOut)
		{
			Result = TickAsyncThreadFromGameThread(bDidSomething);
		}

		if (Result != EAsyncPackageState::TimeOut)
		{
			{
				FScopeLock QueueLock(&QueueCritical);
				FScopeLock LoadedLock(&LoadedPackagesCritical);
				// Flush deferred messages
				if (ExistingAsyncPackagesCounter.GetValue() == 0)
				{
					bDidSomething = true;
					FDeferredMessageLog::Flush();
				}
			}
		}

		// Call update callback once per tick on the game thread
		FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast();
	}

	return Result;
}

FAsyncLoadingThread2::FAsyncLoadingThread2(FIoDispatcher& InIoDispatcher)
	: Thread(nullptr)
	, IoDispatcher(InIoDispatcher)
	, GlobalPackageStore(InIoDispatcher, GlobalNameMap)
{
	GEventDrivenLoaderEnabled = true;

#if LOADTIMEPROFILERTRACE_ENABLED
	FLoadTimeProfilerTracePrivate::Init();
#endif

	AltEventQueues.Add(&ProcessExportBundlesEventQueue);
	AltEventQueues.Add(&AsyncEventQueue);
	AltEventQueues.Add(&EventQueue);
	for (FAsyncLoadEventQueue2* Queue : AltEventQueues)
	{
		Queue->SetZenaphore(&AltZenaphore);
	}

	EventSpecs.AddDefaulted(EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_NumPhases);
	EventSpecs[EEventLoadNode2::Package_ExportsSerialized] = { &FAsyncPackage2::Event_ExportsDone, &AsyncEventQueue, true };
	EventSpecs[EEventLoadNode2::Package_PostLoad] = { &FAsyncPackage2::Event_PostLoad, &AsyncEventQueue, true };
	EventSpecs[EEventLoadNode2::Package_Delete] = { &FAsyncPackage2::Event_Delete, &AsyncEventQueue, false };

	EventSpecs[EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_Process] = { &FAsyncPackage2::Event_ProcessExportBundle, &ProcessExportBundlesEventQueue, false };

	CancelLoadingEvent = FPlatformProcess::GetSynchEventFromPool();
	ThreadSuspendedEvent = FPlatformProcess::GetSynchEventFromPool();
	ThreadResumedEvent = FPlatformProcess::GetSynchEventFromPool();
	AsyncLoadingTickCounter = 0;

	FAsyncLoadingThreadState2::TlsSlot = FPlatformTLS::AllocTlsSlot();
	FAsyncLoadingThreadState2::Create(GraphAllocator, IoDispatcher);

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - Created: Event Driven Loader: %s, Async Loading Thread: %s, Async Post Load: %s"),
		GEventDrivenLoaderEnabled ? TEXT("true") : TEXT("false"),
		FAsyncLoadingThreadSettings::Get().bAsyncLoadingThreadEnabled ? TEXT("true") : TEXT("false"),
		FAsyncLoadingThreadSettings::Get().bAsyncPostLoadEnabled ? TEXT("true") : TEXT("false"));
}

FAsyncLoadingThread2::~FAsyncLoadingThread2()
{
	if (Thread)
	{
		ShutdownLoading();
	}

#if USE_NEW_BULKDATA
	FBulkDataBase::SetIoDispatcher(nullptr);
#endif
}

void FAsyncLoadingThread2::ShutdownLoading()
{
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);
	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);

	delete Thread;
	Thread = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(CancelLoadingEvent);
	CancelLoadingEvent = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(ThreadSuspendedEvent);
	ThreadSuspendedEvent = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(ThreadResumedEvent);
	ThreadResumedEvent = nullptr;
}

void FAsyncLoadingThread2::StartThread()
{
	// Make sure the GC sync object is created before we start the thread (apparently this can happen before we call InitUObject())
	FGCCSyncObject::Create();

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FAsyncLoadingThread2::OnPreGarbageCollect);
	FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &FAsyncLoadingThread2::OnPostGarbageCollect);

	if (!FAsyncLoadingThreadSettings::Get().bAsyncLoadingThreadEnabled)
	{
		GlobalPackageStore.FinalizeInitialLoad();
	}
	else if (!Thread)
	{
		UE_LOG(LogStreaming, Log, TEXT("Starting Async Loading Thread."));
		bThreadStarted = true;
		FPlatformMisc::MemoryBarrier();
		
		int32 WorkerCount = 0;
		FParse::Value(FCommandLine::Get(), TEXT("-zenworkercount="), WorkerCount);
		
		if (WorkerCount > 0)
		{
			WorkerZenaphores.AddDefaulted(FMath::Max(3, WorkerCount));
			Workers.Reserve(WorkerCount);
			for (int32 WorkerIndex = 0; WorkerIndex < WorkerCount; ++WorkerIndex)
			{
				if (WorkerIndex == 0)
				{
					Workers.Emplace(GraphAllocator, ProcessExportBundlesEventQueue, IoDispatcher, WorkerZenaphores[0], ActiveWorkersCount);
					ProcessExportBundlesEventQueue.SetZenaphore(&WorkerZenaphores[0]);
					AltEventQueues.Remove(&ProcessExportBundlesEventQueue);
				}
				else
				{
					Workers.Emplace(GraphAllocator, AsyncEventQueue, IoDispatcher, WorkerZenaphores[2], ActiveWorkersCount);
					AsyncEventQueue.SetZenaphore(&WorkerZenaphores[2]);
					AltEventQueues.Remove(&AsyncEventQueue);
				}
				Workers[WorkerIndex].StartThread();
			}
		}

		Thread = FRunnableThread::Create(this, TEXT("FAsyncLoadingThread"), 0, TPri_Normal);
		if (Thread)
		{
			TRACE_SET_THREAD_GROUP(Thread->GetThreadID(), "AsyncLoading");
		}
	}

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - Thread Started: %s, IsInitialLoad: %s"),
		FAsyncLoadingThreadSettings::Get().bAsyncLoadingThreadEnabled ? TEXT("true") : TEXT("false"),
		GIsInitialLoad ? TEXT("true") : TEXT("false"));
}

bool FAsyncLoadingThread2::Init()
{
	return true;
}

void FAsyncLoadingThread2::SuspendWorkers()
{
	if (bWorkersSuspended)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(SuspendWorkers);
	for (FAsyncLoadingThreadWorker& Worker : Workers)
	{
		Worker.SuspendThread();
	}
	while (ActiveWorkersCount > 0)
	{
		FPlatformProcess::SleepNoStats(0);
	}
	bWorkersSuspended = true;
}

void FAsyncLoadingThread2::ResumeWorkers()
{
	if (!bWorkersSuspended)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(ResumeWorkers);
	for (FAsyncLoadingThreadWorker& Worker : Workers)
	{
		Worker.ResumeThread();
	}
	bWorkersSuspended = false;
}

uint32 FAsyncLoadingThread2::Run()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);

	AsyncLoadingThreadID = FPlatformTLS::GetCurrentThreadId();

	FAsyncLoadingThreadState2::Create(GraphAllocator, IoDispatcher);

	TRACE_LOADTIME_START_ASYNC_LOADING();

	FPlatformProcess::SetThreadAffinityMask(FPlatformAffinity::GetAsyncLoadingThreadMask());
	FMemory::SetupTLSCachesOnCurrentThread();

	FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
	
	GlobalPackageStore.FinalizeInitialLoad();

	FZenaphoreWaiter Waiter(AltZenaphore, TEXT("WaitForEvents"));
	bool bIsSuspended = false;
	while (!bStopRequested)
	{
		if (bIsSuspended)
		{
			if (!bSuspendRequested.Load(EMemoryOrder::SequentiallyConsistent) && !IsGarbageCollectionWaiting())
			{
				ThreadResumedEvent->Trigger();
				bIsSuspended = false;
				ResumeWorkers();
			}
			else
			{
				FPlatformProcess::Sleep(0.001f);
			}
		}
		else
		{
			GlobalPackageStore.GetGlobalImportStore().bNeedToHandleGarbageCollect |= IsAsyncLoadingPackages();
			bool bDidSomething = false;
			{
				FGCScopeGuard GCGuard;
				TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
				do
				{
					bDidSomething = false;

					if (QueuedPackagesCounter)
					{
						if (CreateAsyncPackagesFromQueue())
						{
							bDidSomething = true;
						}
					}

					bool bShouldSuspend = false;
					bool bPopped = false;
					do 
					{
						bPopped = false;
						for (FAsyncLoadEventQueue2* Queue : AltEventQueues)
						{
							if (Queue->PopAndExecute(ThreadState))
							{
								bPopped = true;
								bDidSomething = true;
							}

							if (bSuspendRequested.Load(EMemoryOrder::Relaxed) || IsGarbageCollectionWaiting())
							{
								bShouldSuspend = true;
								bPopped = false;
								break;
							}
						}
					} while (bPopped);

					if (bShouldSuspend || bSuspendRequested.Load(EMemoryOrder::Relaxed) || IsGarbageCollectionWaiting())
					{
						SuspendWorkers();
						ThreadSuspendedEvent->Trigger();
						bIsSuspended = true;
						bDidSomething = true;
						break;
					}

					{
						bool bDidExternalRead = false;
						do
						{
							bDidExternalRead = false;
							FAsyncPackage2* Package = nullptr;
							if (ExternalReadQueue.Peek(Package))
							{
								TRACE_CPUPROFILER_EVENT_SCOPE(ProcessExternalReads);

								FAsyncPackage2::EExternalReadAction Action =
									bDidSomething ?
									FAsyncPackage2::ExternalReadAction_Poll :
									FAsyncPackage2::ExternalReadAction_Wait;

								EAsyncPackageState::Type Result = Package->ProcessExternalReads(Action);
								if (Result == EAsyncPackageState::Complete)
								{
									ExternalReadQueue.Pop();
									bDidExternalRead = true;
									bDidSomething = true;
								}
							}
						} while (bDidExternalRead);
					}

				} while (bDidSomething);
			}

			const bool bWaitingForIo = WaitingForIoBundleCounter.GetValue() > 0;
			const bool bWaitingForPostLoad = WaitingForPostLoadCounter.GetValue() > 0;
			const bool bIsLoadingAndWaiting = bWaitingForIo || bWaitingForPostLoad;
			if (!bDidSomething)
			{
				if (bIsLoadingAndWaiting)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					ThreadState.ProcessDeferredFrees();

					if (bWaitingForIo)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(WaitingForIo);
						Waiter.Wait();
					}
					else// if (bWaitingForPostLoad)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(WaitingForPostLoad);
						Waiter.Wait();
					}
				}
				else
				{
					Waiter.Wait();
				}
			}
		}
	}
	return 0;
}

EAsyncPackageState::Type FAsyncLoadingThread2::TickAsyncThreadFromGameThread(bool& bDidSomething)
{
	check(IsInGameThread());
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	
	int32 ProcessedRequests = 0;
	if (AsyncThreadReady.GetValue())
	{
		if (IsGarbageCollectionWaiting() || FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded(TEXT("TickAsyncThreadFromGameThread")))
		{
			Result = EAsyncPackageState::TimeOut;
		}
		else
		{
			FGCScopeGuard GCGuard;
			Result = ProcessAsyncLoadingFromGameThread(ProcessedRequests);
			bDidSomething = bDidSomething || ProcessedRequests > 0;
		}
	}

	return Result;
}

void FAsyncLoadingThread2::Stop()
{
	for (FAsyncLoadingThreadWorker& Worker : Workers)
	{
		Worker.StopThread();
	}
	bSuspendRequested = true;
	bStopRequested = true;
	AltZenaphore.NotifyAll();
}

void FAsyncLoadingThread2::CancelLoading()
{
	check(false);
	// TODO
}

void FAsyncLoadingThread2::SuspendLoading()
{
	UE_CLOG(!IsInGameThread() || IsInSlateThread(), LogStreaming, Fatal, TEXT("Async loading can only be suspended from the main thread"));
	if (!bSuspendRequested)
	{
		bSuspendRequested = true;
		if (IsMultithreaded())
		{
			TRACE_LOADTIME_SUSPEND_ASYNC_LOADING();
			AltZenaphore.NotifyAll();
			ThreadSuspendedEvent->Wait();
		}
	}
}

void FAsyncLoadingThread2::ResumeLoading()
{
	check(IsInGameThread() && !IsInSlateThread());
	if (bSuspendRequested)
	{
		bSuspendRequested = false;
		if (IsMultithreaded())
		{
			ThreadResumedEvent->Wait();
			TRACE_LOADTIME_RESUME_ASYNC_LOADING();
		}
	}
}

float FAsyncLoadingThread2::GetAsyncLoadPercentage(const FName& PackageName)
{
	float LoadPercentage = -1.0f;
	FAsyncPackage2* Package = FindAsyncPackage(PackageName);
	if (Package)
	{
		LoadPercentage = Package->GetLoadPercentage();
	}
	return LoadPercentage;
}

static void VerifyLoadFlagsWhenFinishedLoading()
{
	const EInternalObjectFlags AsyncFlags =
		EInternalObjectFlags::Async | EInternalObjectFlags::AsyncLoading;

	const EObjectFlags LoadIntermediateFlags = 
		EObjectFlags::RF_NeedLoad | EObjectFlags::RF_WillBeLoaded |
		EObjectFlags::RF_NeedPostLoad | RF_NeedPostLoadSubobjects;

	for (int32 ObjectIndex = 0; ObjectIndex < GUObjectArray.GetObjectArrayNum(); ++ObjectIndex)
	{
		FUObjectItem* ObjectItem = &GUObjectArray.GetObjectItemArrayUnsafe()[ObjectIndex];
		if (UObject* Obj = static_cast<UObject*>(ObjectItem->Object))
		{
			const EInternalObjectFlags InternalFlags = Obj->GetInternalFlags();
			const EObjectFlags Flags = Obj->GetFlags();
			const bool bHasAnyAsyncFlags = !!(InternalFlags & AsyncFlags);
			const bool bHasAnyLoadIntermediateFlags = !!(Flags & LoadIntermediateFlags);
			const bool bWasLoaded = !!(Flags & RF_WasLoaded);
			const bool bLoadCompleted = !!(Flags & RF_LoadCompleted);

			UE_CLOG(bHasAnyLoadIntermediateFlags, LogStreaming, Warning,
				TEXT("Object '%s' (ObjectFlags=%d, InternalObjectFlags=%d) should not have any load flags now")
				TEXT(", or this check is incorrectly reached during active loading."),
				*Obj->GetFullName(),
				Flags,
				InternalFlags);

			if (bWasLoaded)
			{
				const bool bIsPackage = Obj->IsA(UPackage::StaticClass());

				UE_CLOG(!bIsPackage && !bLoadCompleted, LogStreaming, Warning,
					TEXT("Object '%s' (ObjectFlags=%d, InternalObjectFlags=%d) is a serialized object and should be completely loaded now")
					TEXT(", or this check is incorrectly reached during active loading."),
					*Obj->GetFullName(),
					Flags,
					InternalFlags);

				UE_CLOG(bHasAnyAsyncFlags, LogStreaming, Warning,
					TEXT("Object '%s' (ObjectFlags=%d, InternalObjectFlags=%d) is a serialized object and should not have any async flags now")
					TEXT(", or this check is incorrectly reached during active loading."),
					*Obj->GetFullName(),
					Flags,
					InternalFlags);
			}
		}
	}
	UE_LOG(LogStreaming, Log, TEXT("Verified load flags when finished active loading."));
}

void FGlobalImportStore::OnPreGarbageCollect(bool bInIsLoadingPackages)
{
	if (!bNeedToHandleGarbageCollect && !bInIsLoadingPackages)
	{
		return;
	}
	bNeedToHandleGarbageCollect = bInIsLoadingPackages;

	int32 NumWeak = 0;
	for (FGlobalImport& GlobalImport : PublicExports)
	{
		UObject* Object = GlobalImport.GetObjectIfRawPointer();
		if (!Object)
		{
			continue;
		}

		if (GlobalImport.GetRefCount() > 0)
		{
			// Import objects in packages currently being loaded already have the Async flag set.
			// They will never be destroyed during GC, and the object pointers are safe to keep.
			if (!Object->HasAnyInternalFlags(EInternalObjectFlags::Async))
			{
				// Mark object to be kept alive during GC
				Object->SetInternalFlags(EInternalObjectFlags::Async);
				KeepAliveObjects.Add(Object);
			}
		}
		else
		{
			// Convert object pointer to weak since object may get destroyed during GC
			check(!Object->HasAnyInternalFlags(EInternalObjectFlags::Async));
			GlobalImport.MakeWeak();
			++NumWeak;
		}
	}

	if (!bInIsLoadingPackages)
	{
		check(KeepAliveObjects.Num() == 0);
	}

#if ALT2_VERIFY_ASYNC_FLAGS
	if (!bInIsLoadingPackages)
	{
		for (FGlobalImport& GlobalImport : PublicExports)
		{
			check(GlobalImport.GetObjectIfRawPointer() == nullptr);
			check(GlobalImport.GetRefCount() == 0);
		}
		VerifyLoadFlagsWhenFinishedLoading();
	}
#endif

	UE_LOG(LogStreaming, Display, TEXT("FGlobalImportStore::OnPreGarbageCollect - Marked %d objects to keep, made %d object pointers weak"),
		KeepAliveObjects.Num(), NumWeak);
}

void FGlobalImportStore::OnPostGarbageCollect()
{
	if (KeepAliveObjects.Num() == 0)
	{
		return;
	}
	check(bNeedToHandleGarbageCollect);

	for (UObject* Object : KeepAliveObjects)
	{
		Object->ClearInternalFlags(EInternalObjectFlags::Async);
	}

	const int32 UnmarkedCount = KeepAliveObjects.Num();
	KeepAliveObjects.Reset();
	UE_LOG(LogStreaming, Log, TEXT("FGlobalImportStore::UpdateGlobalImportsPostGC - Unmarked %d objects"),
		UnmarkedCount);
}

void FAsyncLoadingThread2::OnPreGarbageCollect()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AltPreGC);
	const bool bIsAsyncLoadingPackages = IsAsyncLoadingPackages();
	GlobalPackageStore.GetGlobalImportStore().OnPreGarbageCollect(bIsAsyncLoadingPackages);
}

void FAsyncLoadingThread2::OnPostGarbageCollect()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AltPostGC);
	GlobalPackageStore.GetGlobalImportStore().OnPostGarbageCollect();
}

/**
 * Call back into the async loading code to inform of the creation of a new object
 * @param Object		Object created
 * @param bSubObject	Object created as a sub-object of a loaded object
 */
void FAsyncLoadingThread2::NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObject)
{
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	if (!ThreadContext.AsyncPackage)
	{
		// Something is creating objects on the async loading thread outside of the actual async loading code
		// e.g. ShaderCodeLibrary::OnExternalReadCallback doing FTaskGraphInterface::Get().WaitUntilTaskCompletes(Event);
		// TODO: Change the code in this callback to only ever set the AsyncLoading flag on serialized objects in the current package
		return;
	}

	// Mark objects created during async loading process (e.g. from within PostLoad or CreateExport) as async loaded so they 
	// cannot be found. This requires also keeping track of them so we can remove the async loading flag later one when we 
	// finished routing PostLoad to all objects.
	if (!bSubObject)
	{
		Object->SetInternalFlags(EInternalObjectFlags::AsyncLoading);
	}
	FAsyncPackage2* AsyncPackage2 = (FAsyncPackage2*)ThreadContext.AsyncPackage;
	AsyncPackage2->AddOwnedObjectFromCallback(Object, /*bForceAdd*/ bSubObject);
}

void FAsyncLoadingThread2::FireCompletedCompiledInImport(void* AsyncPackage, FPackageIndex Import)
{
	int32 ExportNodeIndex = Import.ToImport();
	static_cast<FAsyncPackage2*>(AsyncPackage)->GetNode(ExportNodeIndex)->ReleaseBarrier();
}

/*-----------------------------------------------------------------------------
	FAsyncPackage implementation.
-----------------------------------------------------------------------------*/

/**
* Constructor
*/
FAsyncPackage2::FAsyncPackage2(
	const FAsyncPackageDesc2& InDesc,
	FAsyncLoadingThread2& InAsyncLoadingThread,
	FAsyncLoadEventGraphAllocator& InGraphAllocator,
	const FAsyncLoadEventSpec* EventSpecs)
: Desc(InDesc)
, StoreEntry(InAsyncLoadingThread.GlobalPackageStore.GetStoreEntry(Desc.PackageIdToLoad))
, LinkerRoot(nullptr)
, PostLoadIndex(0)
, DeferredPostLoadIndex(0)
, DeferredFinalizeIndex(0)
, DeferredClusterIndex(0)
, bHasClusterObjects(false)
, bLoadHasFailed(false)
, bLoadHasFinished(false)
, bCreatedLinkerRoot(false)
, LoadStartTime(0.0)
, LoadPercentage(0)
, AsyncLoadingThread(InAsyncLoadingThread)
, GraphAllocator(InGraphAllocator)
, ImportStore(AsyncLoadingThread.GlobalPackageStore, StoreEntry)
, AsyncPackageLoadingState(EAsyncPackageLoadingState2::NewPackage)
, bAllExportsSerialized(false)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(NewAsyncPackage);
	TRACE_LOADTIME_NEW_ASYNC_PACKAGE(this, InDesc.NameToLoad);
	AddRequestID(InDesc.RequestID);

	ExportBundleCount = StoreEntry.ExportBundles.Num();
	ExportCount = StoreEntry.ExportCount;
	Exports.AddDefaulted(ExportCount);
	OwnedObjects.Reserve(ExportCount + 1); // +1 for UPackage

	CreateNodes(EventSpecs);
}

void FAsyncPackage2::CreateNodes(const FAsyncLoadEventSpec* EventSpecs)
{
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CreateNodes);
		ExportBundleNodeCount = ExportBundleCount * EEventLoadNode2::ExportBundle_NumPhases;

		PackageNodes = GraphAllocator.AllocNodes(EEventLoadNode2::Package_NumPhases + ExportBundleNodeCount);
		for (int32 Phase = 0; Phase < EEventLoadNode2::Package_NumPhases; ++Phase)
		{
			new (PackageNodes + Phase) FEventLoadNode2(EventSpecs + Phase, this, -1);
		}

		FEventLoadNode2* ExportsSerializedNode = PackageNodes + EEventLoadNode2::Package_ExportsSerialized;
		FEventLoadNode2* StartPostLoadNode = PackageNodes + EEventLoadNode2::Package_PostLoad;

		StartPostLoadNode->AddBarrier();

		FEventLoadNode2* DeleteNode = PackageNodes + EEventLoadNode2::Package_Delete;
		DeleteNode->AddBarrier();

		ExportBundleNodes = PackageNodes + EEventLoadNode2::Package_NumPhases;
		for (int32 ExportBundleIndex = 0; ExportBundleIndex < ExportBundleCount; ++ExportBundleIndex)
		{
			uint32 NodeIndex = EEventLoadNode2::ExportBundle_NumPhases * ExportBundleIndex;
			FEventLoadNode2* ProcessNode = ExportBundleNodes + NodeIndex + EEventLoadNode2::ExportBundle_Process;
			new (ProcessNode) FEventLoadNode2(EventSpecs + EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_Process, this, ExportBundleIndex);
			ProcessNode->AddBarrier();
		}
		ExportsSerializedNode->AddBarrier();
	}
}

FAsyncPackage2::~FAsyncPackage2()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DeleteAsyncPackage);

	check(RefCount == 0);

	FAsyncLoadingThreadState2::Get()->DeferredFreeNodes.Add(MakeTuple(PackageNodes, EEventLoadNode2::Package_NumPhases + ExportBundleNodeCount));

	TRACE_LOADTIME_DESTROY_ASYNC_PACKAGE(this);

	MarkRequestIDsAsComplete();
	
	check(OwnedObjects.Num() == 0);
}

void FAsyncPackage2::ClearImportedPackages()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ClearImportedPackages);
	// TODO: Use AsyncPackage pointer from global id, namelookup entry has already been removed
#if 0
	FPackageStore& GlobalPackageStore = AsyncLoadingThread.GlobalPackageStore;
	int32 ImportedPackageCount = 0;
	int32* Imports = GlobalPackageStore.GetPackageImportedPackages(PackageId, ImportedPackageCount);
	TArray<FAsyncPackage2*> TempImportedAsyncPackages;
	for (int32 LocalImportIndex = 0; LocalImportIndex < ImportedPackageCount; ++LocalImportIndex)
	{
		int32 EntryIndex = Imports[LocalImportIndex];
		FPackageStoreEntry& Entry = GlobalPackageStore.StoreEntries[EntryIndex];
		FAsyncPackage2* ImportedAsyncPackage = AsyncLoadingThread.FindAsyncPackage(Entry.Name);
		TempImportedAsyncPackages.Add(ImportedAsyncPackage);
		if (ImportedAsyncPackage)
		{
			// ImportedAsyncPackage->ReleaseRef();
		}
	}
	ensure(TempImportedAsyncPackages == ImportedAsyncPackages);
#else
	for (FAsyncPackage2* ImportedAsyncPackage : ImportedAsyncPackages)
	{
		ImportedAsyncPackage->ReleaseRef();
	}
	ImportedAsyncPackages.Empty();
#endif
	ImportStore.ClearReferences();
}

void FAsyncPackage2::ClearOwnedObjects()
{
	for (UObject* Object : OwnedObjects)
	{
		const EObjectFlags Flags = Object->GetFlags();
		const EInternalObjectFlags InternalFlags = Object->GetInternalFlags();
		EInternalObjectFlags InternalFlagsToClear = EInternalObjectFlags::None;

		check(!(Flags & (RF_NeedPostLoad | RF_NeedPostLoadSubobjects)));
		if (!!(InternalFlags & EInternalObjectFlags::AsyncLoading))
		{
			check(!(Flags & RF_WasLoaded));
			InternalFlagsToClear |= EInternalObjectFlags::AsyncLoading;
		}

		if (!!(InternalFlags & EInternalObjectFlags::Async))
		{
			InternalFlagsToClear |= EInternalObjectFlags::Async;
		}
		Object->ClearInternalFlags(InternalFlagsToClear);
	}
	OwnedObjects.Empty();
}

void FAsyncPackage2::AddRequestID(int32 Id)
{
	if (Id > 0)
	{
		if (Desc.RequestID == INDEX_NONE)
		{
			// For debug readability
			Desc.RequestID = Id;
		}
		RequestIDs.Add(Id);
		AsyncLoadingThread.AddPendingRequest(Id);
		TRACE_LOADTIME_ASYNC_PACKAGE_REQUEST_ASSOCIATION(this, Id);
	}
}

void FAsyncPackage2::MarkRequestIDsAsComplete()
{
	AsyncLoadingThread.RemovePendingRequests(RequestIDs);
	RequestIDs.Reset();
}

/**
 * @return Time load begun. This is NOT the time the load was requested in the case of other pending requests.
 */
double FAsyncPackage2::GetLoadStartTime() const
{
	return LoadStartTime;
}

#if WITH_EDITOR 
void FAsyncPackage2::GetLoadedAssets(TArray<FWeakObjectPtr>& AssetList)
{
}
#endif

/**
 * Begin async loading process. Simulates parts of BeginLoad.
 *
 * Objects created during BeginAsyncLoad and EndAsyncLoad will have EInternalObjectFlags::AsyncLoading set
 */
void FAsyncPackage2::BeginAsyncLoad()
{
	if (IsInGameThread())
	{
		AsyncLoadingThread.EnterAsyncLoadingTick();
	}

	// this won't do much during async loading except increase the load count which causes IsLoading to return true
	FUObjectSerializeContext* LoadContext = GetSerializeContext();
	BeginLoad(LoadContext);
}

/**
 * End async loading process. Simulates parts of EndLoad(). FinishObjects 
 * simulates some further parts once we're fully done loading the package.
 */
void FAsyncPackage2::EndAsyncLoad()
{
	check(IsAsyncLoading());

	// this won't do much during async loading except decrease the load count which causes IsLoading to return false
	FUObjectSerializeContext* LoadContext = GetSerializeContext();
	EndLoad(LoadContext);

	if (IsInGameThread())
	{
		AsyncLoadingThread.LeaveAsyncLoadingTick();
	}
}

void FAsyncPackage2::CreateUPackage(const FPackageSummary* PackageSummary)
{
	check(!LinkerRoot);

	// Try to find existing package or create it if not already present.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackageFind);
		LinkerRoot = FindObjectFast<UPackage>(nullptr, Desc.Name);
	}
	if (!LinkerRoot)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackageCreate);
		LinkerRoot = NewObject<UPackage>(/*Outer*/nullptr, Desc.Name, RF_Public);
		// LinkerRoot->FileName = Desc.Name;
		LinkerRoot->SetPackageId(Desc.PackageIdToLoad);
		LinkerRoot->SetPackageFlagsTo(PackageSummary->PackageFlags);
		LinkerRoot->LinkerPackageVersion = GPackageFileUE4Version;
		LinkerRoot->LinkerLicenseeVersion = GPackageFileLicenseeUE4Version;
		// LinkerRoot->LinkerCustomVersion = PackageSummaryVersions; // only if (!bCustomVersionIsLatest)
		LinkerRoot->SetFlags(RF_WasLoaded);
		bCreatedLinkerRoot = true;
	}
	else
	{
		check(LinkerRoot->GetPackageId() == Desc.PackageIdToLoad);
		check(LinkerRoot->GetPackageFlags() == PackageSummary->PackageFlags);
		check(LinkerRoot->LinkerPackageVersion == GPackageFileUE4Version);
		check(LinkerRoot->LinkerLicenseeVersion == GPackageFileLicenseeUE4Version);
		check(LinkerRoot->HasAnyFlags(RF_WasLoaded));
	}

	AddOwnedObjectWithAsyncFlag(LinkerRoot, /*bForceAdd*/ !bCreatedLinkerRoot);
	check(LinkerRoot->HasAnyInternalFlags(EInternalObjectFlags::Async));

	if (bCreatedLinkerRoot)
	{
		UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("CreateUPackage: AddPackage"),
			TEXT("New UPackage created."));
	}
	else
	{
		UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("CreateUPackage: UpdatePackage"),
			TEXT("Existing UPackage updated."));
	}
}

EAsyncPackageState::Type FAsyncPackage2::ProcessExternalReads(EExternalReadAction Action)
{
	double WaitTime;
	if (Action == ExternalReadAction_Poll)
	{
		WaitTime = -1.f;
	}
	else// if (Action == ExternalReadAction_Wait)
	{
		WaitTime = 0.f;
	}

	while (ExternalReadIndex < ExternalReadDependencies.Num())
	{
		FExternalReadCallback& ReadCallback = ExternalReadDependencies[ExternalReadIndex];
		if (!ReadCallback(WaitTime))
		{
			return EAsyncPackageState::TimeOut;
		}
		++ExternalReadIndex;
	}

	ExternalReadDependencies.Empty();
	GetNode(Package_ExportsSerialized)->ReleaseBarrier();
	return EAsyncPackageState::Complete;
}

/**
 * Route PostLoad to all loaded objects. This might load further objects!
 *
 * @return true if we finished calling PostLoad on all loaded objects and no new ones were created, false otherwise
 */
EAsyncPackageState::Type FAsyncPackage2::PostLoadObjects()
{
	if (bLoadHasFailed)
	{
		return EAsyncPackageState::Complete;
	}

	LLM_SCOPE(ELLMTag::UObject);

	SCOPED_LOADTIMER(PostLoadObjectsTime);

	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	TGuardValue<bool> GuardIsRoutingPostLoad(ThreadContext.IsRoutingPostLoad, true);

	FUObjectSerializeContext* LoadContext = GetSerializeContext();

	const bool bAsyncPostLoadEnabled = FAsyncLoadingThreadSettings::Get().bAsyncPostLoadEnabled;
	const bool bIsMultithreaded = AsyncLoadingThread.IsMultithreaded();

	// PostLoad objects.
	while (PostLoadIndex < ExportCount && !FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded(TEXT("PostLoadObject")))
	{
		const FExportObject& Export = Exports[PostLoadIndex++];
		if (Export.bFiltered | Export.bExportLoadFailed)
		{
			continue;
		}

		UObject* Object = Export.Object;
		check(Object);
		check(!Object->HasAnyFlags(RF_NeedLoad));
		if (!Object->HasAnyFlags(RF_NeedPostLoad))
		{
			continue;
		}

		check(Object->IsReadyForAsyncPostLoad());
		if (!bIsMultithreaded || (bAsyncPostLoadEnabled && CanPostLoadOnAsyncLoadingThread(Object)))
		{
			ThreadContext.CurrentlyPostLoadedObjectByALT = Object;
			{
				TRACE_LOADTIME_POSTLOAD_EXPORT_SCOPE(Object);
				Object->ConditionalPostLoad();
				Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
			}
			ThreadContext.CurrentlyPostLoadedObjectByALT = nullptr;
		}
	}

	return (PostLoadIndex == ExportCount) ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
}

EAsyncPackageState::Type FAsyncPackage2::PostLoadDeferredObjects()
{
	if (bLoadHasFailed)
	{
		FSoftObjectPath::InvalidateTag();
		FUniqueObjectGuid::InvalidateTag();
		return EAsyncPackageState::Complete;
	}

	SCOPED_LOADTIMER(PostLoadDeferredObjectsTime);

	FAsyncPackageScope2 PackageScope(this);

	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	TGuardValue<bool> GuardIsRoutingPostLoad(PackageScope.ThreadContext.IsRoutingPostLoad, true);
	FAsyncLoadingTickScope2 InAsyncLoadingTick(AsyncLoadingThread);

	FUObjectSerializeContext* LoadContext = GetSerializeContext();

	while (DeferredPostLoadIndex < ExportCount && 
		!AsyncLoadingThread.IsAsyncLoadingSuspended() &&
		!FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded(TEXT("PostLoadDeferredObjects")))
	{
		const FExportObject& Export = Exports[DeferredPostLoadIndex++];
		if (Export.bFiltered | Export.bExportLoadFailed)
		{
			continue;
		}

		UObject* Object = Export.Object;
		check(Object);
		check(!Object->HasAnyFlags(RF_NeedLoad));
		if (Object->HasAnyFlags(RF_NeedPostLoad))
		{
			PackageScope.ThreadContext.CurrentlyPostLoadedObjectByALT = Object;
			{
				TRACE_LOADTIME_POSTLOAD_EXPORT_SCOPE(Object);
				Object->ConditionalPostLoad();
			}
			PackageScope.ThreadContext.CurrentlyPostLoadedObjectByALT = nullptr;
		}
		Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
	}

	Result = (DeferredPostLoadIndex == ExportCount) ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;

	if (Result == EAsyncPackageState::Complete)
	{
		TArray<UObject*> CDODefaultSubobjects;
		// Clear async loading flags (we still want RF_Async, but EInternalObjectFlags::AsyncLoading can be cleared)
		while (DeferredFinalizeIndex < ExportCount &&
			!AsyncLoadingThread.IsAsyncLoadingSuspended() &&
			!FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded(TEXT("PostLoadDeferredObjects")))
		{
			const FExportObject& Export = Exports[DeferredFinalizeIndex++];
			if (Export.bFiltered | Export.bExportLoadFailed)
			{
				continue;
			}

			UObject* Object = Export.Object;

			// CDO need special handling, no matter if it's listed in DeferredFinalizeObjects or created here for DynamicClass
			UObject* CDOToHandle = nullptr;

			// Dynamic Class doesn't require/use pre-loading (or post-loading). 
			// The CDO is created at this point, because now it's safe to solve cyclic dependencies.
			if (UDynamicClass* DynamicClass = Cast<UDynamicClass>(Object))
			{
				check((DynamicClass->ClassFlags & CLASS_Constructed) != 0);

				//native blueprint 

				check(DynamicClass->HasAnyClassFlags(CLASS_TokenStreamAssembled));
				// this block should be removed entirely when and if we add the CDO to the fake export table
				CDOToHandle = DynamicClass->GetDefaultObject(false);
				UE_CLOG(!CDOToHandle, LogStreaming, Fatal, TEXT("EDL did not create the CDO for %s before it finished loading."), *DynamicClass->GetFullName());
				CDOToHandle->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
			}
			else
			{
				CDOToHandle = ((Object != nullptr) && Object->HasAnyFlags(RF_ClassDefaultObject)) ? Object : nullptr;
			}

			// Clear AsyncLoading in CDO's subobjects.
			if(CDOToHandle != nullptr)
			{
				CDOToHandle->GetDefaultSubobjects(CDODefaultSubobjects);
				for (UObject* SubObject : CDODefaultSubobjects)
				{
					if (SubObject && SubObject->HasAnyInternalFlags(EInternalObjectFlags::AsyncLoading))
					{
						SubObject->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
					}
				}
				CDODefaultSubobjects.Reset();
			}
		}
		if (DeferredFinalizeIndex == ExportCount)
		{
			Result = EAsyncPackageState::Complete;
		}
		else
		{
			Result = EAsyncPackageState::TimeOut;
		}

		// Mark package as having been fully loaded and update load time.
		if (Result == EAsyncPackageState::Complete && LinkerRoot && !bLoadHasFailed)
		{
			LinkerRoot->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
			LinkerRoot->MarkAsFullyLoaded();			
			LinkerRoot->SetLoadTime(FPlatformTime::Seconds() - LoadStartTime);

			if (CanCreateObjectClusters())
			{
				for (const FExportObject& Export : Exports)
				{
					if (!(Export.bFiltered | Export.bExportLoadFailed) && Export.Object->CanBeClusterRoot())
					{
						bHasClusterObjects = true;
						break;
					}
				}
			}
		}

		FSoftObjectPath::InvalidateTag();
		FUniqueObjectGuid::InvalidateTag();
	}

	return Result;
}

EAsyncPackageState::Type FAsyncPackage2::CreateClusters()
{
	while (DeferredClusterIndex < ExportCount &&
			!AsyncLoadingThread.IsAsyncLoadingSuspended() &&
			!FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded(TEXT("CreateClusters")))
	{
		const FExportObject& Export = Exports[DeferredClusterIndex++];

		if (!(Export.bFiltered | Export.bExportLoadFailed) && Export.Object->CanBeClusterRoot())
		{
			Export.Object->CreateCluster();
		}
	}

	return DeferredClusterIndex == ExportCount ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
}

EAsyncPackageState::Type FAsyncPackage2::FinishObjects()
{
	SCOPED_LOADTIMER(FinishObjectsTime);

	EAsyncLoadingResult::Type LoadingResult;
	if (!bLoadHasFailed)
	{
		LoadingResult = EAsyncLoadingResult::Succeeded;
	}
	else
	{		
		// Clean up UPackage so it can't be found later
		if (LinkerRoot && !LinkerRoot->IsRooted())
		{
			if (bCreatedLinkerRoot)
			{
				LinkerRoot->ClearFlags(RF_NeedPostLoad | RF_NeedLoad | RF_NeedPostLoadSubobjects);
				LinkerRoot->MarkPendingKill();
				LinkerRoot->Rename(*MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass()).ToString(), nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders | REN_NonTransactional);
			}
		}

		LoadingResult = EAsyncLoadingResult::Failed;
	}

	for (UObject* Object : OwnedObjects)
	{
		if (!Object->HasAnyFlags(RF_NeedPostLoad | RF_NeedPostLoadSubobjects))
		{
			Object->ClearInternalFlags(EInternalObjectFlags::AsyncLoading);
		}
	}

	return EAsyncPackageState::Complete;
}

void FAsyncPackage2::CallCompletionCallbacks(EAsyncLoadingResult::Type LoadingResult)
{
	checkSlow(!IsInAsyncLoadingThread());

	UPackage* LoadedPackage = (!bLoadHasFailed) ? LinkerRoot : nullptr;
	for (FCompletionCallback& CompletionCallback : CompletionCallbacks)
	{
		CompletionCallback->ExecuteIfBound(Desc.Name, LoadedPackage, LoadingResult);
	}
}

UPackage* FAsyncPackage2::GetLoadedPackage()
{
	UPackage* LoadedPackage = (!bLoadHasFailed) ? LinkerRoot : nullptr;
	return LoadedPackage;
}

void FAsyncPackage2::Cancel()
{
	// Call any completion callbacks specified.
	bLoadHasFailed = true;
	const EAsyncLoadingResult::Type Result = EAsyncLoadingResult::Canceled;
	CallCompletionCallbacks(Result);

	if (LinkerRoot)
	{
		if (bCreatedLinkerRoot)
		{
			LinkerRoot->ClearFlags(RF_WasLoaded);
			LinkerRoot->bHasBeenFullyLoaded = false;
			LinkerRoot->Rename(*MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass()).ToString(), nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders | REN_NonTransactional);
		}
	}
}

void FAsyncPackage2::AddCompletionCallback(TUniquePtr<FLoadPackageAsyncDelegate>&& Callback)
{
	// This is to ensure that there is no one trying to subscribe to a already loaded package
	//check(!bLoadHasFinished && !bLoadHasFailed);
	CompletionCallbacks.Emplace(MoveTemp(Callback));
}

void FAsyncPackage2::UpdateLoadPercentage()
{
	// PostLoadCount is just an estimate to prevent packages to go to 100% too quickly
	// We may never reach 100% this way, but it's better than spending most of the load package time at 100%
	float NewLoadPercentage = 0.0f;
	// It's also possible that we got so many objects to PostLoad that LoadPercantage will actually drop
	LoadPercentage = FMath::Max(NewLoadPercentage, LoadPercentage);
}

int32 FAsyncLoadingThread2::LoadPackage(const FString& InName, const FGuid* InGuid, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, EPackageFlags InPackageFlags, int32 InPIEInstanceID, int32 InPackagePriority)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackage);

	static bool bOnce = false;
	if (!bOnce)
	{
		bOnce = true;
		FGCObject::StaticInit(); // otherwise this thing is created during async loading, but not associated with a package
	}

	int32 RequestID = INDEX_NONE;

	// happy path where all inputs are actual package names
	FName PackageName = FName(*InName);
	FName PackageNameToLoad = InPackageToLoadFrom ? FName(InPackageToLoadFrom) : PackageName;
	FPackageId PackageIdToLoad = GlobalPackageStore.FindPackageId(PackageNameToLoad);
	FPackageId PackageId = PackageName != PackageNameToLoad ? GlobalPackageStore.FindPackageId(PackageName) : PackageIdToLoad;

	// fixup for PackageNameToLoad and PackageIdToLoad to handle any input string that can be converted to a long package name
	if (!PackageIdToLoad.IsValid())
	{
		FString PackageToLoadFrom = PackageNameToLoad.ToString();
		if (!FPackageName::IsValidLongPackageName(PackageToLoadFrom))
		{
			FString NewPackageNameToLoadFrom;
			if (FPackageName::TryConvertFilenameToLongPackageName(PackageToLoadFrom, NewPackageNameToLoadFrom))
			{
				FName NewPackageNameToLoad = *NewPackageNameToLoadFrom;
				FPackageId NewPackageIdToLoad = GlobalPackageStore.FindPackageId(NewPackageNameToLoad);
				if (NewPackageIdToLoad.IsValid())
				{
					PackageIdToLoad = NewPackageIdToLoad;
					PackageNameToLoad = NewPackageNameToLoad;
				}
			}
		}
	}

	// fixup for PackageName and PackageId to handle any input string that can be converted to a long package name
	if (PackageIdToLoad.IsValid() && !PackageId.IsValid())
	{
		FName NewPackageName = PackageName;

		FString PackageNameStr = PackageName.ToString();
		bool bIsValidPackageName = FPackageName::IsValidLongPackageName(PackageNameStr);
		if (!bIsValidPackageName)
		{
			FString NewPackageNameStr;
			if (FPackageName::TryConvertFilenameToLongPackageName(PackageNameStr, NewPackageNameStr))
			{
				NewPackageName = *NewPackageNameStr;
				bIsValidPackageName = true;
			}
		}

		if (bIsValidPackageName)
		{
			FPackageId NewPackageId = GlobalPackageStore.FindOrAddPackageId(NewPackageName);
			PackageId = NewPackageId;
			PackageName = NewPackageName;
		}
	}

	if (PackageId.IsValid() && PackageIdToLoad.IsValid())
	{
		if (FCoreDelegates::OnAsyncLoadPackage.IsBound())
		{
			FCoreDelegates::OnAsyncLoadPackage.Broadcast(InName);
		}

		// Generate new request ID and add it immediately to the global request list (it needs to be there before we exit
		// this function, otherwise it would be added when the packages are being processed on the async thread).
		RequestID = PackageRequestID.Increment();
		TRACE_LOADTIME_BEGIN_REQUEST(RequestID);
		AddPendingRequest(RequestID);

		// Allocate delegate on Game Thread, it is not safe to copy delegates by value on other threads
		TUniquePtr<FLoadPackageAsyncDelegate> CompletionDelegatePtr;
		if (InCompletionDelegate.IsBound())
		{
			CompletionDelegatePtr.Reset(new FLoadPackageAsyncDelegate(InCompletionDelegate));
		}

		// Add new package request
		FAsyncPackageDesc2 PackageDesc(RequestID, PackageId, PackageIdToLoad, PackageName, PackageNameToLoad, MoveTemp(CompletionDelegatePtr));
		QueuePackage(PackageDesc);

		UE_ASYNC_PACKAGE_LOG(Verbose, PackageDesc, TEXT("LoadPackage: QueuePackage"), TEXT("Package added to pending queue."));
	}
	else
	{
		FAsyncPackageDesc2 PackageDesc(RequestID, PackageId, PackageIdToLoad, PackageName, PackageNameToLoad);
		UE_ASYNC_PACKAGE_LOG(Warning, PackageDesc, TEXT("LoadPackage: SkipPackage"), TEXT("Skipping unknown package, the provided package does not exist"));
		if (InCompletionDelegate.IsBound())
		{
			// Queue completion callback and execute at next process loaded packages call to maintain behavior compatibility with old loader
			FQueuedFailedPackageCallback& QueuedFailedPackageCallback = QueuedFailedPackageCallbacks.AddDefaulted_GetRef();
			QueuedFailedPackageCallback.PackageName = PackageName;
			QueuedFailedPackageCallback.Callback.Reset(new FLoadPackageAsyncDelegate(InCompletionDelegate));
		}
	}

	return RequestID;
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadingFromGameThread(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit)
{
	TickAsyncLoadingFromGameThread(bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
	return IsAsyncLoading() ? EAsyncPackageState::TimeOut : EAsyncPackageState::Complete;
}

void FAsyncLoadingThread2::FlushLoading(int32 RequestId)
{
	if (IsAsyncLoading())
	{
		// Flushing async loading while loading is suspend will result in infinite stall
		UE_CLOG(bSuspendRequested, LogStreaming, Fatal, TEXT("Cannot Flush Async Loading while async loading is suspended"));

		if (RequestId != INDEX_NONE && !ContainsRequestID(RequestId))
		{
			return;
		}

		FCoreDelegates::OnAsyncLoadingFlush.Broadcast();

#if NO_LOGGING == 0
		{
			// Log the flush, but only display once per frame to avoid log spam.
			static uint64 LastFrameNumber = -1;
			if (LastFrameNumber != GFrameNumber)
			{
				UE_LOG(LogStreaming, Display, TEXT("Flushing async loaders."));
				LastFrameNumber = GFrameNumber;
			}
			else
			{
				UE_LOG(LogStreaming, Log, TEXT("Flushing async loaders."));
			}
		}
#endif

		double StartTime = FPlatformTime::Seconds();

		// Flush async loaders by not using a time limit. Needed for e.g. garbage collection.
		{
			while (IsAsyncLoading())
			{
				EAsyncPackageState::Type Result = TickAsyncLoadingFromGameThread(false, false, 0, RequestId);
				if (RequestId != INDEX_NONE && !ContainsRequestID(RequestId))
				{
					break;
				}

				if (IsMultithreaded())
				{
					// Update the heartbeat and sleep. If we're not multithreading, the heartbeat is updated after each package has been processed
					FThreadHeartBeat::Get().HeartBeat();
					FPlatformProcess::SleepNoStats(0.0001f);
				}

				// push stats so that we don't overflow number of tags per thread during blocking loading
				LLM_PUSH_STATS_FOR_ASSET_TAGS();
			}
		}

		double EndTime = FPlatformTime::Seconds();
		double ElapsedTime = EndTime - StartTime;

		check(RequestId != INDEX_NONE || !IsAsyncLoading());
	}
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadingUntilCompleteFromGameThread(TFunctionRef<bool()> CompletionPredicate, float TimeLimit)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessLoadingUntilComplete);
	if (!IsAsyncLoading())
	{
		return EAsyncPackageState::Complete;
	}

	// Flushing async loading while loading is suspend will result in infinite stall
	UE_CLOG(bSuspendRequested, LogStreaming, Fatal, TEXT("Cannot Flush Async Loading while async loading is suspended"));

	if (TimeLimit <= 0.0f)
	{
		// Set to one hour if no time limit
		TimeLimit = 60 * 60;
	}

	while (IsAsyncLoading() && TimeLimit > 0 && !CompletionPredicate())
	{
		double TickStartTime = FPlatformTime::Seconds();
		if (ProcessLoadingFromGameThread(true, true, TimeLimit) == EAsyncPackageState::Complete)
		{
			return EAsyncPackageState::Complete;
		}

		if (IsMultithreaded())
		{
			// Update the heartbeat and sleep. If we're not multithreading, the heartbeat is updated after each package has been processed
			FThreadHeartBeat::Get().HeartBeat();
			FPlatformProcess::SleepNoStats(0.0001f);
		}

		TimeLimit -= (FPlatformTime::Seconds() - TickStartTime);
	}

	return TimeLimit <= 0 ? EAsyncPackageState::TimeOut : EAsyncPackageState::Complete;
}

IAsyncPackageLoader* MakeAsyncPackageLoader2(FIoDispatcher& InIoDispatcher)
{
	return new FAsyncLoadingThread2(InIoDispatcher);
}

#endif //WITH_ASYNCLOADING2

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
PRAGMA_ENABLE_OPTIMIZATION
#endif
