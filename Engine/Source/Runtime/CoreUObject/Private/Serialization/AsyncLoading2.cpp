// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnAsyncLoading.cpp: Unreal async loading code.
=============================================================================*/

#include "Serialization/AsyncLoading2.h"
#include "Serialization/AsyncPackageLoader.h"
#include "IO/PackageStore.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "HAL/Event.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ScopeLock.h"
#include "Stats/StatsMisc.h"
#include "Misc/CoreStats.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/StringBuilder.h"
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
#include "Templates/Casts.h"
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
#include "Experimental/Containers/FAAArrayQueue.h"
#include "UObject/ObjectRedirector.h"
#include "Serialization/BulkData.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/MemoryReader.h"
#include "UObject/UObjectClusters.h"
#include "UObject/LinkerInstancingContext.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "HAL/LowLevelMemStats.h"
#include "HAL/IPlatformFileOpenLogWrapper.h"
#include "Modules/ModuleManager.h"
#include "Containers/SpscQueue.h"

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
PRAGMA_DISABLE_OPTIMIZATION
#endif

CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, FileIO);
CSV_DEFINE_STAT(FileIO, FrameCompletedExportBundleLoadsKB);

FArchive& operator<<(FArchive& Ar, FZenPackageVersioningInfo& VersioningInfo)
{
	Ar << VersioningInfo.ZenVersion;
	Ar << VersioningInfo.PackageVersion;
	Ar << VersioningInfo.LicenseeVersion;
	VersioningInfo.CustomVersions.Serialize(Ar);
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
	Ar << ExportBundleHeader.SerialOffset;
	Ar << ExportBundleHeader.FirstEntryIndex;
	Ar << ExportBundleHeader.EntryCount;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FScriptObjectEntry& ScriptObjectEntry)
{
	Ar << ScriptObjectEntry.ObjectName.Index << ScriptObjectEntry.ObjectName.Number;
	Ar << ScriptObjectEntry.GlobalIndex;
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
	Ar << ExportMapEntry.PublicExportHash;

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

	Ar.Serialize(&ExportMapEntry.Pad, sizeof(ExportMapEntry.Pad));

	return Ar;
}

uint64 FPackageObjectIndex::GenerateImportHashFromObjectPath(const FStringView& ObjectPath)
{
	TArray<TCHAR, TInlineAllocator<FName::StringBufferSize>> FullImportPath;
	const int32 Len = ObjectPath.Len();
	FullImportPath.AddUninitialized(Len);
	for (int32 I = 0; I < Len; ++I)
	{
		if (ObjectPath[I] == TEXT('.') || ObjectPath[I] == TEXT(':'))
		{
			FullImportPath[I] = TEXT('/');
		}
		else
		{
			FullImportPath[I] = TChar<TCHAR>::ToLower(ObjectPath[I]);
		}
	}
	uint64 Hash = CityHash64(reinterpret_cast<const char*>(FullImportPath.GetData()), Len * sizeof(TCHAR));
	Hash &= ~(3ull << 62ull);
	return Hash;
}

void FindAllRuntimeScriptPackages(TArray<UPackage*>& OutPackages)
{
	OutPackages.Empty(256);
	ForEachObjectOfClass(UPackage::StaticClass(), [&OutPackages](UObject* InPackageObj)
	{
		UPackage* Package = CastChecked<UPackage>(InPackageObj);
		if (Package->HasAnyPackageFlags(PKG_CompiledIn))
		{
			TCHAR Buffer[FName::StringBufferSize];
			if (FStringView(Buffer, Package->GetFName().ToString(Buffer)).StartsWith(TEXT("/Script/"), ESearchCase::CaseSensitive))
			{
				OutPackages.Add(Package);
			}
		}
	}, /*bIncludeDerivedClasses*/false);
}

#if WITH_ASYNCLOADING2

#ifndef ALT2_VERIFY_ASYNC_FLAGS
#define ALT2_VERIFY_ASYNC_FLAGS DO_CHECK && !(WITH_IOSTORE_IN_EDITOR)
#endif

#ifndef ALT2_VERIFY_RECURSIVE_LOADS
#define ALT2_VERIFY_RECURSIVE_LOADS !(WITH_IOSTORE_IN_EDITOR) && DO_CHECK
#endif

#ifndef ALT2_LOG_VERBOSE
#define ALT2_LOG_VERBOSE DO_CHECK
#endif

static TSet<FPackageId> GAsyncLoading2_DebugPackageIds;
static FString GAsyncLoading2_DebugPackageNamesString;
static TSet<FPackageId> GAsyncLoading2_VerbosePackageIds;
static FString GAsyncLoading2_VerbosePackageNamesString;
static int32 GAsyncLoading2_VerboseLogFilter = 2; //None=0,Filter=1,All=2
#if !UE_BUILD_SHIPPING
static void ParsePackageNames(const FString& PackageNamesString, TSet<FPackageId>& PackageIds)
{
	TArray<FString> Args;
	const TCHAR* Delimiters[] = { TEXT(","), TEXT(" ") };
	PackageNamesString.ParseIntoArray(Args, Delimiters, UE_ARRAY_COUNT(Delimiters), true);
	PackageIds.Reserve(PackageIds.Num() + Args.Num());
	for (const FString& PackageName : Args)
	{
		if (PackageName.Len() > 0 && FChar::IsDigit(PackageName[0]))
		{
			uint64 Value;
			LexFromString(Value, *PackageName);
			PackageIds.Add(*(FPackageId*)(&Value));
		}
		else
		{
			PackageIds.Add(FPackageId::FromName(FName(*PackageName)));
		}
	}
}
static FAutoConsoleVariableRef CVar_DebugPackageNames(
	TEXT("s.DebugPackageNames"),
	GAsyncLoading2_DebugPackageNamesString,
	TEXT("Add debug breaks for all listed package names, also automatically added to s.VerbosePackageNames."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		GAsyncLoading2_DebugPackageIds.Reset();
		ParsePackageNames(Variable->GetString(), GAsyncLoading2_DebugPackageIds);
		ParsePackageNames(Variable->GetString(), GAsyncLoading2_VerbosePackageIds);
		GAsyncLoading2_VerboseLogFilter = GAsyncLoading2_VerbosePackageIds.Num() > 0 ? 1 : 2;
	}),
	ECVF_Default);
static FAutoConsoleVariableRef CVar_VerbosePackageNames(
	TEXT("s.VerbosePackageNames"),
	GAsyncLoading2_VerbosePackageNamesString,
	TEXT("Restrict verbose logging to listed package names."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		GAsyncLoading2_VerbosePackageIds.Reset();
		ParsePackageNames(Variable->GetString(), GAsyncLoading2_VerbosePackageIds);
		GAsyncLoading2_VerboseLogFilter = GAsyncLoading2_VerbosePackageIds.Num() > 0 ? 1 : 2;
	}),
	ECVF_Default);
#endif

#define UE_ASYNC_PACKAGE_DEBUG(PackageDesc) \
if (GAsyncLoading2_DebugPackageIds.Contains((PackageDesc).UPackageId)) \
{ \
	UE_DEBUG_BREAK(); \
}

#define UE_ASYNC_UPACKAGE_DEBUG(UPackage) \
if (GAsyncLoading2_DebugPackageIds.Contains((UPackage)->GetPackageId())) \
{ \
	UE_DEBUG_BREAK(); \
}

// The ELogVerbosity::VerbosityMask is used to silence PVS,
// using constexpr gave the same warning, and the disable comment can can't be used in a macro: //-V501 
// warning V501: There are identical sub-expressions 'ELogVerbosity::Verbose' to the left and to the right of the '<' operator.
#define UE_ASYNC_PACKAGE_LOG(Verbosity, PackageDesc, LogDesc, Format, ...) \
if ((ELogVerbosity::Type(ELogVerbosity::Verbosity & ELogVerbosity::VerbosityMask) < ELogVerbosity::Verbose) || \
	(GAsyncLoading2_VerboseLogFilter == 2) || \
	(GAsyncLoading2_VerboseLogFilter == 1 && GAsyncLoading2_VerbosePackageIds.Contains((PackageDesc).UPackageId))) \
{ \
	UE_LOG(LogStreaming, Verbosity, LogDesc TEXT(": %s (0x%llX) %s (0x%llX) - ") Format, \
		*(PackageDesc).UPackageName.ToString(), \
		(PackageDesc).UPackageId.ValueForDebugging(), \
		*(PackageDesc).PackageNameToLoad.ToString(), \
		(PackageDesc).PackageIdToLoad.ValueForDebugging(), \
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

CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, Basic);
CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, FileIO);

TRACE_DECLARE_INT_COUNTER(AsyncLoadingPendingIoRequests, TEXT("AsyncLoading/PendingIoRequests"));
TRACE_DECLARE_MEMORY_COUNTER(AsyncLoadingTotalLoaded, TEXT("AsyncLoading/TotalLoaded"));

struct FAsyncPackage2;
class FAsyncLoadingThread2;

class FSimpleArchive final
	: public FArchive
{
public:
	FSimpleArchive(const uint8* BufferPtr, uint64 BufferSize)
	{
#if (!DEVIRTUALIZE_FLinkerLoad_Serialize)
		ActiveFPLB = &InlineFPLB;
#endif
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
private:
#if (!DEVIRTUALIZE_FLinkerLoad_Serialize)
	FArchive::FFastPathLoadBuffer InlineFPLB;
	FArchive::FFastPathLoadBuffer* ActiveFPLB;
#endif
};

struct FExportObject
{
	UObject* Object = nullptr;
	UObject* TemplateObject = nullptr;
	UObject* SuperObject = nullptr;
	bool bFiltered = false;
	bool bExportLoadFailed = false;
};

struct FPackageRequest
{
	int32 RequestId = -1;
	int32 Priority = -1;
	FName CustomName;
	FName PackageNameToLoad;
	FName UPackageName;
	FPackageId PackageIdToLoad;
	TUniquePtr<FLoadPackageAsyncDelegate> PackageLoadedDelegate;
	FPackageRequest* Next = nullptr;

	static FPackageRequest Create(const int32 RequestId, const int32 Priority, FName PackageNameToLoad, FPackageId PackageIdToLoad, FName CustomName, TUniquePtr<FLoadPackageAsyncDelegate> PackageLoadedDelegate)
	{
		return FPackageRequest
		{
			RequestId,
			Priority,
			CustomName,
			PackageNameToLoad,
			FName(),
			PackageIdToLoad,
			MoveTemp(PackageLoadedDelegate),
			nullptr
		};
	}
};

struct FAsyncPackageDesc2
{
	// A unique request id for each external call to LoadPackage
	int32 RequestID;
	// Package priority
	int32 Priority;
	// The package id of the UPackage being loaded
	// It will be used as key when tracking active async packages
	FPackageId UPackageId;
	// The id of the package being loaded from disk
	FPackageId PackageIdToLoad; 
	// The name of the UPackage being loaded
	// Set to none for imported packages up until the package summary has been serialized
	FName UPackageName;
	// The name of the package being loaded from disk
	// Set to none up until the package summary has been serialized
	FName PackageNameToLoad;
	// Packages with a a custom name can't be imported
	bool bCanBeImported;

	static FAsyncPackageDesc2 FromPackageRequest(
		int32 RequestID,
		int32 Priority,
		FName UPackageName,
		FPackageId PackageIdToLoad,
		bool bHasCustomName)
	{
		return FAsyncPackageDesc2
		{
			RequestID,
			Priority,
			FPackageId::FromName(UPackageName),
			PackageIdToLoad,
			UPackageName,
			FName(),
			!bHasCustomName
		};
	}

	static FAsyncPackageDesc2 FromPackageImport(
		int32 RequestID,
		int32 Priority,
		FPackageId ImportedPackageId,
		FPackageId PackageIdToLoad,
		FName UPackageName)
	{
		return FAsyncPackageDesc2
		{
			RequestID,
			Priority,
			ImportedPackageId,
			PackageIdToLoad,
			UPackageName,
			FName(),
			true
		};
	}
};

// Note: RemoveUnreachableObjects could move from GT to ALT by removing the debug raw pointers here,
// the tradeoff would be increased complexity and more restricted debug and log possibilities.
using FUnreachablePublicExport = TPair<int32, UObject*>;
using FUnreachablePackages = TArray<UPackage*>;
using FUnreachablePublicExports = TArray<FUnreachablePublicExport>;

struct FGlobalImportStore
{
	TMap<FPackageObjectIndex, UObject*> ScriptObjects;
	TMap<FPublicExportKey, int32> PublicExportToObjectIndex;
	TMap<int32, FPublicExportKey> ObjectIndexToPublicExport;
	// Temporary initial load data
	TArray<FScriptObjectEntry> ScriptObjectEntries;
	TMap<FPackageObjectIndex, FScriptObjectEntry*> ScriptObjectEntriesMap;
	bool bHasInitializedScriptObjects = false;

	FGlobalImportStore()
	{
		PublicExportToObjectIndex.Reserve(32768);
		ObjectIndexToPublicExport.Reserve(32768);
	}

	void Initialize(FIoDispatcher& IoDispatcher)
	{
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SetupInitialLoadData);

			FEvent* InitialLoadEvent = FPlatformProcess::GetSynchEventFromPool();

			FIoBatch IoBatch = IoDispatcher.NewBatch();
			FIoRequest IoRequest = IoBatch.Read(CreateIoChunkId(0, 0, EIoChunkType::ScriptObjects), FIoReadOptions(), IoDispatcherPriority_High);
			IoBatch.IssueAndTriggerEvent(InitialLoadEvent);

			InitialLoadEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(InitialLoadEvent);

			const FIoBuffer& InitialLoadIoBuffer = IoRequest.GetResultOrDie();
			FLargeMemoryReader InitialLoadArchive(InitialLoadIoBuffer.Data(), InitialLoadIoBuffer.DataSize());
			FNameMap NameMap;
			NameMap.Load(InitialLoadArchive, FMappedName::EType::Global);
			int32 NumScriptObjects = 0;
			InitialLoadArchive << NumScriptObjects;
			ScriptObjectEntries = MakeArrayView(reinterpret_cast<const FScriptObjectEntry*>(InitialLoadIoBuffer.Data() + InitialLoadArchive.Tell()), NumScriptObjects);

			ScriptObjectEntriesMap.Reserve(ScriptObjectEntries.Num());
			for (FScriptObjectEntry& ScriptObjectEntry : ScriptObjectEntries)
			{
				const FMappedName& MappedName = FMappedName::FromMinimalName(ScriptObjectEntry.ObjectName);
				check(MappedName.IsGlobal());
				ScriptObjectEntry.ObjectName = NameMap.GetMinimalName(MappedName);

				ScriptObjectEntriesMap.Add(ScriptObjectEntry.GlobalIndex, &ScriptObjectEntry);
			}
		}
	}

	TArray<FPackageId> RemovePublicExports(const FUnreachablePublicExports& PublicExports)
	{
		TArray<FPackageId> PackageIds;
		TArray<FPublicExportKey> PublicExportKeys;
		PublicExportKeys.Reserve(PublicExports.Num());
		PackageIds.Reserve(PublicExports.Num());

		for (const FUnreachablePublicExport& Item : PublicExports)
		{
			int32 ObjectIndex = Item.Key;
			FPublicExportKey PublicExportKey;
			if (ObjectIndexToPublicExport.RemoveAndCopyValue(ObjectIndex, PublicExportKey))
			{
				PublicExportKeys.Emplace(PublicExportKey);
#if DO_CHECK
				{
					UObject* GCObject = Item.Value;

					checkf(GCObject->HasAllFlags(RF_WasLoaded | RF_Public),
						TEXT("The serialized GC Object '%s' with (ObjectFlags=%x, InternalObjectFlags=%x)and id 0x%llX:0x%llX is currently missing RF_WasLoaded or RF_Public. ")
						TEXT("The flags must have been altered incorrectly by a higher level system after the object was loaded. ")
						TEXT("This may cause memory corruption."),
						*GCObject->GetFullName(),
						GCObject->GetFlags(),
						GCObject->GetInternalFlags(),
						PublicExportKey.GetPackageId().Value(), PublicExportKey.GetExportHash());

					UObject* ExistingObject = FindPublicExportObjectUnchecked(PublicExportKey);
					checkf(ExistingObject,
						TEXT("The serialized GC object '%s' with flags (ObjectFlags=%x, InternalObjectFlags=%x) and id 0x%llX:0x%llX is missing in ImportStore. ")
						TEXT("Reason unknown. Double delete? Bug or hash collision?"),
						*GCObject->GetFullName(),
						GCObject->GetFlags(),
						GCObject->GetInternalFlags(),
						PublicExportKey.GetPackageId().Value(),
						PublicExportKey.GetExportHash());

					checkf(ExistingObject && ExistingObject == GCObject,
						TEXT("The serialized GC Object '%s' with flags (ObjectFlags=%x, InternalObjectFlags=%x) and id 0x%llX:0x%llX is not matching the object '%s' in ImportStore. "),
						TEXT("Reason unknown. Overwritten after it was added? Bug or hash collision?"),
						*GCObject->GetFullName(),
						GCObject->GetFlags(),
						GCObject->GetInternalFlags(),
						PublicExportKey.GetPackageId().Value(),
						PublicExportKey.GetExportHash(),
						*ExistingObject->GetFullName());
				}
#endif
			}
		}

		FPackageId LastPackageId;
		for (const FPublicExportKey& PublicExportKey : PublicExportKeys)
		{
			PublicExportToObjectIndex.Remove(PublicExportKey);
			FPackageId PackageId = PublicExportKey.GetPackageId();
			if (!(PackageId == LastPackageId)) // fast approximation of Contains()
			{
				LastPackageId = PackageId;
				PackageIds.Emplace(LastPackageId);
			}
		}
		return PackageIds;
	}

	inline UObject* FindPublicExportObjectUnchecked(const FPublicExportKey& Key)
	{
		int32* FindObjectIndex = PublicExportToObjectIndex.Find(Key);
		if (!FindObjectIndex)
		{
			return nullptr;
		}
		FUObjectItem* ObjectItem = GUObjectArray.IndexToObject(*FindObjectIndex);
		if (!ObjectItem)
		{
			return nullptr;
		}
		return static_cast<UObject*>(ObjectItem->Object);
	}

	inline UObject* FindPublicExportObject(const FPublicExportKey& Key)
	{
		UObject* Object = FindPublicExportObjectUnchecked(Key);
		checkf(!Object || !Object->IsUnreachable(), TEXT("%s"), Object ? *Object->GetFullName() : TEXT("null"));
		return Object;
	}

	UObject* FindScriptImportObjectFromIndex(FPackageObjectIndex ScriptImportIndex);

	inline UObject* FindScriptImportObject(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsScriptImport());
		UObject* Object = nullptr;
		if (!bHasInitializedScriptObjects)
		{
			Object = FindScriptImportObjectFromIndex(GlobalIndex);
		}
		else
		{
			Object = ScriptObjects.FindRef(GlobalIndex);
		}
		return Object;
	}

	void StoreGlobalObject(FPackageId PackageId, uint64 ExportHash, UObject* Object)
	{
		check(PackageId.IsValid());
		check(ExportHash != 0);
		int32 ObjectIndex = GUObjectArray.ObjectToIndex(Object);
		FPublicExportKey Key = FPublicExportKey::MakeKey(PackageId, ExportHash);
#if DO_CHECK
		{
			UObject* ExistingObject = FindPublicExportObjectUnchecked(Key);
			if (ExistingObject)
			{
				checkf(ExistingObject == Object,
					TEXT("The constructed serialized object '%s' with index %d and id 0x%llX:0x%llX collides with the object '%s' in ImportStore. ")
					TEXT("Reason unknown. Bug or hash collision?"),
					Object ? *Object->GetFullName() : TEXT("null"),
					ObjectIndex,
					Key.GetPackageId().Value(), Key.GetExportHash(),
					ExistingObject ? *ExistingObject->GetFullName() : TEXT("null"));
			}

			FPublicExportKey* ExistingKey = ObjectIndexToPublicExport.Find(ObjectIndex);
			if (ExistingKey)
			{
				checkf(*ExistingKey == Key,
					TEXT("The constructed serialized object '%s' with index %d and id 0x%llX:0x%llX collides with the object '%s' in ImportStore. ")
					TEXT("Reason unknown. Bug or hash collision?"),
					Object ? *Object->GetFullName() : TEXT("null"),
					ObjectIndex,
					Key.GetPackageId().Value(), Key.GetExportHash(),
					ExistingObject ? *ExistingObject->GetFullName() : TEXT("null"));
			}
		}
#endif
		PublicExportToObjectIndex.Add(Key, ObjectIndex);
		ObjectIndexToPublicExport.Add(ObjectIndex, Key);
	}

	void FindAllScriptObjects();
};

class FLoadedPackageRef
{
	UPackage* Package = nullptr;
	int32 RefCount = 0;
	bool bAreAllPublicExportsLoaded = false;
	bool bIsMissing = false;
	bool bHasFailed = false;
	bool bHasBeenLoadedDebug = false;

public:
	inline int32 GetRefCount() const
	{
		return RefCount;
	}

	inline bool AddRef()
	{
		++RefCount;
		// is this the first reference to a package that has been loaded earlier?
		return RefCount == 1 && Package;
	}

	inline bool ReleaseRef(FPackageId FromPackageId, FPackageId ToPackageId)
	{
		check(RefCount > 0);
		--RefCount;

#if DO_CHECK
		ensureMsgf(!bHasBeenLoadedDebug || bAreAllPublicExportsLoaded || bIsMissing || bHasFailed,
			TEXT("LoadedPackageRef from None (0x%llX) to %s (0x%llX) should not have been released when the package is not complete.")
			TEXT("RefCount=%d, AreAllExportsLoaded=%d, IsMissing=%d, HasFailed=%d, HasBeenLoaded=%d"),
			FromPackageId.Value(),
			Package ? *Package->GetName() : TEXT("None"),
			ToPackageId.Value(),
			RefCount,
			bAreAllPublicExportsLoaded,
			bIsMissing,
			bHasFailed,
			bHasBeenLoadedDebug);

		if (bAreAllPublicExportsLoaded)
		{
			check(!bIsMissing);
		}
		if (bIsMissing)
		{
			check(!bAreAllPublicExportsLoaded);
		}
#endif
		// is this the last reference to a loaded package?
		return RefCount == 0 && Package;
	}

	inline UPackage* GetPackage() const
	{
#if DO_CHECK
		if (Package)
		{
			check(!bIsMissing);
			check(!Package->IsUnreachable());
		}
		else
		{
			check(!bAreAllPublicExportsLoaded);
		}
#endif
		return Package;
	}

	inline void SetPackage(UPackage* InPackage)
	{
		check(!bAreAllPublicExportsLoaded);
		check(!bIsMissing);
		check(!bHasFailed);
		check(!Package);
		Package = InPackage;
	}

	inline bool AreAllPublicExportsLoaded() const
	{
		return bAreAllPublicExportsLoaded;
	}

	inline void SetAllPublicExportsLoaded()
	{
		check(!bIsMissing);
		check(!bHasFailed);
		check(Package);
		bIsMissing = false;
		bAreAllPublicExportsLoaded = true;
		bHasBeenLoadedDebug = true;
	}

	inline void ClearAllPublicExportsLoaded()
	{
		check(!bIsMissing);
		check(Package);
		bIsMissing = false;
		bAreAllPublicExportsLoaded = false;
	}

	inline void SetIsMissingPackage()
	{
		check(!bAreAllPublicExportsLoaded);
		check(!Package);
		bIsMissing = true;
		bAreAllPublicExportsLoaded = false;
	}

	inline void ClearErrorFlags()
	{
		bIsMissing = false;
		bHasFailed = false;
	}

	inline void SetHasFailed()
	{
		bHasFailed = true;
	}
};

class FLoadedPackageStore
{
private:
	// Packages in active loading or completely loaded packages, with Desc.DiskPackageName as key.
	// Does not track temp packages with custom UPackage names, since they are never imorted by other packages.
	TMap<FPackageId, FLoadedPackageRef> Packages;
	IPackageStore* PackageStore = nullptr;

public:
	FLoadedPackageStore()
	{
		Packages.Reserve(32768);
	}

	void Initialize(IPackageStore& InPackageStore)
	{
		PackageStore = &InPackageStore;
	}

	int32 NumTracked() const
	{
		return Packages.Num();
	}

	inline FLoadedPackageRef* FindPackageRef(FPackageId PackageId)
	{
		return Packages.Find(PackageId);
	}

	inline FLoadedPackageRef& GetPackageRef(FPackageId PackageId)
	{
		return Packages.FindOrAdd(PackageId);
	}

	inline int32 RemovePackage(FPackageId PackageId)
	{
		FLoadedPackageRef Ref;
		bool bRemoved = Packages.RemoveAndCopyValue(PackageId, Ref);
		return bRemoved ? Ref.GetRefCount() : -1;
	}

#if ALT2_VERIFY_ASYNC_FLAGS
	void VerifyLoadedPackages()
	{
		for (TPair<FPackageId, FLoadedPackageRef>& Pair : Packages)
		{
			FPackageId& PackageId = Pair.Key;
			FLoadedPackageRef& Ref = Pair.Value;
			ensureMsgf(Ref.GetRefCount() == 0,
				TEXT("PackageId '0x%llX' with ref count %d should not have a ref count now")
				TEXT(", or this check is incorrectly reached during active loading."),
				PackageId.Value(),
				Ref.GetRefCount());
		}
	}
#endif

	void RemovePackage(UPackage* Package)
	{
		UE_ASYNC_UPACKAGE_DEBUG(Package);
		check(IsGarbageCollecting());

		if (!Package->CanBeImported())
		{
			return;
		}

		int32 RefCount = RemovePackage(Package->GetPackageId());
		if (RefCount > 0)
		{
			UE_LOG(LogStreaming, Error,
				TEXT("RemovePackage: %s %s (0x%llX) - with (ObjectFlags=%x, InternalObjectFlags=%x) - ")
				TEXT("Package destroyed while still being referenced, RefCount %d > 0."),
				*Package->GetName(),
				*Package->GetLoadedPath().GetDebugName(), Package->GetPackageId().Value(),
				Package->GetFlags(), Package->GetInternalFlags(), RefCount);
			checkf(false, TEXT("Package %s destroyed with RefCount"), *Package->GetName());
		}
		else if (RefCount < 0)
		{
			UE_LOG(LogStreaming, Error,
				TEXT("RemovePackage: %s %s (0x%llX) - with (ObjectFlags=%x, InternalObjectFlags=%x) - ")
				TEXT("Package not found!"),
				*Package->GetName(),
				*Package->GetLoadedPath().GetDebugName(), Package->GetPackageId().Value(),
				Package->GetFlags(), Package->GetInternalFlags());
			checkf(false, TEXT("Package %s not found"), *Package->GetName());
		}
	}

	void RemovePackages(const FUnreachablePackages& UnreachablePackages)
	{
		const int32 PackageCount = UnreachablePackages.Num();
		for (int32 Index = 0; Index < PackageCount; ++Index)
		{
			RemovePackage(UnreachablePackages[Index]);
		}
	}

	void ClearAllPublicExportsLoaded(const TArray<FPackageId>& PackageIds)
	{
		const int32 PackageCount = PackageIds.Num();
		const bool bForceSingleThreaded = PackageCount < 1024;
		ParallelFor(PackageCount, [this, &PackageIds](int32 Index)
		{
			if (FLoadedPackageRef* PackageRef = FindPackageRef(PackageIds[Index]))
			{
				PackageRef->ClearAllPublicExportsLoaded();
			}
		}, bForceSingleThreaded);
	}
};

struct FPackageImportStore
{
	IPackageStore& PackageStore;
	FGlobalImportStore& GlobalImportStore;
	FLoadedPackageStore& LoadedPackageStore;
	const FAsyncPackageDesc2& Desc;
	const TArrayView<const FPackageId>& ImportedPackageIds;
	TArrayView<const uint64> ImportedPublicExportHashes;
	TArrayView<const FPackageObjectIndex> ImportMap;

	FPackageImportStore(IPackageStore& InPackageStore, FGlobalImportStore& InGlobalImportStore, FLoadedPackageStore& InLoadedPackageStore, const FAsyncPackageDesc2& InDesc, const TArrayView<const FPackageId>& InImportedPackageIds)
		: PackageStore(InPackageStore)
		, GlobalImportStore(InGlobalImportStore)
		, LoadedPackageStore(InLoadedPackageStore)
		, Desc(InDesc)
		, ImportedPackageIds(InImportedPackageIds)
	{
	}

	~FPackageImportStore()
	{
		check(ImportMap.Num() == 0);
	}

	inline bool IsValidLocalImportIndex(FPackageIndex LocalIndex)
	{
		check(ImportMap.Num() > 0);
		return LocalIndex.IsImport() && LocalIndex.ToImport() < ImportMap.Num();
	}

	inline UObject* FindOrGetImportObjectFromLocalIndex(FPackageIndex LocalIndex)
	{
		check(LocalIndex.IsImport());
		check(ImportMap.Num() > 0);
		const int32 LocalImportIndex = LocalIndex.ToImport();
		check(LocalImportIndex < ImportMap.Num());
		const FPackageObjectIndex GlobalIndex = ImportMap[LocalIndex.ToImport()];
		return FindOrGetImportObject(GlobalIndex);
	}

	inline UObject* FindOrGetImportObject(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsImport());
		UObject* Object = nullptr;
		if (GlobalIndex.IsScriptImport())
		{
			Object = GlobalImportStore.FindScriptImportObject(GlobalIndex);
		}
		else if (GlobalIndex.IsPackageImport())
		{
			Object = GlobalImportStore.FindPublicExportObject(FPublicExportKey::FromPackageImport(GlobalIndex, ImportedPackageIds, ImportedPublicExportHashes));
		}
		else
		{
			check(GlobalIndex.IsNull());
		}
		return Object;
	}

	bool GetUnresolvedCDOs(TArray<UClass*, TInlineAllocator<8>>& Classes)
	{
		for (const FPackageObjectIndex& Index : ImportMap)
		{
			if (!Index.IsScriptImport())
			{
				continue;
			}

			UObject* Object = GlobalImportStore.FindScriptImportObjectFromIndex(Index);
			if (Object)
			{
				continue;
			}

			const FScriptObjectEntry* Entry = GlobalImportStore.ScriptObjectEntriesMap.FindRef(Index);
			check(Entry);
			const FPackageObjectIndex& CDOClassIndex = Entry->CDOClassIndex;
			if (CDOClassIndex.IsScriptImport())
			{
				UObject* CDOClassObject = GlobalImportStore.FindScriptImportObjectFromIndex(CDOClassIndex);
				if (CDOClassObject)
				{
					UClass* CDOClass = static_cast<UClass*>(CDOClassObject);
					Classes.AddUnique(CDOClass);
				}
			}
		}
		return Classes.Num() > 0;
	}

	inline void StoreGlobalObject(FPackageId PackageId, uint64 ExportHash, UObject* Object)
	{
		GlobalImportStore.StoreGlobalObject(PackageId, ExportHash, Object);
	}

private:
	void AddAsyncFlags(UPackage* ImportedPackage)
	{
		UE_ASYNC_UPACKAGE_DEBUG(ImportedPackage);
		
		if (GUObjectArray.IsDisregardForGC(ImportedPackage))
		{
			return;
		}
		ForEachObjectWithOuter(ImportedPackage, [](UObject* Object)
		{
			if (Object->HasAllFlags(RF_Public | RF_WasLoaded))
			{
				checkf(!Object->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *Object->GetFullName());
				Object->SetInternalFlags(EInternalObjectFlags::Async);
			}
		}, /* bIncludeNestedObjects*/ true);
		checkf(!ImportedPackage->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *ImportedPackage->GetFullName());
		ImportedPackage->SetInternalFlags(EInternalObjectFlags::Async);
	}

	void ClearAsyncFlags(UPackage* ImportedPackage)
	{
		UE_ASYNC_UPACKAGE_DEBUG(ImportedPackage);

		if (GUObjectArray.IsDisregardForGC(ImportedPackage))
		{
			return;
		}
		ForEachObjectWithOuter(ImportedPackage, [](UObject* Object)
		{
			if (Object->HasAllFlags(RF_Public | RF_WasLoaded))
			{
				checkf(Object->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *Object->GetFullName());
				Object->AtomicallyClearInternalFlags(EInternalObjectFlags::Async);
			}
		}, /* bIncludeNestedObjects*/ true);
		checkf(ImportedPackage->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *ImportedPackage->GetFullName());
		ImportedPackage->AtomicallyClearInternalFlags(EInternalObjectFlags::Async);
	}

public:
	void AddPackageReferences()
	{
		for (const FPackageId& ImportedPackageId : ImportedPackageIds)
		{
			FLoadedPackageRef& PackageRef = LoadedPackageStore.GetPackageRef(ImportedPackageId);
			if (PackageRef.AddRef())
			{
				AddAsyncFlags(PackageRef.GetPackage());
			}
		}
		if (Desc.bCanBeImported)
		{
			FLoadedPackageRef& PackageRef = LoadedPackageStore.GetPackageRef(Desc.UPackageId);
			PackageRef.ClearErrorFlags();
			if (PackageRef.AddRef())
			{
				AddAsyncFlags(PackageRef.GetPackage());
			}
		}
	}

	void ReleasePackageReferences()
	{
		for (const FPackageId& ImportedPackageId : ImportedPackageIds)
		{
			FLoadedPackageRef& PackageRef = LoadedPackageStore.GetPackageRef(ImportedPackageId);
			if (PackageRef.ReleaseRef(Desc.UPackageId, ImportedPackageId))
			{
				ClearAsyncFlags(PackageRef.GetPackage());
			}
		}
		if (Desc.bCanBeImported)
		{
			// clear own reference, and possible all async flags if no remaining ref count
			FLoadedPackageRef& PackageRef = LoadedPackageStore.GetPackageRef(Desc.UPackageId);
			if (PackageRef.ReleaseRef(Desc.UPackageId, Desc.UPackageId))
			{
				ClearAsyncFlags(PackageRef.GetPackage());
			}
		}
	}
};
	
class FExportArchive final : public FArchive
{
public:
	FExportArchive(const uint8* AllExportDataPtr, const uint8* CurrentExportPtr, uint64 AllExportDataSize)
	{
#if (!DEVIRTUALIZE_FLinkerLoad_Serialize)
		ActiveFPLB = &InlineFPLB;
#endif
		ActiveFPLB->OriginalFastPathLoadBuffer = AllExportDataPtr;
		ActiveFPLB->StartFastPathLoadBuffer = CurrentExportPtr;
		ActiveFPLB->EndFastPathLoadBuffer = AllExportDataPtr + AllExportDataSize;
	}

	~FExportArchive()
	{
	}
	void ExportBufferBegin(UObject* Object, uint64 InExportCookedFileSerialOffset, uint64 InExportSerialSize)
	{
		CurrentExport = Object;
		CookedSerialOffset = InExportCookedFileSerialOffset;
		BufferSerialOffset = (ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer);
		CookedSerialSize = InExportSerialSize;
	}

	void ExportBufferEnd()
	{
		CurrentExport = nullptr;
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

	virtual bool IsUsingEventDrivenLoader() const override
	{
		return true;
	}

	/** FExportArchive will be created on the stack so we do not want BulkData objects caching references to it. */
	virtual FArchive* GetCacheableArchive()
	{
		return nullptr;
	}

	//~ Begin FArchive::FArchiveUObject Interface
	virtual FArchive& operator<<(FObjectPtr& Value) override { return FArchiveUObject::SerializeObjectPtr(*this, Value); }
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
		UE_ASYNC_PACKAGE_LOG(Fatal, *PackageDesc, TEXT("ObjectSerializationError"),
			TEXT("%s: Bad export index %d/%d."),
			CurrentExport ? *CurrentExport->GetFullName() : TEXT("null"), ExportIndex, Exports.Num());

		Object = nullptr;
	}

	FORCENOINLINE void HandleBadImportIndex(int32 ImportIndex, UObject*& Object)
	{
		UE_ASYNC_PACKAGE_LOG(Fatal, *PackageDesc, TEXT("ObjectSerializationError"),
			TEXT("%s: Bad import index %d/%d."),
			CurrentExport ? *CurrentExport->GetFullName() : TEXT("null"), ImportIndex, ImportStore->ImportMap.Num());
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
			if (ExportIndex < Exports.Num())
			{
				Object = Exports[ExportIndex].Object;

#if ALT2_LOG_VERBOSE
				const FExportMapEntry& Export = ExportMap[ExportIndex];
				FName ObjectName = NameMap->GetName(Export.ObjectName);
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
					TEXT("FExportArchive: Object"), TEXT("Import index %d is null"),
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
		UE_ASYNC_PACKAGE_LOG(Fatal, *PackageDesc, TEXT("ObjectSerializationError"),
			TEXT("%s: Bad name index %d/%d."),
			CurrentExport ? *CurrentExport->GetFullName() : TEXT("null"), NameIndex, NameMap->Num());
		Name = FName();
		SetCriticalError();
	}

	inline virtual FArchive& operator<<(FName& Name) override
	{
		FArchive& Ar = *this;
		uint32 NameIndex;
		Ar << NameIndex;
		uint32 Number = 0;
		Ar << Number;

		FMappedName MappedName = FMappedName::Create(NameIndex, Number, FMappedName::EType::Package);
		if (!NameMap->TryGetName(MappedName, Name))
		{
			HandleBadNameIndex(NameIndex, Name);
		}
		return *this;
	}
	//~ End FArchive::FLinkerLoad Interface

private:
	friend FAsyncPackage2;
#if (!DEVIRTUALIZE_FLinkerLoad_Serialize)
	FArchive::FFastPathLoadBuffer InlineFPLB;
	FArchive::FFastPathLoadBuffer* ActiveFPLB;
#endif

	UObject* TemplateForGetArchetypeFromLoader = nullptr;

	FAsyncPackageDesc2* PackageDesc = nullptr;
	FPackageImportStore* ImportStore = nullptr;
	TArray<FExternalReadCallback>* ExternalReadDependencies;
	const FNameMap* NameMap = nullptr;
	TArrayView<const FExportObject> Exports;
	const FExportMapEntry* ExportMap = nullptr;
	UObject* CurrentExport = nullptr;
	uint32 CookedHeaderSize = 0;
	uint64 CookedSerialOffset = 0;
	uint64 CookedSerialSize = 0;
	uint64 BufferSerialOffset = 0;
};

enum class EAsyncPackageLoadingState2 : uint8
{
	NewPackage,
	ImportPackages,
	ImportPackagesDone,
	WaitingForIo,
	ProcessPackageSummary,
	SetupDependencies,
	ProcessExportBundles,
	WaitingForExternalReads,
	ExportsDone,
	PostLoad,
	DeferredPostLoad,
	DeferredPostLoadDone,
	Finalize,
	CreateClusters,
	Complete,
	DeferredDelete,
};

class FEventLoadGraphAllocator;
struct FAsyncLoadEventSpec;
struct FAsyncLoadingThreadState2;

/** [EDL] Event Load Node */
class FEventLoadNode2
{
	enum class ENodeState : uint8
	{
		Waiting = 0,
		Executing,
		Timeout,
		Completed
	};
public:
	FEventLoadNode2(const FAsyncLoadEventSpec* InSpec, FAsyncPackage2* InPackage, int32 InImportOrExportIndex, int32 InBarrierCount);
	void DependsOn(FEventLoadNode2* Other);
	void AddBarrier();
	void AddBarrier(int32 Count);
	void ReleaseBarrier(FAsyncLoadingThreadState2* ThreadState = nullptr);
	void Execute(FAsyncLoadingThreadState2& ThreadState);

	int32 GetBarrierCount()
	{
		return BarrierCount.Load();
	}

	inline bool IsDone()
	{
		return ENodeState::Completed == static_cast<ENodeState>(NodeState.Load());
	}

	inline bool IsExecuting() const
	{
		return ENodeState::Executing == static_cast<ENodeState>(NodeState.Load());
	}

	inline void SetState(ENodeState InNodeState)
	{
		NodeState.Store(static_cast<uint8>(InNodeState));
	}

private:
	void ProcessDependencies(FAsyncLoadingThreadState2& ThreadState);
	void Fire(FAsyncLoadingThreadState2* ThreadState = nullptr);

	union
	{
		FEventLoadNode2* SingleDependent;
		FEventLoadNode2** MultipleDependents;
	};
	uint32 DependenciesCount = 0;
	uint32 DependenciesCapacity = 0;
	TAtomic<int32> BarrierCount { 0 };
	TAtomic<uint8> DependencyWriterCount { 0 };
	TAtomic<uint8> NodeState { static_cast<uint8>(ENodeState::Waiting) };
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	TAtomic<uint8> bFired { 0 };
#endif

	const FAsyncLoadEventSpec* Spec = nullptr;
	FAsyncPackage2* Package = nullptr;
	int32 ImportOrExportIndex = -1;
};

class FAsyncLoadEventGraphAllocator
{
public:
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
	FAAArrayQueue<FEventLoadNode2> Queue;
};

struct FAsyncLoadEventSpec
{
	typedef EAsyncPackageState::Type(*FAsyncLoadEventFunc)(FAsyncLoadingThreadState2&, FAsyncPackage2*, int32);
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

	bool HasDeferredFrees() const
	{
		return DeferredFreeArcs.Num() > 0;
	}

	void ProcessDeferredFrees()
	{
		if (DeferredFreeArcs.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessDeferredFrees);
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
 * Event node.
 */
enum EEventLoadNode2 : uint8
{
	Package_ProcessSummary,
	Package_SetupDependencies,
	Package_ExportsSerialized,
	Package_NumPhases,

	ExportBundle_Process = 0,
	ExportBundle_PostLoad,
	ExportBundle_DeferredPostLoad,
	ExportBundle_NumPhases,
};

struct FAsyncPackageData
{
	uint8* MemoryBuffer = nullptr;
	FPackageStoreExportInfo ExportInfo;
	uint64 ExportBundleHeadersSize = 0;
	uint64 ExportBundleEntriesSize = 0;
	uint8* ExportBundlesMetaMemory = nullptr;
	const FExportBundleHeader* ExportBundleHeaders = nullptr;
	const FExportBundleEntry* ExportBundleEntries = nullptr;
	TArrayView<FExportObject> Exports;
	TArrayView<FAsyncPackage2*> ImportedAsyncPackages;
	TArrayView<FEventLoadNode2> ExportBundleNodes;
	TArrayView<const FPackageId> ImportedPackageIds;
	TArrayView<const FSHAHash> ShaderMapHashes;
};

/**
* Structure containing intermediate data required for async loading of all exports of a package.
*/

struct FAsyncPackage2
{
	friend struct FScopedAsyncPackageEvent2;
	friend struct FAsyncPackageScope2;
	friend class FAsyncLoadingThread2;

	FAsyncPackage2(const FAsyncPackageDesc2& InDesc,
		FAsyncLoadingThread2& InAsyncLoadingThread,
		FAsyncLoadEventGraphAllocator& InGraphAllocator,
		const FAsyncLoadEventSpec* EventSpecs);
	virtual ~FAsyncPackage2();


	void AddRef()
	{
		++RefCount;
	}

	void ReleaseRef();

	void ClearImportedPackages();

	/** Marks a specific request as complete */
	void MarkRequestIDsAsComplete();

	/**
	 * @return Time load begun. This is NOT the time the load was requested in the case of other pending requests.
	 */
	double GetLoadStartTime() const;

	void AddCompletionCallback(TUniquePtr<FLoadPackageAsyncDelegate>&& Callback);

	FORCEINLINE UPackage* GetLinkerRoot() const
	{
		return LinkerRoot;
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

	void AddConstructedObject(UObject* Object, bool bSubObjectThatAlreadyExists)
	{
		if (bSubObjectThatAlreadyExists)
		{
			ConstructedObjects.AddUnique(Object);
		}
		else
		{
			checkf(!ConstructedObjects.Contains(Object), TEXT("%s"), *Object->GetFullName());
			ConstructedObjects.Add(Object);
		}
	}

	void PinObjectForGC(UObject* Object, bool bIsNewObject)
	{
		if (bIsNewObject && !IsInGameThread())
		{
			checkf(Object->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *Object->GetFullName());
		}
		else
		{
			Object->SetInternalFlags(EInternalObjectFlags::Async);
		}
	}

	void ClearConstructedObjects();

	/** Returns the UPackage wrapped by this, if it is valid */
	UPackage* GetLoadedPackage();

	/** Creates GC clusters from loaded objects */
	EAsyncPackageState::Type CreateClusters(FAsyncLoadingThreadState2& ThreadState);

	void ImportPackagesRecursive(FIoBatch& IoBatch, IPackageStore& PackageStore);
	void StartLoading(FIoBatch& IoBatch);

#if WITH_IOSTORE_IN_EDITOR
	void GetLoadedAssetsAndPackages(TSet<FWeakObjectPtr>& AssetList, TSet<UPackage*>& PackageList);
#endif

private:

	struct FExportToBundleMapping
	{
		uint64 ExportHash;
		int32 BundleIndex[FExportBundleEntry::ExportCommandType_Count];
	};

	uint8 PackageNodesMemory[EEventLoadNode2::Package_NumPhases * sizeof(FEventLoadNode2)];
	/** Basic information associated with this package */
	FAsyncPackageDesc2 Desc;
	FAsyncPackageData Data;
	/** Cached async loading thread object this package was created by */
	FAsyncLoadingThread2& AsyncLoadingThread;
	FAsyncLoadEventGraphAllocator& GraphAllocator;
	FPackageImportStore ImportStore;
	/** Package which is going to have its exports and imports loaded */
	UPackage* LinkerRoot = nullptr;
	/** Time load begun. This is NOT the time the load was requested in the case of pending requests.	*/
	double						LoadStartTime = 0.0;
	TAtomic<int32> RefCount{ 0 };
	int32						ProcessedExportBundlesCount = 0;
	/** Current bundle entry index in the current export bundle */
	int32						ExportBundleEntryIndex = 0;
	/** Current index into ExternalReadDependencies array used to spread wating for external reads over several frames			*/
	int32						ExternalReadIndex = 0;
	/** Current index into DeferredClusterObjects array used to spread routing CreateClusters over several frames			*/
	int32						DeferredClusterIndex = 0;
	EAsyncPackageLoadingState2	AsyncPackageLoadingState = EAsyncPackageLoadingState2::NewPackage;

	struct FAllDependenciesState
	{
		FAsyncPackage2* WaitingForPackage = nullptr;
		FAsyncPackage2* PackagesWaitingForThisHead = nullptr;
		FAsyncPackage2* PackagesWaitingForThisTail = nullptr;
		FAsyncPackage2* PrevLink = nullptr;
		FAsyncPackage2* NextLink = nullptr;
		uint32 LastTick = 0;
		bool bAllDone = false;
		bool bAnyNotDone = false;
		bool bVisitedMark = false;

		void UpdateTick(int32 CurrentTick)
		{
			if (LastTick != CurrentTick)
			{
				LastTick = CurrentTick;
				bAnyNotDone = false;
				bVisitedMark = false;
			}
		}

		static void AddToWaitList(FAllDependenciesState FAsyncPackage2::* StateMemberPtr, FAsyncPackage2* WaitListPackage, FAsyncPackage2* PackageToAdd)
		{
			check(WaitListPackage);
			check(PackageToAdd);
			FAllDependenciesState& WaitListPackageState = WaitListPackage->*StateMemberPtr;
			FAllDependenciesState& PackageToAddState = PackageToAdd->*StateMemberPtr;
			
			if (PackageToAddState.WaitingForPackage == WaitListPackage)
			{
				return;
			}
			if (PackageToAddState.WaitingForPackage)
			{
				PackageToAddState.RemoveFromWaitList(StateMemberPtr, PackageToAddState.WaitingForPackage, PackageToAdd);
			}

			check(!PackageToAddState.PrevLink);
			check(!PackageToAddState.NextLink);
			if (WaitListPackageState.PackagesWaitingForThisTail)
			{
				FAllDependenciesState& WaitListTailState = WaitListPackageState.PackagesWaitingForThisTail->*StateMemberPtr;
				check(!WaitListTailState.NextLink);
				WaitListTailState.NextLink = PackageToAdd;
				PackageToAddState.PrevLink = WaitListPackageState.PackagesWaitingForThisTail;
			}
			else
			{
				check(!WaitListPackageState.PackagesWaitingForThisHead);
				WaitListPackageState.PackagesWaitingForThisHead = PackageToAdd;
			}
			WaitListPackageState.PackagesWaitingForThisTail = PackageToAdd;
			PackageToAddState.WaitingForPackage = WaitListPackage;
		}

		static void RemoveFromWaitList(FAllDependenciesState FAsyncPackage2::* StateMemberPtr, FAsyncPackage2* WaitListPackage, FAsyncPackage2* PackageToRemove)
		{
			check(WaitListPackage);
			check(PackageToRemove);

			FAllDependenciesState& WaitListPackageState = WaitListPackage->*StateMemberPtr;
			FAllDependenciesState& PackageToRemoveState = PackageToRemove->*StateMemberPtr;

			check(PackageToRemoveState.WaitingForPackage == WaitListPackage);
			if (PackageToRemoveState.PrevLink)
			{
				FAllDependenciesState& PrevLinkState = PackageToRemoveState.PrevLink->*StateMemberPtr;
				PrevLinkState.NextLink = PackageToRemoveState.NextLink;
			}
			else
			{
				check(WaitListPackageState.PackagesWaitingForThisHead == PackageToRemove);
				WaitListPackageState.PackagesWaitingForThisHead = PackageToRemoveState.NextLink;
			}
			if (PackageToRemoveState.NextLink)
			{
				FAllDependenciesState& NextLinkState = PackageToRemoveState.NextLink->*StateMemberPtr;
				NextLinkState.PrevLink = PackageToRemoveState.PrevLink;
			}
			else
			{
				check(WaitListPackageState.PackagesWaitingForThisTail == PackageToRemove);
				WaitListPackageState.PackagesWaitingForThisTail = PackageToRemoveState.PrevLink;
			}
			PackageToRemoveState.PrevLink = nullptr;
			PackageToRemoveState.NextLink = nullptr;
			PackageToRemoveState.WaitingForPackage = nullptr;
		}
	};
	FAllDependenciesState		AllDependenciesSerializedState;
	FAllDependenciesState		AllDependenciesFullyLoadedState;

	/** True if our load has failed */
	bool						bLoadHasFailed = false;
	/** True if this package was created by this async package */
	bool						bCreatedLinkerRoot = false;

	/** List of all request handles */
	TArray<int32, TInlineAllocator<2>> RequestIDs;
	/** List of ConstructedObjects = Exports + UPackage + ObjectsCreatedFromExports */
	TArray<UObject*> ConstructedObjects;
	TArray<FExternalReadCallback> ExternalReadDependencies;
	/** Call backs called when we finished loading this package											*/
	using FCompletionCallback = TUniquePtr<FLoadPackageAsyncDelegate>;
	TArray<FCompletionCallback, TInlineAllocator<2>> CompletionCallbacks;

	FIoRequest IoRequest;
	const uint8* AllExportDataPtr = nullptr;
	const uint8* CurrentExportDataPtr = nullptr;
	uint32 CookedHeaderSize = 0;

	TArrayView<const FExportMapEntry> ExportMap;
	TArrayView<const uint64> PublicExportHashes;
	TArrayView<const uint8> ArcsData;
	FNameMap NameMap;
	TArray<FExportToBundleMapping> ExportToBundleMappings;

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

	static EAsyncPackageState::Type Event_ProcessExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex);
	static EAsyncPackageState::Type Event_ProcessPackageSummary(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32);
	static EAsyncPackageState::Type Event_SetupDependencies(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32);
	static EAsyncPackageState::Type Event_ExportsDone(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32);
	static EAsyncPackageState::Type Event_PostLoadExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex);
	static EAsyncPackageState::Type Event_DeferredPostLoadExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex);
	
	void EventDrivenCreateExport(int32 LocalExportIndex);
	bool EventDrivenSerializeExport(int32 LocalExportIndex, FExportArchive& Ar);

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

	FEventLoadNode2& GetPackageNode(EEventLoadNode2 Phase);
	FEventLoadNode2& GetExportBundleNode(EEventLoadNode2 Phase, uint32 ExportBundleIndex);

	/** [EDL] End Event driven loader specific stuff */

	void CallCompletionCallbacks(EAsyncLoadingResult::Type LoadingResult);

private:
	void CreatePackageNodes(const FAsyncLoadEventSpec* EventSpecs);
	void CreateExportBundleNodes(const FAsyncLoadEventSpec* EventSpecs);
	void SetupSerializedArcs();
	void SetupScriptDependencies();
	bool HaveAllDependenciesReachedStateDebug(FAsyncPackage2* Package, TSet<FAsyncPackage2*>& VisitedPackages, EAsyncPackageLoadingState2 WaitForPackageState);
	bool HaveAllDependenciesReachedState(FAllDependenciesState FAsyncPackage2::* StateMemberPtr, EAsyncPackageLoadingState2 WaitForPackageState, uint32 CurrentTick);
	void UpdateDependenciesStateRecursive(FAllDependenciesState FAsyncPackage2::* StateMemberPtr, EAsyncPackageLoadingState2 WaitForPackageState, uint32 CurrentTick, FAsyncPackage2* Root);
	void WaitForAllDependenciesToReachState(FAllDependenciesState FAsyncPackage2::* StateMemberPtr, EAsyncPackageLoadingState2 WaitForPackageState, uint32& CurrentTickVariable, TFunctionRef<void(FAsyncPackage2*)> OnStateReached);
	void ConditionalBeginPostLoad();
	void ConditionalFinishLoading();

	/**
	 * Begin async loading process. Simulates parts of BeginLoad.
	 *
	 * Objects created during BeginAsyncLoad and EndAsyncLoad will have EInternalObjectFlags::AsyncLoading set
	 */
	void BeginAsyncLoad();
	/**
	 * End async loading process. Simulates parts of EndLoad().
	 */
	void EndAsyncLoad();
	/**
	 * Create UPackage
	 *
	 * @return true
	 */
	void CreateUPackage(const FZenPackageSummary* PackageSummary, const FZenPackageVersioningInfo* VersioningInfo);

	/**
	 * Finish up UPackage
	 *
	 * @return true
	 */
	void FinishUPackage();

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
#if WITH_IOSTORE_IN_EDITOR
	IAsyncPackageLoader* PreviousAsyncPackageLoader;
#endif

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

	mutable bool bLazyInitializedFromLoadPackage = false;

#if ALT2_VERIFY_RECURSIVE_LOADS
	int32 LoadRecursionLevel = 0;
#endif

#if !UE_BUILD_SHIPPING
	FPlatformFileOpenLog* FileOpenLogWrapper = nullptr;
#endif

	/** [ASYNC/GAME THREAD] Event used to signal loading should be cancelled */
	FEvent* CancelLoadingEvent;
	/** [ASYNC/GAME THREAD] Event used to signal that the async loading thread should be suspended */
	FEvent* ThreadSuspendedEvent;
	/** [ASYNC/GAME THREAD] Event used to signal that the async loading thread has resumed */
	FEvent* ThreadResumedEvent;
	TArray<FAsyncPackage2*> LoadedPackagesToProcess;
	/** [GAME THREAD] Game thread CompletedPackages list */
	TArray<FAsyncPackage2*> CompletedPackages;
#if WITH_IOSTORE_IN_EDITOR
	/** [GAME THREAD] Game thread LoadedAssets list */
	TSet<FWeakObjectPtr> LoadedAssets;
#endif
	/** [ASYNC/GAME THREAD] Packages to be deleted from async thread */
	TQueue<FAsyncPackage2*, EQueueMode::Spsc> DeferredDeletePackages;
	
	struct FFailedPackageRequest
	{
		int32 RequestID = INDEX_NONE;
		FName PackageName;
		TUniquePtr<FLoadPackageAsyncDelegate> Callback;
	};
	TArray<FFailedPackageRequest> FailedPackageRequests;
	FCriticalSection FailedPackageRequestsCritical;

	FCriticalSection AsyncPackagesCritical;
	/** Packages in active loading with GetAsyncPackageId() as key */
	TMap<FPackageId, FAsyncPackage2*> AsyncPackageLookup;

	TQueue<FAsyncPackage2*, EQueueMode::Mpsc> ExternalReadQueue;
	TAtomic<int32> PendingIoRequestsCounter{ 0 };
	
	/** List of all pending package requests */
	TSet<int32> PendingRequests;
	/** Synchronization object for PendingRequests list */
	FCriticalSection PendingRequestsCritical;

	/** [ASYNC/GAME THREAD] Number of package load requests in the async loading queue */
	TAtomic<uint32> QueuedPackagesCounter { 0 };
	/** [ASYNC/GAME THREAD] Number of packages being loaded on the async thread and post loaded on the game thread */
	FThreadSafeCounter ExistingAsyncPackagesCounter;
	/** [ASYNC/GAME THREAD] Number of packages being loaded on the async thread and post loaded on the game thread. Excludes packages in the deferred delete queue*/
	FThreadSafeCounter ActiveAsyncPackagesCounter;

	FThreadSafeCounter AsyncThreadReady;

	/** When cancelling async loading: list of package requests to cancel */
	TArray<FAsyncPackageDesc2*> QueuedPackagesToCancel;
	/** When cancelling async loading: list of packages to cancel */
	TSet<FAsyncPackage2*> PackagesToCancel;

	/** Async loading thread ID */
	uint32 AsyncLoadingThreadID;

	/** I/O Dispatcher */
	FIoDispatcher& IoDispatcher;

	TSharedPtr<IPackageStore> PackageStore;
	FLoadedPackageStore LoadedPackageStore;
	FGlobalImportStore GlobalImportStore;
	TSpscQueue<FPackageRequest> PackageRequestQueue;
	TArray<FAsyncPackage2*> PendingPackages;

	/** Initial load pending CDOs */
	TMap<UClass*, TArray<FEventLoadNode2*>> PendingCDOs;

	uint32 ConditionalBeginPostLoadTick = 0;
	uint32 ConditionalFinishLoadingTick = 0;

public:

	//~ Begin FRunnable Interface.
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	//~ End FRunnable Interface

	/** Start the async loading thread */
	virtual void StartThread() override;

	/** [EDL] Event queue */
	FZenaphore AltZenaphore;
	TArray<FZenaphore> WorkerZenaphores;
	FAsyncLoadEventGraphAllocator GraphAllocator;
	FAsyncLoadEventQueue2 EventQueue;
	FAsyncLoadEventQueue2 MainThreadEventQueue;
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

	virtual void NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObjectThatAlreadyExists) override;

	virtual void NotifyUnreachableObjects(const TArrayView<FUObjectItem*>& UnreachableObjects) override;

	virtual void FireCompletedCompiledInImport(void* AsyncPackage, FPackageIndex Import) override {}

	FORCEINLINE FAsyncPackage2* FindAsyncPackage(FPackageId PackageId)
	{
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
		//checkSlow(IsInAsyncLoadThread());
		return AsyncPackageLookup.FindRef(PackageId);
	}

	FORCEINLINE FAsyncPackage2* GetAsyncPackage(const FPackageId& PackageId)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(GetAsyncPackage);
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
		return AsyncPackageLookup.FindRef(PackageId);
	}

	void UpdatePackagePriority(FAsyncPackage2* Package, int32 NewPriority);

	FAsyncPackage2* FindOrInsertPackage(FAsyncPackageDesc2& InDesc, bool& bInserted, TUniquePtr<FLoadPackageAsyncDelegate>&& PackageLoadedDelegate = TUniquePtr<FLoadPackageAsyncDelegate>());
	void QueueMissingPackage(FAsyncPackageDesc2& PackageDesc, TUniquePtr<FLoadPackageAsyncDelegate>&& LoadPackageAsyncDelegate);

	/**
	* [ASYNC* THREAD] Loads all packages
	*
	* @param OutPackagesProcessed Number of packages processed in this call.
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type ProcessAsyncLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, int32& OutPackagesProcessed);

	/**
	* [GAME THREAD] Ticks game thread side of async loading.
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type TickAsyncLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, int32 FlushRequestID = INDEX_NONE);

	/**
	* [ASYNC THREAD] Main thread loop
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	*/
	EAsyncPackageState::Type TickAsyncThreadFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool& bDidSomething);

	/** Initializes async loading thread */
	virtual void InitializeLoading() override;

	virtual void ShutdownLoading() override;

	virtual int32 LoadPackage(
		const FPackagePath& InPackagePath,
		FName InCustomName,
		FLoadPackageAsyncDelegate InCompletionDelegate,
		EPackageFlags InPackageFlags,
		int32 InPIEInstanceID,
		int32 InPackagePriority,
		const FLinkerInstancingContext* InstancingContext = nullptr) override;

	EAsyncPackageState::Type ProcessLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool bUseTimeLimit, bool bUseFullTimeLimit, double TimeLimit);

	inline virtual EAsyncPackageState::Type ProcessLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, double TimeLimit) override
	{
		FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
		return ProcessLoadingFromGameThread(ThreadState, bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
	}

	EAsyncPackageState::Type ProcessLoadingUntilCompleteFromGameThread(FAsyncLoadingThreadState2& ThreadState, TFunctionRef<bool()> CompletionPredicate, double TimeLimit);

	inline virtual EAsyncPackageState::Type ProcessLoadingUntilComplete(TFunctionRef<bool()> CompletionPredicate, double TimeLimit) override
	{
		FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
		return ProcessLoadingUntilCompleteFromGameThread(ThreadState, CompletionPredicate, TimeLimit);
	}

	virtual void CancelLoading() override;

	virtual void SuspendLoading() override;

	virtual void ResumeLoading() override;

	virtual void FlushLoading(int32 PackageId) override;

	virtual int32 GetNumQueuedPackages() override
	{
		return QueuedPackagesCounter;
	}

	virtual int32 GetNumAsyncPackages() override
	{
		return ActiveAsyncPackagesCounter.GetValue();
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
	void RemovePendingRequests(TArrayView<int32> RequestIDs)
	{
		FScopeLock Lock(&PendingRequestsCritical);
		for (int32 ID : RequestIDs)
		{
			PendingRequests.Remove(ID);
			TRACE_LOADTIME_END_REQUEST(ID);
		}
	}

	void AddPendingCDOs(FAsyncPackage2* Package, TArray<UClass*, TInlineAllocator<8>>& Classes)
	{
		FEventLoadNode2& FirstBundleNode = Package->GetExportBundleNode(ExportBundle_Process, 0);
		FirstBundleNode.AddBarrier(Classes.Num());
		for (UClass* Class : Classes)
		{
			PendingCDOs.FindOrAdd(Class).Add(&FirstBundleNode);
		}
	}

private:

	void SuspendWorkers();
	void ResumeWorkers();

	void LazyInitializeFromLoadPackage();
	void FinalizeInitialLoad();

	void RemoveUnreachableObjects(const FUnreachablePublicExports& PublicExports, const FUnreachablePackages& Packages);

	bool ProcessPendingCDOs()
	{
		if (PendingCDOs.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessPendingCDOs);

			auto It = PendingCDOs.CreateIterator();
			UClass* Class = It.Key();
			TArray<FEventLoadNode2*> Nodes = MoveTemp(It.Value());
			It.RemoveCurrent();

			UE_LOG(LogStreaming, Verbose, TEXT("ProcessPendingCDOs: Creating CDO for %s. %d entries remaining."), *Class->GetFullName(), PendingCDOs.Num());
			UObject* CDO = Class->GetDefaultObject();

			ensureMsgf(CDO, TEXT("Failed to create CDO for %s"), *Class->GetFullName());
			UE_LOG(LogStreaming, Verbose, TEXT("ProcessPendingCDOs: Created CDO for %s."), *Class->GetFullName());

			for (FEventLoadNode2* Node : Nodes)
			{
				Node->ReleaseBarrier();
			}
			return true;
		}
		return false;
	}

	/**
	* [GAME THREAD] Performs game-thread specific operations on loaded packages (not-thread-safe PostLoad, callbacks)
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type ProcessLoadedPackagesFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool& bDidSomething, int32 FlushRequestID = INDEX_NONE);

	bool CreateAsyncPackagesFromQueue(FAsyncLoadingThreadState2& ThreadState);

	FAsyncPackage2* CreateAsyncPackage(const FAsyncPackageDesc2& Desc)
	{
		UE_ASYNC_PACKAGE_DEBUG(Desc);

		ExistingAsyncPackagesCounter.Increment();
		return new FAsyncPackage2(Desc, *this, GraphAllocator, EventSpecs.GetData());
	}

	void InitializeAsyncPackageFromPackageStore(FAsyncPackage2* AsyncPackage, const FPackageStoreEntry& PackageStoreEntry)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(InitializeAsyncPackageFromPackageStore);
		UE_ASYNC_PACKAGE_DEBUG(AsyncPackage->Desc);
		
		FAsyncPackageData& Data = AsyncPackage->Data;
		Data.ExportInfo = PackageStoreEntry.ExportInfo;

		const int32 ExportBundleNodeCount = Data.ExportInfo.ExportBundleCount * EEventLoadNode2::ExportBundle_NumPhases;
		const int32 ImportedPackageCount = PackageStoreEntry.ImportedPackageIds.Num();
		const int32 ShaderMapHashesCount = PackageStoreEntry.ShaderMapHashes.Num();
		
		Data.ExportBundleHeadersSize = sizeof(FExportBundleHeader) * Data.ExportInfo.ExportBundleCount;
		Data.ExportBundleEntriesSize = sizeof(FExportBundleEntry) * Data.ExportInfo.ExportCount * FExportBundleEntry::ExportCommandType_Count;
		const uint64 ExportBundlesMetaSize = Data.ExportBundleHeadersSize + Data.ExportBundleEntriesSize;

		const uint64 ExportBundlesMetaMemSize = Align(ExportBundlesMetaSize, 8);
		const uint64 ExportsMemSize = Align(sizeof(FExportObject) * Data.ExportInfo.ExportCount, 8);
		const uint64 ImportedPackagesMemSize = Align(sizeof(FAsyncPackage2*) * ImportedPackageCount, 8);
		const uint64 ExportBundleNodesMemSize = Align(sizeof(FEventLoadNode2) * ExportBundleNodeCount, 8);
		const uint64 ImportedPackageIdsMemSize = Align(sizeof(FPackageId) * ImportedPackageCount, 8);
		const uint64 ShaderMapHashesMemSize = Align(sizeof(FSHAHash) * ShaderMapHashesCount, 8);
		const uint64 MemoryBufferSize =
			ExportBundlesMetaMemSize +
			ExportsMemSize +
			ImportedPackagesMemSize +
			ExportBundleNodesMemSize +
			ImportedPackageIdsMemSize +
			ShaderMapHashesMemSize;

		Data.MemoryBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(MemoryBufferSize));

		uint8* DataPtr = Data.MemoryBuffer;

		Data.ExportBundlesMetaMemory = DataPtr;
		Data.ExportBundleHeaders = reinterpret_cast<const FExportBundleHeader*>(Data.ExportBundlesMetaMemory);
		Data.ExportBundleEntries = reinterpret_cast<const FExportBundleEntry*>(Data.ExportBundleHeaders + Data.ExportInfo.ExportBundleCount);

		DataPtr += ExportBundlesMetaMemSize;
		Data.Exports = MakeArrayView(reinterpret_cast<FExportObject*>(DataPtr), Data.ExportInfo.ExportCount);
		DataPtr += ExportsMemSize;
		Data.ImportedAsyncPackages = MakeArrayView(reinterpret_cast<FAsyncPackage2**>(DataPtr), 0);
		DataPtr += ImportedPackagesMemSize;
		Data.ExportBundleNodes = MakeArrayView(reinterpret_cast<FEventLoadNode2*>(DataPtr), ExportBundleNodeCount);
		DataPtr += ExportBundleNodesMemSize;
		Data.ImportedPackageIds = MakeArrayView(reinterpret_cast<const FPackageId*>(DataPtr), ImportedPackageCount);
		FMemory::Memcpy((void*)Data.ImportedPackageIds.GetData(), PackageStoreEntry.ImportedPackageIds.GetData(), sizeof(FPackageId) * ImportedPackageCount);
		DataPtr += ImportedPackageIdsMemSize;
		Data.ShaderMapHashes = MakeArrayView(reinterpret_cast<const FSHAHash*>(DataPtr), ShaderMapHashesCount);
		FMemory::Memcpy((void*)Data.ShaderMapHashes.GetData(), PackageStoreEntry.ShaderMapHashes.GetData(), sizeof(FSHAHash) * ShaderMapHashesCount);
	
		AsyncPackage->CreateExportBundleNodes(EventSpecs.GetData());

		AsyncPackage->ImportStore.AddPackageReferences();
		AsyncPackage->ConstructedObjects.Reserve(Data.ExportInfo.ExportCount + 1); // +1 for UPackage, may grow dynamically beoynd that
		for (FExportObject& Export : Data.Exports)
		{
			Export = FExportObject();
		}
	}

	void DeleteAsyncPackage(FAsyncPackage2* Package)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DeleteAsyncPackage);
		UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
		delete Package;
		ExistingAsyncPackagesCounter.Decrement();
	}

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
#if WITH_IOSTORE_IN_EDITOR
	IAsyncPackageLoader* PreviousAsyncPackageLoader;
#endif
	/** Cached ThreadContext so we don't have to access it again */
	FUObjectThreadContext& ThreadContext;

	FAsyncPackageScope2(FAsyncPackage2* InPackage)
		: ThreadContext(FUObjectThreadContext::Get())
	{
		PreviousPackage = ThreadContext.AsyncPackage;
		ThreadContext.AsyncPackage = InPackage;
#if WITH_IOSTORE_IN_EDITOR
		PreviousAsyncPackageLoader = ThreadContext.AsyncPackageLoader;
		ThreadContext.AsyncPackageLoader = &InPackage->AsyncLoadingThread;
#endif
	}
	~FAsyncPackageScope2()
	{
		ThreadContext.AsyncPackage = PreviousPackage;
#if WITH_IOSTORE_IN_EDITOR
		ThreadContext.AsyncPackageLoader = PreviousAsyncPackageLoader;
#endif
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
#if !UE_BUILD_SHIPPING
	{
		FString DebugPackageNamesString;
		FParse::Value(FCommandLine::Get(), TEXT("-s.DebugPackageNames="), DebugPackageNamesString);
		ParsePackageNames(DebugPackageNamesString, GAsyncLoading2_DebugPackageIds);
		FString VerbosePackageNamesString;
		FParse::Value(FCommandLine::Get(), TEXT("-s.VerbosePackageNames="), VerbosePackageNamesString);
		ParsePackageNames(VerbosePackageNamesString, GAsyncLoading2_VerbosePackageIds);
		ParsePackageNames(DebugPackageNamesString, GAsyncLoading2_VerbosePackageIds);
		GAsyncLoading2_VerboseLogFilter = GAsyncLoading2_VerbosePackageIds.Num() > 0 ? 1 : 2;
	}

	FileOpenLogWrapper = (FPlatformFileOpenLog*)(FPlatformFileManager::Get().FindPlatformFile(FPlatformFileOpenLog::GetTypeName()));
#endif

#if USE_NEW_BULKDATA || WITH_IOSTORE_IN_EDITOR
	FBulkDataBase::SetIoDispatcher(&IoDispatcher);
#endif

	UE_CLOG(!FCoreDelegates::CreatePackageStore.IsBound(), LogStreaming, Fatal, TEXT("CreatePackageStore delegate not bound when initializing package loader"));
	PackageStore = FCoreDelegates::CreatePackageStore.Execute();
	UE_CLOG(!PackageStore.IsValid(), LogStreaming, Fatal, TEXT("CreatePackageStore delegate returned null when initializing package loader"));
	PackageStore->Initialize();
	PackageStore->OnPendingEntriesAdded().AddLambda([this]()
	{
		AltZenaphore.NotifyOne();
	});
	
	FPackageName::DoesPackageExistOverride().BindLambda([this](FName PackageName)
	{
		LazyInitializeFromLoadPackage();
		
		PackageStore->Lock();
		bool bExists = PackageStore->DoesPackageExist(FPackageId::FromName(PackageName));
		PackageStore->Unlock();
		return bExists;
	});

	AsyncThreadReady.Increment();

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - Initialized"));
}

void FAsyncLoadingThread2::UpdatePackagePriority(FAsyncPackage2* Package, int32 NewPriority)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdatePackagePriority);
	Package->Desc.Priority = NewPriority;
	Package->IoRequest.UpdatePriority(NewPriority);
}

FAsyncPackage2* FAsyncLoadingThread2::FindOrInsertPackage(FAsyncPackageDesc2& Desc, bool& bInserted, TUniquePtr<FLoadPackageAsyncDelegate>&& PackageLoadedDelegate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindOrInsertPackage);
	FAsyncPackage2* Package = nullptr;
	bInserted = false;
	{
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
		Package = AsyncPackageLookup.FindRef(Desc.UPackageId);
		if (!Package)
		{
			Package = CreateAsyncPackage(Desc);
			checkf(Package, TEXT("Failed to create async package %s"), *Desc.UPackageName.ToString());
			Package->AddRef();
			ActiveAsyncPackagesCounter.Increment();
			AsyncPackageLookup.Add(Desc.UPackageId, Package);
			bInserted = true;
		}
		else
		{
			if (Desc.RequestID > 0)
			{
				Package->AddRequestID(Desc.RequestID);
			}
			if (Desc.Priority > Package->Desc.Priority)
			{
				UpdatePackagePriority(Package, Desc.Priority);
			}
		}
		if (PackageLoadedDelegate.IsValid())
		{
			Package->AddCompletionCallback(MoveTemp(PackageLoadedDelegate));
		}
	}
	return Package;
}

bool FAsyncLoadingThread2::CreateAsyncPackagesFromQueue(FAsyncLoadingThreadState2& ThreadState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateAsyncPackagesFromQueue);
	
	bool bPackagesCreated = false;
	const int32 TimeSliceGranularity = ThreadState.UseTimeLimit() ? 4 : MAX_int32;

	FIoBatch IoBatch = IoDispatcher.NewBatch();

	PackageStore->Lock();

	for (auto It = PendingPackages.CreateIterator(); It; ++It)
	{
		FAsyncPackage2* PendingPackage = *It;
		FPackageStoreEntry PackageEntry;
		if (EPackageStoreEntryStatus::Ok == PackageStore->GetPackageStoreEntry(PendingPackage->Desc.PackageIdToLoad, PackageEntry))
		{
			InitializeAsyncPackageFromPackageStore(PendingPackage, PackageEntry);
			PendingPackage->ImportPackagesRecursive(IoBatch, *PackageStore.Get());
			PendingPackage->StartLoading(IoBatch);
			It.RemoveCurrent();
		}
	}
	for (;;)
	{
		int32 NumDequeued = 0;
		while (NumDequeued < TimeSliceGranularity)
		{
			TOptional<FPackageRequest> OptionalRequest = PackageRequestQueue.Dequeue();
			if (!OptionalRequest.IsSet())
			{
				break;
			}
			++NumDequeued;
		
			FPackageRequest& Request = OptionalRequest.GetValue();
			TCHAR NameBuffer[FName::StringBufferSize];
			uint32 NameLen = Request.PackageNameToLoad.ToString(NameBuffer);
			FStringView PackageNameStr = FStringView(NameBuffer, NameLen);
			if (!FPackageName::IsValidLongPackageName(PackageNameStr))
			{
				FString NewPackageNameStr;
				if (FPackageName::TryConvertFilenameToLongPackageName(FString(PackageNameStr), NewPackageNameStr))
				{
					Request.PackageNameToLoad = *NewPackageNameStr;
					Request.PackageIdToLoad = FPackageId::FromName(Request.PackageNameToLoad);
				}
			}

			Request.UPackageName = Request.PackageNameToLoad;
			{
				FName SourcePackageName;
				FPackageId RedirectedToPackageId;
				if (PackageStore->GetPackageRedirectInfo(Request.PackageIdToLoad, SourcePackageName, RedirectedToPackageId))
				{
					Request.PackageIdToLoad = RedirectedToPackageId;
					Request.UPackageName = SourcePackageName;
				}
			}

			// Fixup CustomName to handle any input string that can be converted to a long package name.
			if (!Request.CustomName.IsNone())
			{
				NameLen = Request.CustomName.ToString(NameBuffer);
				PackageNameStr = FStringView(NameBuffer, NameLen);
				if (!FPackageName::IsValidLongPackageName(PackageNameStr))
				{
					FString NewPackageNameStr;
					if (FPackageName::TryConvertFilenameToLongPackageName(FString(PackageNameStr), NewPackageNameStr))
					{
						Request.CustomName = *NewPackageNameStr;
					}
				}
				Request.UPackageName = Request.CustomName;
			}

			FPackageId PackageIdToLoad = Request.PackageIdToLoad;
			FPackageStoreEntry PackageEntry;
			EPackageStoreEntryStatus PackageStatus = PackageStore->GetPackageStoreEntry(PackageIdToLoad, PackageEntry);
			if (PackageStatus == EPackageStoreEntryStatus::Missing)
			{
				// While there is an active load request for (InName=/Temp/PackageABC_abc, InPackageToLoadFrom=/Game/PackageABC), then allow these requests too:
				// (InName=/Temp/PackageA_abc, InPackageToLoadFrom=/Temp/PackageABC_abc) and (InName=/Temp/PackageABC_xyz, InPackageToLoadFrom=/Temp/PackageABC_abc)
				FAsyncPackage2* Package = GetAsyncPackage(Request.PackageIdToLoad);
				if (Package)
				{
					PackageIdToLoad = Package->Desc.PackageIdToLoad;
					PackageStatus = PackageStore->GetPackageStoreEntry(PackageIdToLoad, PackageEntry);
				}
			}
			FAsyncPackageDesc2 PackageDesc = FAsyncPackageDesc2::FromPackageRequest(Request.RequestId, Request.Priority, Request.UPackageName, PackageIdToLoad, !Request.CustomName.IsNone());
			if (PackageStatus == EPackageStoreEntryStatus::Missing)
			{
				QueueMissingPackage(PackageDesc, MoveTemp(Request.PackageLoadedDelegate));
			}
			else
			{
				bool bInserted;
				FAsyncPackage2* Package = FindOrInsertPackage(PackageDesc, bInserted, MoveTemp(Request.PackageLoadedDelegate));
				checkf(Package, TEXT("Failed to find or insert package %s"), *PackageDesc.UPackageName.ToString());

				if (bInserted)
				{
					UE_ASYNC_PACKAGE_LOG(Verbose, PackageDesc, TEXT("CreateAsyncPackages: AddPackage"),
						TEXT("Start loading package."));
#if !UE_BUILD_SHIPPING
					if (FileOpenLogWrapper)
					{
						FileOpenLogWrapper->AddPackageToOpenLog(*PackageDesc.UPackageName.ToString());
					}
#endif
					if (PackageStatus == EPackageStoreEntryStatus::Ok)
					{
						InitializeAsyncPackageFromPackageStore(Package, PackageEntry);
						{
							TRACE_CPUPROFILER_EVENT_SCOPE(ImportPackages);
							Package->ImportPackagesRecursive(IoBatch, *PackageStore.Get());
						}
						Package->StartLoading(IoBatch);
					}
					else
					{
						check(PackageStatus == EPackageStoreEntryStatus::Pending);
						PendingPackages.Add(Package);
					}
				}
				else
				{
					UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbose, PackageDesc, TEXT("CreateAsyncPackages: UpdatePackage"),
						TEXT("Package is alreay being loaded."));
				}

				--QueuedPackagesCounter;
			}
		}
		
		bPackagesCreated |= NumDequeued > 0;

		if (!NumDequeued || ThreadState.IsTimeLimitExceeded(TEXT("CreateAsyncPackagesFromQueue")))
		{
			break;
		}
	}

	PackageStore->Unlock();

	IoBatch.Issue();
	
	return bPackagesCreated;
}

FEventLoadNode2::FEventLoadNode2(const FAsyncLoadEventSpec* InSpec, FAsyncPackage2* InPackage, int32 InImportOrExportIndex, int32 InBarrierCount)
	: BarrierCount(InBarrierCount)
	, Spec(InSpec)
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
	check(!IsDone());
	check(!bFired);
#endif
	uint8 Expected = 0;
	while (!Other->DependencyWriterCount.CompareExchange(Expected, 1))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DependsOnContested);
		check(Expected == 1);
		Expected = 0;
	}
	if (!Other->IsDone())
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
	check(!IsDone());
	check(!bFired);
#endif
	++BarrierCount;
}

void FEventLoadNode2::AddBarrier(int32 Count)
{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	check(!IsDone());
	check(!bFired);
#endif
	BarrierCount += Count;
}

void FEventLoadNode2::ReleaseBarrier(FAsyncLoadingThreadState2* ThreadState)
{
	check(BarrierCount > 0);
	if (--BarrierCount == 0)
	{
		Fire(ThreadState);
	}
}

void FEventLoadNode2::Fire(FAsyncLoadingThreadState2* ThreadState)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(Fire);

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	bFired.Store(1);
#endif

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
	check(WITH_IOSTORE_IN_EDITOR || !ThreadState.CurrentEventNode || ThreadState.CurrentEventNode == this);

#if WITH_IOSTORE_IN_EDITOR
	// Allow recursive execution of event nodes in editor builds
	FEventLoadNode2* PrevNode = ThreadState.CurrentEventNode != this ? ThreadState.CurrentEventNode : nullptr;
	SetState(ENodeState::Executing);
#endif
	ThreadState.CurrentEventNode = this;
	EAsyncPackageState::Type State = Spec->Func(ThreadState, Package, ImportOrExportIndex);
	if (State == EAsyncPackageState::Complete)
	{
		SetState(ENodeState::Completed);
		ThreadState.CurrentEventNode = nullptr;
		ProcessDependencies(ThreadState);
#if WITH_IOSTORE_IN_EDITOR
		ThreadState.CurrentEventNode = PrevNode;
#endif
	}
#if WITH_IOSTORE_IN_EDITOR
	else
	{
		check(PrevNode == nullptr);
		SetState(ENodeState::Timeout);
	}
#endif
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
			ThreadState.NodesToFire.Pop(false)->Fire(&ThreadState);
		}
		ThreadState.bShouldFireNodes = true;
	}
}

FAsyncLoadEventQueue2::FAsyncLoadEventQueue2()
{
}

FAsyncLoadEventQueue2::~FAsyncLoadEventQueue2()
{
}

void FAsyncLoadEventQueue2::Push(FEventLoadNode2* Node)
{
	LLM_SCOPE_BYNAME(TEXT("AsyncLoadEventQueue2"));
	Queue.enqueue(Node);

	if (Zenaphore)
	{
		Zenaphore->NotifyOne();
	}
}

bool FAsyncLoadEventQueue2::PopAndExecute(FAsyncLoadingThreadState2& ThreadState)
{
	if (ThreadState.CurrentEventNode
#if WITH_IOSTORE_IN_EDITOR
		&& !ThreadState.CurrentEventNode->IsExecuting()
#endif
		)
	{
		check(!ThreadState.CurrentEventNode->IsDone());
		ThreadState.CurrentEventNode->Execute(ThreadState);
		return true;
	}

	if (FEventLoadNode2* Node = Queue.dequeue())
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
#if WITH_IOSTORE_IN_EDITOR
	PreviousAsyncPackageLoader = ThreadContext.AsyncPackageLoader;
	ThreadContext.AsyncPackageLoader = &InPackage->AsyncLoadingThread;
#endif
	Package->BeginAsyncLoad();
}

FScopedAsyncPackageEvent2::~FScopedAsyncPackageEvent2()
{
	Package->EndAsyncLoad();

	// Restore the package from the outer scope
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	ThreadContext.AsyncPackage = PreviousPackage;
#if WITH_IOSTORE_IN_EDITOR
	ThreadContext.AsyncPackageLoader = PreviousAsyncPackageLoader;
#endif
}

void FAsyncLoadingThreadWorker::StartThread()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	UE::Trace::ThreadGroupBegin(TEXT("AsyncLoading"));
	Thread = FRunnableThread::Create(this, TEXT("FAsyncLoadingThreadWorker"), 0, TPri_Normal);
	ThreadId = Thread->GetThreadID();
	UE::Trace::ThreadGroupEnd();
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

void FAsyncPackage2::SetupSerializedArcs()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SetupExternalArcs);

	FSimpleArchive ArcsArchive(ArcsData.GetData(), ArcsData.Num());
	int32 InternalArcsCount;
	ArcsArchive << InternalArcsCount;
	for (int32 InternalArcIndex = 0; InternalArcIndex < InternalArcsCount; ++InternalArcIndex)
	{
		int32 FromExportBundleIndex;
		ArcsArchive << FromExportBundleIndex;
		int32 ToExportBundleIndex;
		ArcsArchive << ToExportBundleIndex;
		uint32 FromNodeIndexBase = FromExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases;
		uint32 ToNodeIndexBase = ToExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases;
		for (int32 Phase = 0; Phase < EEventLoadNode2::ExportBundle_NumPhases; ++Phase)
		{
			uint32 ToNodeIndex = ToNodeIndexBase + Phase;
			uint32 FromNodeIndex = FromNodeIndexBase + Phase;
			Data.ExportBundleNodes[ToNodeIndex].DependsOn(&Data.ExportBundleNodes[FromNodeIndex]);
		}
	}
	for (const FAsyncPackage2* ImportedPackage : Data.ImportedAsyncPackages)
	{
		int32 ExternalArcCount;
		ArcsArchive << ExternalArcCount;

		int32 PreviousFromExportBundleIndex = -1;
		int32 PreviousToExportBundleIndex = -1;

		for (int32 ExternalArcIndex = 0; ExternalArcIndex < ExternalArcCount; ++ExternalArcIndex)
		{
			int32 FromImportIndex;
			uint8 FromCommandType;
			int32 ToExportBundleIndex;
			ArcsArchive << FromImportIndex;
			ArcsArchive << FromCommandType;
			ArcsArchive << ToExportBundleIndex;
			if (ImportedPackage)
			{
				check(FromImportIndex < ImportStore.ImportMap.Num());
				check(FromCommandType < FExportBundleEntry::ExportCommandType_Count);
				check(ToExportBundleIndex < Data.ExportInfo.ExportBundleCount);
				
				FPackageObjectIndex GlobalImportIndex = ImportStore.ImportMap[FromImportIndex];
				FPackageImportReference PackageImportRef = GlobalImportIndex.ToPackageImportRef();
				const uint64 ImportedPublicExportHash = ImportStore.ImportedPublicExportHashes[PackageImportRef.GetImportedPublicExportHashIndex()];
				for (const FExportToBundleMapping& ExportToBundleMapping : ImportedPackage->ExportToBundleMappings)
				{
					if (ExportToBundleMapping.ExportHash == ImportedPublicExportHash)
					{
						int32 FromExportBundleIndex = ExportToBundleMapping.BundleIndex[FromCommandType];
						if (PreviousFromExportBundleIndex != FromExportBundleIndex || PreviousToExportBundleIndex != ToExportBundleIndex)
						{
							PreviousFromExportBundleIndex = FromExportBundleIndex;
							PreviousToExportBundleIndex = ToExportBundleIndex;
							uint32 FromNodeIndexBase = FromExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases;
							uint32 ToNodeIndexBase = ToExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases;
							for (int32 Phase = 0; Phase < EEventLoadNode2::ExportBundle_NumPhases; ++Phase)
							{
								uint32 ToNodeIndex = ToNodeIndexBase + Phase;
								uint32 FromNodeIndex = FromNodeIndexBase + Phase;
								Data.ExportBundleNodes[ToNodeIndex].DependsOn(&ImportedPackage->Data.ExportBundleNodes[FromNodeIndex]);
							}
						}
						break;
					}
				}
			}
		}
	}
}

void FAsyncPackage2::SetupScriptDependencies()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SetupScriptDependencies);

	// UObjectLoadAllCompiledInDefaultProperties is creating CDOs from a flat list.
	// During initial laod, if a CDO called LoadObject for this package it may depend on other CDOs later in the list.
	// Then collect them here, and wait for them to be created before allowing this package to proceed.
	TArray<UClass*, TInlineAllocator<8>> UnresolvedCDOs;
	if (ImportStore.GetUnresolvedCDOs(UnresolvedCDOs))
	{
		AsyncLoadingThread.AddPendingCDOs(this, UnresolvedCDOs);
	}
}

static UObject* GFindExistingScriptImport(FPackageObjectIndex GlobalImportIndex,
	TMap<FPackageObjectIndex, UObject*>& ScriptObjects,
	const TMap<FPackageObjectIndex, FScriptObjectEntry*>& ScriptObjectEntriesMap)
{
	UObject** Object = &ScriptObjects.FindOrAdd(GlobalImportIndex);
	if (!*Object)
	{
		const FScriptObjectEntry* Entry = ScriptObjectEntriesMap.FindRef(GlobalImportIndex);
		check(Entry);
		if (Entry->OuterIndex.IsNull())
		{
			*Object = StaticFindObjectFast(UPackage::StaticClass(), nullptr, MinimalNameToName(Entry->ObjectName), true);
		}
		else
		{
			UObject* Outer = GFindExistingScriptImport(Entry->OuterIndex, ScriptObjects, ScriptObjectEntriesMap);
			Object = &ScriptObjects.FindChecked(GlobalImportIndex);
			if (Outer)
			{
				*Object = StaticFindObjectFast(UObject::StaticClass(), Outer, MinimalNameToName(Entry->ObjectName), false, true);
			}
		}
	}
	return *Object;
}

UObject* FGlobalImportStore::FindScriptImportObjectFromIndex(FPackageObjectIndex GlobalImportIndex)
{
	check(ScriptObjectEntries.Num() > 0);
	return GFindExistingScriptImport(GlobalImportIndex, ScriptObjects, ScriptObjectEntriesMap);
}

void FGlobalImportStore::FindAllScriptObjects()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindAllScriptObjects);
	TStringBuilder<FName::StringBufferSize> Name;
	TArray<UPackage*> ScriptPackages;
	TArray<UObject*> Objects;
	FindAllRuntimeScriptPackages(ScriptPackages);

	for (UPackage* Package : ScriptPackages)
	{
		Objects.Reset();
		GetObjectsWithOuter(Package, Objects, /*bIncludeNestedObjects*/true);
		for (UObject* Object : Objects)
		{
			if (Object->HasAnyFlags(RF_Public))
			{
				Name.Reset();
				Object->GetPathName(nullptr, Name);
				FPackageObjectIndex GlobalImportIndex = FPackageObjectIndex::FromScriptPath(Name);
				ScriptObjects.Add(GlobalImportIndex, Object);
			}
		}
	}

	ScriptObjectEntriesMap.Empty();
	ScriptObjectEntries.Empty();
	ScriptObjects.Shrink();

	bHasInitializedScriptObjects = true;

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - InitialLoad Finalized: %d script object entries in %.2f KB"),
		ScriptObjects.Num(), (float)ScriptObjects.GetAllocatedSize() / 1024.f);
}

void FAsyncPackage2::ImportPackagesRecursive(FIoBatch& IoBatch, IPackageStore& PackageStore)
{
	if (AsyncPackageLoadingState >= EAsyncPackageLoadingState2::ImportPackages)
	{
		return;
	}
	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::NewPackage);

	const int32 ImportedPackageCount = Data.ImportedPackageIds.Num();
	if (!ImportedPackageCount)
	{
		AsyncPackageLoadingState = EAsyncPackageLoadingState2::ImportPackagesDone;
		return;
	}
	else
	{
		AsyncPackageLoadingState = EAsyncPackageLoadingState2::ImportPackages;
	}

	int32 ImportedPackageIndex = 0;

	Data.ImportedAsyncPackages = MakeArrayView(Data.ImportedAsyncPackages.GetData(), Data.ImportedPackageIds.Num());
	for (const FPackageId& ImportedPackageId : Data.ImportedPackageIds)
	{
		FName ImportedPackageUPackageName = FName();
		FPackageId ImportedPackageIdToLoad = ImportedPackageId;
		{
			FName SourcePackageName;
			FPackageId RedirectedToPackageId;
			if (PackageStore.GetPackageRedirectInfo(ImportedPackageId, SourcePackageName, RedirectedToPackageId))
			{
				ImportedPackageUPackageName = SourcePackageName;
				ImportedPackageIdToLoad = RedirectedToPackageId;
			}
		}
		
		FLoadedPackageRef& PackageRef = ImportStore.LoadedPackageStore.GetPackageRef(ImportedPackageId);
		FPackageStoreEntry ImportedPackageEntry;
		EPackageStoreEntryStatus ImportedPackageStatus = PackageStore.GetPackageStoreEntry(ImportedPackageIdToLoad, ImportedPackageEntry);

		if (ImportedPackageStatus == EPackageStoreEntryStatus::Missing)
		{
			UE_ASYNC_PACKAGE_LOG(Warning, Desc, TEXT("ImportPackages: SkipPackage"),
				TEXT("Skipping non mounted imported package with id '0x%llX'"), ImportedPackageId.Value());
			PackageRef.SetIsMissingPackage();
			Data.ImportedAsyncPackages[ImportedPackageIndex++] = nullptr;
			continue;
		}

		FAsyncPackage2* ImportedPackage = nullptr;
		bool bInserted = false;
		FAsyncPackageDesc2 PackageDesc = FAsyncPackageDesc2::FromPackageImport(INDEX_NONE, Desc.Priority, ImportedPackageId, ImportedPackageIdToLoad, ImportedPackageUPackageName);
		if (PackageRef.AreAllPublicExportsLoaded())
		{
			ImportedPackage = AsyncLoadingThread.FindAsyncPackage(ImportedPackageId);
			if (!ImportedPackage)
			{
				Data.ImportedAsyncPackages[ImportedPackageIndex++] = nullptr;
				continue;
			}
			bInserted = false;
		}
		else
		{
			ImportedPackage = AsyncLoadingThread.FindOrInsertPackage(PackageDesc, bInserted);
		}

		checkf(ImportedPackage, TEXT("Failed to find or insert imported package with id '0x%llX'"), ImportedPackageId.Value());
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
		Data.ImportedAsyncPackages[ImportedPackageIndex++] = ImportedPackage;
		GetPackageNode(Package_SetupDependencies).DependsOn(&ImportedPackage->GetPackageNode(Package_ProcessSummary));

		if (bInserted)
		{
			if (ImportedPackageStatus == EPackageStoreEntryStatus::Ok)
			{
				AsyncLoadingThread.InitializeAsyncPackageFromPackageStore(ImportedPackage, ImportedPackageEntry);
				ImportedPackage->ImportPackagesRecursive(IoBatch, PackageStore);
				ImportedPackage->StartLoading(IoBatch);
			}
			else
			{
				check(ImportedPackageStatus == EPackageStoreEntryStatus::Pending);
				AsyncLoadingThread.PendingPackages.Add(ImportedPackage);
			}
		}
	}
	UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("ImportPackages: ImportsDone"),
		TEXT("All imported packages are now being loaded."));

	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::ImportPackages);
	AsyncPackageLoadingState = EAsyncPackageLoadingState2::ImportPackagesDone;
}

void FAsyncPackage2::StartLoading(FIoBatch& IoBatch)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StartLoading);
	TRACE_LOADTIME_BEGIN_LOAD_ASYNC_PACKAGE(this);
	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::ImportPackagesDone);

	LoadStartTime = FPlatformTime::Seconds();

	AsyncPackageLoadingState = EAsyncPackageLoadingState2::WaitingForIo;

	int32 LocalPendingIoRequestsCounter = AsyncLoadingThread.PendingIoRequestsCounter.IncrementExchange() + 1;
	TRACE_COUNTER_SET(AsyncLoadingPendingIoRequests, LocalPendingIoRequestsCounter);

	FIoReadOptions ReadOptions;
	IoRequest = IoBatch.ReadWithCallback(CreateIoChunkId(Desc.PackageIdToLoad.Value(), 0, EIoChunkType::ExportBundleData),
		ReadOptions,
		Desc.Priority,
		[this](TIoStatusOr<FIoBuffer> Result)
		{
			if (Result.IsOk())
			{
				TRACE_COUNTER_ADD(AsyncLoadingTotalLoaded, Result.ValueOrDie().DataSize());
				CSV_CUSTOM_STAT_DEFINED(FrameCompletedExportBundleLoadsKB, float((double)Result.ValueOrDie().DataSize() / 1024.0), ECsvCustomStatOp::Accumulate);
			}
			else
			{
				UE_ASYNC_PACKAGE_LOG(Warning, Desc, TEXT("StartBundleIoRequests: FailedRead"),
					TEXT("Failed reading chunk for package: %s"), *Result.Status().ToString());
				bLoadHasFailed = true;
			}
			int32 LocalPendingIoRequestsCounter = AsyncLoadingThread.PendingIoRequestsCounter.DecrementExchange() - 1;
			TRACE_COUNTER_SET(AsyncLoadingPendingIoRequests, LocalPendingIoRequestsCounter);
			GetPackageNode(EEventLoadNode2::Package_ProcessSummary).ReleaseBarrier();
		});

	if (!Data.ShaderMapHashes.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(StartShaderMapRequests);
		auto ReadShaderMapFunc = [this, &IoBatch](const FIoChunkId& ChunkId, FGraphEventRef GraphEvent)
		{
			GetPackageNode(Package_ExportsSerialized).AddBarrier();
			int32 LocalPendingIoRequestsCounter = AsyncLoadingThread.PendingIoRequestsCounter.IncrementExchange() + 1;
			TRACE_COUNTER_SET(AsyncLoadingPendingIoRequests, LocalPendingIoRequestsCounter);
			return IoBatch.ReadWithCallback(ChunkId, FIoReadOptions(), Desc.Priority,
				[this, GraphEvent](TIoStatusOr<FIoBuffer> Result)
				{
					GraphEvent->DispatchSubsequents();
					int32 LocalPendingIoRequestsCounter = AsyncLoadingThread.PendingIoRequestsCounter.DecrementExchange() - 1;
					TRACE_COUNTER_SET(AsyncLoadingPendingIoRequests, LocalPendingIoRequestsCounter);
					GetPackageNode(Package_ExportsSerialized).ReleaseBarrier();
				});
		};
		FCoreDelegates::PreloadPackageShaderMaps.ExecuteIfBound(Data.ShaderMapHashes, ReadShaderMapFunc);
	}
}

EAsyncPackageState::Type FAsyncPackage2::Event_ProcessPackageSummary(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_ProcessPackageSummary);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::WaitingForIo);
	Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::ProcessPackageSummary;

	FScopedAsyncPackageEvent2 Scope(Package);

	if (Package->bLoadHasFailed)
	{
		if (Package->Desc.bCanBeImported)
		{
			FLoadedPackageRef* PackageRef = Package->ImportStore.LoadedPackageStore.FindPackageRef(Package->Desc.UPackageId);
			check(PackageRef);
			PackageRef->SetHasFailed();
		}
	}
	else
	{
		check(Package->ExportBundleEntryIndex == 0);

		const uint8* PackageHeaderData = Package->IoRequest.GetResultOrDie().Data();
		const FZenPackageSummary* PackageSummary = reinterpret_cast<const FZenPackageSummary*>(PackageHeaderData);
		
		TArrayView<const uint8> PackageHeaderDataView(PackageHeaderData + sizeof(FZenPackageSummary), PackageSummary->HeaderSize - sizeof(FZenPackageSummary));
		FMemoryReaderView PackageHeaderDataReader(PackageHeaderDataView);

		FZenPackageVersioningInfo VersioningInfo;
		if (PackageSummary->bHasVersioningInfo)
		{
			PackageHeaderDataReader << VersioningInfo;
		}

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageNameMap);
			Package->NameMap.Load(PackageHeaderDataReader, FMappedName::EType::Package);
		}

		{
			FName PackageName = Package->NameMap.GetName(PackageSummary->Name);
			check(Package->Desc.PackageNameToLoad.IsNone());
			Package->Desc.PackageNameToLoad = PackageName;
			check(Package->Desc.PackageIdToLoad == FPackageId::FromName(Package->Desc.PackageNameToLoad));
			// Imported packages won't have a UPackage name set unless they were redirected, in which case they will have the source package name
			if (Package->Desc.UPackageName.IsNone())
			{
				Package->Desc.UPackageName = PackageName;
			}
			check(Package->Desc.UPackageId == FPackageId::FromName(Package->Desc.UPackageName));
		}

		Package->CookedHeaderSize = PackageSummary->CookedHeaderSize;
		Package->ImportStore.ImportedPublicExportHashes = TArrayView<const uint64>(
			reinterpret_cast<const uint64*>(PackageHeaderData + PackageSummary->ImportedPublicExportHashesOffset),
			(PackageSummary->ImportMapOffset - PackageSummary->ImportedPublicExportHashesOffset) / sizeof(uint64));
		Package->ImportStore.ImportMap = TArrayView<const FPackageObjectIndex>(
			reinterpret_cast<const FPackageObjectIndex*>(PackageHeaderData + PackageSummary->ImportMapOffset),
			(PackageSummary->ExportMapOffset - PackageSummary->ImportMapOffset) / sizeof(FPackageObjectIndex));
		Package->ExportMap = TArrayView<const FExportMapEntry>(
			reinterpret_cast<const FExportMapEntry*>(PackageHeaderData + PackageSummary->ExportMapOffset),
			(PackageSummary->ExportBundleEntriesOffset - PackageSummary->ExportMapOffset) / sizeof(FExportMapEntry));
		check(Package->ExportMap.Num() == Package->Data.ExportInfo.ExportCount);

		uint64 ExportBundleHeadersOffset = PackageSummary->GraphDataOffset;
		uint64 ArcsDataOffset = ExportBundleHeadersOffset + Package->Data.ExportBundleHeadersSize;
		uint64 ArcsDataSize = PackageSummary->HeaderSize - ArcsDataOffset;
		Package->ArcsData = TArrayView<const uint8>(PackageHeaderData + ArcsDataOffset, ArcsDataSize);
		FMemory::Memcpy(Package->Data.ExportBundlesMetaMemory, PackageHeaderData + ExportBundleHeadersOffset, Package->Data.ExportBundleHeadersSize);
		FMemory::Memcpy(Package->Data.ExportBundlesMetaMemory + Package->Data.ExportBundleHeadersSize, PackageHeaderData + PackageSummary->ExportBundleEntriesOffset, Package->Data.ExportBundleEntriesSize);

		Package->CreateUPackage(PackageSummary, PackageSummary->bHasVersioningInfo ? &VersioningInfo : nullptr);

		Package->AllExportDataPtr = PackageHeaderData + PackageSummary->HeaderSize;

		Package->ExportToBundleMappings.SetNum(Package->Data.ExportInfo.ExportCount);
		for (int32 ExportBundleIndex = 0, ExportBundleCount = Package->Data.ExportInfo.ExportBundleCount; ExportBundleIndex < ExportBundleCount; ++ExportBundleIndex)
		{
			const FExportBundleHeader* ExportBundle = Package->Data.ExportBundleHeaders + ExportBundleIndex;
			const FExportBundleEntry* BundleEntries = Package->Data.ExportBundleEntries + ExportBundle->FirstEntryIndex;
			const FExportBundleEntry* BundleEntry = BundleEntries + Package->ExportBundleEntryIndex;
			const FExportBundleEntry* BundleEntryEnd = BundleEntries + ExportBundle->EntryCount;
			while (BundleEntry < BundleEntryEnd)
			{
				const FExportMapEntry& ExportMapEntry = Package->ExportMap[BundleEntry->LocalExportIndex];
				FExportToBundleMapping& ExportToBundleMapping = Package->ExportToBundleMappings[BundleEntry->LocalExportIndex];
				ExportToBundleMapping.ExportHash = ExportMapEntry.PublicExportHash;
				ExportToBundleMapping.BundleIndex[BundleEntry->CommandType] = ExportBundleIndex;
				++BundleEntry;
			}
		}

		TRACE_LOADTIME_PACKAGE_SUMMARY(Package, Package->Desc.PackageNameToLoad, PackageSummary->HeaderSize, Package->ImportStore.ImportMap.Num(), Package->Data.ExportInfo.ExportCount);
	}

	Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::SetupDependencies;
	Package->GetPackageNode(Package_SetupDependencies).ReleaseBarrier();
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::Event_SetupDependencies(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_SetupDependencies);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::SetupDependencies);
	
	if (!Package->bLoadHasFailed)
	{
		if (GIsInitialLoad)
		{
			Package->SetupScriptDependencies();
		}
		Package->SetupSerializedArcs();
	}
	Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::ProcessExportBundles;
	for (int32 ExportBundleIndex = 0, ExportBundleCount = Package->Data.ExportInfo.ExportBundleCount; ExportBundleIndex < ExportBundleCount; ++ExportBundleIndex)
	{
		Package->GetExportBundleNode(ExportBundle_Process, ExportBundleIndex).ReleaseBarrier();
	}
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::Event_ProcessExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 InExportBundleIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_ProcessExportBundle);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessExportBundles);

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

	check(InExportBundleIndex < Package->Data.ExportInfo.ExportBundleCount);
	
	if (!Package->bLoadHasFailed)
	{
		const FExportBundleHeader* ExportBundle = Package->Data.ExportBundleHeaders + InExportBundleIndex;
		const FIoBuffer& IoBuffer = Package->IoRequest.GetResultOrDie();
		const uint64 AllExportDataSize = IoBuffer.DataSize() - (Package->AllExportDataPtr - IoBuffer.Data());
		if (Package->ExportBundleEntryIndex == 0)
		{
			Package->CurrentExportDataPtr = Package->AllExportDataPtr + ExportBundle->SerialOffset;
		}
		FExportArchive Ar(Package->AllExportDataPtr, Package->CurrentExportDataPtr, AllExportDataSize);
		{
			Ar.SetUEVer(Package->LinkerRoot->LinkerPackageVersion);
			Ar.SetLicenseeUEVer(Package->LinkerRoot->LinkerLicenseeVersion);
			// Ar.SetEngineVer(Summary.SavedByEngineVersion); // very old versioning scheme
			if (!Package->LinkerRoot->LinkerCustomVersion.GetAllVersions().IsEmpty())
			{
				Ar.SetCustomVersions(Package->LinkerRoot->LinkerCustomVersion);
			}
			Ar.SetUseUnversionedPropertySerialization((Package->LinkerRoot->GetPackageFlags() & PKG_UnversionedProperties) != 0);
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
			Ar.NameMap = &Package->NameMap;
			Ar.ImportStore = &Package->ImportStore;
			Ar.Exports = Package->Data.Exports;
			Ar.ExportMap = Package->ExportMap.GetData();
			Ar.ExternalReadDependencies = &Package->ExternalReadDependencies;
		}
		const FExportBundleEntry* BundleEntries = Package->Data.ExportBundleEntries + ExportBundle->FirstEntryIndex;
		const FExportBundleEntry* BundleEntry = BundleEntries + Package->ExportBundleEntryIndex;
		const FExportBundleEntry* BundleEntryEnd = BundleEntries + ExportBundle->EntryCount;
		check(BundleEntry <= BundleEntryEnd);
		while (BundleEntry < BundleEntryEnd)
		{
			if (ThreadState.IsTimeLimitExceeded(TEXT("Event_ProcessExportBundle")))
			{
				return EAsyncPackageState::TimeOut;
			}
			const FExportMapEntry& ExportMapEntry = Package->ExportMap[BundleEntry->LocalExportIndex];
			FExportObject& Export = Package->Data.Exports[BundleEntry->LocalExportIndex];
			Export.bFiltered = FilterExport(ExportMapEntry.FilterFlags);

			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Create)
			{
				Package->EventDrivenCreateExport(BundleEntry->LocalExportIndex);
			}
			else
			{
				check(BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize);

				const uint64 CookedSerialSize = ExportMapEntry.CookedSerialSize;
				UObject* Object = Export.Object;

				check(Package->CurrentExportDataPtr + CookedSerialSize <= IoBuffer.Data() + IoBuffer.DataSize());
				check(Object || Export.bFiltered || Export.bExportLoadFailed);

				Ar.ExportBufferBegin(Object, ExportMapEntry.CookedSerialOffset, ExportMapEntry.CookedSerialSize);

				const int64 Pos = Ar.Tell();
				UE_ASYNC_PACKAGE_CLOG(
					CookedSerialSize > uint64(Ar.TotalSize() - Pos), Fatal, Package->Desc, TEXT("ObjectSerializationError"),
					TEXT("%s: Serial size mismatch: Expected read size %d, Remaining archive size: %d"),
					Object ? *Object->GetFullName() : TEXT("null"), CookedSerialSize, uint64(Ar.TotalSize() - Pos));

				const bool bSerialized = Package->EventDrivenSerializeExport(BundleEntry->LocalExportIndex, Ar);
				if (!bSerialized)
				{
					Ar.Skip(CookedSerialSize);
				}
				UE_ASYNC_PACKAGE_CLOG(
					CookedSerialSize != uint64(Ar.Tell() - Pos), Fatal, Package->Desc, TEXT("ObjectSerializationError"),
					TEXT("%s: Serial size mismatch: Expected read size %d, Actual read size %d"),
					Object ? *Object->GetFullName() : TEXT("null"), CookedSerialSize, uint64(Ar.Tell() - Pos));

				Ar.ExportBufferEnd();

				check((Object && !Object->HasAnyFlags(RF_NeedLoad)) || Export.bFiltered || Export.bExportLoadFailed);

				Package->CurrentExportDataPtr += CookedSerialSize;
			}
			++BundleEntry;
			++Package->ExportBundleEntryIndex;
		}
	}
	
	Package->ExportBundleEntryIndex = 0;

	if (++Package->ProcessedExportBundlesCount == Package->Data.ExportInfo.ExportBundleCount)
	{
		Package->ProcessedExportBundlesCount = 0;
		Package->ImportStore.ImportMap = TArrayView<const FPackageObjectIndex>();
		Package->IoRequest.Release();

		if (Package->ExternalReadDependencies.Num() == 0)
		{
			check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessExportBundles);
			Package->GetPackageNode(Package_ExportsSerialized).ReleaseBarrier(&ThreadState);
		}
		else
		{
			check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessExportBundles);
			Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::WaitingForExternalReads;
			Package->AsyncLoadingThread.ExternalReadQueue.Enqueue(Package);
		}
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
		Result = Data.Exports[Index.ToExport()].Object;
	}
	else if (Index.IsImport())
	{
		Result = ImportStore.FindOrGetImportObject(Index);
		UE_CLOG(!Result, LogStreaming, Warning, TEXT("Missing %s import 0x%llX for package %s"),
			Index.IsScriptImport() ? TEXT("script") : TEXT("package"),
			Index.Value(),
			*Desc.PackageNameToLoad.ToString());
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
	FExportObject& ExportObject = Data.Exports[LocalExportIndex];
	UObject*& Object = ExportObject.Object;
	check(!Object);

	TRACE_LOADTIME_CREATE_EXPORT_SCOPE(this, &Object);

	FName ObjectName;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ObjectNameFixup);
		ObjectName = NameMap.GetName(Export.ObjectName);
	}

	if (ExportObject.bFiltered | ExportObject.bExportLoadFailed)
	{
		if (ExportObject.bExportLoadFailed)
		{
			UE_ASYNC_PACKAGE_LOG(Warning, Desc, TEXT("CreateExport"), TEXT("Skipped failed export %s"), *ObjectName.ToString());
		}
		else
		{
			UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbose, Desc, TEXT("CreateExport"), TEXT("Skipped filtered export %s"), *ObjectName.ToString());
		}
		return;
	}

	LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(GetLinkerRoot(), ELLMTagSet::Assets);
	// LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, false), ELLMTagSet::AssetClasses);

	bool bIsCompleteyLoaded = false;
	UClass* LoadClass = Export.ClassIndex.IsNull() ? UClass::StaticClass() : CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, true);
	UObject* ThisParent = Export.OuterIndex.IsNull() ? LinkerRoot : EventDrivenIndexToObject(Export.OuterIndex, false);

	if (!LoadClass)
	{
		UE_ASYNC_PACKAGE_LOG(Error, Desc, TEXT("CreateExport"), TEXT("Could not find class object for %s"), *ObjectName.ToString());
		ExportObject.bExportLoadFailed = true;
		return;
	}
	if (!ThisParent)
	{
		UE_ASYNC_PACKAGE_LOG(Error, Desc, TEXT("CreateExport"), TEXT("Could not find outer object for %s"), *ObjectName.ToString());
		ExportObject.bExportLoadFailed = true;
		return;
	}
	check(!dynamic_cast<UObjectRedirector*>(ThisParent));
	if (!Export.SuperIndex.IsNull())
	{
		ExportObject.SuperObject = EventDrivenIndexToObject(Export.SuperIndex, false);
		if (!ExportObject.SuperObject)
		{
			UE_ASYNC_PACKAGE_LOG(Error, Desc, TEXT("CreateExport"), TEXT("Could not find SuperStruct object for %s"), *ObjectName.ToString());
			ExportObject.bExportLoadFailed = true;
			return;
		}
	}
	// Find the Archetype object for the one we are loading.
	check(!Export.TemplateIndex.IsNull());
	ExportObject.TemplateObject = EventDrivenIndexToObject(Export.TemplateIndex, true);
	if (!ExportObject.TemplateObject)
	{
		UE_ASYNC_PACKAGE_LOG(Error, Desc, TEXT("CreateExport"), TEXT("Could not find template object for %s"), *ObjectName.ToString());
		ExportObject.bExportLoadFailed = true;
		return;
	}

	// Try to find existing object first as we cannot in-place replace objects, could have been created by other export in this package
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FindExport);
		Object = StaticFindObjectFastInternal(NULL, ThisParent, ObjectName, true);
	}

	const bool bIsNewObject = !Object;

	// Object is found in memory.
	if (Object)
	{
		// If this object was allocated but never loaded (components created by a constructor, CDOs, etc) make sure it gets loaded
		// Do this for all subobjects created in the native constructor.
		const EObjectFlags ObjectFlags = Object->GetFlags();
		bIsCompleteyLoaded = !!(ObjectFlags & RF_LoadCompleted);
		if (!bIsCompleteyLoaded)
		{
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
		}
	}
	else
	{
		// we also need to ensure that the template has set up any instances
		ExportObject.TemplateObject->ConditionalPostLoadSubobjects();

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
			check(!SuperCDO || ExportObject.TemplateObject == SuperCDO); // the template for a CDO is the CDO of the super
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
				check(ExportObject.TemplateObject->IsA(LoadClass));
			}
		}
#endif
		checkf(!LoadClass->HasAnyFlags(RF_NeedLoad),
			TEXT("LoadClass %s had RF_NeedLoad while creating %s"), *LoadClass->GetFullName(), *ObjectName.ToString());
		checkf(!(LoadClass->GetDefaultObject() && LoadClass->GetDefaultObject()->HasAnyFlags(RF_NeedLoad)), 
			TEXT("Class CDO %s had RF_NeedLoad while creating %s"), *LoadClass->GetDefaultObject()->GetFullName(), *ObjectName.ToString());
		checkf(!ExportObject.TemplateObject->HasAnyFlags(RF_NeedLoad),
			TEXT("Template %s had RF_NeedLoad while creating %s"), *ExportObject.TemplateObject->GetFullName(), *ObjectName.ToString());

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ConstructObject);
			FStaticConstructObjectParameters Params(LoadClass);
			Params.Outer = ThisParent;
			Params.Name = ObjectName;
			Params.SetFlags = ObjectLoadFlags;
			Params.Template = ExportObject.TemplateObject;
			Params.bAssumeTemplateIsArchetype = true;
			Object = StaticConstructObject_Internal(Params);
		}

		if (GIsInitialLoad || GUObjectArray.IsOpenForDisregardForGC())
		{
			Object->AddToRoot();
		}

		check(Object->GetClass() == LoadClass);
		check(Object->GetFName() == ObjectName);
	}

	check(Object);
	PinObjectForGC(Object, bIsNewObject);

	if (Desc.bCanBeImported && Export.PublicExportHash)
	{
		check(Object->HasAnyFlags(RF_Public));
		ImportStore.StoreGlobalObject(Desc.UPackageId, Export.PublicExportHash, Object);

		UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("CreateExport"),
			TEXT("Created public export %s. Tracked as 0x%llX:0x%llX"), *Object->GetPathName(), Desc.UPackageId.Value(), Export.PublicExportHash);
	}
	else
	{
		UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("CreateExport"), TEXT("Created %s export %s. Not tracked."),
			Object->HasAnyFlags(RF_Public) ? TEXT("public") : TEXT("private"), *Object->GetPathName());
	}
}

bool FAsyncPackage2::EventDrivenSerializeExport(int32 LocalExportIndex, FExportArchive& Ar)
{
	LLM_SCOPE(ELLMTag::UObject);
	TRACE_CPUPROFILER_EVENT_SCOPE(SerializeExport);

	const FExportMapEntry& Export = ExportMap[LocalExportIndex];
	FExportObject& ExportObject = Data.Exports[LocalExportIndex];
	UObject* Object = ExportObject.Object;
	check(Object || (ExportObject.bFiltered | ExportObject.bExportLoadFailed));

	TRACE_LOADTIME_SERIALIZE_EXPORT_SCOPE(Object, Export.CookedSerialSize);

	if ((ExportObject.bFiltered | ExportObject.bExportLoadFailed) || !(Object && Object->HasAnyFlags(RF_NeedLoad)))
	{
		if (ExportObject.bExportLoadFailed)
		{
			UE_ASYNC_PACKAGE_LOG(Warning, Desc, TEXT("SerializeExport"),
				TEXT("Skipped failed export %s"), *NameMap.GetName(Export.ObjectName).ToString());
		}
		else if (ExportObject.bFiltered)
		{
			UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbose, Desc, TEXT("SerializeExport"),
				TEXT("Skipped filtered export %s"), *NameMap.GetName(Export.ObjectName).ToString());
		}
		else
		{
			UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("SerializeExport"),
				TEXT("Skipped already serialized export %s"), *NameMap.GetName(Export.ObjectName).ToString());
		}
		return false;
	}

	// If this is a struct, make sure that its parent struct is completely loaded
	if (UStruct* Struct = dynamic_cast<UStruct*>(Object))
	{
		if (UStruct* SuperStruct = dynamic_cast<UStruct*>(ExportObject.SuperObject))
		{
			Struct->SetSuperStruct(SuperStruct);
			if (UClass* ClassObject = dynamic_cast<UClass*>(Object))
			{
				ClassObject->Bind();
			}
		}
	}

	LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(GetLinkerRoot(), ELLMTagSet::Assets);
	// LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, false), ELLMTagSet::AssetClasses);

	// cache archetype
	// prevents GetArchetype from hitting the expensive GetArchetypeFromRequiredInfoImpl
	check(ExportObject.TemplateObject);
	CacheArchetypeForObject(Object, ExportObject.TemplateObject);

	Object->ClearFlags(RF_NeedLoad);

	FUObjectSerializeContext* LoadContext = GetSerializeContext();
	UObject* PrevSerializedObject = LoadContext->SerializedObject;
	LoadContext->SerializedObject = Object;

	Ar.TemplateForGetArchetypeFromLoader = ExportObject.TemplateObject;

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

	UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("SerializeExport"), TEXT("Serialized export %s"), *Object->GetPathName());

	// push stats so that we don't overflow number of tags per thread during blocking loading
	LLM_PUSH_STATS_FOR_ASSET_TAGS();

	return true;
}

EAsyncPackageState::Type FAsyncPackage2::Event_ExportsDone(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_ExportsDone);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessExportBundles || Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::WaitingForExternalReads);
	Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::ExportsDone;

	if (!Package->bLoadHasFailed && Package->Desc.bCanBeImported)
	{
		FLoadedPackageRef& PackageRef =
			Package->AsyncLoadingThread.LoadedPackageStore.GetPackageRef(Package->Desc.UPackageId);
		PackageRef.SetAllPublicExportsLoaded();
	}

	if (!Package->Data.ShaderMapHashes.IsEmpty())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ReleasePreloadedShaderMaps);
		FCoreDelegates::ReleasePreloadedPackageShaderMaps.ExecuteIfBound(Package->Data.ShaderMapHashes);
	}

	Package->ConditionalBeginPostLoad();
	return EAsyncPackageState::Complete;
}

bool FAsyncPackage2::HaveAllDependenciesReachedStateDebug(FAsyncPackage2* Package, TSet<FAsyncPackage2*>& VisitedPackages, EAsyncPackageLoadingState2 WaitForPackageState)
{
	for (FAsyncPackage2* ImportedPackage : Package->Data.ImportedAsyncPackages)
	{
		if (ImportedPackage)
		{

			if (VisitedPackages.Contains(ImportedPackage))
			{
				continue;
			}
			VisitedPackages.Add(ImportedPackage);

			if (ImportedPackage->AsyncPackageLoadingState < WaitForPackageState)
			{
				return false;
			}

			if (!HaveAllDependenciesReachedStateDebug(ImportedPackage, VisitedPackages, WaitForPackageState))
			{
				return false;
			}
		}
	}
	return true;
}

bool FAsyncPackage2::HaveAllDependenciesReachedState(FAllDependenciesState FAsyncPackage2::* StateMemberPtr, EAsyncPackageLoadingState2 WaitForPackageState, uint32 CurrentTick)
{
	FAllDependenciesState& ThisState = this->*StateMemberPtr;
	if (ThisState.bAllDone)
	{
		return true;
	}
	if (AsyncPackageLoadingState < WaitForPackageState)
	{
		return false;
	}
	ThisState.UpdateTick(CurrentTick);
	UpdateDependenciesStateRecursive(StateMemberPtr, WaitForPackageState, CurrentTick, this);
	check(ThisState.bAllDone || (ThisState.WaitingForPackage && ThisState.WaitingForPackage->AsyncPackageLoadingState <= WaitForPackageState));
	return ThisState.bAllDone;
}

void FAsyncPackage2::UpdateDependenciesStateRecursive(FAllDependenciesState FAsyncPackage2::* StateMemberPtr, EAsyncPackageLoadingState2 WaitForPackageState, uint32 CurrentTick, FAsyncPackage2* Root)
{
	FAllDependenciesState& ThisState = this->*StateMemberPtr;

	check(!ThisState.bVisitedMark);
	check(!ThisState.bAllDone);
	check(!ThisState.bAnyNotDone);

	ThisState.bVisitedMark = true;

	if (FAsyncPackage2* WaitingForPackage = ThisState.WaitingForPackage)
	{
		FAllDependenciesState& WaitingForPackageState = WaitingForPackage->*StateMemberPtr;
		if (WaitingForPackage->AsyncPackageLoadingState < WaitForPackageState)
		{
			ThisState.bAnyNotDone = true;
			return;
		}
		else if (!WaitingForPackageState.bAllDone)
		{
			WaitingForPackageState.UpdateTick(CurrentTick);
			if (!WaitingForPackageState.bVisitedMark)
			{
				WaitingForPackage->UpdateDependenciesStateRecursive(StateMemberPtr, WaitForPackageState, CurrentTick, Root);
			}
			if (WaitingForPackageState.bAnyNotDone)
			{
				ThisState.bAnyNotDone = true;
				return;
			}
		}
	}

	bool bAllDone = true;
	FAsyncPackage2* WaitingForPackage = nullptr;
	for (FAsyncPackage2* ImportedPackage : Data.ImportedAsyncPackages)
	{
		if (!ImportedPackage)
		{
			continue;
		}

		FAllDependenciesState& ImportedPackageState = ImportedPackage->*StateMemberPtr;

		if (ImportedPackageState.bAllDone)
		{
			continue;
		}

		ImportedPackageState.UpdateTick(CurrentTick);

		if (ImportedPackage->AsyncPackageLoadingState < WaitForPackageState)
		{
			ImportedPackageState.bAnyNotDone = true;
		}
		else if (!ImportedPackageState.bVisitedMark)
		{
			ImportedPackage->UpdateDependenciesStateRecursive(StateMemberPtr, WaitForPackageState, CurrentTick, Root);
		}

		if (ImportedPackageState.bAnyNotDone)
		{
			ThisState.bAnyNotDone = true;
			WaitingForPackage = ImportedPackage;
			break;
		}
		else if (!ImportedPackageState.bAllDone)
		{
			bAllDone = false;
		}
	}
	if (WaitingForPackage)
	{
		check(WaitingForPackage != this);
		FAllDependenciesState::AddToWaitList(StateMemberPtr, WaitingForPackage, this);
	}
	else if (bAllDone || this == Root)
	{
		// If we're the root an not waiting for any package we're done
		ThisState.bAllDone = true;
	}
	else
	{
		// We didn't find any imported package that was not done but we could have a circular dependency back to the root which could either be done or end up waiting
		// for another package. Make us wait for the root so that we are ticked when it completes.
		FAllDependenciesState::AddToWaitList(StateMemberPtr, Root, this);
	}
}

void FAsyncPackage2::WaitForAllDependenciesToReachState(FAllDependenciesState FAsyncPackage2::* StateMemberPtr, EAsyncPackageLoadingState2 WaitForPackageState, uint32& CurrentTickVariable, TFunctionRef<void(FAsyncPackage2*)> OnStateReached)
{
	if (HaveAllDependenciesReachedState(StateMemberPtr, WaitForPackageState, CurrentTickVariable++))
	{
		FAsyncPackage2* FirstPackageReadyToProceed = this;

		while (FirstPackageReadyToProceed)
		{
			FAsyncPackage2* PackageReadyToProceed = FirstPackageReadyToProceed;
			FAllDependenciesState& PackageReadyToProceedState = PackageReadyToProceed->*StateMemberPtr;
			FirstPackageReadyToProceed = PackageReadyToProceedState.NextLink;

			if (PackageReadyToProceed->AsyncPackageLoadingState > WaitForPackageState)
			{
				continue;
			}

#if DO_CHECK
			TSet<FAsyncPackage2*> VisitedPackages;
			check(HaveAllDependenciesReachedStateDebug(this, VisitedPackages, WaitForPackageState));
#endif

			while (FAsyncPackage2* WaitingPackage = PackageReadyToProceedState.PackagesWaitingForThisHead)
			{
				FAllDependenciesState& WaitingPackageState = WaitingPackage->*StateMemberPtr;
				check(WaitingPackageState.WaitingForPackage == PackageReadyToProceed);
				if (WaitingPackage->HaveAllDependenciesReachedState(StateMemberPtr, WaitForPackageState, CurrentTickVariable++))
				{
					FAllDependenciesState::RemoveFromWaitList(StateMemberPtr, PackageReadyToProceed, WaitingPackage);
					WaitingPackageState.NextLink = FirstPackageReadyToProceed;
					FirstPackageReadyToProceed = WaitingPackage;
				}
			}
			check(!PackageReadyToProceedState.PackagesWaitingForThisTail);
			check(PackageReadyToProceed->AsyncPackageLoadingState == WaitForPackageState);
			PackageReadyToProceed->AsyncPackageLoadingState = static_cast<EAsyncPackageLoadingState2>(static_cast<uint32>(WaitForPackageState) + 1);
			OnStateReached(PackageReadyToProceed);
		}
	}
}

void FAsyncPackage2::ConditionalBeginPostLoad()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ConditionalBeginPostLoad);
	WaitForAllDependenciesToReachState(&FAsyncPackage2::AllDependenciesSerializedState, EAsyncPackageLoadingState2::ExportsDone, AsyncLoadingThread.ConditionalBeginPostLoadTick,
		[](FAsyncPackage2* Package)
		{
			for (int32 ExportBundleIndex = 0, ExportBundleCount = Package->Data.ExportInfo.ExportBundleCount; ExportBundleIndex < ExportBundleCount; ++ExportBundleIndex)
			{
				Package->GetExportBundleNode(EEventLoadNode2::ExportBundle_PostLoad, ExportBundleIndex).ReleaseBarrier();
			}
		});
}

void FAsyncPackage2::ConditionalFinishLoading()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ConditionalFinishLoading);
	WaitForAllDependenciesToReachState(&FAsyncPackage2::AllDependenciesFullyLoadedState, EAsyncPackageLoadingState2::DeferredPostLoadDone, AsyncLoadingThread.ConditionalFinishLoadingTick,
		[](FAsyncPackage2* Package)
		{
			Package->AsyncLoadingThread.LoadedPackagesToProcess.Add(Package);
		});
}

EAsyncPackageState::Type FAsyncPackage2::Event_PostLoadExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 InExportBundleIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_PostLoad);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::PostLoad);
	check(Package->ExternalReadDependencies.Num() == 0);
	
	FAsyncPackageScope2 PackageScope(Package);

	/*TSet<FAsyncPackage2*> Visited;
	TArray<FAsyncPackage2*> ProcessQueue;
	ProcessQueue.Push(Package);
	while (ProcessQueue.Num() > 0)
	{
		FAsyncPackage2* CurrentPackage = ProcessQueue.Pop();
		Visited.Add(CurrentPackage);
		if (CurrentPackage->AsyncPackageLoadingState < EAsyncPackageLoadingState2::ExportsDone)
		{
			UE_DEBUG_BREAK();
		}
		for (const FPackageId& ImportedPackageId : CurrentPackage->StoreEntry.ImportedPackages)
		{
			FAsyncPackage2* ImportedPackage = CurrentPackage->AsyncLoadingThread.GetAsyncPackage(ImportedPackageId);
			if (ImportedPackage && !Visited.Contains(ImportedPackage))
			{
				ProcessQueue.Push(ImportedPackage);
			}
		}
	}*/
	
	check(InExportBundleIndex < Package->Data.ExportInfo.ExportBundleCount);

	EAsyncPackageState::Type LoadingState = EAsyncPackageState::Complete;

	if (!Package->bLoadHasFailed)
	{
		// Begin async loading, simulates BeginLoad
		Package->BeginAsyncLoad();

		SCOPED_LOADTIMER(PostLoadObjectsTime);

		FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
		TGuardValue<bool> GuardIsRoutingPostLoad(ThreadContext.IsRoutingPostLoad, true);

		const bool bAsyncPostLoadEnabled = FAsyncLoadingThreadSettings::Get().bAsyncPostLoadEnabled;
		const bool bIsMultithreaded = Package->AsyncLoadingThread.IsMultithreaded();

		const FExportBundleHeader* ExportBundle = Package->Data.ExportBundleHeaders + InExportBundleIndex;
		const FExportBundleEntry* BundleEntries = Package->Data.ExportBundleEntries + ExportBundle->FirstEntryIndex;
		const FExportBundleEntry* BundleEntry = BundleEntries + Package->ExportBundleEntryIndex;
		const FExportBundleEntry* BundleEntryEnd = BundleEntries + ExportBundle->EntryCount;
		check(BundleEntry <= BundleEntryEnd);
		while (BundleEntry < BundleEntryEnd)
		{
			if (ThreadState.IsTimeLimitExceeded(TEXT("Event_PostLoadExportBundle")))
			{
				LoadingState = EAsyncPackageState::TimeOut;
				break;
			}
			
			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				do
				{
					FExportObject& Export = Package->Data.Exports[BundleEntry->LocalExportIndex];
					if (Export.bFiltered | Export.bExportLoadFailed)
					{
						break;
					}

					UObject* Object = Export.Object;
					check(Object);
					check(!Object->HasAnyFlags(RF_NeedLoad));
					if (!Object->HasAnyFlags(RF_NeedPostLoad))
					{
						break;
					}

					check(Object->IsReadyForAsyncPostLoad());
					if (!bIsMultithreaded || (bAsyncPostLoadEnabled && CanPostLoadOnAsyncLoadingThread(Object)))
					{
						ThreadContext.CurrentlyPostLoadedObjectByALT = Object;
						{
							TRACE_LOADTIME_POSTLOAD_EXPORT_SCOPE(Object);
							Object->ConditionalPostLoad();
						}
						ThreadContext.CurrentlyPostLoadedObjectByALT = nullptr;
					}
				} while (false);
			}
			++BundleEntry;
			++Package->ExportBundleEntryIndex;
		}

		// End async loading, simulates EndLoad
		Package->EndAsyncLoad();
	}
	
	if (LoadingState == EAsyncPackageState::TimeOut)
	{
		return LoadingState;
	}

	Package->ExportBundleEntryIndex = 0;

	if (++Package->ProcessedExportBundlesCount == Package->Data.ExportInfo.ExportBundleCount)
	{
		Package->ProcessedExportBundlesCount = 0;
		if (Package->LinkerRoot && !Package->bLoadHasFailed)
		{
			UE_ASYNC_PACKAGE_LOG(Verbose, Package->Desc, TEXT("AsyncThread: FullyLoaded"),
				TEXT("Async loading of package is done, and UPackage is marked as fully loaded."));
			// mimic old loader behavior for now, but this is more correctly also done in FinishUPackage
			// called from ProcessLoadedPackagesFromGameThread just before complection callbacks
			Package->LinkerRoot->MarkAsFullyLoaded();
		}

		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::PostLoad);
		Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::DeferredPostLoad;
		for (int32 ExportBundleIndex = 0, ExportBundleCount = Package->Data.ExportInfo.ExportBundleCount; ExportBundleIndex < ExportBundleCount; ++ExportBundleIndex)
		{
			Package->GetExportBundleNode(ExportBundle_DeferredPostLoad, ExportBundleIndex).ReleaseBarrier();
		}
	}

	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::Event_DeferredPostLoadExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 InExportBundleIndex)
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_PostLoadObjectsGameThread);
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_DeferredPostLoad);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::DeferredPostLoad);

	FAsyncPackageScope2 PackageScope(Package);

	check(InExportBundleIndex < Package->Data.ExportInfo.ExportBundleCount);
	EAsyncPackageState::Type LoadingState = EAsyncPackageState::Complete;

	if (Package->bLoadHasFailed)
	{
		FSoftObjectPath::InvalidateTag();
		FUniqueObjectGuid::InvalidateTag();
	}
	else
	{
		TGuardValue<bool> GuardIsRoutingPostLoad(PackageScope.ThreadContext.IsRoutingPostLoad, true);
		FAsyncLoadingTickScope2 InAsyncLoadingTick(Package->AsyncLoadingThread);

		const FExportBundleHeader* ExportBundle = Package->Data.ExportBundleHeaders + InExportBundleIndex;
		const FExportBundleEntry* BundleEntries = Package->Data.ExportBundleEntries + ExportBundle->FirstEntryIndex;
		const FExportBundleEntry* BundleEntry = BundleEntries + Package->ExportBundleEntryIndex;
		const FExportBundleEntry* BundleEntryEnd = BundleEntries + ExportBundle->EntryCount;
		check(BundleEntry <= BundleEntryEnd);
		while (BundleEntry < BundleEntryEnd)
		{
			if (ThreadState.IsTimeLimitExceeded(TEXT("Event_DeferredPostLoadExportBundle")))
			{
				LoadingState = EAsyncPackageState::TimeOut;
				break;
			}

			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				do
				{
					FExportObject& Export = Package->Data.Exports[BundleEntry->LocalExportIndex];
					if (Export.bFiltered | Export.bExportLoadFailed)
					{
						break;
					}

					UObject* Object = Export.Object;
					check(Object);
					check(!Object->HasAnyFlags(RF_NeedLoad));
					if (Object->HasAnyFlags(RF_NeedPostLoad))
					{
						PackageScope.ThreadContext.CurrentlyPostLoadedObjectByALT = Object;
						{
							TRACE_LOADTIME_POSTLOAD_EXPORT_SCOPE(Object);
							FScopeCycleCounterUObject ConstructorScope(Object, GET_STATID(STAT_FAsyncPackage_PostLoadObjectsGameThread));
							Object->ConditionalPostLoad();
						}
						PackageScope.ThreadContext.CurrentlyPostLoadedObjectByALT = nullptr;
					}
				} while (false);
			}
			++BundleEntry;
			++Package->ExportBundleEntryIndex;
		}
	}

	if (LoadingState == EAsyncPackageState::TimeOut)
	{
		return LoadingState;
	}

	Package->ExportBundleEntryIndex = 0;

	if (++Package->ProcessedExportBundlesCount == Package->Data.ExportInfo.ExportBundleCount)
	{
		Package->ProcessedExportBundlesCount = 0;
		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::DeferredPostLoad);
		Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::DeferredPostLoadDone;
		Package->ConditionalFinishLoading();
	}

	return EAsyncPackageState::Complete;
}

FEventLoadNode2& FAsyncPackage2::GetPackageNode(EEventLoadNode2 Phase)
{
	check(Phase < EEventLoadNode2::Package_NumPhases);
	return *(reinterpret_cast<FEventLoadNode2*>(PackageNodesMemory) + Phase);
}

FEventLoadNode2& FAsyncPackage2::GetExportBundleNode(EEventLoadNode2 Phase, uint32 ExportBundleIndex)
{
	check(ExportBundleIndex < uint32(Data.ExportInfo.ExportBundleCount));
	uint32 ExportBundleNodeIndex = ExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases + Phase;
	return Data.ExportBundleNodes[ExportBundleNodeIndex];
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

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessAsyncLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, int32& OutPackagesProcessed)
{
	SCOPED_LOADTIMER(AsyncLoadingTime);

	check(IsInGameThread());

	OutPackagesProcessed = 0;

#if ALT2_VERIFY_RECURSIVE_LOADS 
	FScopedLoadRecursionVerifier LoadRecursionVerifier(this->LoadRecursionLevel);
#endif
	FAsyncLoadingTickScope2 InAsyncLoadingTick(*this);
	uint32 LoopIterations = 0;

	while (true)
	{
		do 
		{
			if ((++LoopIterations) % 32 == 31)
			{
				// We're not multithreaded and flushing async loading
				// Update heartbeat after 32 events
				FThreadHeartBeat::Get().HeartBeat();
				FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast();
			}

			if (ThreadState.IsTimeLimitExceeded(TEXT("ProcessAsyncLoadingFromGameThread")))
			{
				return EAsyncPackageState::TimeOut;
			}

			if (IsAsyncLoadingSuspended())
			{
				return EAsyncPackageState::TimeOut;
			}

			if (QueuedPackagesCounter || !PendingPackages.IsEmpty())
			{
				if (CreateAsyncPackagesFromQueue(ThreadState))
				{
					OutPackagesProcessed++;
					break;
				}
				else
				{
					return EAsyncPackageState::TimeOut;
				}
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

			if (!ExternalReadQueue.IsEmpty())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(WaitingForExternalReads);

				FAsyncPackage2* Package = nullptr;
				ExternalReadQueue.Dequeue(Package);

				EAsyncPackageState::Type Result = Package->ProcessExternalReads(FAsyncPackage2::ExternalReadAction_Wait);
				check(Result == EAsyncPackageState::Complete);

				OutPackagesProcessed++;
				break;
			}

			ThreadState.ProcessDeferredFrees();

			if (!DeferredDeletePackages.IsEmpty())
			{
				FAsyncPackage2* Package = nullptr;
				DeferredDeletePackages.Dequeue(Package);
				DeleteAsyncPackage(Package);
				OutPackagesProcessed++;
				break;
			}

			return EAsyncPackageState::Complete;
		} while (false);
	}
	check(false);
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadedPackagesFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool& bDidSomething, int32 FlushRequestID)
{
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;

	// This is for debugging purposes only. @todo remove
	volatile int32 CurrentAsyncLoadingCounter = AsyncLoadingTickCounter;

	if (IsMultithreaded() &&
		ENamedThreads::GetRenderThread() == ENamedThreads::GameThread &&
		!FTaskGraphInterface::Get().IsThreadProcessingTasks(ENamedThreads::GameThread)) // render thread tasks are actually being sent to the game thread.
	{
		// The async loading thread might have queued some render thread tasks (we don't have a render thread yet, so these are actually sent to the game thread)
		// We need to process them now before we do any postloads.
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		if (ThreadState.IsTimeLimitExceeded(TEXT("ProcessLoadedPackagesFromGameThread")))
		{
			return EAsyncPackageState::TimeOut;
		}
	}
	for (;;)
	{
		FPlatformMisc::PumpEssentialAppMessages();

		if (ThreadState.IsTimeLimitExceeded(TEXT("ProcessAsyncLoadingFromGameThread")))
		{
			Result = EAsyncPackageState::TimeOut;
			break;
		}

		bool bLocalDidSomething = false;
		bLocalDidSomething |= MainThreadEventQueue.PopAndExecute(ThreadState);

		bLocalDidSomething |= LoadedPackagesToProcess.Num() > 0;
		TArray<FAsyncPackage2*, TInlineAllocator<4>> PackagesReadyForCallback;
#if WITH_IOSTORE_IN_EDITOR
		TSet<UPackage*> CompletedUPackages;
#endif
		for (int32 PackageIndex = 0; PackageIndex < LoadedPackagesToProcess.Num(); ++PackageIndex)
		{
			SCOPED_LOADTIMER(ProcessLoadedPackagesTime);
			FAsyncPackage2* Package = LoadedPackagesToProcess[PackageIndex];
			UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
			check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::Finalize);

			bool bHasClusterObjects = false;
			TArray<UObject*> CDODefaultSubobjects;
			// Clear async loading flags (we still want RF_Async, but EInternalObjectFlags::AsyncLoading can be cleared)
			for (int32 FinalizeIndex = 0, ExportCount = Package->Data.ExportInfo.ExportCount; FinalizeIndex < ExportCount; ++FinalizeIndex)
			{
				const FExportObject& Export = Package->Data.Exports[FinalizeIndex];
				if (Export.bFiltered | Export.bExportLoadFailed)
				{
					continue;
				}

				UObject* Object = Export.Object;

				// CDO need special handling, no matter if it's listed in DeferredFinalizeObjects
				UObject* CDOToHandle = ((Object != nullptr) && Object->HasAnyFlags(RF_ClassDefaultObject)) ? Object : nullptr;

				// Clear AsyncLoading in CDO's subobjects.
				if (CDOToHandle != nullptr)
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

			Package->FinishUPackage();

			if (!Package->bLoadHasFailed && CanCreateObjectClusters())
			{
				for (const FExportObject& Export : Package->Data.Exports)
				{
					if (!(Export.bFiltered | Export.bExportLoadFailed) && Export.Object->CanBeClusterRoot())
					{
						bHasClusterObjects = true;
						break;
					}
				}
			}

			FSoftObjectPath::InvalidateTag();
			FUniqueObjectGuid::InvalidateTag();

			{
				FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
				AsyncPackageLookup.Remove(Package->Desc.UPackageId);
				if (!Package->bLoadHasFailed)
				{
#if WITH_IOSTORE_IN_EDITOR
					// In the editor we need to find any assets and packages and add them to list for later callback
					Package->GetLoadedAssetsAndPackages(LoadedAssets, CompletedUPackages);
#endif
					Package->ClearConstructedObjects();
				}
			}

			// Remove the package from the list before we trigger the callbacks, 
			// this is to ensure we can re-enter FlushAsyncLoading from any of the callbacks
			LoadedPackagesToProcess.RemoveAt(PackageIndex--);

			// Incremented on the Async Thread, now decrement as we're done with this package				
			ActiveAsyncPackagesCounter.Decrement();

			TRACE_LOADTIME_END_LOAD_ASYNC_PACKAGE(Package);

			check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::Finalize);
			if (bHasClusterObjects)
			{
				Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::CreateClusters;
			}
			else
			{
				Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::Complete;
			}
			PackagesReadyForCallback.Add(Package);
		}

		// Call callbacks in a batch in a stack-local array. This is to ensure that callbacks that trigger
		// on each package load and call FlushAsyncLoading do not stack overflow by adding one FlushAsyncLoading
		// call per LoadedPackageToProcess onto the stack
		for (FAsyncPackage2* Package : PackagesReadyForCallback)
		{
			// Call external callbacks
			const EAsyncLoadingResult::Type LoadingResult = Package->HasLoadFailed() ? EAsyncLoadingResult::Failed : EAsyncLoadingResult::Succeeded;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PackageCompletionCallbacks);
				Package->CallCompletionCallbacks(LoadingResult);
			}

			check(!CompletedPackages.Contains(Package));
			CompletedPackages.Add(Package);
			Package->MarkRequestIDsAsComplete();

			UE_ASYNC_PACKAGE_LOG(Verbose, Package->Desc, TEXT("GameThread: LoadCompleted"),
				TEXT("All loading of package is done, and the async package and load request will be deleted."));
		}
#if WITH_IOSTORE_IN_EDITOR
		// Call the global delegate for package endloads and set the bHasBeenLoaded flag that is used to
		// check which packages have reached this state
		for (UPackage* CompletedUPackage : CompletedUPackages)
		{
			CompletedUPackage->SetHasBeenEndLoaded(true);
		}
		FCoreUObjectDelegates::OnEndLoadPackage.Broadcast(CompletedUPackages.Array());
#endif
		
		{
			TArray<FFailedPackageRequest> LocalFailedPackageRequests;
			{
				FScopeLock _(&FailedPackageRequestsCritical);
				Swap(LocalFailedPackageRequests, FailedPackageRequests);
			}

			bLocalDidSomething |= LocalFailedPackageRequests.Num() > 0;
			for (FFailedPackageRequest& FailedPackageRequest : LocalFailedPackageRequests)
			{
				FailedPackageRequest.Callback->ExecuteIfBound(FailedPackageRequest.PackageName, nullptr, EAsyncLoadingResult::Failed);
				RemovePendingRequests(TArrayView<int32>(&FailedPackageRequest.RequestID, 1));
				--QueuedPackagesCounter;
			}
		}

		bLocalDidSomething |= CompletedPackages.Num() > 0;
		for (int32 PackageIndex = 0; PackageIndex < CompletedPackages.Num(); ++PackageIndex)
		{
			FAsyncPackage2* Package = CompletedPackages[PackageIndex];
			{
				bool bSafeToDelete = false;
				if (Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::CreateClusters)
				{
					SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_CreateClustersGameThread);
					// This package will create GC clusters but first check if all dependencies of this package have been fully loaded
					if (Package->CreateClusters(ThreadState) == EAsyncPackageState::Complete)
					{
						// All clusters created, it's safe to delete the package
						bSafeToDelete = true;
						Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::Complete;
					}
					else
					{
						// Cluster creation timed out
						Result = EAsyncPackageState::TimeOut;
						break;
					}
				}
				else
				{
					// No clusters to create so it's safe to delete
					bSafeToDelete = true;
				}

				if (bSafeToDelete)
				{
					UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
					check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::Complete);
					Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::DeferredDelete;

					CompletedPackages.RemoveAtSwap(PackageIndex--);
					Package->ClearImportedPackages();
					Package->ReleaseRef();
				}
			}

			// push stats so that we don't overflow number of tags per thread during blocking loading
			LLM_PUSH_STATS_FOR_ASSET_TAGS();
		}
		
		if (!bLocalDidSomething)
		{
			break;
		}

		bDidSomething = true;
		
		if (FlushRequestID != INDEX_NONE && !ContainsRequestID(FlushRequestID))
		{
			// The only package we care about has finished loading, so we're good to exit
			break;
		}
	}

	if (Result == EAsyncPackageState::Complete)
	{
#if WITH_IOSTORE_IN_EDITOR
		// In editor builds, call the asset load callback. This happens in both editor and standalone to match EndLoad
		TSet<FWeakObjectPtr> TempLoadedAssets = LoadedAssets;
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
		Result = CompletedPackages.Num() ? EAsyncPackageState::PendingImports  : EAsyncPackageState::Complete;
		if (Result == EAsyncPackageState::Complete && ThreadState.HasDeferredFrees())
		{
			ThreadState.ProcessDeferredFrees();
		}
	}

	return Result;
}

EAsyncPackageState::Type FAsyncLoadingThread2::TickAsyncLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, int32 FlushRequestID)
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_TickAsyncLoadingGameThread);
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
		ThreadState.SetTimeLimit(bUseTimeLimit, TimeLimit);

		const bool bIsMultithreaded = FAsyncLoadingThread2::IsMultithreaded();
		double TickStartTime = FPlatformTime::Seconds();

		bool bDidSomething = false;
		{
			Result = ProcessLoadedPackagesFromGameThread(ThreadState, bDidSomething, FlushRequestID);
			double TimeLimitUsedForProcessLoaded = FPlatformTime::Seconds() - TickStartTime;
			UE_CLOG(!GIsEditor && bUseTimeLimit && TimeLimitUsedForProcessLoaded > .1f, LogStreaming, Warning, TEXT("Took %6.2fms to ProcessLoadedPackages"), float(TimeLimitUsedForProcessLoaded) * 1000.0f);
		}

		if (!bIsMultithreaded && Result != EAsyncPackageState::TimeOut)
		{
			Result = TickAsyncThreadFromGameThread(ThreadState, bDidSomething);
		}

		if (Result != EAsyncPackageState::TimeOut)
		{
			// Flush deferred messages
			if (ExistingAsyncPackagesCounter.GetValue() == 0)
			{
				bDidSomething = true;
				FDeferredMessageLog::Flush();
			}

			if (GIsInitialLoad && !bDidSomething)
			{
				bDidSomething = ProcessPendingCDOs();
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
{
#if !WITH_IOSTORE_IN_EDITOR
	GEventDrivenLoaderEnabled = true;
#endif

	AltEventQueues.Add(&EventQueue);
	for (FAsyncLoadEventQueue2* Queue : AltEventQueues)
	{
		Queue->SetZenaphore(&AltZenaphore);
	}

	EventSpecs.AddDefaulted(EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_NumPhases);
	EventSpecs[EEventLoadNode2::Package_ProcessSummary] = { &FAsyncPackage2::Event_ProcessPackageSummary, &EventQueue, false };
	EventSpecs[EEventLoadNode2::Package_SetupDependencies] = { &FAsyncPackage2::Event_SetupDependencies, &EventQueue, false };
	EventSpecs[EEventLoadNode2::Package_ExportsSerialized] = { &FAsyncPackage2::Event_ExportsDone, &EventQueue, true };

	EventSpecs[EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_Process] = { &FAsyncPackage2::Event_ProcessExportBundle, &EventQueue, false };
	EventSpecs[EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_PostLoad] = { &FAsyncPackage2::Event_PostLoadExportBundle, &EventQueue, false };
	EventSpecs[EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_DeferredPostLoad] = { &FAsyncPackage2::Event_DeferredPostLoadExportBundle, &MainThreadEventQueue, false };

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

	if (!FAsyncLoadingThreadSettings::Get().bAsyncLoadingThreadEnabled)
	{
		FinalizeInitialLoad();
	}
	else if (!Thread)
	{
		UE_LOG(LogStreaming, Log, TEXT("Starting Async Loading Thread."));
		bThreadStarted = true;
		FPlatformMisc::MemoryBarrier();
		UE::Trace::ThreadGroupBegin(TEXT("AsyncLoading"));
		Thread = FRunnableThread::Create(this, TEXT("FAsyncLoadingThread"), 0, TPri_Normal);
		UE::Trace::ThreadGroupEnd();
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

void FAsyncLoadingThread2::LazyInitializeFromLoadPackage()
{
	if (bLazyInitializedFromLoadPackage)
	{
		return;	
	}
	bLazyInitializedFromLoadPackage = true;

	TRACE_CPUPROFILER_EVENT_SCOPE(LazyInitializeFromLoadPackage);
	if (GIsInitialLoad)
	{
		GlobalImportStore.Initialize(IoDispatcher);
	}

	LoadedPackageStore.Initialize(*PackageStore.Get());
}

void FAsyncLoadingThread2::FinalizeInitialLoad()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FinalizeInitialLoad);
	GlobalImportStore.FindAllScriptObjects();
	check(PendingCDOs.Num() == 0);
	PendingCDOs.Empty();
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
	
	FinalizeInitialLoad();

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
			bool bDidSomething = false;
			{
				FGCScopeGuard GCGuard;
				TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
				do
				{
					bDidSomething = false;

					if (QueuedPackagesCounter || !PendingPackages.IsEmpty())
					{
						if (CreateAsyncPackagesFromQueue(ThreadState))
						{
							bDidSomething = true;
						}
					}

					bool bShouldSuspend = false;
					bool bPopped = false;
					do 
					{
						bPopped = false;
						{
							FAsyncPackage2* Package = nullptr;
							if (ExternalReadQueue.Peek(Package))
							{
								TRACE_CPUPROFILER_EVENT_SCOPE(PollExternalReads);
								EAsyncPackageState::Type Result = Package->ProcessExternalReads(FAsyncPackage2::ExternalReadAction_Poll);
								if (Result == EAsyncPackageState::Complete)
								{
									ExternalReadQueue.Pop();
									bPopped = true;
									bDidSomething = true;
								}
							}
						}

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

				} while (bDidSomething);
			}

			if (!bDidSomething)
			{
				if (ThreadState.HasDeferredFrees())
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					ThreadState.ProcessDeferredFrees();
					bDidSomething = true;
				}

				if (!DeferredDeletePackages.IsEmpty())
				{
					FGCScopeGuard GCGuard;
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					FAsyncPackage2* Package = nullptr;
					int32 Count = 0;
					while (++Count <= 100 && DeferredDeletePackages.Dequeue(Package))
					{
						DeleteAsyncPackage(Package);
					}
					bDidSomething = true;
				}
			}

			if (!bDidSomething)
			{
				FAsyncPackage2* Package = nullptr;
				if (PendingIoRequestsCounter.Load() > 0)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					TRACE_CPUPROFILER_EVENT_SCOPE(WaitingForIo);
					Waiter.Wait();
				}
				else if (ExternalReadQueue.Peek(Package))
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					TRACE_CPUPROFILER_EVENT_SCOPE(WaitingForExternalReads);

					EAsyncPackageState::Type Result = Package->ProcessExternalReads(FAsyncPackage2::ExternalReadAction_Wait);
					check(Result == EAsyncPackageState::Complete);
					ExternalReadQueue.Pop();
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

EAsyncPackageState::Type FAsyncLoadingThread2::TickAsyncThreadFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool& bDidSomething)
{
	check(IsInGameThread());
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	
	int32 ProcessedRequests = 0;
	if (AsyncThreadReady.GetValue())
	{
		if (ThreadState.IsTimeLimitExceeded(TEXT("TickAsyncThreadFromGameThread")))
		{
			Result = EAsyncPackageState::TimeOut;
		}
		else
		{
			FGCScopeGuard GCGuard;
			Result = ProcessAsyncLoadingFromGameThread(ThreadState, ProcessedRequests);
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
	/*
	FAsyncPackage2* Package = FindAsyncPackage(PackageName);
	if (Package)
	{
		LoadPercentage = Package->GetLoadPercentage();
	}
	*/
	return LoadPercentage;
}

#if ALT2_VERIFY_ASYNC_FLAGS
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

			ensureMsgf(!bHasAnyLoadIntermediateFlags,
				TEXT("Object '%s' (ObjectFlags=%X, InternalObjectFlags=%x) should not have any load flags now")
				TEXT(", or this check is incorrectly reached during active loading."),
				*Obj->GetFullName(),
				Flags,
				InternalFlags);

			if (bWasLoaded)
			{
				const bool bIsPackage = Obj->IsA(UPackage::StaticClass());

				ensureMsgf(bIsPackage || bLoadCompleted,
					TEXT("Object '%s' (ObjectFlags=%x, InternalObjectFlags=%x) is a serialized object and should be completely loaded now")
					TEXT(", or this check is incorrectly reached during active loading."),
					*Obj->GetFullName(),
					Flags,
					InternalFlags);

				ensureMsgf(!bHasAnyAsyncFlags,
					TEXT("Object '%s' (ObjectFlags=%x, InternalObjectFlags=%x) is a serialized object and should not have any async flags now")
					TEXT(", or this check is incorrectly reached during active loading."),
					*Obj->GetFullName(),
					Flags,
					InternalFlags);
			}
		}
	}
	UE_LOG(LogStreaming, Log, TEXT("Verified load flags when finished active loading."));
}
#endif

FORCENOINLINE static void FilterUnreachableObjects(
	const TArrayView<FUObjectItem*>& UnreachableObjects,
	FUnreachablePublicExports& PublicExports,
	FUnreachablePackages& Packages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FilterUnreachableObjects);

	PublicExports.Reserve(UnreachableObjects.Num());
	Packages.Reserve(UnreachableObjects.Num());

	for (FUObjectItem* ObjectItem : UnreachableObjects)
	{
		UObject* Object = static_cast<UObject*>(ObjectItem->Object);
		// Including all objects is slow,
		// but allows RemovePublicExports to check for serialized public exports that have unintentionally lost their flags
		if (DO_CHECK || Object->HasAllFlags(RF_WasLoaded | RF_Public))
		{
			if (Object->GetOuter())
			{
				PublicExports.Emplace(GUObjectArray.ObjectToIndex(Object), Object);
			}
			else
			{
				UPackage* Package = static_cast<UPackage*>(Object);
#if WITH_IOSTORE_IN_EDITOR
				if (Package->HasAnyPackageFlags(PKG_Cooked))
#endif
				{
					Packages.Emplace(Package);
				}
			}
		}
	}
}

void FAsyncLoadingThread2::RemoveUnreachableObjects(const FUnreachablePublicExports& PublicExports, const FUnreachablePackages& Packages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RemoveUnreachableObjects);

	TArray<FPackageId> PublicExportPackages;
	if (PublicExports.Num() > 0)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(RemovePublicExports);
		PublicExportPackages = GlobalImportStore.RemovePublicExports(PublicExports);
	}
	if (Packages.Num() > 0)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(RemovePackages);
		LoadedPackageStore.RemovePackages(Packages);
	}
	if (PublicExportPackages.Num() > 0)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(ClearAllPublicExportsLoaded);
		LoadedPackageStore.ClearAllPublicExportsLoaded(PublicExportPackages);
	}
}

void FAsyncLoadingThread2::NotifyUnreachableObjects(const TArrayView<FUObjectItem*>& UnreachableObjects)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(NotifyUnreachableObjects);

	if (GExitPurge)
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	FUnreachablePackages Packages;
	FUnreachablePublicExports PublicExports;
	FilterUnreachableObjects(UnreachableObjects, PublicExports, Packages);

	const int32 PackageCount = Packages.Num();
	const int32 PublicExportCount = PublicExports.Num();
	if (PackageCount > 0 || PublicExportCount > 0)
	{
		const int32 OldLoadedPackageCount = LoadedPackageStore.NumTracked();
		const int32 OldPublicExportCount = GlobalImportStore.PublicExportToObjectIndex.Num();

		const double RemoveStartTime = FPlatformTime::Seconds();
		RemoveUnreachableObjects(PublicExports, Packages);

		const int32 NewLoadedPackageCount = LoadedPackageStore.NumTracked();
		const int32 NewPublicExportCount = GlobalImportStore.PublicExportToObjectIndex.Num();
		const int32 RemovedLoadedPackageCount = OldLoadedPackageCount - NewLoadedPackageCount;
		const int32 RemovedPublicExportCount = OldPublicExportCount - NewPublicExportCount;

		const double StopTime = FPlatformTime::Seconds();
		UE_LOG(LogStreaming, Display,
			TEXT("%.3f ms (%.3f+%.3f) ms for processing %d/%d objects in NotifyUnreachableObjects( Queued=%d, Async=%d). ")
			TEXT("Removed %d/%d (%d->%d tracked) packages and %d/%d (%d->%d tracked) public exports."),
			(StopTime - StartTime) * 1000,
			(RemoveStartTime - StartTime) * 1000,
			(StopTime - RemoveStartTime) * 1000,
			PublicExportCount + PackageCount, UnreachableObjects.Num(),
			GetNumQueuedPackages(), GetNumAsyncPackages(),
			RemovedLoadedPackageCount, PackageCount, OldLoadedPackageCount, NewLoadedPackageCount,
			RemovedPublicExportCount, PublicExportCount, OldPublicExportCount, NewPublicExportCount);
	}
	else
	{
		UE_LOG(LogStreaming, Display, TEXT("%.3f ms for skipping %d objects in NotifyUnreachableObjects (Queued=%d, Async=%d)."),
			(FPlatformTime::Seconds() - StartTime) * 1000,
			UnreachableObjects.Num(),
			GetNumQueuedPackages(), GetNumAsyncPackages());
	}

#if ALT2_VERIFY_ASYNC_FLAGS
	if (!IsAsyncLoadingPackages())
	{
		LoadedPackageStore.VerifyLoadedPackages();
		VerifyLoadFlagsWhenFinishedLoading();
	}
#endif
}

/**
 * Call back into the async loading code to inform of the creation of a new object
 * @param Object		Object created
 * @param bSubObjectThatAlreadyExists	Object created as a sub-object of a loaded object
 */
void FAsyncLoadingThread2::NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObjectThatAlreadyExists)
{
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	if (!ThreadContext.AsyncPackage)
	{
		// Something is creating objects on the async loading thread outside of the actual async loading code
		// e.g. ShaderCodeLibrary::OnExternalReadCallback doing FTaskGraphInterface::Get().WaitUntilTaskCompletes(Event);
		return;
	}

	// Mark objects created during async loading process (e.g. from within PostLoad or CreateExport) as async loaded so they 
	// cannot be found. This requires also keeping track of them so we can remove the async loading flag later one when we 
	// finished routing PostLoad to all objects.
	if (!bSubObjectThatAlreadyExists)
	{
		Object->SetInternalFlags(EInternalObjectFlags::AsyncLoading);
	}
	FAsyncPackage2* AsyncPackage2 = (FAsyncPackage2*)ThreadContext.AsyncPackage;
	AsyncPackage2->AddConstructedObject(Object, bSubObjectThatAlreadyExists);
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
, AsyncLoadingThread(InAsyncLoadingThread)
, GraphAllocator(InGraphAllocator)
, ImportStore(*AsyncLoadingThread.PackageStore.Get(), AsyncLoadingThread.GlobalImportStore, AsyncLoadingThread.LoadedPackageStore, Desc, Data.ImportedPackageIds)
{
	TRACE_LOADTIME_NEW_ASYNC_PACKAGE(this);
	AddRequestID(Desc.RequestID);

	CreatePackageNodes(EventSpecs);
}

void FAsyncPackage2::CreatePackageNodes(const FAsyncLoadEventSpec* EventSpecs)
{
	const int32 BarrierCount = 1;
	
	FEventLoadNode2* Node = reinterpret_cast<FEventLoadNode2*>(PackageNodesMemory);
	for (int32 Phase = 0; Phase < EEventLoadNode2::Package_NumPhases; ++Phase)
	{
		new (Node) FEventLoadNode2(EventSpecs + Phase, this, -1, BarrierCount);
		++Node;
	}
}

void FAsyncPackage2::CreateExportBundleNodes(const FAsyncLoadEventSpec* EventSpecs)
{
	const int32 BarrierCount = 1;
	for (int32 ExportBundleIndex = 0; ExportBundleIndex < Data.ExportInfo.ExportBundleCount; ++ExportBundleIndex)
	{
		uint32 NodeIndex = EEventLoadNode2::ExportBundle_NumPhases * ExportBundleIndex;
		for (int32 Phase = 0; Phase < EEventLoadNode2::ExportBundle_NumPhases; ++Phase)
		{
			new (&Data.ExportBundleNodes[NodeIndex + Phase]) FEventLoadNode2(EventSpecs + EEventLoadNode2::Package_NumPhases + Phase, this, ExportBundleIndex, BarrierCount);
		}
	}
}

FAsyncPackage2::~FAsyncPackage2()
{
	TRACE_LOADTIME_DESTROY_ASYNC_PACKAGE(this);
	UE_ASYNC_PACKAGE_LOG(Verbose, Desc, TEXT("AsyncThread: Deleted"), TEXT("Package deleted."));

	ImportStore.ReleasePackageReferences();

	checkf(RefCount == 0, TEXT("RefCount is not 0 when deleting package %s"),
		*Desc.PackageNameToLoad.ToString());

	checkf(RequestIDs.Num() == 0, TEXT("MarkRequestIDsAsComplete() has not been called for package %s"),
		*Desc.PackageNameToLoad.ToString());
	
	checkf(ConstructedObjects.Num() == 0, TEXT("ClearConstructedObjects() has not been called for package %s"),
		*Desc.PackageNameToLoad.ToString());

	FMemory::Free(Data.MemoryBuffer);
}

void FAsyncPackage2::ReleaseRef()
{
	check(RefCount > 0);
	if (--RefCount == 0)
	{
		FAsyncLoadingThread2& AsyncLoadingThreadLocal = AsyncLoadingThread;
		AsyncLoadingThreadLocal.DeferredDeletePackages.Enqueue(this);
		AsyncLoadingThreadLocal.AltZenaphore.NotifyOne();
	}
}

void FAsyncPackage2::ClearImportedPackages()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ClearImportedPackages);
	for (FAsyncPackage2* ImportedAsyncPackage : Data.ImportedAsyncPackages)
	{
		if (ImportedAsyncPackage)
		{
			ImportedAsyncPackage->ReleaseRef();
		}
	}
	Data.ImportedAsyncPackages = MakeArrayView(Data.ImportedAsyncPackages.GetData(), 0);
}

void FAsyncPackage2::ClearConstructedObjects()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ClearConstructedObjects);

	for (UObject* Object : ConstructedObjects)
	{
		if (Object->HasAnyFlags(RF_WasLoaded))
		{
			// exports and the upackage itself are are handled below
			continue;
		}
		Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading | EInternalObjectFlags::Async);
	}
	ConstructedObjects.Empty();

	// the async flag of all GC'able public export objects in non-temp packages are handled by FGlobalImportStore::ClearAsyncFlags
	const bool bShouldClearAsyncFlagForPublicExports = GUObjectArray.IsDisregardForGC(LinkerRoot) || !Desc.bCanBeImported;

	for (FExportObject& Export : Data.Exports)
	{
		if (Export.bFiltered | Export.bExportLoadFailed)
		{
			continue;
		}

		UObject* Object = Export.Object;
		check(Object);
		checkf(Object->HasAnyFlags(RF_WasLoaded), TEXT("%s"), *Object->GetFullName());
		checkf(Object->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *Object->GetFullName());
		if (bShouldClearAsyncFlagForPublicExports || !Object->HasAnyFlags(RF_Public))
		{
			Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading | EInternalObjectFlags::Async);
		}
		else
		{
			Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
		}
	}

	if (LinkerRoot)
	{
		if (bShouldClearAsyncFlagForPublicExports)
		{
			LinkerRoot->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading | EInternalObjectFlags::Async);
		}
		else
		{
			LinkerRoot->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
		}
	}
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

#if WITH_IOSTORE_IN_EDITOR
void FAsyncPackage2::GetLoadedAssetsAndPackages(TSet<FWeakObjectPtr>& AssetList, TSet<UPackage*>& PackageList)
{
	for (UObject* Object : ConstructedObjects)
	{
		if (IsValid(Object) && Object->IsAsset())
		{
			AssetList.Add(Object);
		}
	}

	// All ConstructedObjects belong to this package, so we only have to consider the single package in this->LinkerRoot
	if (LinkerRoot && !LinkerRoot->HasAnyFlags(RF_Transient) && !LinkerRoot->HasAnyPackageFlags(PKG_InMemoryOnly))
	{
		PackageList.Add(LinkerRoot);
	}
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
 * End async loading process. Simulates parts of EndLoad(). 
 */
void FAsyncPackage2::EndAsyncLoad()
{
	check(AsyncLoadingThread.IsAsyncLoadingPackages());

	// this won't do much during async loading except decrease the load count which causes IsLoading to return false
	FUObjectSerializeContext* LoadContext = GetSerializeContext();
	EndLoad(LoadContext);

	if (IsInGameThread())
	{
		AsyncLoadingThread.LeaveAsyncLoadingTick();
	}
}

void FAsyncPackage2::CreateUPackage(const FZenPackageSummary* PackageSummary, const FZenPackageVersioningInfo* VersioningInfo)
{
	check(!LinkerRoot);

	// temp packages are never stored or found in loaded package store
	FLoadedPackageRef* PackageRef = nullptr;

	// Try to find existing package or create it if not already present.
	UPackage* ExistingPackage = nullptr;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackageFind);
		if (Desc.bCanBeImported)
		{
			PackageRef = ImportStore.LoadedPackageStore.FindPackageRef(Desc.UPackageId);
			UE_ASYNC_PACKAGE_CLOG(!PackageRef, Fatal, Desc, TEXT("CreateUPackage"), TEXT("Package has been destroyed by GC."));
			LinkerRoot = PackageRef->GetPackage();
#if DO_CHECK
			if (LinkerRoot)
			{
				UPackage* FoundPackage = FindObjectFast<UPackage>(nullptr, Desc.UPackageName);
				checkf(LinkerRoot == FoundPackage,
					TEXT("LinkerRoot '%s' (%p) is different from FoundPackage '%s' (%p)"),
					*LinkerRoot->GetName(), LinkerRoot, *FoundPackage->GetName(), FoundPackage);
			}
#endif
		}
		if (!LinkerRoot)
		{
			// Packages can be created outside the loader, i.e from ResolveName via StaticLoadObject
			ExistingPackage = FindObjectFast<UPackage>(nullptr, Desc.UPackageName);
		}
	}
	if (!LinkerRoot)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackageCreate);
		if (ExistingPackage)
		{
			LinkerRoot = ExistingPackage;
		}
		else 
		{
			LinkerRoot = NewObject<UPackage>(/*Outer*/nullptr, Desc.UPackageName);
			bCreatedLinkerRoot = true;
		}
		LinkerRoot->SetFlags(RF_Public | RF_WasLoaded);
		LinkerRoot->SetLoadedPath(FPackagePath::FromPackageNameUnchecked(Desc.PackageNameToLoad));
		LinkerRoot->SetCanBeImportedFlag(Desc.bCanBeImported);
		LinkerRoot->SetPackageId(Desc.UPackageId);
		LinkerRoot->SetPackageFlagsTo(PackageSummary->PackageFlags | PKG_Cooked);
		if (VersioningInfo)
		{
			LinkerRoot->LinkerPackageVersion = VersioningInfo->PackageVersion;
			LinkerRoot->LinkerLicenseeVersion = VersioningInfo->LicenseeVersion;
			LinkerRoot->LinkerCustomVersion = VersioningInfo->CustomVersions;
		}
		else
		{
			LinkerRoot->LinkerPackageVersion = GPackageFileUEVersion;
			LinkerRoot->LinkerLicenseeVersion = GPackageFileLicenseeUEVersion;
		}
#if WITH_IOSTORE_IN_EDITOR
		LinkerRoot->bIsCookedForEditor = !!(PackageSummary->PackageFlags & PKG_FilterEditorOnly);
#endif
		if (PackageRef)
		{
			PackageRef->SetPackage(LinkerRoot);
		}
	}
	else
	{
		check(LinkerRoot->CanBeImported() == Desc.bCanBeImported);
		check(LinkerRoot->GetPackageId() == Desc.UPackageId);
		check(LinkerRoot->GetPackageFlags() == (PackageSummary->PackageFlags | PKG_Cooked));
		check(LinkerRoot->LinkerPackageVersion == GPackageFileUEVersion);
		check(LinkerRoot->LinkerLicenseeVersion == GPackageFileLicenseeUEVersion);
		check(LinkerRoot->HasAnyFlags(RF_WasLoaded));
	}

	PinObjectForGC(LinkerRoot, bCreatedLinkerRoot);

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
	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::WaitingForExternalReads);
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
	GetPackageNode(Package_ExportsSerialized).ReleaseBarrier();
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::CreateClusters(FAsyncLoadingThreadState2& ThreadState)
{
	while (DeferredClusterIndex < Data.ExportInfo.ExportCount && !ThreadState.IsTimeLimitExceeded(TEXT("CreateClusters")))
	{
		const FExportObject& Export = Data.Exports[DeferredClusterIndex++];

		if (!(Export.bFiltered | Export.bExportLoadFailed) && Export.Object->CanBeClusterRoot())
		{
			Export.Object->CreateCluster();
		}
	}

	return DeferredClusterIndex == Data.ExportInfo.ExportCount ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
}

void FAsyncPackage2::FinishUPackage()
{
	if (LinkerRoot)
	{
		if (!bLoadHasFailed)
		{
			// Mark package as having been fully loaded and update load time.
			LinkerRoot->MarkAsFullyLoaded();
			LinkerRoot->SetLoadTime(FPlatformTime::Seconds() - LoadStartTime);
		}
		else
		{
			// Clean up UPackage so it can't be found later
			if (bCreatedLinkerRoot && !LinkerRoot->IsRooted())
			{
				LinkerRoot->ClearFlags(RF_NeedPostLoad | RF_NeedLoad | RF_NeedPostLoadSubobjects);
				LinkerRoot->MarkAsGarbage();
				LinkerRoot->Rename(*MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass()).ToString(), nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders | REN_NonTransactional);
			}
		}
	}
}

void FAsyncPackage2::CallCompletionCallbacks(EAsyncLoadingResult::Type LoadingResult)
{
	checkSlow(IsInGameThread());

	UPackage* LoadedPackage = (!bLoadHasFailed) ? LinkerRoot : nullptr;
	for (FCompletionCallback& CompletionCallback : CompletionCallbacks)
	{
		CompletionCallback->ExecuteIfBound(Desc.UPackageName, LoadedPackage, LoadingResult);
	}
	CompletionCallbacks.Empty();
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

int32 FAsyncLoadingThread2::LoadPackage(const FPackagePath& InPackagePath, FName InCustomName, FLoadPackageAsyncDelegate InCompletionDelegate, EPackageFlags InPackageFlags, int32 InPIEInstanceID, int32 InPackagePriority, const FLinkerInstancingContext*)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackage);

	LazyInitializeFromLoadPackage();
	
	const FName PackageNameToLoad = InPackagePath.GetPackageFName();
	const FPackageId PackageIdToLoad = FPackageId::FromName(PackageNameToLoad);
	if (InCustomName == PackageNameToLoad)
	{
		InCustomName = NAME_None;
	}
	if (FCoreDelegates::OnAsyncLoadPackage.IsBound())
	{
		const FName PackageName = InCustomName.IsNone() ? PackageNameToLoad : InCustomName;
		FCoreDelegates::OnAsyncLoadPackage.Broadcast(PackageName.ToString());
	}

	// Generate new request ID and add it immediately to the global request list (it needs to be there before we exit
	// this function, otherwise it would be added when the packages are being processed on the async thread).
	const int32 RequestId = IAsyncPackageLoader::GetNextRequestId();
	TRACE_LOADTIME_BEGIN_REQUEST(RequestId);
	AddPendingRequest(RequestId);

	// Allocate delegate on Game Thread, it is not safe to copy delegates by value on other threads
	TUniquePtr<FLoadPackageAsyncDelegate> CompletionDelegate = InCompletionDelegate.IsBound()
		? MakeUnique<FLoadPackageAsyncDelegate>(MoveTemp(InCompletionDelegate))
		: TUniquePtr<FLoadPackageAsyncDelegate>();

	PackageRequestQueue.Enqueue(FPackageRequest::Create(RequestId, InPackagePriority, PackageNameToLoad, PackageIdToLoad, InCustomName, MoveTemp(CompletionDelegate)));
	++QueuedPackagesCounter;

	AltZenaphore.NotifyOne();

	return RequestId;
}

void FAsyncLoadingThread2::QueueMissingPackage(FAsyncPackageDesc2& PackageDesc, TUniquePtr<FLoadPackageAsyncDelegate>&& PackageLoadedDelegate)
{
	const FName FailedPackageName = PackageDesc.UPackageName;

	static TSet<FName> SkippedPackages;
	bool bIsAlreadySkipped = false;

	SkippedPackages.Add(FailedPackageName, &bIsAlreadySkipped);

	if (!bIsAlreadySkipped)
	{
		UE_LOG(LogStreaming, Warning,
			TEXT("LoadPackage: SkipPackage: %s (0x%llX) - The package to load does not exist on disk or in the loader"),
			*FailedPackageName.ToString(), PackageDesc.PackageIdToLoad.ValueForDebugging());
	}

	if (PackageLoadedDelegate.IsValid())
	{
		FScopeLock LockMissingPackages(&FailedPackageRequestsCritical);
		FailedPackageRequests.Add(FFailedPackageRequest
		{
			PackageDesc.RequestID,
			FailedPackageName,
			MoveTemp(PackageLoadedDelegate)
		});
	}
	else
	{
		RemovePendingRequests(TArrayView<int32>(&PackageDesc.RequestID, 1));
		--QueuedPackagesCounter;
	}
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool bUseTimeLimit, bool bUseFullTimeLimit, double TimeLimit)
{
	SCOPE_CYCLE_COUNTER(STAT_AsyncLoadingTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(AsyncLoading);

	// CSV_CUSTOM_STAT(FileIO, EDLEventQueueDepth, (int32)GraphAllocator.TotalNodeCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FileIO, QueuedPackagesQueueDepth, GetNumQueuedPackages(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FileIO, ExistingQueuedPackagesQueueDepth, GetNumAsyncPackages(), ECsvCustomStatOp::Set);
	
	TickAsyncLoadingFromGameThread(ThreadState, bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
	return IsAsyncLoading() ? EAsyncPackageState::TimeOut : EAsyncPackageState::Complete;
}

void FAsyncLoadingThread2::FlushLoading(int32 RequestId)
{
	if (IsAsyncLoadingPackages())
	{
		// Flushing async loading while loading is suspend will result in infinite stall
		UE_CLOG(bSuspendRequested, LogStreaming, Fatal, TEXT("Cannot Flush Async Loading while async loading is suspended"));

		SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FlushAsyncLoadingGameThread);

		if (RequestId != INDEX_NONE && !ContainsRequestID(RequestId))
		{
			return;
		}

		FCoreDelegates::OnAsyncLoadingFlush.Broadcast();

		double StartTime = FPlatformTime::Seconds();
		double LogFlushTime = StartTime;

		// Flush async loaders by not using a time limit. Needed for e.g. garbage collection.
		{
			FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
			while (IsAsyncLoadingPackages())
			{
				EAsyncPackageState::Type Result = TickAsyncLoadingFromGameThread(ThreadState, false, false, 0, RequestId);
				if (RequestId != INDEX_NONE && !ContainsRequestID(RequestId))
				{
					break;
				}

				if (IsMultithreaded())
				{
					// Update the heartbeat and sleep. If we're not multithreading, the heartbeat is updated after each package has been processed
					FThreadHeartBeat::Get().HeartBeat();
					FPlatformProcess::SleepNoStats(0.0001f);

					// Flush logging when runing cook-on-the-fly and waiting for packages
					if (IsRunningCookOnTheFly() && FPlatformTime::Seconds() - LogFlushTime > 1.0)
					{
						GLog->FlushThreadedLogs();
						LogFlushTime = FPlatformTime::Seconds();
					}
				}

				// push stats so that we don't overflow number of tags per thread during blocking loading
				LLM_PUSH_STATS_FOR_ASSET_TAGS();
			}
		}

		check(RequestId != INDEX_NONE || !IsAsyncLoading());
	}
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadingUntilCompleteFromGameThread(FAsyncLoadingThreadState2& ThreadState, TFunctionRef<bool()> CompletionPredicate, double TimeLimit)
{
	if (!IsAsyncLoadingPackages())
	{
		return EAsyncPackageState::Complete;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessLoadingUntilComplete);
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FlushAsyncLoadingGameThread);

	// Flushing async loading while loading is suspend will result in infinite stall
	UE_CLOG(bSuspendRequested, LogStreaming, Fatal, TEXT("Cannot Flush Async Loading while async loading is suspended"));

	bool bUseTimeLimit = TimeLimit > 0.0f;
	double TimeLoadingPackage = 0.0f;

	while (IsAsyncLoadingPackages() && (!bUseTimeLimit || TimeLimit > 0.0f) && !CompletionPredicate())
	{
		double TickStartTime = FPlatformTime::Seconds();
		if (ProcessLoadingFromGameThread(ThreadState, bUseTimeLimit, bUseTimeLimit, TimeLimit) == EAsyncPackageState::Complete)
		{
			return EAsyncPackageState::Complete;
		}

		if (IsMultithreaded())
		{
			// Update the heartbeat and sleep. If we're not multithreading, the heartbeat is updated after each package has been processed
			// only update the heartbeat up to the limit of the hang detector to ensure if we get stuck in this loop that the hang detector gets a chance to trigger
			if (TimeLoadingPackage < FThreadHeartBeat::Get().GetHangDuration())
			{
				FThreadHeartBeat::Get().HeartBeat();
			}
			FPlatformProcess::SleepNoStats(0.0001f);
		}

		double TimeDelta = (FPlatformTime::Seconds() - TickStartTime);
		TimeLimit -= TimeDelta;
		TimeLoadingPackage += TimeDelta;
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
