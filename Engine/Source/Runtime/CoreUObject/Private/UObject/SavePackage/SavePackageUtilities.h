// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "HAL/ThreadSingleton.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "Serialization/ArchiveStackTrace.h"
#include "Serialization/FileRegions.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectMarks.h"

// This file contains private utilities shared by UPackage::Save and UPackage::Save2 

class FMD5;
class FSavePackageContext;
template<typename StateType> class TAsyncWorkSequence;

DECLARE_LOG_CATEGORY_EXTERN(LogSavePackage, Log, All);

struct FLargeMemoryDelete
{
	void operator()(uint8* Ptr) const
	{
		if (Ptr)
		{
			FMemory::Free(Ptr);
		}
	}
};
typedef TUniquePtr<uint8, FLargeMemoryDelete> FLargeMemoryPtr;

enum class EAsyncWriteOptions
{
	None = 0,
	WriteFileToDisk = 0x01,
	ComputeHash = 0x02
};
ENUM_CLASS_FLAGS(EAsyncWriteOptions)

struct FScopedSavingFlag
{
	FScopedSavingFlag(bool InSavingConcurrent);
	~FScopedSavingFlag();

	bool bSavingConcurrent;
};

struct FSavePackageDiffSettings
{
	int32 MaxDiffsToLog;
	bool bIgnoreHeaderDiffs;
	bool bSaveForDiff;
	FSavePackageDiffSettings(bool bDiffing);
};

struct FCanSkipEditorReferencedPackagesWhenCooking
{
	bool bCanSkipEditorReferencedPackagesWhenCooking;
	FCanSkipEditorReferencedPackagesWhenCooking();
	FORCEINLINE operator bool() const { return bCanSkipEditorReferencedPackagesWhenCooking; }
};

/**
 * Helper structure to encapsulate sorting a linker's export table alphabetically, taking into account conforming to other linkers.
 * @note Save2 should not have to use this sorting long term
 */
struct FObjectExportSortHelper
{
private:
	struct FObjectFullName
	{
	public:
		FObjectFullName(const UObject* Object, const UObject* Root);
		FObjectFullName(FObjectFullName&& InFullName);

		FName ClassName;
		TArray<FName> Path;
	};

public:
	FObjectExportSortHelper() : bUseFObjectFullName(false) {}

	/**
	 * Sorts exports alphabetically.  If a package is specified to be conformed against, ensures that the order
	 * of the exports match the order in which the corresponding exports occur in the old package.
	 *
	 * @param	Linker				linker containing the exports that need to be sorted
	 * @param	LinkerToConformTo	optional linker to conform against.
	 */
	void SortExports(FLinkerSave* Linker, FLinkerLoad* LinkerToConformTo = nullptr, bool InbUseFObjectFullName = false);

private:
	/** Comparison function used by Sort */
	bool operator()(const FObjectExport& A, const FObjectExport& B) const;

	/** the linker that we're sorting exports for */
	friend struct TDereferenceWrapper<FObjectExport, FObjectExportSortHelper>;

	bool bUseFObjectFullName;

	TMap<UObject*, FObjectFullName> ObjectToObjectFullNameMap;

	/**
	 * Map of UObject => full name; optimization for sorting.
	 */
	TMap<UObject*, FString>			ObjectToFullNameMap;
};

/**
 * Helper struct used during cooking to validate EDL dependencies
 */
struct FEDLCookChecker : public TThreadSingleton<FEDLCookChecker>
{
	friend TThreadSingleton<FEDLCookChecker>;

	struct FEDLNodeID
	{
		TArray<FName> ObjectPath;
		bool bDepIsSerialize;

		FEDLNodeID();
		FEDLNodeID(UObject* DepObject, bool bInDepIsSerialize);

		bool operator==(const FEDLNodeID& Other) const;

		FString ToString() const;

		friend uint32 GetTypeHash(const FEDLNodeID& A);
		
	};

	static FCriticalSection CookCheckerInstanceCritical;
	static TArray<FEDLCookChecker*> CookCheckerInstances;

	bool bIsActive;
	TMultiMap<FEDLNodeID, FName> ImportToImportingPackage;
	TSet<FEDLNodeID> Exports;
	TMultiMap<FEDLNodeID, FEDLNodeID> NodePrereqs;

	FEDLCookChecker();

	void SetActiveIfNeeded();

	void Reset();

	void AddImport(UObject* Import, UPackage* ImportingPackage);
	void AddExport(UObject* Export);
	void AddArc(UObject* DepObject, bool bDepIsSerialize, UObject* Export, bool bExportIsSerialize);

	static void StartSavingEDLCookInfoForVerification();
	static bool CheckForCyclesInner(TMultiMap<FEDLNodeID, FEDLNodeID>& NodePrereqs, TSet<FEDLNodeID>& Visited, TSet<FEDLNodeID>& Stack, const FEDLNodeID& Visit, FEDLNodeID& FailNode);
	static void Verify(bool bFullReferencesExpected);
};

#if WITH_EDITORONLY_DATA

/**
 * Archive to calculate a checksum on an object's serialized data stream, but only of its non-editor properties.
 */
class FArchiveObjectCrc32NonEditorProperties : public FArchiveObjectCrc32
{
	using Super = FArchiveObjectCrc32;

public:
	FArchiveObjectCrc32NonEditorProperties()
		: EditorOnlyProp(0)
	{
	}

	virtual FString GetArchiveName() const
	{
		return TEXT("FArchiveObjectCrc32NonEditorProperties");
	}

	virtual void Serialize(void* Data, int64 Length);
private:
	int32 EditorOnlyProp;
};

#else

class COREUOBJECT_API FArchiveObjectCrc32NonEditorProperties : public FArchiveObjectCrc32
{
};

#endif

// Utility functions used by both UPackage::Save and/or UPackage::Save2
namespace SavePackageUtilities
{
	extern const FName NAME_World;
	extern const FName NAME_Level;
	extern const FName NAME_PrestreamPackage;

	void GetBlueprintNativeCodeGenReplacement(UObject* InObj, UClass*& ObjClass, UObject*& ObjOuter, FName& ObjName, const ITargetPlatform* TargetPlatform);

	void IncrementOutstandingAsyncWrites();
	void DecrementOutstandingAsyncWrites();

	void SaveThumbnails(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FSlot Slot);
	void SaveBulkData(FLinkerSave* Linker, const UPackage* InOuter, const TCHAR* Filename, const ITargetPlatform* TargetPlatform,
		FSavePackageContext* SavePackageContext, const bool bTextFormat, const bool bDiffing, const bool bComputeHash, TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, int64& TotalPackageSizeUncompressed);
	void SaveWorldLevelInfo(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FRecord Record);
	EObjectMark GetExcludedObjectMarksForTargetPlatform(const class ITargetPlatform* TargetPlatform);
	bool HasUnsaveableOuter(UObject* InObj, UPackage* InSavingPackage);
	void CheckObjectPriorToSave(FArchiveUObject& Ar, UObject* InObj, UPackage* InSavingPackage);
	void ConditionallyExcludeObjectForTarget(UObject* Obj, EObjectMark ExcludedObjectMarks, const ITargetPlatform* TargetPlatform);
	void FindMostLikelyCulprit(TArray<UObject*> BadObjects, UObject*& MostLikelyCulprit, const FProperty*& PropertyRef);
	void AddFileToHash(FString const& Filename, FMD5& Hash);

	void WriteToFile(const FString& Filename, const uint8* InDataPtr, int64 InDataSize);
	void AsyncWriteFile(TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, FLargeMemoryPtr Data, const int64 DataSize, const TCHAR* Filename, EAsyncWriteOptions Options, TArrayView<const FFileRegion> InFileRegions);
	void AsyncWriteFileWithSplitExports(TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, FLargeMemoryPtr Data, const int64 DataSize, const int64 HeaderSize, const TCHAR* Filename, EAsyncWriteOptions Options, TArrayView<const FFileRegion> InFileRegions);

	void GetCDOSubobjects(UObject* CDO, TArray<UObject*>& Subobjects);
}

#if ENABLE_COOK_STATS
struct FSavePackageStats
{
	static int32 NumPackagesSaved;
	static double SavePackageTimeSec;
	static double TagPackageExportsPresaveTimeSec;
	static double TagPackageExportsTimeSec;
	static double FullyLoadLoadersTimeSec;
	static double ResetLoadersTimeSec;
	static double TagPackageExportsGetObjectsWithOuter;
	static double TagPackageExportsGetObjectsWithMarks;
	static double SerializeImportsTimeSec;
	static double SortExportsSeekfreeInnerTimeSec;
	static double SerializeExportsTimeSec;
	static double SerializeBulkDataTimeSec;
	static double AsyncWriteTimeSec;
	static double MBWritten;
	static TMap<FName, FArchiveDiffStats> PackageDiffStats;
	static int32 NumberOfDifferentPackages;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats;
	static void AddSavePackageStats(FCookStatsManager::AddStatFuncRef AddStat);
	static void MergeStats(const TMap<FName, FArchiveDiffStats>& ToMerge);
};
#endif

