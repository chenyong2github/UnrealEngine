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
#include "IO/IoDispatcher.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectRedirector.h"
#include "Serialization/BulkData.h"
#include "Serialization/LargeMemoryReader.h"
#include "UObject/UObjectClusters.h"

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
//PRAGMA_DISABLE_OPTIMIZATION
#define ALT2_VERIFY_ASYNC_FLAGS
#endif

struct FAsyncPackage2;
class FAsyncLoadingThread2;

class FSimpleArchive
	: public FArchive
{
public:
	FSimpleArchive(const uint8* BufferPtr, uint64 BufferSize)
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		ActiveFPLB->OriginalFastPathLoadBuffer = BufferPtr;
		ActiveFPLB->StartFastPathLoadBuffer = BufferPtr;
		ActiveFPLB->EndFastPathLoadBuffer = BufferPtr + BufferSize;
#endif
	}

	int64 TotalSize() override
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		return ActiveFPLB->EndFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer;
#else
		return 0;
#endif
	}

	int64 Tell() override
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		return ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer;
#else
		return 0;
#endif
	}

	void Seek(int64 Position) override
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		ActiveFPLB->StartFastPathLoadBuffer = ActiveFPLB->OriginalFastPathLoadBuffer + Position;
		check(ActiveFPLB->StartFastPathLoadBuffer <= ActiveFPLB->EndFastPathLoadBuffer);
#endif
	}

	void Serialize(void* Data, int64 Length) override
	{
#if DEVIRTUALIZE_FLinkerLoad_Serialize
		if (!Length || ArIsError)
		{
			return;
		}
		check(ActiveFPLB->StartFastPathLoadBuffer + Length <= ActiveFPLB->EndFastPathLoadBuffer);
		FMemory::Memcpy(Data, ActiveFPLB->StartFastPathLoadBuffer, Length);
		ActiveFPLB->StartFastPathLoadBuffer += Length;
#endif
	}
};

struct FExportMapEntry
{
	uint64 SerialSize;
	int32 ObjectName[2];
	FPackageIndex OuterIndex;
	FPackageIndex ClassIndex;
	FPackageIndex SuperIndex;
	FPackageIndex TemplateIndex;
	int32 GlobalImportIndex;
	EObjectFlags ObjectFlags;
	EExportFilterFlags FilterFlags;
	uint8 Pad[7];
};

struct FExportBundleHeader
{
	uint32 FirstEntryIndex;
	uint32 EntryCount;
};

struct FExportObject
{
	UObject* Object = nullptr;
	bool bFiltered = false;
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
		FUObjectItem* ObjectItem = GUObjectArray.IndexToObject(WeakPointer.ObjectIndex);
		int32 ActualSerialNumber = GUObjectArray.GetSerialNumber(WeakPointer.ObjectIndex);
		Object = (ObjectItem && ActualSerialNumber == WeakPointer.SerialNumber) ? (UObject*)ObjectItem->Object : nullptr;
		bIsWeakPointer = false;
	}
};

struct FPackageStoreEntry
{
	FName Name;
	int32 ExportCount;
	int32 ExportBundleCount;
	int32 FirstExportBundleIndex;
	int32 FirstGlobalImport;
	int32 GlobalImportCount;
	int32 ImportedPackagesCount;
	int32 ImportedPackagesOffset;
};

struct FPackageStoreInitialLoadEntry
{
	int32 ScriptArcsOffset;
	int32 ScriptArcsCount;
};

class FGlobalNameMap
{
public:
	void Load(FIoDispatcher& IoDispatcher)
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

		LoadNameBatch(/* Out */ NameEntries, 
						MakeArrayView(NameBuffer.Data(), NameBuffer.DataSize()),
						MakeArrayView(HashBuffer.Data(), HashBuffer.DataSize()));

		IoDispatcher.FreeBatch(Batch);
	}

	FName GetName(const uint32 NameIndex, const uint32 NameNumber) const
	{
		FNameEntryId NameEntry = NameEntries[NameIndex];
		return FName::CreateFromDisplayId(NameEntry, NameNumber);
	}

	FName FromSerializedName(const FName& SerializedName) const
	{
		const int32 EntryIndex = SerializedName.GetComparisonIndex().ToUnstableInt();
		FNameEntryId NameEntry = NameEntries[EntryIndex];
		return FName::CreateFromDisplayId(NameEntry, SerializedName.GetNumber());
	}

	const TArray<FNameEntryId>& GetNameEntries() const
	{
		return NameEntries;
	}

private:
	TArray<FNameEntryId> NameEntries;
};

struct FGlobalImportStore
{
	/** Serialized data */
	int32 Count = 0;
	int32 ScriptImportCount = 0;
	FName* Names = nullptr;
	FPackageIndex* ScriptImportOuters = nullptr;
	/** Runtime data */
	FGlobalImport* Imports = nullptr;
	/** Reference tracking for GC management */
	TArray<UObject*> KeepAliveObjects;
	bool bNeedToHandleGarbageCollect = false;

	void OnPreGarbageCollect(bool bInIsLoadingPackages);
	void OnPostGarbageCollect();

	void FindAllScriptImports();
};

class FPackageStore
{
public:
	FCriticalSection PackageNameToPackageIdCritical;
	TMap<FName, FPackageId> PackageNameToPackageId;

	FGlobalImportStore ImportStore;
	FIoBuffer InitialLoadIoBuffer;
	FPackageStoreEntry* StoreEntries = nullptr;
	FPackageStoreInitialLoadEntry* InitialLoadStoreEntries = nullptr;
	int32* ImportedPackages = nullptr;
	FExportBundleMetaEntry* ExportBundleMetaEntries = nullptr;
	int32* ScriptArcs = nullptr;
	int32 PackageCount = 0;
	int32 ImportedPackagesCount = 0;
	int32 ScriptArcsCount = 0;

public:
	void Load(FIoDispatcher& IoDispatcher, FGlobalNameMap& GlobalNameMap)
	{
		FIoBuffer IoBuffer;
		FEvent* Event = FPlatformProcess::GetSynchEventFromPool();

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

			Event->Wait();
		}
		
		FLargeMemoryReader GlobalMetaAr(IoBuffer.Data(), IoBuffer.DataSize());
		
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreTocFixup);

			int32 PackageByteCount = 0;
			GlobalMetaAr << PackageByteCount;

			PackageCount = PackageByteCount / sizeof(FPackageStoreEntry);
			StoreEntries = reinterpret_cast<FPackageStoreEntry*>(FMemory::Malloc(PackageByteCount));

			// In-place loading
			GlobalMetaAr.Serialize(StoreEntries, PackageByteCount);

			// FName fixup
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreFNameFixup);
				for (int I = 0; I < PackageCount; ++I)
				{
					StoreEntries[I].Name = GlobalNameMap.FromSerializedName(StoreEntries[I].Name);
				}
			}

			// Global package index
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreGlobalIds);
				// add 10% slack for temp package names
				PackageNameToPackageId.Reserve(PackageCount + PackageCount / 10);
				for (int I = 0; I < PackageCount; ++I)
				{
					PackageNameToPackageId.Add(StoreEntries[I].Name, FPackageId::FromIndex(I));
				}
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreImportedPackagesFixup);

			int32 PackageImportByteCount = 0;
			GlobalMetaAr << PackageImportByteCount;

			ImportedPackagesCount = PackageImportByteCount / sizeof(int32);
			ImportedPackages = reinterpret_cast<int32*>(FMemory::Malloc(PackageImportByteCount));
			GlobalMetaAr.Serialize(ImportedPackages, PackageImportByteCount);
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreGlobalImportNamesFixup);

			int32 ImportByteCount = 0;
			GlobalMetaAr << ImportByteCount; 

			ImportStore.Count = ImportByteCount / sizeof(FName);

			FName* SerializedNames = reinterpret_cast<FName*>(IoBuffer.Data() + GlobalMetaAr.Tell());
			GlobalMetaAr.Seek(GlobalMetaAr.Tell() + ImportByteCount);
			ImportStore.Names = reinterpret_cast<FName*>(FMemory::Malloc(ImportByteCount));

			for (int I = 0; I < ImportStore.Count; ++I)
			{
				ImportStore.Names[I] = GlobalNameMap.FromSerializedName(SerializedNames[I]);
			}
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreInitGlobalImports);
			ImportStore.Imports = new FGlobalImport[ImportStore.Count];
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreBundleMeta);
			
			int32 BundleMetaByteCount;
			GlobalMetaAr << BundleMetaByteCount;
			ExportBundleMetaEntries = reinterpret_cast<FExportBundleMetaEntry*>(FMemory::Malloc(BundleMetaByteCount));
			GlobalMetaAr.Serialize(ExportBundleMetaEntries, BundleMetaByteCount);
		}


		// Load initial loading meta data
		{
			FIoBuffer TempIoBuffer;
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreInitialLoadIo);
			IoDispatcher.ReadWithCallback(
				CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta),
				FIoReadOptions(),
				[Event, &TempIoBuffer](TIoStatusOr<FIoBuffer> Result)
				{
					TempIoBuffer = Result.ConsumeValueOrDie();
					Event->Trigger();
				});

			Event->Wait();
			FPlatformProcess::ReturnSynchEventToPool(Event);
			InitialLoadIoBuffer = TempIoBuffer;
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreInitialLoadFixup);
			uint8* Data = InitialLoadIoBuffer.Data();

			int32 TempPackageCount = *reinterpret_cast<int32*>(Data);
			Data += 4;
			ImportStore.ScriptImportCount = *reinterpret_cast<int32*>(Data);
			Data += 4;

			check(TempPackageCount == PackageCount);

			InitialLoadStoreEntries = reinterpret_cast<FPackageStoreInitialLoadEntry*>(Data);
			Data += TempPackageCount * sizeof(FPackageStoreInitialLoadEntry);

			ImportStore.ScriptImportOuters = reinterpret_cast<FPackageIndex*>(Data);
			Data += ImportStore.ScriptImportCount * sizeof(FPackageIndex);

			ScriptArcs = reinterpret_cast<int32*>(Data);
		}

		FPackageName::DoesPackageExistOverride().BindLambda([this](FName InPackageName)
		{
			return PackageNameToPackageId.Contains(InPackageName);
		});
	}

	void FinalizeInitialLoad()
	{
		ImportStore.FindAllScriptImports();
		InitialLoadStoreEntries = nullptr;
		ScriptArcs = nullptr;
		InitialLoadIoBuffer = FIoBuffer();

		UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - InitialLoad Finalized: Script Imports: %d"),
			ImportStore.ScriptImportCount);
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

	inline FPackageIndex* GetGlobalImportScriptOuters(int32& OutCount) const
	{
		if (GIsInitialLoad)
		{
			OutCount = ImportStore.ScriptImportCount;
			return ImportStore.ScriptImportOuters;
		}
		OutCount = 0;
		return nullptr;
	}

	inline FName* GetGlobalImportNames(int32& OutCount) const
	{
		OutCount = ImportStore.Count;
		return ImportStore.Names;
	}

	inline FGlobalImport* GetGlobalImports()
	{
		return ImportStore.Imports;
	}

	inline FPackageStoreEntry* GetGlobalStoreEntries()
	{
		return StoreEntries;
	}

	inline int32* GetPackageImportedPackages(FPackageId PackageId, int32& OutCount) const 
	{
		FPackageStoreEntry& Entry = StoreEntries[PackageId.ToIndex()];
		OutCount = Entry.ImportedPackagesCount;
		if (Entry.ImportedPackagesCount >= 0)
		{
			return ImportedPackages + Entry.ImportedPackagesOffset / sizeof(int32);
		}
		return nullptr;
	}

	inline int32* GetPackageScriptArcs(FPackageId PackageId, int32& OutCount) const 
	{
		if (ensure(GIsInitialLoad))
		{
			FPackageStoreInitialLoadEntry& Entry = InitialLoadStoreEntries[PackageId.ToIndex()];
			OutCount = Entry.ScriptArcsCount;
			return ScriptArcs + Entry.ScriptArcsOffset / sizeof(int32);
		}
		OutCount = 0;
		return nullptr;
	}

	inline int32 GetPackageExportCount(FPackageId PackageId) const 
	{
		return StoreEntries[PackageId.ToIndex()].ExportCount;
	}

	inline TTuple<FExportBundleMetaEntry*, uint32> GetPackageExportBundleMetaEntries(FPackageId PackageId) const
	{
		const FPackageStoreEntry* StoreEntry = StoreEntries + PackageId.ToIndex();
		return MakeTuple(ExportBundleMetaEntries + StoreEntry->FirstExportBundleIndex, uint32(StoreEntry->ExportBundleCount));
	}
};

struct FPackageImportStore
{
	FPackageStoreEntry* GlobalStoreEntries = nullptr;
	FGlobalImport* GlobalImports = nullptr;
	const FName* GlobalImportNames = nullptr;
	const FPackageIndex* GlobalImportScriptOuters = nullptr;
	const int32* ImportedPackages = nullptr;
	const int32* ImportMap = nullptr;
	FPackageId PackageId;
	int32 GlobalScriptImportCount = 0;
	int32 ImportedPackagesCount = 0;
	int32 ImportMapCount = 0;

	FPackageImportStore(FPackageStore& GlobalPackageStore, FPackageId InPackageId) : PackageId(InPackageId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NewPackageImportStore);
		{
			int32 GlobalImportCount = 0;
			GlobalStoreEntries = GlobalPackageStore.GetGlobalStoreEntries();
			GlobalImports = GlobalPackageStore.GetGlobalImports();
			GlobalImportNames = GlobalPackageStore.GetGlobalImportNames(GlobalImportCount);
		}
		GlobalImportScriptOuters = GlobalPackageStore.GetGlobalImportScriptOuters(GlobalScriptImportCount);
		ImportedPackages = GlobalPackageStore.GetPackageImportedPackages(PackageId, ImportedPackagesCount);
		AddGlobalImportObjectReferences();
	}

	~FPackageImportStore()
	{
		check(!ImportMap);
	}

	UObject* FindImportFromGlobalIndex(int32 GlobalImportIndex);

	inline UObject* GetImportFromGlobalIndex(FPackageIndex GlobalIndex)
	{
		check(GlobalIndex.IsImport());
		const int32 GlobalImportIndex = GlobalIndex.ToImport();
		return GlobalImports[GlobalImportIndex].GetObject();
	}

	inline UObject* GetImportFromGlobalIndex(int32 GlobalImportIndex)
	{
		return GlobalImports[GlobalImportIndex].GetObject();
	}

	inline UObject* FindOrGetImportFromLocalIndex(FPackageIndex LocalIndex)
	{
		check(LocalIndex.IsImport());
		check(ImportMap);
		const int32 LocalImportIndex = LocalIndex.ToImport();
		check(LocalImportIndex < ImportMapCount);
		const int32 GlobalImportIndex = ImportMap[LocalIndex.ToImport()];
		return
			GIsInitialLoad && GlobalImportIndex < GlobalScriptImportCount ?
			FindImportFromGlobalIndex(GlobalImportIndex) :
			GetImportFromGlobalIndex(GlobalImportIndex);
	}

	inline UObject* FindOrGetImportFromGlobalIndex(FPackageIndex GlobalIndex)
	{
		check(GlobalIndex.IsImport());
		const int32 GlobalImportIndex = GlobalIndex.ToImport();
		return
			GIsInitialLoad && GlobalImportIndex < GlobalScriptImportCount ?
			FindImportFromGlobalIndex(GlobalImportIndex) :
			GetImportFromGlobalIndex(GlobalImportIndex);
	}

	void StoreGlobalImportObject(int32 GlobalImportIndex, UObject* InObject)
	{
		if (GlobalImportIndex >= 0)
		{
			GlobalImports[GlobalImportIndex].SetObject(InObject);
		}
	}

	void ClearReferences()
	{
		ReleaseGlobalImportObjectReferences();
	}

private:
	void AddGlobalImportObjectReferences()
	{
		for (int32 LocalPackageIndex = 0; LocalPackageIndex < ImportedPackagesCount; ++LocalPackageIndex)
		{
			int32 GlobalPackageIndex = ImportedPackages[LocalPackageIndex];
			FPackageStoreEntry& Entry = GlobalStoreEntries[GlobalPackageIndex];
			for (int32 ObjectImportIndex = 0; ObjectImportIndex < Entry.GlobalImportCount; ObjectImportIndex++)
			{
				int32 GlobalImportIndex = Entry.FirstGlobalImport + ObjectImportIndex;
				GlobalImports[GlobalImportIndex].AddRef();
			}
		}
		// Add ref counts to own imports to speed up OnPreGarbageCollect
		FPackageStoreEntry& Entry = GlobalStoreEntries[PackageId.ToIndex()];
		for (int32 ObjectImportIndex = 0; ObjectImportIndex < Entry.GlobalImportCount; ObjectImportIndex++)
		{
			int32 GlobalImportIndex = Entry.FirstGlobalImport + ObjectImportIndex;
			GlobalImports[GlobalImportIndex].AddRef();
		}
	}

	void ReleaseGlobalImportObjectReferences()
	{
		for (int32 LocalPackageIndex = 0; LocalPackageIndex < ImportedPackagesCount; ++LocalPackageIndex)
		{
			int32 GlobalPackageIndex = ImportedPackages[LocalPackageIndex];
			FPackageStoreEntry& Entry = GlobalStoreEntries[GlobalPackageIndex];
			for (int32 ObjectImportIndex = 0; ObjectImportIndex < Entry.GlobalImportCount; ObjectImportIndex++)
			{
				int32 GlobalImportIndex = Entry.FirstGlobalImport + ObjectImportIndex;
				GlobalImports[GlobalImportIndex].ReleaseRef();
			}
		}
		// Release ref counts to own imports to speed up OnPreGarbageCollect
		FPackageStoreEntry& Entry = GlobalStoreEntries[PackageId.ToIndex()];
		for (int32 ObjectImportIndex = 0; ObjectImportIndex < Entry.GlobalImportCount; ObjectImportIndex++)
		{
			int32 GlobalImportIndex = Entry.FirstGlobalImport + ObjectImportIndex;
			GlobalImports[GlobalImportIndex].ReleaseRef();
		}
	}
};
	
class FSimpleExportArchive
	: public FSimpleArchive
{
public:
	FSimpleExportArchive(const uint8* BufferPtr, uint64 BufferSize) : FSimpleArchive(BufferPtr, BufferSize) {}

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
			Object = (*Exports)[Index.ToExport()].Object;
		}
		else
		{
			Object = ImportStore->FindOrGetImportFromLocalIndex(Index);
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

	void BadNameIndexError(int32 NameIndex)
	{
		UE_LOG(LogStreaming, Error, TEXT("Bad name index %i/%i"), NameIndex, GlobalNameMap->Num());
	}

	inline virtual FArchive& operator<<(FName& Name) override
	{
		FArchive& Ar = *this;
		int32 NameIndex;
		Ar << NameIndex;
		int32 Number = 0;
		Ar << Number;

		NameIndex = PackageNameMap[NameIndex];

		if (GlobalNameMap->IsValidIndex(NameIndex))
		{
			// if the name wasn't loaded (because it wasn't valid in this context)
			FNameEntryId MappedName = (*GlobalNameMap)[NameIndex];

			// simply create the name from the NameMap's name and the serialized instance number
			Name = FName::CreateFromDisplayId(MappedName, Number);
		}
		else
		{
			Name = FName();
			BadNameIndexError(NameIndex);
			ArIsError = true;
			ArIsCriticalError = true;
		}
		return *this;
	}
	//~ End FArchive::FLinkerLoad Interface

private:
	friend FAsyncPackage2;

	UObject* TemplateForGetArchetypeFromLoader = nullptr;
	FPackageImportStore* ImportStore = nullptr;
	const int32* PackageNameMap = nullptr;
	const TArray<FNameEntryId>* GlobalNameMap = nullptr;
	const FExportObjects* Exports = nullptr;
	TArray<FExternalReadCallback>* ExternalReadDependencies;
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

	void SetTimeLimit(bool bUseTimeLimit, float TimeLimit)
	{

	}

	bool IsTimeLimitExceeded()
	{
		/*static double LastTestTime = -1.0;
		bool bTimeLimitExceeded = false;
		if (bUseTimeLimit)
		{
			double CurrentTime = FPlatformTime::Seconds();
			bTimeLimitExceeded = CurrentTime - InTickStartTime > InTimeLimit;

			if (bTimeLimitExceeded && GWarnIfTimeLimitExceeded)
			{
				IsTimeLimitExceededPrint(InTickStartTime, CurrentTime, LastTestTime, InTimeLimit, InLastTypeOfWorkPerformed, InLastObjectWorkWasPerformedOn);
			}
			LastTestTime = CurrentTime;
		}
		if (!bTimeLimitExceeded)
		{
			bTimeLimitExceeded = IsGarbageCollectionWaiting();
			UE_CLOG(bTimeLimitExceeded, LogStreaming, Verbose, TEXT("Timing out async loading due to Garbage Collection request"));
		}
		return bTimeLimitExceeded;*/
		return false;
	}

	FAsyncLoadEventGraphAllocator& GraphAllocator;
	TArray<TTuple<FEventLoadNode2*, uint32>> DeferredFreeNodes;
	TArray<TTuple<FEventLoadNode2**, uint32>> DeferredFreeArcs;
	TArray<FEventLoadNode2*> NodesToFire;
	bool bShouldFireNodes = true;
	static uint32 TlsSlot;
};

uint32 FAsyncLoadingThreadState2::TlsSlot;

struct FAsyncPackageDesc2
{
	int32 RequestID;
	FPackageId PackageId;
	FPackageId PackageIdToLoad;
	FName Name;
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
/**
* Structure containing intermediate data required for async loading of all exports of a package.
*/

struct FAsyncPackage2
{
	friend struct FScopedAsyncPackageEvent2;
	friend class FAsyncLoadingThread2;

	FAsyncPackage2(const FAsyncPackageDesc2& InDesc,
		FAsyncLoadingThread2& InAsyncLoadingThread,
		IEDLBootNotificationManager& InEDLBootNotificationManager,
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

#if WITH_EDITOR
	/** Gets all assets loaded by this async package, used in the editor */
	void GetLoadedAssets(TArray<FWeakObjectPtr>& AssetList);
#endif

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
	/** Package which is going to have its exports and imports loaded									*/
	UPackage*				LinkerRoot;
	/** Call backs called when we finished loading this package											*/
	using FCompletionCallback = TUniquePtr<FLoadPackageAsyncDelegate>;
	TArray<FCompletionCallback, TInlineAllocator<2>> CompletionCallbacks;
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
#if WITH_EDITORONLY_DATA
	/** Index of the meta-data object within the linkers export table (unset if not yet processed, although may still be INDEX_NONE if there is no meta-data) */
	TOptional<int32> MetaDataIndex;
#endif // WITH_EDITORONLY_DATA
	/** Number of times we recursed to load this package. */
	int32 ReentryCount;
	TArray<FAsyncPackage2*> ImportedAsyncPackages;
	/** List of OwnedObjects = Exports + UPackage + ObjectsCreatedFromExports */
	TArray<UObject*> OwnedObjects;
	/** Cached async loading thread object this package was created by */
	FAsyncLoadingThread2& AsyncLoadingThread;
	IEDLBootNotificationManager& EDLBootNotificationManager;
	FAsyncLoadEventGraphAllocator& GraphAllocator;

	FEventLoadNode2* PackageNodes = nullptr;
	FEventLoadNode2* ExportBundleNodes = nullptr;
	uint32 ExportBundleNodeCount = 0;

	FIoBuffer IoBuffer;
	const uint8* SerialDataPtr = nullptr;

	// FZenLinkerLoad
	TArray<FExternalReadCallback> ExternalReadDependencies;
	int32 ExportCount = 0;
	const FExportMapEntry* ExportMap = nullptr;
	const int32* PackageNameMap = nullptr;
	FExportObjects Exports;
	FPackageImportStore ImportStore;

	FExportBundleMetaEntry* ExportBundleMetaEntries = nullptr;
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
	void EventDrivenSerializeExport(int32 LocalExportIndex, FSimpleExportArchive& Ar);

	UObject* EventDrivenIndexToObject(FPackageIndex Index, bool bCheckSerialized);
	template<class T>
	T* CastEventDrivenIndexToObject(FPackageIndex Index, bool bCheckSerialized)
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
	void SetupScriptArcs();
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
	FAsyncLoadingThread2(FIoDispatcher& IoDispatcher, IEDLBootNotificationManager& InEDLBootNotificationManager);
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
#if WITH_EDITOR
	TArray<FWeakObjectPtr> LoadedAssets;
#endif

	FCriticalSection AsyncPackagesCritical;
	TMap<FPackageId, FAsyncPackage2*> AsyncPackageLookup;

	IEDLBootNotificationManager& EDLBootNotificationManager;

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
	FGlobalNameMap GlobalNameMap;
	FIoDispatcher& IoDispatcher;

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
		return new FAsyncPackage2(Desc, *this, EDLBootNotificationManager, GraphAllocator, EventSpecs.GetData());
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
	FIoStoreEnvironment GlobalEnvironment;
	GlobalEnvironment.InitializeFileEnvironment(FPaths::ProjectDir() / TEXT("global"));
	FIoStatus MountStatus = IoDispatcher.Mount(GlobalEnvironment);
	UE_CLOG(!MountStatus.IsOk(), LogInit, Fatal, TEXT("Failed to initialize I/O dispatcher: '%s'"), *MountStatus.ToString());

#if USE_NEW_BULKDATA
	FBulkDataBase::SetIoDispatcher(&IoDispatcher);
#endif

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadGlobalNameMap);
		GlobalNameMap.Load(IoDispatcher);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStore);
		GlobalPackageStore.Load(IoDispatcher, GlobalNameMap);
	}

	AsyncThreadReady.Increment();

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - Initialized: Packages: %d, GlobalImports: %d, FNames: %d"),
		GlobalPackageStore.PackageCount,
		GlobalPackageStore.ImportStore.Count,
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
	TArray<FAsyncPackageDesc2*> QueueCopy;
	{
		FScopeLock QueueLock(&QueueCritical);
		QueueCopy.Append(QueuedPackages);
		QueuedPackages.Reset();
	}

	for (FAsyncPackageDesc2* PackageRequest : QueueCopy)
	{
		bool bInserted;
		FAsyncPackage2* Package = FindOrInsertPackage(PackageRequest, bInserted);
		--QueuedPackagesCounter;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ImportPackages);
			Package->ImportPackagesRecursive();
		}
		if (bInserted)
		{
			Package->StartLoading();
		}
		delete PackageRequest;

		StartBundleIoRequests();
	}

	return QueueCopy.Num() > 0;
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
			Package->IoBuffer = Result.ConsumeValueOrDie();
			Package->GetExportBundleNode(EEventLoadNode2::ExportBundle_Process, 0)->ReleaseBarrier();
			Package->AsyncLoadingThread.WaitingForIoBundleCounter.Decrement();
		});
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
	if (Spec->bExecuteImmediately && ThreadState)
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
	EAsyncPackageState::Type State = Spec->Func(Package, ImportOrExportIndex);
	check(State == EAsyncPackageState::Complete);
	bDone.Store(1);
	ProcessDependencies(ThreadState);
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
	FGlobalImport* GlobalImports,
	const FPackageIndex* GlobalImportScriptOuters,
	const FName* GlobalImportNames)
{
	FGlobalImport& GlobalImport = GlobalImports[GlobalImportIndex];
	UObject* Object = GlobalImport.GetObject();
	if (!Object)
	{
		const FPackageIndex& OuterIndex = GlobalImportScriptOuters[GlobalImportIndex];
		const FName& ObjectName = GlobalImportNames[GlobalImportIndex];
		if (OuterIndex.IsNull())
		{
			Object = StaticFindObjectFast(UPackage::StaticClass(), nullptr, ObjectName, true);
		}
		else
		{
			UObject* Outer = GFindExistingScriptImport(OuterIndex.ToImport(), GlobalImports, GlobalImportScriptOuters, GlobalImportNames);
			if (Outer)
			{
				Object = StaticFindObjectFast(UObject::StaticClass(), Outer, ObjectName, false, true);
			}
		}
		GlobalImport.SetObject(Object);
	}
	return Object;
}

UObject* FPackageImportStore::FindImportFromGlobalIndex(int32 GlobalImportIndex)
{
	check(GlobalImportScriptOuters);
	check(GlobalImportIndex < GlobalScriptImportCount);
	return GFindExistingScriptImport(
		GlobalImportIndex,
		GlobalImports,
		GlobalImportScriptOuters,
		GlobalImportNames);
}

void FGlobalImportStore::FindAllScriptImports()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindAllScriptImports);
	check(ScriptImportOuters);
	for (int32 GlobalImportIndex = 0; GlobalImportIndex < ScriptImportCount; ++GlobalImportIndex)
	{
		UObject* Object = GFindExistingScriptImport(GlobalImportIndex, Imports, ScriptImportOuters, Names);
#if DO_CHECK
		if (!Object)
		{
			FPackageIndex Outer = ScriptImportOuters[GlobalImportIndex];
			if (Outer.IsNull())
			{
				UE_LOG(LogStreaming, Warning, TEXT("AsyncLoading2 - Failed to find import script package after initial load: %s"),
					*Names[GlobalImportIndex].ToString());
			}
			else
			{
				UE_LOG(LogStreaming, Warning, TEXT("AsyncLoading2 - Failed to find import script object after initial load: %s - %s"),
					*Names[Outer.ToImport()].ToString(),
					*Names[GlobalImportIndex].ToString());
			}
		}
#endif
	}
	ScriptImportOuters = nullptr;
}

void FAsyncPackage2::ImportPackagesRecursive()
{
	if (bHasImportedPackagesRecursive)
	{
		return;
	}
	bHasImportedPackagesRecursive = true;

	FPackageStore& GlobalPackageStore = AsyncLoadingThread.GlobalPackageStore;
	int32 ImportedPackageCount = 0;
	int32* Imports = GlobalPackageStore.GetPackageImportedPackages(Desc.PackageIdToLoad, ImportedPackageCount);
	if (!Imports)
	{
		return;
	}

	for (int32 LocalImportIndex = 0; LocalImportIndex < ImportedPackageCount; ++LocalImportIndex)
	{
		// AreAllImportsInImportPackageAlreadyLoaded?
		int32 GlobalPackageIndex = Imports[LocalImportIndex];
		FPackageStoreEntry& Entry = GlobalPackageStore.StoreEntries[GlobalPackageIndex];
		bool bNeedToLoadPackage = false;
		// start from 1 to skip checking the UPackage
		for (int32 ObjectImportIndex = 1; ObjectImportIndex < Entry.GlobalImportCount; ObjectImportIndex++)
		{
			int32 GlobalImportIndex = Entry.FirstGlobalImport + ObjectImportIndex;
			UObject* ImportedObject = ImportStore.GetImportFromGlobalIndex(GlobalImportIndex);
			if (!ImportedObject || !ImportedObject->HasAllFlags(RF_WasLoaded | RF_LoadCompleted))
			{
				bNeedToLoadPackage = true;
				break;
			}
		}

		if (bNeedToLoadPackage)
		{
			FPackageId PackageId = FPackageId::FromIndex(GlobalPackageIndex);
			FAsyncPackageDesc2 Info(INDEX_NONE, PackageId, PackageId, Entry.Name, Entry.Name);
			bool bInserted;
			FAsyncPackage2* ImportedAsyncPackage = AsyncLoadingThread.FindOrInsertPackage(&Info, bInserted);
			if (ImportedAsyncPackage)
			{
				TRACE_LOADTIME_ASYNC_PACKAGE_IMPORT_DEPENDENCY(this, ImportedAsyncPackage);
				ImportedAsyncPackage->AddRef();
				ImportedAsyncPackages.Reserve(ImportedPackageCount);
				ImportedAsyncPackages.Add(ImportedAsyncPackage);
				if (bInserted)
				{
					ImportedAsyncPackage->ImportPackagesRecursive();
					ImportedAsyncPackage->StartLoading();
				}
			}
		}
	}
}

void FAsyncPackage2::StartLoading()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StartLoading);
	TRACE_LOADTIME_BEGIN_LOAD_ASYNC_PACKAGE(this);
	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::NewPackage);
	AsyncPackageLoadingState = EAsyncPackageLoadingState2::WaitingForSummary;

	LoadStartTime = FPlatformTime::Seconds();

	check(ExportBundleCount > 0);
	FExportBundleMetaEntry& BundleMetaEntry = ExportBundleMetaEntries[0];
	AsyncLoadingThread.AddBundleIoRequest(this, BundleMetaEntry);
}

void FAsyncPackage2::SetupScriptArcs()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SetupScriptArcs);

	FPackageStore& GlobalPackageStore = AsyncLoadingThread.GlobalPackageStore;
	int32 ScriptArcsCount = 0;
	int32* ScriptArcs = GlobalPackageStore.GetPackageScriptArcs(Desc.PackageIdToLoad, ScriptArcsCount);
	for (int32 ScriptArcIndex = 0; ScriptArcIndex < ScriptArcsCount * 2;)
	{
		int32 GlobalImportIndex = ScriptArcs[ScriptArcIndex++];
		int32 ToNodeIndex = ScriptArcs[ScriptArcIndex++];
		FEventLoadNode2* ToNode = PackageNodes + ToNodeIndex;

		FPackageIndex OuterMostIndex = FPackageIndex::FromImport(GlobalImportIndex);
		FPackageIndex OuterMostNonPackageIndex = OuterMostIndex;
		while (true)
		{
			check(!OuterMostIndex.IsNull() && OuterMostIndex.IsImport());
			FPackageIndex NextOuterMostIndex = ImportStore.GlobalImportScriptOuters[OuterMostIndex.ToImport()];

			if (NextOuterMostIndex.IsNull())
			{
				break;
			}
			OuterMostNonPackageIndex = OuterMostIndex;
			OuterMostIndex = NextOuterMostIndex;
		}
		FName PackageName = ImportStore.GlobalImportNames[OuterMostIndex.ToImport()];
		FName OuterMostNonPackageObjectName = ImportStore.GlobalImportNames[OuterMostNonPackageIndex.ToImport()];
		// OuterMostNonPackageIndex is used here because if it is a CDO or subobject, etc,
		// we wait for the outermost thing that is not a package
		const bool bWaitingForCompiledInImport = EDLBootNotificationManager.AddWaitingPackage(
			this, PackageName, OuterMostNonPackageObjectName, FPackageIndex::FromImport(ToNodeIndex));
		if (bWaitingForCompiledInImport)
		{
			PackageNodes[ToNodeIndex].AddBarrier();
		}
	}
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

	if (ExportBundleIndex == 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ProcessPackageSummary);

		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::WaitingForSummary);

		Package->SerialDataPtr = Package->IoBuffer.Data();
		const uint8* PackageSummaryData = Package->SerialDataPtr;
		const FPackageSummary* PackageSummary = reinterpret_cast<const FPackageSummary*>(PackageSummaryData);

		Package->PackageNameMap = reinterpret_cast<const int32*>(PackageSummaryData + PackageSummary->NameMapOffset);
		Package->ImportStore.ImportMap = reinterpret_cast<const int32*>(PackageSummaryData + PackageSummary->ImportMapOffset);
		Package->ImportStore.ImportMapCount = (PackageSummary->ExportMapOffset - PackageSummary->ImportMapOffset) / sizeof(int32);
		Package->ExportMap = reinterpret_cast<const FExportMapEntry*>(PackageSummaryData + PackageSummary->ExportMapOffset);
		Package->ExportBundles = reinterpret_cast<const FExportBundleHeader*>(PackageSummaryData + PackageSummary->ExportBundlesOffset);
		Package->ExportBundleEntries = reinterpret_cast<const FExportBundleEntry*>(Package->ExportBundles + Package->ExportBundleCount);

		const uint8* GraphData = PackageSummaryData + PackageSummary->GraphDataOffset;

		uint64 PackageSummarySize = GraphData + PackageSummary->GraphDataSize - PackageSummaryData;
		Package->SerialDataPtr += PackageSummarySize;

		Package->CreateUPackage(PackageSummary);

		Package->SetupSerializedArcs(GraphData, PackageSummary->GraphDataSize);

		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::WaitingForSummary);
		Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::ProcessNewImportsAndExports;

		TRACE_LOADTIME_PACKAGE_SUMMARY(Package, PackageSummarySize, Package->ImportStore.ImportMapCount, Package->ExportCount);
	}

	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessNewImportsAndExports);

	uint64 ExportsBufferSize = Package->IoBuffer.DataSize() - (Package->SerialDataPtr - Package->IoBuffer.Data());
	FSimpleExportArchive Ar(Package->SerialDataPtr, ExportsBufferSize);
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

		// FSimpleExportArchive special fields
		Ar.PackageNameMap = Package->PackageNameMap;
		Ar.GlobalNameMap = &Package->AsyncLoadingThread.GlobalNameMap.GetNameEntries();
		Ar.ImportStore = &Package->ImportStore;
		Ar.Exports = &Package->Exports;
		Ar.ExternalReadDependencies = &Package->ExternalReadDependencies;
	}
	const FExportBundleHeader* ExportBundle = Package->ExportBundles + ExportBundleIndex;
	
	const FExportBundleEntry* BundleEntry = Package->ExportBundleEntries + ExportBundle->FirstEntryIndex;
	const FExportBundleEntry* BundleEntryEnd = BundleEntry + ExportBundle->EntryCount;
	check(BundleEntry <= BundleEntryEnd);
	while (BundleEntry < BundleEntryEnd)
	{
		const FExportMapEntry& Export = Package->ExportMap[BundleEntry->LocalExportIndex];

		if (FilterExport(Export.FilterFlags))
		{
			Package->Exports[BundleEntry->LocalExportIndex].bFiltered = true;
			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				const uint64 SerialSize = Package->ExportMap[BundleEntry->LocalExportIndex].SerialSize;
				Package->SerialDataPtr += SerialSize;
				Ar.Seek(Ar.Tell() + SerialSize);
			}

			++BundleEntry;
			continue;
		}

		if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Create)
		{
			Package->EventDrivenCreateExport(BundleEntry->LocalExportIndex);
		}
		else
		{
			check(BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize);
			const uint64 ExportSerialSize = Export.SerialSize;
			check(Package->SerialDataPtr + ExportSerialSize <= Package->IoBuffer.Data() + Package->IoBuffer.DataSize());
			UObject* Object = Package->Exports[BundleEntry->LocalExportIndex].Object;
			check(Object);
			if (Object->HasAnyFlags(RF_NeedLoad))
			{
				TRACE_LOADTIME_SERIALIZE_EXPORT_SCOPE(Object, ExportSerialSize);
				const int64 Pos = Ar.Tell();
				check(ExportSerialSize <= uint64(Ar.TotalSize() - Pos));
				Package->EventDrivenSerializeExport(BundleEntry->LocalExportIndex, Ar);
				check(ExportSerialSize == uint64(Ar.Tell() - Pos));
			}
			check(!Object->HasAnyFlags(RF_NeedLoad));

			Package->SerialDataPtr += ExportSerialSize;
		}
		++BundleEntry;
	}

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
		Package->AsyncLoadingThread.BundleIoRequestCompleted(Package->ExportBundleMetaEntries[0]);
	}

	return EAsyncPackageState::Complete;
}

UObject* FAsyncPackage2::EventDrivenIndexToObject(FPackageIndex Index, bool bCheckSerialized)
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
		Result = ImportStore.FindOrGetImportFromGlobalIndex(Index);
		check(Result);
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
		UE_LOG(LogStreaming, Fatal, TEXT("Missing Dependency"));
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
		const FGlobalNameMap& GlobalNameMap = AsyncLoadingThread.GlobalNameMap;
		ObjectName = GlobalNameMap.GetName(Export.ObjectName[0], Export.ObjectName[1]);
	}

	TRACE_LOADTIME_CREATE_EXPORT_SCOPE(this, &Object);

	LLM_SCOPE(ELLMTag::AsyncLoading);
	LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(GetLinkerRoot(), ELLMTagSet::Assets);
	// LLM_SCOPED_TAG_WITH_OBJECT_IN_SET((Export.DynamicType == FObjectExport::EDynamicType::DynamicType) ? UDynamicClass::StaticClass() : 
	// 	CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, false), ELLMTagSet::AssetClasses);

	bool bIsCompleteyLoaded = false;
	UClass* LoadClass = Export.ClassIndex.IsNull() ? UClass::StaticClass() : CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, true);
	UObject* ThisParent = Export.OuterIndex.IsNull() ? LinkerRoot : EventDrivenIndexToObject(Export.OuterIndex, false);

	checkf(LoadClass, TEXT("Could not find class object for %s in %s"), *ObjectName.ToString(), *Desc.NameToLoad.ToString());
	checkf(ThisParent, TEXT("Could not find outer object for %s in %s"), *ObjectName.ToString(), *Desc.NameToLoad.ToString());
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
		checkf(Template, TEXT("Could not find template for %s in %s"), *ObjectName.ToString(), *Desc.NameToLoad.ToString());
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
	ImportStore.StoreGlobalImportObject(Export.GlobalImportIndex, Object);
}

void FAsyncPackage2::EventDrivenSerializeExport(int32 LocalExportIndex, FSimpleExportArchive& Ar)
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
			checkf(SuperStruct, TEXT("Could not find SuperStruct for %s"), *Object->GetFullName());
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

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessAsyncLoadingFromGameThread(int32& OutPackagesProcessed)
{
	SCOPED_LOADTIMER(AsyncLoadingTime);

	check(IsInGameThread());

	// If we're not multithreaded and flushing async loading, update the thread heartbeat
	const bool bNeedsHeartbeatTick = !FAsyncLoadingThread2::IsMultithreaded();
	OutPackagesProcessed = 0;

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

			if (ThreadState.IsTimeLimitExceeded())
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
	TArrayView<const int32> ImportedPackageIndices(Package->ImportStore.ImportedPackages, Package->ImportStore.ImportedPackagesCount);

	for (const int32 ImportedPackageIndex : ImportedPackageIndices)
	{
		const FPackageId PackageId = FPackageId::FromIndex(ImportedPackageIndex);

		if (VisitedPackages.Contains(PackageId))
		{
			continue;
		}
		VisitedPackages.Add(PackageId);

		FAsyncPackage2* AsyncRoot = AsyncLoadingThread.GetAsyncPackage(PackageId);
		if (AsyncRoot)
		{
			if (AsyncRoot->DeferredPostLoadIndex < AsyncRoot->ExportCount)
			{
				OutPackageId = PackageId;
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
	if (IsMultithreaded() && ENamedThreads::GetRenderThread() == ENamedThreads::GameThread) // render thread tasks are actually being sent to the game thread.
	{
		// The async loading thread might have queued some render thread tasks (we don't have a render thread yet, so these are actually sent to the game thread)
		// We need to process them now before we do any postloads.
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		if (FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded())
		{
			return EAsyncPackageState::TimeOut;
		}
	}

		
	bDidSomething = LoadedPackagesToProcess.Num() > 0;
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
#if WITH_EDITOR
			// In the editor we need to find any assets and add them to list for later callback
			Package->GetLoadedAssets(LoadedAssets);
#endif
			// We don't need the package anymore
			check(!Package->bAddedForDelete);
			check(!PackagesToDelete.Contains(Package));
			PackagesToDelete.Add(Package);
			Package->bAddedForDelete = true;
			Package->MarkRequestIDsAsComplete();

			if (FlushRequestID != INDEX_NONE && !ContainsRequestID(FlushRequestID))
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
					Package->ReleaseRef();
					Package->AsyncLoadingThread.WaitingForPostLoadCounter.Decrement();
				}
			}

			// push stats so that we don't overflow number of tags per thread during blocking loading
			LLM_PUSH_STATS_FOR_ASSET_TAGS();
		}
	}

	if (Result == EAsyncPackageState::Complete)
	{
#if WITH_EDITORONLY_DATA
		// This needs to happen after loading new blueprints in the editor, and this is handled in EndLoad for synchronous loads
		FBlueprintSupport::FlushReinstancingQueue();
#endif

#if WITH_EDITOR
		// In editor builds, call the asset load callback. This happens in both editor and standalone to match EndLoad
		TArray<FWeakObjectPtr> TempLoadedAssets = LoadedAssets;
		LoadedAssets.Reset();

		// Make a copy because LoadedAssets could be modified by one of the OnAssetLoaded callbacks
		for (const FWeakObjectPtr& WeakAsset : TempLoadedAssets)
		{
			// It may have been unloaded/marked pending kill since being added, ignore those cases
			if (UObject* LoadedAsset = WeakAsset.Get())
			{
				FCoreUObjectDelegates::OnAssetLoaded.Broadcast(LoadedAsset);
			}
		}
#endif

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

			if (!bDidSomething && GIsInitialLoad)
			{
				 bDidSomething = EDLBootNotificationManager.FireCompletedCompiledInImports();
				 if (!bDidSomething)
				 {
				 	bDidSomething = EDLBootNotificationManager.ConstructWaitingBootObjects();
				 }
			}
		}

		// Call update callback once per tick on the game thread
		FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast();
	}

	return Result;
}

FAsyncLoadingThread2::FAsyncLoadingThread2(FIoDispatcher& InIoDispatcher, IEDLBootNotificationManager& InEDLBootNotificationManager)
	: Thread(nullptr)
	, EDLBootNotificationManager(InEDLBootNotificationManager)
	, IoDispatcher(InIoDispatcher)
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
		if (IsGarbageCollectionWaiting() || FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded())
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
			check(!bHasAnyAsyncFlags);
			check(!bHasAnyLoadIntermediateFlags);
			if (bWasLoaded)
			{
				const bool bIsPackage = Obj->IsA(UPackage::StaticClass());
				check(bIsPackage || bLoadCompleted);
			}
		}
	}
	UE_LOG(LogStreaming, Log, TEXT("Verified load flags when finished loading"));
}

void FGlobalImportStore::OnPreGarbageCollect(bool bInIsLoadingPackages)
{
	if (!bNeedToHandleGarbageCollect && !bInIsLoadingPackages)
	{
		return;
	}
	bNeedToHandleGarbageCollect = bInIsLoadingPackages;

	int32 NumWeak = 0;
	for (int32 GlobalImportIndex = ScriptImportCount; GlobalImportIndex < Count; ++GlobalImportIndex)
	{
		FGlobalImport& GlobalImport = Imports[GlobalImportIndex];
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

#ifdef ALT2_VERIFY_ASYNC_FLAGS
	if (!bInIsLoadingPackages)
	{
		for (int32 GlobalImportIndex = 0; GlobalImportIndex < Count; ++GlobalImportIndex)
		{
			check(GlobalImportIndex < ScriptImportCount || Imports[GlobalImportIndex].GetObjectIfRawPointer() == nullptr);
			check(Imports[GlobalImportIndex].GetRefCount() == 0);
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
	// Mark objects created during async loading process (e.g. from within PostLoad or CreateExport) as async loaded so they 
	// cannot be found. This requires also keeping track of them so we can remove the async loading flag later one when we 
	// finished routing PostLoad to all objects.
	if (!bSubObject)
	{
		Object->SetInternalFlags(EInternalObjectFlags::AsyncLoading);
	}
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	check(ThreadContext.AsyncPackage); // Otherwise something is wrong and we're creating objects outside of async loading code
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
	IEDLBootNotificationManager& InEDLBootNotificationManager,
	FAsyncLoadEventGraphAllocator& InGraphAllocator,
	const FAsyncLoadEventSpec* EventSpecs)
: Desc(InDesc)
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
, EDLBootNotificationManager(InEDLBootNotificationManager)
, GraphAllocator(InGraphAllocator)
// Begin EDL specific properties
, ImportStore(AsyncLoadingThread.GlobalPackageStore, Desc.PackageIdToLoad)
, AsyncPackageLoadingState(EAsyncPackageLoadingState2::NewPackage)
, bAllExportsSerialized(false)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(NewAsyncPackage);
	TRACE_LOADTIME_NEW_ASYNC_PACKAGE(this, InDesc.NameToLoad);
	AddRequestID(InDesc.RequestID);

	TTuple<FExportBundleMetaEntry*, uint32> PackageExportBundles = AsyncLoadingThread.GlobalPackageStore.GetPackageExportBundleMetaEntries(Desc.PackageIdToLoad);
	ExportBundleMetaEntries = PackageExportBundles.Get<0>();
	ExportBundleCount = PackageExportBundles.Get<1>();
	ExportCount = AsyncLoadingThread.GlobalPackageStore.GetPackageExportCount(Desc.PackageIdToLoad);
	Exports.AddDefaulted(ExportCount);
	OwnedObjects.Reserve(ExportCount + 1); // +1 for UPackage

	CreateNodes(EventSpecs);
	if (GIsInitialLoad)
	{
		SetupScriptArcs();
	}
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

	ImportStore.StoreGlobalImportObject(PackageSummary->GlobalImportIndex, LinkerRoot);
	AddOwnedObjectWithAsyncFlag(LinkerRoot, /*bForceAdd*/ !bCreatedLinkerRoot);
	check(LinkerRoot->HasAnyInternalFlags(EInternalObjectFlags::Async));

	UE_LOG(LogStreaming, Verbose, TEXT("FAsyncPackage::CreateUPackage for %s finished."), *Desc.Name.ToString());
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
	LLM_SCOPE(ELLMTag::UObject);

	SCOPED_LOADTIMER(PostLoadObjectsTime);

	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	TGuardValue<bool> GuardIsRoutingPostLoad(ThreadContext.IsRoutingPostLoad, true);

	FUObjectSerializeContext* LoadContext = GetSerializeContext();

	const bool bAsyncPostLoadEnabled = FAsyncLoadingThreadSettings::Get().bAsyncPostLoadEnabled;
	const bool bIsMultithreaded = AsyncLoadingThread.IsMultithreaded();

	// PostLoad objects.
	while (PostLoadIndex < ExportCount && !FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded())
	{
		const FExportObject& Export = Exports[PostLoadIndex++];
		if (Export.bFiltered)
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
	SCOPED_LOADTIMER(PostLoadDeferredObjectsTime);

	FAsyncPackageScope2 PackageScope(this);

	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	TGuardValue<bool> GuardIsRoutingPostLoad(PackageScope.ThreadContext.IsRoutingPostLoad, true);
	FAsyncLoadingTickScope2 InAsyncLoadingTick(AsyncLoadingThread);

	FUObjectSerializeContext* LoadContext = GetSerializeContext();

	while (DeferredPostLoadIndex < ExportCount && 
		!AsyncLoadingThread.IsAsyncLoadingSuspended() &&
		!FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded())
	{
		const FExportObject& Export = Exports[DeferredPostLoadIndex++];
		if (Export.bFiltered)
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
			!FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded())
		{
			const FExportObject& Export = Exports[DeferredFinalizeIndex++];
			if (Export.bFiltered)
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
					if (!Export.bFiltered && Export.Object->CanBeClusterRoot())
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
			!FAsyncLoadingThreadState2::Get()->IsTimeLimitExceeded())
	{
		const FExportObject& Export = Exports[DeferredClusterIndex++];

		if (!Export.bFiltered && Export.Object->CanBeClusterRoot())
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

	FName PackageName = FName(*InName);
	FName PackageNameToLoad = InPackageToLoadFrom ? FName(InPackageToLoadFrom) : PackageName;

	FPackageId PackageIdToLoad = GlobalPackageStore.FindPackageId(PackageNameToLoad);

	if (PackageIdToLoad.IsValid())
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

		FPackageId PackageId =
			PackageName != PackageNameToLoad ?
			GlobalPackageStore.FindOrAddPackageId(PackageName) :
			PackageIdToLoad;

		// Add new package request
		FAsyncPackageDesc2 PackageDesc(RequestID, PackageId, PackageIdToLoad, PackageName, PackageNameToLoad, MoveTemp(CompletionDelegatePtr));
		QueuePackage(PackageDesc);
	}
	else
	{
		UE_LOG(LogStreaming, Warning, TEXT("AsyncLoading2 - LoadPackage: Skipping package: '%s'. Name to load is unknown: '%s')"),
			*PackageName.ToString(), *PackageNameToLoad.ToString());
		InCompletionDelegate.ExecuteIfBound(PackageName, nullptr, EAsyncLoadingResult::Failed);
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

IAsyncPackageLoader* MakeAsyncPackageLoader2(FIoDispatcher& InIoDispatcher, IEDLBootNotificationManager& InEDLBootNotificationManager)
{
	return new FAsyncLoadingThread2(InIoDispatcher, InEDLBootNotificationManager);
}
