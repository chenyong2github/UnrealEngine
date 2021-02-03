// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDataGatherer.h"
#include "AssetDataGathererPrivate.h"

#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistryArchive.h"
#include "AssetRegistryPrivate.h"
#include "Async/ParallelFor.h"
#include "Containers/BinaryHeap.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Math/NumericLimits.h"
#include "Misc/AsciiSet.h"
#include "Misc/Char.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeExit.h"
#include "PackageReader.h"
#include "Serialization/Archive.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"

namespace AssetDataGathererConstants
{
	constexpr int32 SingleThreadFilesPerBatch = 3;
	constexpr int32 ExpectedMaxBatchSize = 100;
	constexpr int32 MinSecondsToElapseBeforeCacheWrite = 60;
	static constexpr uint32 CacheSerializationMagic = 0xCBA78339; 
}

namespace UE
{
namespace AssetDataGather
{
namespace Private
{

/** InOutResult = Value, but without shrinking the string to fit. */
void AssignStringWithoutShrinking(FString& InOutResult, FStringView Value)
{
	TArray<TCHAR>& Result = InOutResult.GetCharArray();
	if (Value.IsEmpty())
	{
		Result.Reset();
	}
	else
	{
		Result.SetNumUninitialized(Value.Len() + 1, false /* bAllowShrinking */);
		FMemory::Memcpy(Result.GetData(), Value.GetData(), Value.Len() * sizeof(Value[0]));
		Result[Value.Len()] = '\0';
	}
}

/** Adapter to allow us to use a lambda for IterateDirectoryStat */
class FLambdaDirectoryStatVisitor : public IPlatformFile::FDirectoryStatVisitor
{
public:
	typedef TFunctionRef<bool(const TCHAR*, const FFileStatData&)> FLambdaRef;
	FLambdaRef Callback;
	explicit FLambdaDirectoryStatVisitor(FLambdaRef InCallback)
		: Callback(MoveTemp(InCallback))
	{
	}
	virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData) override
	{
		return Callback(FilenameOrDirectory, StatData);
	}
};

FDiscoveredPathData::FDiscoveredPathData(FStringView InLocalAbsPath, FStringView InLongPackageName, FStringView InRelPath, const FDateTime& InPackageTimestamp)
	: LocalAbsPath(InLocalAbsPath)
	, LongPackageName(InLongPackageName)
	, RelPath(InRelPath)
	, PackageTimestamp(InPackageTimestamp)
{
}

FDiscoveredPathData::FDiscoveredPathData(FStringView InLocalAbsPath, FStringView InLongPackageName, FStringView InRelPath)
	: LocalAbsPath(InLocalAbsPath)
	, LongPackageName(InLongPackageName)
	, RelPath(InRelPath)
{
}

void FDiscoveredPathData::Assign(FStringView InLocalAbsPath, FStringView InLongPackageName, FStringView InRelPath)
{
	AssignStringWithoutShrinking(LocalAbsPath, InLocalAbsPath);
	AssignStringWithoutShrinking(LongPackageName, InLongPackageName);
	AssignStringWithoutShrinking(RelPath, InRelPath);
}

void FDiscoveredPathData::Assign(FStringView InLocalAbsPath, FStringView InLongPackageName, FStringView InRelPath, const FDateTime& InPackageTimestamp)
{
	Assign(InLocalAbsPath, InLongPackageName, InRelPath);
	PackageTimestamp = InPackageTimestamp;
}

uint32 FDiscoveredPathData::GetAllocatedSize() const
{
	return LocalAbsPath.GetAllocatedSize() + LongPackageName.GetAllocatedSize() + RelPath.GetAllocatedSize();
}

FGatheredPathData::FGatheredPathData(FStringView InLocalAbsPath, FStringView InLongPackageName, const FDateTime& InPackageTimestamp)
	: LocalAbsPath(InLocalAbsPath)
	, LongPackageName(InLongPackageName)
	, PackageTimestamp(InPackageTimestamp)
{
}

FGatheredPathData::FGatheredPathData(const FDiscoveredPathData& DiscoveredData)
	:FGatheredPathData(DiscoveredData.LocalAbsPath, DiscoveredData.LongPackageName, DiscoveredData.PackageTimestamp)
{
}

void FGatheredPathData::Assign(FStringView InLocalAbsPath, FStringView InLongPackageName, const FDateTime& InPackageTimestamp)
{
	AssignStringWithoutShrinking(LocalAbsPath, InLocalAbsPath);
	AssignStringWithoutShrinking(LongPackageName, InLongPackageName);
	PackageTimestamp = InPackageTimestamp;
}

void FGatheredPathData::Assign(const FDiscoveredPathData& DiscoveredData)
{
	Assign(DiscoveredData.LocalAbsPath, DiscoveredData.LongPackageName, DiscoveredData.PackageTimestamp);
}

uint32 FGatheredPathData::GetAllocatedSize() const
{
	return LocalAbsPath.GetAllocatedSize() + LongPackageName.GetAllocatedSize();
}

FScanDir::FScanDir(FMountDir& InMountDir, FScanDir* InParent, FStringView InRelPath)
	: MountDir(&InMountDir)
	, Parent(InParent)
	, RelPath(InRelPath)
{
	InMountDir.GetDiscovery().NumDirectoriesToScan.Increment();
}

FScanDir::~FScanDir()
{
	// Assert that Shutdown has been called to confirm that the parent no longer has a reference we need to clear.
	check(!MountDir);
}

void FScanDir::Shutdown()
{
	if (!MountDir)
	{
		// Already shutdown
		return;
	}

	// Shutdown all children
	for (TRefCountPtr<FScanDir>& ScanDir : SubDirs)
	{
		// Destruction contract for FScanDir requires that the parent calls Shutdown before dropping the reference
		ScanDir->Shutdown();
		ScanDir.SafeRelease();
	}
	SubDirs.Empty();

	// Update MountDir data that we influence
	if (!bIsComplete)
	{
		MountDir->GetDiscovery().NumDirectoriesToScan.Decrement();
	}

	// Update Parent data that we influence
	if (Parent) // Root ScanDir has no parent
	{
		if (AccumulatedPriority != EPriority::Normal)
		{
			Parent->OnChildPriorityChanged(AccumulatedPriority, -1);
		}
	}

	// Clear backpointers (which also marks us as shutdown)
	MountDir = nullptr;
	Parent = nullptr;
}

bool FScanDir::IsValid() const
{
	return MountDir != nullptr;
}

FMountDir* FScanDir::GetMountDir() const
{
	return MountDir;
}

FStringView FScanDir::GetRelPath() const
{
	return RelPath;
}

EPriority FScanDir::GetPriority() const
{
	return AccumulatedPriority;
}

void FScanDir::AppendLocalAbsPath(FStringBuilderBase& OutFullPath) const
{
	if (!MountDir)
	{
		return;
	}

	if (Parent)
	{
		Parent->AppendLocalAbsPath(OutFullPath);
		FPathViews::AppendPath(OutFullPath, RelPath);
	}
	else
	{
		// The root ScanDir should have an empty RelPath from the MountDir
		check(RelPath.IsEmpty());
		OutFullPath << MountDir->GetLocalAbsPath();
	}
}

FString FScanDir::GetLocalAbsPath() const
{
	TStringBuilder<128> Result;
	AppendLocalAbsPath(Result);
	return FString(Result);
}

void FScanDir::AppendMountRelPath(FStringBuilderBase& OutRelPath) const
{
	if (!MountDir)
	{
		return;
	}

	if (Parent)
	{
		Parent->AppendMountRelPath(OutRelPath);
		FPathViews::AppendPath(OutRelPath, RelPath);
	}
	else
	{
		// The root ScanDir should have an empty RelPath from the MountDir
		check(RelPath.IsEmpty());
	}
}

FString FScanDir::GetMountRelPath() const
{
	TStringBuilder<128> Result;
	AppendMountRelPath(Result);
	return FString(Result);
}

bool FScanDir::IsBlacklisted() const
{
	if (!MountDir)
	{
		return false;
	}

	const TSet<FString>& Blacklist = MountDir->GetBlacklist();
	if (Blacklist.Num())
	{
		TStringBuilder<128> MountRelPath;
		AppendMountRelPath(MountRelPath);
		FStringView MountRelPathSV(MountRelPath);
		if (Blacklist.ContainsByHash(GetTypeHash(MountRelPathSV), MountRelPathSV))
		{
			return true;
		}
	}
	return false;
}

bool FScanDir::IsDirectWhitelisted() const
{
	return bIsDirectWhitelisted;
}

bool FScanDir::IsRecursiveMonitored(bool bParentIsWhitelisted) const
{
	if (!MountDir)
	{
		return false;
	}
	if (!bParentIsWhitelisted && !bIsDirectWhitelisted)
	{
		return false;
	}
	if (IsBlacklisted())
	{
		return false;
	}
	return true;
}

bool FScanDir::IsPathWhitelisted(FStringView InRelPath, bool bParentIsWhitelisted) const
{
	bool bIsRecursiveWhitelisted = bParentIsWhitelisted || bIsDirectWhitelisted;
	if (bIsRecursiveWhitelisted)
	{
		return true;
	}
	const FScanDir* SubDir = nullptr;
	if (!InRelPath.IsEmpty())
	{
		FStringView FirstComponent;
		FPathViews::SplitFirstComponent(InRelPath, FirstComponent, InRelPath);
		SubDir = FindSubDir(FirstComponent);
	}
	if (!SubDir)
	{
		return false;
	}
	return SubDir->IsPathWhitelisted(InRelPath, false /* bParentIsWhitelisted */);
}

bool FScanDir::ShouldScan(bool bParentIsWhitelisted) const
{
	return !bHasScanned && IsRecursiveMonitored(bParentIsWhitelisted);
}

bool FScanDir::HasScanned() const
{
	return bHasScanned;
}

bool FScanDir::IsComplete() const
{
	return bIsComplete;
}

uint32 FScanDir::GetAllocatedSize() const
{
	uint32 Result = 0;
	Result += SubDirs.GetAllocatedSize();
	for (const TRefCountPtr<FScanDir>& Value : SubDirs)
	{
		Result += sizeof(*Value);
		Result += Value->GetAllocatedSize();
	}
	Result += AlreadyScannedFiles.GetAllocatedSize();
	for (const FString& Value : AlreadyScannedFiles)
	{
		Result += Value.GetAllocatedSize();
	}
	Result += RelPath.GetAllocatedSize();
	return Result;
}

FScanDir* FScanDir::GetControllingDir(FStringView InRelPath, bool bIsDirectory, bool bParentIsWhitelisted, bool& bOutIsWhitelisted, FString& OutRelPath)
{
	// GetControllingDir can only be called on valid ScanDirs, which we rely on since we need to call FindOrAddSubDir which relies on that
	check(IsValid());

	bool bIsWhitelisted = bParentIsWhitelisted || bIsDirectWhitelisted;
	if (InRelPath.IsEmpty())
	{
		if (!bIsDirectory)
		{
			UE_LOG(LogAssetRegistry, Warning, TEXT("GetControllingDir called on %s with !bIsDirectory, but we have it recorded as a directory. Returning null."), *GetLocalAbsPath());
			bOutIsWhitelisted = false;
			OutRelPath.Reset();
			return nullptr;
		}
		else
		{
			bOutIsWhitelisted = bIsWhitelisted;
			OutRelPath = InRelPath;
			return this;
		}
	}

	FStringView FirstComponent;
	FStringView RemainingPath;
	FPathViews::SplitFirstComponent(InRelPath, FirstComponent, RemainingPath);
	if (RemainingPath.IsEmpty() && !bIsDirectory)
	{
		bOutIsWhitelisted = bIsWhitelisted;
		OutRelPath = InRelPath;
		return this;
	}
	else
	{
		FScanDir* SubDir = nullptr;
		if (ShouldScan(bParentIsWhitelisted))
		{
			SubDir = &FindOrAddSubDir(FirstComponent);
		}
		else
		{
			SubDir = FindSubDir(FirstComponent);
			if (!SubDir)
			{
				bOutIsWhitelisted = bIsWhitelisted;
				OutRelPath = InRelPath;
				return this;
			}
		}
		return SubDir->GetControllingDir(RemainingPath, bIsDirectory, bIsWhitelisted, bOutIsWhitelisted, OutRelPath);
	}
}

bool FScanDir::TrySetDirectoryProperties(FStringView InRelPath, const FSetPathProperties& InProperties, bool bConfirmedExists)
{
	// TrySetDirectoryProperties can only be called on valid ScanDirs, which we rely on so we can call FindOrAddSubDir which requires that
	check(IsValid()); 

	SetComplete(false);
	if (InRelPath.IsEmpty())
	{
		// The properties apply to this entire directory
		if (InProperties.IsWhitelisted.IsSet() && bIsDirectWhitelisted != *InProperties.IsWhitelisted)
		{
			if (bScanInFlight)
			{
				bScanInFlightInvalidated = true;
			}
			bIsDirectWhitelisted = *InProperties.IsWhitelisted;

			if (bIsDirectWhitelisted)
			{
				// Since we are setting this directory to be monitored, we need to implement the guarantee that all Monitored flags of its children are set to false
				// We also need to SetComplete false on all directories in between this and a previously whitelisted directory, since those non-whitelisted parent directories
				// marked themselves complete once their whitelisted children finished
				ForEachDescendent([](FScanDir& ScanDir)
					{
						ScanDir.bIsDirectWhitelisted = false;
						ScanDir.SetComplete(false);
					});
			}
			else
			{
				// Cancel any scans since they are no longer whitelisted
				ForEachDescendent([](FScanDir& ScanDir)
					{
						if (ScanDir.bScanInFlight)
						{
							ScanDir.bScanInFlightInvalidated = true;
						}
					});
			}
		}
		if (InProperties.HasScanned.IsSet())
		{
			bool bNewValue = *InProperties.HasScanned;
			auto SetProperties = [bNewValue](FScanDir& ScanDir)
			{
				if (ScanDir.bScanInFlight)
				{
					ScanDir.bScanInFlightInvalidated = true;
				}
				ScanDir.bHasScanned = bNewValue;
				ScanDir.AlreadyScannedFiles.Reset();
			};
			SetProperties(*this);
			ForEachDescendent(SetProperties);
		}
		if (InProperties.Priority.IsSet() && DirectPriority != *InProperties.Priority)
		{
			SetDirectPriority(*InProperties.Priority);
		}
		// InProperties.IgnoreBlacklist does not require an action on ScanDirs; it is implemented on the MountDir level
		return true;
	}
	else
	{
		TOptional<FSetPathProperties> ModifiedProperties;
		const FSetPathProperties* Properties = &InProperties;
		if (Properties->IsWhitelisted.IsSet())
		{
			if (bIsDirectWhitelisted)
			{
				// If this directory is set to be monitored, all Monitored flags of its children are unused, are guaranteed set to false, and should not be changed
				ModifiedProperties = *Properties;
				ModifiedProperties->IsWhitelisted.Reset();
				if (!ModifiedProperties->IsSet())
				{
					return false;
				}
				Properties = &ModifiedProperties.GetValue();
			}
		}

		FStringView FirstComponent;
		FStringView Remainder;
		FPathViews::SplitFirstComponent(InRelPath, FirstComponent, Remainder);

		FScanDir* SubDir = nullptr;
		if (bHasScanned &&
			(!Properties->HasScanned.IsSet() || *Properties->HasScanned == true) &&
			(!Properties->IsWhitelisted.IsSet()))
		{
			// If this parent directory has already been scanned and we are not changing the target directory's has-been-scanned value,
			// and the next child subdirectory does not exist, then the child directory has already been scanned and we do not need to set the properties on it.
			// Therefore call FindSubDir instead of FindOrAddSubDir, and abort if we do not find it
			SubDir = FindSubDir(FirstComponent);
			if (!SubDir)
			{
				return false;
			}
		}
		else
		{
			SubDir = FindSubDir(FirstComponent);
			if (!SubDir)
			{
				if (!bConfirmedExists)
				{
					TStringBuilder<256> LocalAbsPath;
					AppendLocalAbsPath(LocalAbsPath);
					FPathViews::AppendPath(LocalAbsPath, InRelPath);
					FFileStatData StatData = IFileManager::Get().GetStatData(LocalAbsPath.ToString());
					if (!StatData.bIsValid || !StatData.bIsDirectory)
					{
						UE_LOG(LogAssetRegistry, Warning, TEXT("SetDirectoryProperties called on %s path %.*s. Ignoring the call."),
							StatData.bIsValid ? TEXT("file") : TEXT("non-existent"), LocalAbsPath.Len(), LocalAbsPath.GetData());
						return false;
					}
					bConfirmedExists = true;
				}
				SubDir = &FindOrAddSubDir(FirstComponent);
			}
		}
		return SubDir->TrySetDirectoryProperties(Remainder, *Properties, bConfirmedExists);
	}
}

void FScanDir::MarkFileAlreadyScanned(FStringView BaseName)
{
	if (bHasScanned)
	{
		return;
	}
	check(FPathViews::IsPathLeaf(BaseName));
	for (const FString& AlreadyScannedFile : AlreadyScannedFiles)
	{
		if (FStringView(AlreadyScannedFile).Equals(BaseName, ESearchCase::IgnoreCase))
		{
			return;
		}
	}
	AlreadyScannedFiles.Emplace(BaseName);
}

void FScanDir::SetDirectPriority(EPriority InPriority)
{
	DirectPriority = InPriority;
	UpdateAccumulatedPriority();
}

void FScanDir::UpdateAccumulatedPriority()
{
	uint32 LocalAccumulated = static_cast<uint32>(DirectPriority);
	for (uint32 PriorityLevel = 0; PriorityLevel < CountEPriority; ++PriorityLevel)
	{
		if (PriorityRefCounts[PriorityLevel] > 0 && PriorityLevel < LocalAccumulated)
		{
			LocalAccumulated = PriorityLevel;
		}
	}

	EPriority LocalEPriority = static_cast<EPriority>(LocalAccumulated);
	if (LocalEPriority != AccumulatedPriority)
	{
		if (Parent)
		{
			if (AccumulatedPriority != EPriority::Normal)
			{
				Parent->OnChildPriorityChanged(AccumulatedPriority, -1);
			}
			if (LocalEPriority != EPriority::Normal)
			{
				Parent->OnChildPriorityChanged(LocalEPriority, 1);
			}
		}
		AccumulatedPriority = LocalEPriority;
	}
}

void FScanDir::OnChildPriorityChanged(EPriority InPriority, int32 Delta)
{
	check(-(int32)TNumericLimits<uint8>::Max() < Delta && Delta < TNumericLimits<uint8>::Max());
	uint8& PriorityRefCount = PriorityRefCounts[static_cast<uint32>(InPriority)];
	check(Delta > 0 || PriorityRefCount >= -Delta);

	if (Delta > 0 && PriorityRefCount >= TNumericLimits<uint8>::Max() - Delta)
	{
		// Mark that the count is now stuck
		PriorityRefCount = TNumericLimits<uint8>::Max();
	}
	else if (Delta < 0 && PriorityRefCount == TNumericLimits<uint8>::Max())
	{
		// The count is stuck, do not decrement it
	}
	else
	{
		PriorityRefCount += Delta;
	}
	UpdateAccumulatedPriority();
}

void FScanDir::SetScanResults(FStringView LocalAbsPath, TArrayView<FDiscoveredPathData>& InOutSubDirs, TArrayView<FDiscoveredPathData>& InOutFiles)
{
	// Note that by contract SetScanResults is only called on Paths with ShouldScan == true, so we do not need to check IsWhitelisted for the files in this directory or for any of its subdirs
	// We do still need to check IsBlacklisted for subdirs, since the blacklist can be true for a subdirectory even if not true for the parent
	SetComplete(false);
	check(!bScanInFlightInvalidated);
	check(MountDir);

	if (!ensure(!bHasScanned))
	{
		return;
	}

	// Add SubDirectories in the tree for the directories found by the scan, and report the directories as discovered directory paths as well
	// Remove any SubDirectories from the tree that were previously present but are not in the latest scan results
	TSet<FScanDir*> SubDirsToRemove;
	ForEachSubDir([&SubDirsToRemove](FScanDir& SubScanDir) { SubDirsToRemove.Add(&SubScanDir); });
	for (int32 Index = 0; Index < InOutSubDirs.Num(); )
	{
		FDiscoveredPathData& SubDirPath = InOutSubDirs[Index];
		bool bReportResult = false;
		if (!MountDir->IsBlacklisted(SubDirPath.LocalAbsPath))
		{
			FScanDir& SubScanDir = FindOrAddSubDir(SubDirPath.RelPath);
			SubDirsToRemove.Remove(&SubScanDir);
			bReportResult = MountDir->GetDiscovery().ShouldDirBeReported(SubDirPath.LongPackageName);
		}
		if (!bReportResult)
		{
			Swap(SubDirPath, InOutSubDirs.Last());
			InOutSubDirs = InOutSubDirs.Slice(0, InOutSubDirs.Num() - 1);
		}
		else
		{
			++Index;
		}
	}
	for (FScanDir* ScanDir : SubDirsToRemove)
	{
		RemoveSubDir(ScanDir->GetRelPath());
	}

	// Add the files that were found in the scan, skipping any files that have already been scanned
	if (InOutFiles.Num())
	{
		auto IsAlreadyScanned = [this, &LocalAbsPath](const FDiscoveredPathData& InFile)
		{
			return Algo::AnyOf(AlreadyScannedFiles, [&InFile](const FString& AlreadyScannedFileRelPath) { return FPathViews::Equals(AlreadyScannedFileRelPath, InFile.RelPath); });
		};
		bool bScanAll = AlreadyScannedFiles.Num() == 0;
		for (int32 Index = 0; Index < InOutFiles.Num(); )
		{
			FDiscoveredPathData& InFile = InOutFiles[Index];
			if (!bScanAll && IsAlreadyScanned(InFile))
			{
				// Remove this file from InOutFiles
				Swap(InFile, InOutFiles.Last());
				InOutFiles = InOutFiles.Slice(0, InOutFiles.Num() - 1);
			}
			else
			{
				++Index;
			}
		}
	}
	AlreadyScannedFiles.Empty();

	MountDir->SetHasStartedScanning();
	bHasScanned = true;
}

void FScanDir::Update(FScanDir*& OutCursor, bool& bInOutParentIsWhitelisted)
{
	check(MountDir);
	if (bIsComplete)
	{
		return;
	}
	if (ShouldScan(bInOutParentIsWhitelisted))
	{
		OutCursor = this;
		return;
	}

	if (SubDirs.Num())
	{
		FScanDir* SubDirToScan = FindHighestPrioritySubDir();
		if (SubDirToScan)
		{
			OutCursor = SubDirToScan;
			bInOutParentIsWhitelisted = bInOutParentIsWhitelisted || bIsDirectWhitelisted;
			return;
		}
	}
	SetComplete(true);

	OutCursor = Parent; // Note this will be null for the root ScanDir
	if (!Parent)
	{
		bInOutParentIsWhitelisted = false;
	}
	else if (Parent->bIsDirectWhitelisted)
	{
		// We have a contract bIsDirectWhitelisted is only set on the highest-level directory to monitor and applies to all directories under it.
		// So we only need to change bInOutParentIsWhitelisted from true to false when we move up the tree into a parent directory with bIsDirectWhitelisted = true.
		check(!this->bIsDirectWhitelisted); // Verify the contract that children below the ScanDir with bIsDirectWhitelisted = true have bIsDirectWhitelisted = false
		check(!Parent->Parent || !Parent->Parent->bIsDirectWhitelisted); // Verify the contract that the parent above the ScanDir with bIsDirectWhitelisted = true has bIsDirectWhitelisted = false
		check(bInOutParentIsWhitelisted); // Verify that the original bInOutParentIsWhitelisted was set to true; it should have been since the parent's bIsDirectWhitelisted is true
		bInOutParentIsWhitelisted = false;
	}
}

bool FScanDir::IsScanInFlight() const
{
	return bScanInFlight;
}

void FScanDir::SetScanInFlight(bool bInScanInFlight)
{
	bScanInFlight = bInScanInFlight;
}

bool FScanDir::IsScanInFlightInvalidated() const
{
	return bScanInFlightInvalidated;
}

void FScanDir::SetScanInFlightInvalidated(bool bInvalidated)
{
	bScanInFlightInvalidated = bInvalidated;
}

void FScanDir::MarkDirty(bool bMarkDescendents)
{
	if (bMarkDescendents)
	{
		ForEachDescendent([](FScanDir& Descendent) { Descendent.SetComplete(false); });
	}
	FScanDir* Current = this;
	while (Current)
	{
		Current->SetComplete(false);
		Current = Current->Parent;
	}
}

void FScanDir::Shrink()
{
	ForEachSubDir([](FScanDir& SubDir) {SubDir.Shrink(); });
	SubDirs.Shrink();
	AlreadyScannedFiles.Shrink();
}


void FScanDir::SetComplete(bool bInIsComplete)
{
	if (!MountDir || bIsComplete == bInIsComplete)
	{
		return;
	}

	bIsComplete = bInIsComplete;
	if (bIsComplete)
	{
		MountDir->GetDiscovery().NumDirectoriesToScan.Decrement();
		// If we were given a priority, remove it when we complete
		SetDirectPriority(EPriority::Normal);
		// All subDirs are complete, so all of their priorities should be set back to normal, so we can unstick any stuck priorities now by setting them all to 0
#if DO_CHECK
		bool bHasPriority = false;
		ForEachSubDir([&bHasPriority](FScanDir& SubDir) { if (SubDir.GetPriority() != EPriority::Normal) { bHasPriority = true; }});
		if (bHasPriority)
		{
			UE_LOG(LogAssetRegistry, Warning, TEXT("ScanDir %s is marked complete, but it has subdirectories with still-set priorities."), *GetLocalAbsPath());
		}
		else
#endif
		{
			bool bModifiedRefCount = false;
			for (uint8& PriorityRefCount : PriorityRefCounts)
			{
				bModifiedRefCount = bModifiedRefCount | (PriorityRefCount != 0);
				PriorityRefCount = 0;
			}
			if (bModifiedRefCount)
			{
				UpdateAccumulatedPriority();
			}
		}
		// Upon completion, subdirs that do not need to be maintained are deleted, which is done by removing them from the parent
		// ScanDirs need to be maintained if they are the root, or are whitelisted, or have child ScanDirs that need to be maintained.
		if (Parent != nullptr && !bIsDirectWhitelisted && SubDirs.IsEmpty())
		{
			Parent->RemoveSubDir(GetRelPath());
			// *this is Shutdown (e.g. Parent is now null) and it may also have been deallocated
			return;
		}
	}
	else
	{
		MountDir->GetDiscovery().NumDirectoriesToScan.Increment();
	}
}

FScanDir* FScanDir::FindSubDir(FStringView SubDirBaseName)
{
	int32 Index = FindLowerBoundSubDir(SubDirBaseName);
	if (Index == SubDirs.Num() || !FPathViews::Equals(SubDirs[Index]->GetRelPath(), SubDirBaseName))
	{
		return nullptr;
	}
	else
	{
		return SubDirs[Index].GetReference();
	}
}

const FScanDir* FScanDir::FindSubDir(FStringView SubDirBaseName) const
{
	return const_cast<FScanDir*>(this)->FindSubDir(SubDirBaseName);
}

FScanDir& FScanDir::FindOrAddSubDir(FStringView SubDirBaseName)
{
	// FindOrAddSubDir is only allowed to be called on valid FScanDirs, which we rely on since we need a non-null MountDir which valid ScanDirs have
	check(MountDir != nullptr);

	int32 Index = FindLowerBoundSubDir(SubDirBaseName);
	if (Index == SubDirs.Num() || !FPathViews::Equals(SubDirs[Index]->GetRelPath(), SubDirBaseName))
	{
		return *SubDirs.EmplaceAt_GetRef(Index, new FScanDir(*MountDir, this, SubDirBaseName));
	}
	else
	{
		return *SubDirs[Index];
	}
}

void FScanDir::RemoveSubDir(FStringView SubDirBaseName)
{
	int32 Index = FindLowerBoundSubDir(SubDirBaseName);
	if (Index < SubDirs.Num() && FPathViews::Equals(SubDirs[Index]->GetRelPath(), SubDirBaseName))
	{
		// Destruction contract for FScanDir requires that the parent calls Shutdown before dropping the reference
		SubDirs[Index]->Shutdown();
		SubDirs.RemoveAt(Index);
	}
}

int32 FScanDir::FindLowerBoundSubDir(FStringView SubDirBaseName)
{
	return Algo::LowerBound(SubDirs, SubDirBaseName,
		[](const TRefCountPtr<FScanDir>& SubDir, FStringView BaseName)
		{
			return FPathViews::Less(SubDir->GetRelPath(), BaseName);
		}
	);
}

FScanDir* FScanDir::FindHighestPrioritySubDir()
{
	if (SubDirs.Num() == 0)
	{
		return nullptr;
	}

	FScanDir* WinningSubDir = nullptr;
	EPriority WinningPriority = EPriority::Normal;

	for (const TRefCountPtr<FScanDir>& SubDir : SubDirs)
	{
		if (SubDir->bIsComplete)
		{
			continue;
		}
		if (WinningSubDir == nullptr || SubDir->AccumulatedPriority < WinningPriority)
		{
			WinningSubDir = SubDir.GetReference();
			WinningPriority = SubDir->AccumulatedPriority;
		}
	}
	return WinningSubDir;
}

template <typename CallbackType>
void FScanDir::ForEachSubDir(const CallbackType& Callback)
{
	for (TRefCountPtr<FScanDir>& Ptr : SubDirs)
	{
		Callback(*Ptr);
	}
}

/** Depth-first-search traversal of all descedent subdirs under this (not including this). Callback is called on parents before children. */
template <typename CallbackType>
void FScanDir::ForEachDescendent(const CallbackType& Callback)
{
	TArray<TPair<FScanDir*, int32>, TInlineAllocator<10>> Stack; // 10 chosen arbitrarily as a depth that is greater than most of our content root directory tree depths
	Stack.Add(TPair<FScanDir*, int32>(this, 0));
	while (Stack.Num())
	{
		TPair<FScanDir*, int32>& Top = Stack.Last();
		FScanDir* ParentOnStack = Top.Get<0>();
		int32& NextIndex = Top.Get<1>();
		if (NextIndex == ParentOnStack->SubDirs.Num())
		{
			Stack.SetNum(Stack.Num() - 1, false /* bAllowShrinking */);
			continue;
		}
		FScanDir* Child = ParentOnStack->SubDirs[NextIndex++];
		Callback(*Child);
		Stack.Add(TPair<FScanDir*, int32>(Child, 0));
	}
}

FMountDir::FMountDir(FAssetDataDiscovery& InDiscovery, FStringView InLocalAbsPath, FStringView InLongPackageName)
	: LocalAbsPath(InLocalAbsPath)
	, LongPackageName(InLongPackageName)
	, Discovery(InDiscovery)
{
	Root = new FScanDir(*this, nullptr, FStringView());
	UpdateBlacklist();
}

FMountDir::~FMountDir()
{
	// ScanDir's destruction contract requires that the parent calls Shutdown on it before dropping the reference
	Root->Shutdown();
	Root.SafeRelease();
}

FStringView FMountDir::GetLocalAbsPath() const
{
	return LocalAbsPath;
}

FStringView FMountDir::GetLongPackageName() const
{
	return LongPackageName;
}

const TSet<FString>& FMountDir::GetBlacklist() const
{
	return BlacklistedRelPaths;
}

FAssetDataDiscovery& FMountDir::GetDiscovery() const
{
	return Discovery;
}

FScanDir* FMountDir::GetControllingDir(FStringView InLocalAbsPath, bool bIsDirectory, bool& bOutIsWhitelisted, FString& OutRelPath)
{
	FStringView RemainingPath;
	if (!FPathViews::TryMakeChildPathRelativeTo(InLocalAbsPath, GetLocalAbsPath(), RemainingPath))
	{
		return nullptr;
	}
	return Root->GetControllingDir(RemainingPath, bIsDirectory, false /* bParentIsWhitelisted */, bOutIsWhitelisted, OutRelPath);
}

uint32 FMountDir::GetAllocatedSize() const
{
	uint32 Result = sizeof(*Root);
	Result += Root->GetAllocatedSize();
	Result += PathDatas.GetAllocatedSize();
	for (const FPathData& Value : PathDatas)
	{
		Result += Value.GetAllocatedSize();
	}
	Result += LongPackageName.GetAllocatedSize();
	Result += BlacklistedRelPaths.GetAllocatedSize();
	for (const FString& Value : BlacklistedRelPaths)
	{
		Result += Value.GetAllocatedSize();
	}
	return Result;
}

void FMountDir::Shrink()
{
	Root->Shrink();
	PathDatas.Shrink();
	BlacklistedRelPaths.Shrink();
}

bool FMountDir::IsComplete() const
{
	return Root->IsComplete();
}

EPriority FMountDir::GetPriority() const
{
	return Root->GetPriority();
}

bool FMountDir::IsPathWhitelisted(FStringView InLocalAbsPath) const
{
	FStringView QueryRelPath;
	if (!ensure(FPathViews::TryMakeChildPathRelativeTo(InLocalAbsPath, GetLocalAbsPath(), QueryRelPath)))
	{
		return false;
	}

	return Root->IsPathWhitelisted(QueryRelPath, false);
}

bool FMountDir::IsBlacklisted(FStringView InLocalAbsPath) const
{
	FStringView QueryRelPath;
	verify(FPathViews::TryMakeChildPathRelativeTo(InLocalAbsPath, GetLocalAbsPath(), QueryRelPath));
	for (const FString& Blacklist : BlacklistedRelPaths)
	{
		if (FPathViews::IsParentPathOf(Blacklist, QueryRelPath))
		{
			return true;
		}
	}
	return false;
}

bool FMountDir::IsMonitored(FStringView InLocalAbsPath) const
{
	FStringView QueryRelPath;
	if (!ensure(FPathViews::TryMakeChildPathRelativeTo(InLocalAbsPath, GetLocalAbsPath(), QueryRelPath)))
	{
		return false;
	}
	if (IsBlacklisted(InLocalAbsPath))
	{
		return false;
	}

	return Root->IsPathWhitelisted(QueryRelPath, false);
}

bool FMountDir::TrySetDirectoryProperties(FStringView InLocalAbsPath, const FSetPathProperties& InProperties, bool bConfirmedExists)
{
	FStringView RelPath;
	if (!ensure(FPathViews::TryMakeChildPathRelativeTo(InLocalAbsPath, GetLocalAbsPath(), RelPath)))
	{
		return false;
	}
	bool bResult = false;

	const FSetPathProperties* Properties = &InProperties;
	TOptional<FSetPathProperties> ModifiedProperties;
	if (Properties->IgnoreBlacklist.IsSet())
	{
		// IgnoreBlacklist is applied at the MountDir level, so we handle it separately
		bool bIgnoreBlacklist = *Properties->IgnoreBlacklist;
		FPathData* PathData = nullptr;
		if (bIgnoreBlacklist)
		{
			PathData = &FindOrAddPathData(RelPath);
		}
		else
		{
			PathData = FindPathData(RelPath);
		}
		if (PathData)
		{
			PathData->bIgnoreBlacklist = bIgnoreBlacklist;
			if (PathData->IsEmpty())
			{
				RemovePathData(RelPath);
			}
		}
		UpdateBlacklist();
		
		MarkDirty(RelPath);

		ModifiedProperties = InProperties;
		ModifiedProperties->IgnoreBlacklist.Reset();
		if (!ModifiedProperties->IsSet())
		{
			return true;
		}
		Properties = &ModifiedProperties.GetValue();
		bResult = true;
	}

	bResult = Root->TrySetDirectoryProperties(RelPath, InProperties, bConfirmedExists) || bResult;
	return bResult;
}

void FMountDir::UpdateBlacklist()
{
	BlacklistedRelPaths.Empty(Discovery.BlacklistMountRelativePaths.Num());
	for (const FString& BlacklistName : Discovery.BlacklistLongPackageNames)
	{
		FStringView MountRelPath;
		if (FPathViews::TryMakeChildPathRelativeTo(BlacklistName, LongPackageName, MountRelPath))
		{
			FPathData* PathData = FindPathData(MountRelPath);
			if (!PathData || !PathData->bIgnoreBlacklist)
			{
				// Note that an empty RelPath means we blacklist the entire mountpoint
				BlacklistedRelPaths.Emplace(MountRelPath);
			}
		}
	}
	for (const FString& MountRelPath : Discovery.BlacklistMountRelativePaths)
	{
		FPathData* PathData = FindPathData(MountRelPath);
		if (!PathData || !PathData->bIgnoreBlacklist)
		{
			BlacklistedRelPaths.Emplace(MountRelPath);
		}
	}
	for (const FPathData& PathData : PathDatas)
	{
		if (PathData.bIsChildPath)
		{
			BlacklistedRelPaths.Emplace(PathData.RelPath);
		}
	}
}

void FMountDir::Update(FScanDir*& OutCursor, bool& bOutCursorParentIsWhitelisted)
{
	bOutCursorParentIsWhitelisted = false;
	Root->Update(OutCursor, bOutCursorParentIsWhitelisted);
}

void FMountDir::SetHasStartedScanning()
{
	bHasStartedScanning = true;
}

void FMountDir::AddChildMount(FMountDir* ChildMount)
{
	if (!ChildMount)
	{
		return;
	}
	FStringView RelPath;
	if (!FPathViews::TryMakeChildPathRelativeTo(ChildMount->GetLocalAbsPath(), LocalAbsPath, RelPath))
	{
		return;
	}
	FindOrAddPathData(RelPath).bIsChildPath = true;
	if (bHasStartedScanning)
	{
		UE_LOG(LogAssetRegistry, Warning, TEXT("AssetDataGatherer directory %.*s has already started scanning when a new mountpoint was added under it at %.*s. ")
			TEXT("Assets in the new mount point may exist twice in the AssetRegistry under two different package names."),
			LocalAbsPath.Len(), *LocalAbsPath, ChildMount->LocalAbsPath.Len(), *ChildMount->LocalAbsPath);
	}
	UpdateBlacklist();
	MarkDirty(RelPath);
}

void FMountDir::RemoveChildMount(FMountDir* ChildMount)
{
	if (!ChildMount)
	{
		return;
	}
	FStringView RelPath;
	if (!FPathViews::TryMakeChildPathRelativeTo(ChildMount->GetLocalAbsPath(), LocalAbsPath, RelPath))
	{
		return;
	}
	FPathData* PathData = FindPathData(RelPath);
	if (!PathData)
	{
		return;
	}
	PathData->bIsChildPath = false;
	if (PathData->IsEmpty())
	{
		RemovePathData(RelPath);
	}
	if (ChildMount->bHasStartedScanning)
	{
		UE_LOG(LogAssetRegistry, Warning, TEXT("AssetDataGatherer directory %.*s has already started scanning when it was removed and merged into its parent mount at %.*s. ")
			TEXT("Assets in the new mount point may exist twice in the AssetRegistry under two different package names."),
			ChildMount->LocalAbsPath.Len(), *ChildMount->LocalAbsPath, LocalAbsPath.Len(), *LocalAbsPath);
	}
	UpdateBlacklist();
	MarkDirty(RelPath);
}

void FMountDir::OnDestroyClearChildMounts()
{
	// This function deletes more data than just the child paths; if it becomes used for purposes other than deleting the MountDir,
	// we will need to change it to only set PathData->bIsChildPath = false;
	PathDatas.Empty();
}

void FMountDir::SetParentMount(FMountDir* Parent)
{
	ParentMount = Parent;
}

FMountDir* FMountDir::GetParentMount() const
{
	return ParentMount;
}

TArray<FMountDir*> FMountDir::GetChildMounts() const
{
	// Called within Discovery's TreeLock
	TArray<FMountDir*> Result;
	for (const FPathData& PathData : PathDatas)
	{
		if (PathData.bIsChildPath)
		{
			TStringBuilder<256> ChildAbsPath;
			ChildAbsPath << LocalAbsPath;
			FPathViews::AppendPath(ChildAbsPath, PathData.RelPath);
			FMountDir* ChildMount = Discovery.FindMountPoint(ChildAbsPath);
			if (ensure(ChildMount)) // This PathData information should have been removed with RemoveChildMount when the child MountDir was removed from the Discovery
			{
				Result.Add(ChildMount);
			}
		}
	}
	return Result;
}

void FMountDir::MarkDirty(FStringView MountRelPath)
{
	bool bIsWhitelisted = true;
	FString ControlRelPath;
	FScanDir* ScanDir = Root->GetControllingDir(MountRelPath, true /* bIsDirectory */, false /* bParentIsWhitelisted */, bIsWhitelisted, ControlRelPath);
	if (ScanDir)
	{
		// If a ScanDir exists for the directory that is being marked dirty, mark all of its descendents dirty as well.
		// If the control dir is a parent directory of the requested path, just mark it and its parents dirty
		// Mark all parent directories that exist as incomplete
		bool bDirtyAllDescendents = ControlRelPath.IsEmpty();
		ScanDir->MarkDirty(bDirtyAllDescendents);
	}
}

FMountDir::FPathData* FMountDir::FindPathData(FStringView MountRelPath)
{
	return Algo::FindByPredicate(PathDatas, [MountRelPath](const FPathData& PathData) { return FPathViews::Equals(PathData.RelPath, MountRelPath); });
}

FMountDir::FPathData& FMountDir::FindOrAddPathData(FStringView MountRelPath)
{
	FPathData* PathData = FindPathData(MountRelPath);
	if (!PathData)
	{
		PathData = &PathDatas.Emplace_GetRef(MountRelPath);
	}
	return *PathData;
}

void FMountDir::RemovePathData(FStringView MountRelPath)
{
	PathDatas.RemoveAllSwap([MountRelPath](const FPathData& PathData) { return FPathViews::Equals(PathData.RelPath, MountRelPath);  });
}

FMountDir::FPathData::FPathData(FStringView MountRelPath)
:RelPath(MountRelPath)
{
}

bool FMountDir::FPathData::IsEmpty() const
{
	return !bIgnoreBlacklist && !bIsChildPath;
}

uint32 FMountDir::FPathData::GetAllocatedSize() const
{
	return RelPath.GetAllocatedSize();
}

FAssetDataDiscovery::FAssetDataDiscovery(const TArray<FString>& InBlacklistLongPackageNames, const TArray<FString>& InBlacklistMountRelativePaths, bool bInIsSynchronous)
	: BlacklistLongPackageNames(InBlacklistLongPackageNames)
	, BlacklistMountRelativePaths(InBlacklistMountRelativePaths)
	, Thread(nullptr)
	, bIsSynchronous(bInIsSynchronous)
	, bIsIdle(false)
	, IsStopped(0)
	, IsPaused(0)
	, NumDirectoriesToScan(0)
{
	DirLongPackageNamesToNotReport.Add(TEXT("/Game/Collections"));

	if (!bIsSynchronous && !FPlatformProcess::SupportsMultithreading())
	{
		bIsSynchronous = true;
		UE_LOG(LogAssetRegistry, Warning, TEXT("Requested asyncronous asset data discovery, but threading support is disabled. Performing a synchronous discovery instead!"));
	}
}

FAssetDataDiscovery::~FAssetDataDiscovery()
{
	EnsureCompletion();
	Cursor.SafeRelease();
	// Remove pointers to other MountDirs before we delete any of them
	for (TUniquePtr<FMountDir>& MountDir : MountDirs)
	{
		MountDir->SetParentMount(nullptr);
		MountDir->OnDestroyClearChildMounts();
	}
	MountDirs.Empty();
}

void FAssetDataDiscovery::StartAsync()
{
	if (!bIsSynchronous && !Thread)
	{
		Thread = FRunnableThread::Create(this, TEXT("FAssetDataDiscovery"), 0, TPri_BelowNormal);
		checkf(Thread, TEXT("Failed to create asset data discovery thread"));
	}
}

bool FAssetDataDiscovery::Init()
{
	return true;
}

uint32 FAssetDataDiscovery::Run()
{
	constexpr double IdleSleepTime = 0.1;
	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		DiscoverStartTime = FPlatformTime::Seconds();
		NumDiscoveredFiles = 0;
	}

	while (!IsStopped)
	{
		{
			CHECK_IS_NOT_LOCKED_CURRENT_THREAD(TreeLock);
			CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
			FGathererScopeLock TickScopeLock(&TickLock);
			while (!IsStopped && !bIsIdle && !IsPaused)
			{
				TickInternal();
			}
		}

		while (!IsStopped && (IsPaused || bIsIdle))
		{
			// No work to do. Sleep for a little and try again later.
			// TODO: Need IsPaused to be a condition variable so we avoid sleeping while waiting for it and then taking a long time to wake after it is unset.
			FPlatformProcess::Sleep(IdleSleepTime);
		} 
	}
	return 0;
}

FAssetDataDiscovery::FScopedPause::FScopedPause(const FAssetDataDiscovery& InOwner)
	:Owner(InOwner)
{
	if (!Owner.bIsSynchronous)
	{
		Owner.IsPaused++;
	}
}

FAssetDataDiscovery::FScopedPause::~FScopedPause()
{
	if (!Owner.bIsSynchronous)
	{
		check(Owner.IsPaused > 0);
		Owner.IsPaused--;
	}
}

void FAssetDataDiscovery::Stop()
{
	IsStopped++;
}

void FAssetDataDiscovery::Exit()
{
}

void FAssetDataDiscovery::EnsureCompletion()
{
	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}


void FAssetDataDiscovery::TickInternal()
{
	TStringBuilder<256> DirLocalAbsPath;
	TStringBuilder<128> DirLongPackageName;
	TStringBuilder<128> DirMountRelPath;
	int32 DirLongPackageNameRootLen;
	TRefCountPtr<FScanDir> LocalCursor = nullptr;
	{
		CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
		FGathererScopeLock TreeScopeLock(&TreeLock);
		for (;;)
		{
			// Start at the existing cursor (initializing it if necessary) at call Update until we find a ScanTree that requires scanning
			if (!Cursor || !Cursor->IsValid())
			{
				FScanDir* NewCursor;
				FindFirstCursor(NewCursor, bCursorParentIsWhitelisted);
				Cursor = NewCursor;
				if (!NewCursor)
				{
					SetIsIdle(true);
					return;
				}
			}
			if (Cursor->ShouldScan(bCursorParentIsWhitelisted))
			{
				break;
			}

			FScanDir* NewCursor = Cursor.GetReference();
			NewCursor->Update(NewCursor, bCursorParentIsWhitelisted);
			check(NewCursor != Cursor);
			Cursor = NewCursor;
		}
		// IsScanInFlight must be false, because it is not valid to have two TickInternals run at the same time, and we set ScanInFlight back to false after each TickInternal.
		// If ScanInFlight were true here we would not be able to proceed since we currently don't have a way to find the next ScanTree to update without scanning the current one.
		check(!Cursor->IsScanInFlight());

		Cursor->SetScanInFlight(true);
		FMountDir* MountDir = Cursor->GetMountDir();
		check(MountDir);
		Cursor->AppendMountRelPath(DirMountRelPath);
		DirLocalAbsPath << MountDir->GetLocalAbsPath();
		FPathViews::AppendPath(DirLocalAbsPath, DirMountRelPath);
		DirLongPackageName << MountDir->GetLongPackageName();
		FPathViews::AppendPath(DirLongPackageName, DirMountRelPath);
		DirLongPackageNameRootLen = DirLongPackageName.Len();
		LocalCursor = Cursor;
	}

	int32 NumIteratedDirs = 0;
	int32 NumIteratedFiles = 0;
	FLambdaDirectoryStatVisitor Visitor(FLambdaDirectoryStatVisitor::FLambdaRef(
		[this, &DirLocalAbsPath, &DirLongPackageName, DirLongPackageNameRootLen, &NumIteratedDirs, &NumIteratedFiles]
	(const TCHAR* InPackageFilename, const FFileStatData& InPackageStatData)
		{
			FStringView LocalAbsPath(InPackageFilename);
			FStringView RelPath;
			FString Buffer;
			if (!FPathViews::TryMakeChildPathRelativeTo(InPackageFilename, DirLocalAbsPath, RelPath))
			{
				// Try again with the path converted to the absolute path format that we passed in; some IFileManagers can send relative paths to the visitor even though the search path is absolute
				Buffer = FPaths::ConvertRelativePathToFull(FString(InPackageFilename));
				LocalAbsPath = Buffer;
				if (!FPathViews::TryMakeChildPathRelativeTo(Buffer, DirLocalAbsPath, RelPath))
				{
					UE_LOG(LogAssetRegistry, Warning, TEXT("IterateDirectoryStat returned unexpected result %s which is not a child of the requested path %s."), InPackageFilename, DirLocalAbsPath.ToString());
					return true;
				}
			}
			if (FPathViews::GetPathLeaf(RelPath).Len() != RelPath.Len())
			{
				UE_LOG(LogAssetRegistry, Warning, TEXT("IterateDirectoryStat returned unexpected result %* which is not a direct child of the requested path %s."), InPackageFilename, DirLocalAbsPath.ToString());
				return true;
			}
			ON_SCOPE_EXIT{ DirLongPackageName.RemoveSuffix(DirLongPackageName.Len() - DirLongPackageNameRootLen); };

			if (InPackageStatData.bIsDirectory)
			{
				FPathViews::AppendPath(DirLongPackageName, RelPath);
				// Don't enter directories that contain invalid packagepath characters (including '.'; extensions are not valid in content directories because '.' is not valid in a packagepath)
				if (!FPackageName::DoesPackageNameContainInvalidCharacters(RelPath))
				{
					if (IteratedSubDirs.Num() < NumIteratedDirs + 1)
					{
						check(IteratedSubDirs.Num() == NumIteratedDirs);
						IteratedSubDirs.Emplace();
					}
					IteratedSubDirs[NumIteratedDirs++].Assign(LocalAbsPath, DirLongPackageName, RelPath);
				}
			}
			else
			{
				FStringView BaseName = FPathViews::GetBaseFilename(RelPath);
				FPathViews::AppendPath(DirLongPackageName, BaseName);
				// Don't record files that contain invalid packagepath characters (not counting their extension) or that do not end with a recognized extension
				if (!FPackageName::DoesPackageNameContainInvalidCharacters(BaseName) && FPackageName::IsPackageFilename(RelPath))
				{
					if (IteratedFiles.Num() < NumIteratedFiles + 1)
					{
						check(IteratedFiles.Num() == NumIteratedFiles);
						IteratedFiles.Emplace();
					}
					IteratedFiles[NumIteratedFiles++].Assign(LocalAbsPath, DirLongPackageName, RelPath, InPackageStatData.ModificationTime);
				}
			}
			return true;
		}));

	IFileManager::Get().IterateDirectoryStat(DirLocalAbsPath.ToString(), Visitor);
	TArrayView<FDiscoveredPathData> LocalSubDirs(IteratedSubDirs.GetData(), NumIteratedDirs);
	TArrayView<FDiscoveredPathData> LocalDiscoveredFiles(IteratedFiles.GetData(), NumIteratedFiles);
	bool bValid = false;
	{
		CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
		FGathererScopeLock TreeScopeLock(&TreeLock);
		if (!LocalCursor->IsValid())
		{
			// The ScanDir has been shutdown, and it is only still allocated to prevent us from crashing. Drop our reference and allow it to delete.
		}
		else if (LocalCursor->IsScanInFlightInvalidated())
		{
			// Some setting has been applied to the ScanDir that requires a new scan
			// Consume the invalidated flag and ignore the results of our scan
			LocalCursor->SetScanInFlightInvalidated(false);
		}
		else
		{
			LocalCursor->SetScanResults(DirLocalAbsPath, LocalSubDirs, LocalDiscoveredFiles);
			bValid = true;
			FScanDir* NewCursor = Cursor.GetReference();
			// Other thread may have set the cursor to a new spot; in that case do not update and on the next tick start at the new cursor
			if (LocalCursor == NewCursor)
			{
				LocalCursor->Update(NewCursor, bCursorParentIsWhitelisted);
				Cursor = NewCursor;
			}
		}
		LocalCursor->SetScanInFlight(false);
	}

	if (bValid && (!LocalSubDirs.IsEmpty() || !LocalDiscoveredFiles.IsEmpty()))
	{
		AddDiscovered(LocalSubDirs, LocalDiscoveredFiles);
	}
}

void FAssetDataDiscovery::FindFirstCursor(FScanDir*& OutCursor, bool& bOutCursorParentIsWhitelisted)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	OutCursor = nullptr;
	while (!OutCursor)
	{
		EPriority WinningPriority = EPriority::Normal;
		FMountDir* WinningMountDir = nullptr;
		for (TUniquePtr<FMountDir>& MountDir : MountDirs)
		{
			if (MountDir->IsComplete())
			{
				continue;
			}
			if (WinningMountDir == nullptr || MountDir->GetPriority() < WinningPriority)
			{
				WinningMountDir = MountDir.Get();
				WinningPriority = MountDir->GetPriority();
			}
		}

		if (!WinningMountDir)
		{
			OutCursor = nullptr;
			bOutCursorParentIsWhitelisted = false;
			break;
		}

		WinningMountDir->Update(OutCursor, bOutCursorParentIsWhitelisted);
		check(OutCursor != nullptr || WinningMountDir->IsComplete()); // The WinningMountDir's update should either return something to update or it should mark itself complete
	}
}

void FAssetDataDiscovery::InvalidateCursor()
{
	if (Cursor)
	{
		if (Cursor->IsScanInFlight())
		{
			Cursor->SetScanInFlightInvalidated(true);
		}
		Cursor.SafeRelease();
	}
}

void FAssetDataDiscovery::SetIsIdle(bool bInIsIdle)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);

	// Caller is responsible for holding TreeLock around this function; writes of SetIsIdle are done inside the TreeLock
	// If bIsIdle is true, caller holds TickLock and TreeLock
	if (bIsIdle == bInIsIdle)
	{
		return;
	}
	FGathererScopeLock ResultsScopeLock(&ResultsLock);
	bIsIdle = bInIsIdle;
	if (!bIsSynchronous)
	{
		if (bIsIdle)
		{
			UE_LOG(LogAssetRegistry, Verbose, TEXT("Discovery took %0.6f seconds and found %d files to process"), FPlatformTime::Seconds() - DiscoverStartTime, NumDiscoveredFiles);
		}
		else
		{
			DiscoverStartTime = FPlatformTime::Seconds();
			NumDiscoveredFiles = 0;
		}
	}

	if (bIsIdle)
	{
		CHECK_IS_LOCKED_CURRENT_THREAD(TickLock);
		Shrink();
	}
}


void FAssetDataDiscovery::GetAndTrimSearchResults(bool& bOutIsComplete, TArray<FString>& OutDiscoveredPaths, TRingBuffer<FGatheredPathData>& OutDiscoveredFiles, int32& OutNumPathsToSearch)
{
	FGathererScopeLock ResultsScopeLock(&ResultsLock);

	OutDiscoveredPaths.Append(MoveTemp(DiscoveredDirectories));
	DiscoveredDirectories.Reset();
	OutDiscoveredFiles.Reserve(OutDiscoveredFiles.Num() + DiscoveredFiles.Num());
	for (FGatheredPathData& DiscoveredFile : DiscoveredFiles)
	{
		OutDiscoveredFiles.Add(MoveTemp(DiscoveredFile));
	}
	DiscoveredFiles.Reset();

	OutNumPathsToSearch = NumDirectoriesToScan.GetValue();
	bOutIsComplete = bIsIdle;
}

void FAssetDataDiscovery::WaitForIdle()
{
	if (bIsIdle)
	{
		return;
	}
	FScopedPause ScopedPause(*this);
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(TreeLock);
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TickScopeLock(&TickLock);
	while (!bIsIdle)
	{
		TickInternal();
	}
}

void FAssetDataDiscovery::SetPropertiesAndWait(const FString& LocalAbsPath, bool bAddToWhitelist, bool bForceRescan, bool bIgnoreBlackListScanFilters)
{
	FFileStatData StatData = IFileManager::Get().GetStatData(*LocalAbsPath);
	if (!StatData.bIsValid)
	{
		// SetPropertiesAndWait is called for every ScanPathsSynchronous, and this is the first spot that checks for existence. Some systems call ScanPathsSynchronous
		// speculatively to scan whatever is present, so this is not a significant enough occurrence for a log.
		UE_LOG(LogAssetRegistry, Verbose, TEXT("SetPropertiesAndWait called on non-existent path %.*s. Call will be ignored."), LocalAbsPath.Len(), *LocalAbsPath);
		return;
	}

	{
		CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
		FGathererScopeLock TreeScopeLock(&TreeLock);
		FMountDir* MountDir = FindContainingMountPoint(LocalAbsPath);
		if (!MountDir)
		{
			UE_LOG(LogAssetRegistry, Log, TEXT("SetPropertiesAndWait called on %.*s which is not in a mounted directory. Call will be ignored."), LocalAbsPath.Len(), *LocalAbsPath);
			return;
		}
		bool bIsBlacklisted = MountDir->IsBlacklisted(LocalAbsPath);
		if (bIsBlacklisted && !bIgnoreBlackListScanFilters)
		{
			UE_LOG(LogAssetRegistry, Log, TEXT("SetPropertiesAndWait called on %.*s which is blacklisted. Call will be ignored."), LocalAbsPath.Len(), *LocalAbsPath);
			return;
		}

		if (StatData.bIsDirectory)
		{
			FSetPathProperties Properties;
			if (bAddToWhitelist)
			{
				Properties.IsWhitelisted = bAddToWhitelist;
			}
			if (bForceRescan)
			{
				Properties.HasScanned = false;
			}
			if (bIgnoreBlackListScanFilters)
			{
				Properties.IgnoreBlacklist = true;
			}
			if (Properties.IsSet())
			{
				SetIsIdle(false);
				MountDir->TrySetDirectoryProperties(LocalAbsPath, Properties, true /* bConfirmedExists */);
			}
		}

		FString RelPath;
		bool bIsWhitelisted;
		TRefCountPtr<FScanDir> ScanDir = MountDir->GetControllingDir(LocalAbsPath, StatData.bIsDirectory, bIsWhitelisted, RelPath);
		if (!ScanDir || (!bIsWhitelisted && !bAddToWhitelist))
		{
			UE_LOG(LogAssetRegistry, Log, TEXT("SetPropertiesAndWait called on %.*s which is not whitelisted. Call will be ignored."), LocalAbsPath.Len(), *LocalAbsPath);
			return;
		}

		if (StatData.bIsDirectory)
		{
			// If Relpath from the controlling dir to the requested dir is not empty then we have found a parent directory rather than the requested directory.
			// This can only occur for a monitored directory when the requested directory is already complete and we do not need to wait on it.
			if (RelPath.IsEmpty() && !ScanDir->IsComplete())
			{
				// We are going to wait on the path, so set its priority to blocking
				SetIsIdle(false);
				ScanDir->SetDirectPriority(EPriority::Blocking);
				InvalidateCursor();

				TreeScopeLock.Unlock(); // Entering the ticklock, as well as any long duration task such as a tick, has to be done outside of any locks

				FScopedPause ScopedPause(*this);
				CHECK_IS_NOT_LOCKED_CURRENT_THREAD(TreeLock);
				CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
				FGathererScopeLock TickScopeLock(&TickLock);
				for (;;)
				{
					TickInternal();
					FGathererScopeLock LoopTreeScopeLock(&TreeLock);
					if (!ScanDir->IsValid() || ScanDir->IsComplete())
					{
						break;
					}
					else if (!ensureMsgf(!bIsIdle, TEXT("It should not be possible for the Discovery to go idle while there is an incomplete ScanDir.")))
					{
						break;
					}
				}
			}
		}
		else
		{
			bool bAlreadyScanned = ScanDir->HasScanned() && !bIsBlacklisted;
			if (!bAlreadyScanned || bForceRescan)
			{
				FStringView RelPathFromParentDir = FPathViews::GetCleanFilename(RelPath);
				FStringView FileRelPathNoExt = FPathViews::GetBaseFilenameWithPath(RelPath);
				if (!FPackageName::DoesPackageNameContainInvalidCharacters(FileRelPathNoExt) && FPackageName::IsPackageFilename(RelPathFromParentDir))
				{
					TStringBuilder<256> LongPackageName;
					LongPackageName << MountDir->GetLongPackageName();
					FPathViews::AppendPath(LongPackageName, ScanDir->GetMountRelPath());
					FPathViews::AppendPath(LongPackageName, FileRelPathNoExt);
					AddDiscovered(TConstArrayView<FDiscoveredPathData>(), TConstArrayView<FDiscoveredPathData> { FDiscoveredPathData(LocalAbsPath, LongPackageName, RelPathFromParentDir, StatData.ModificationTime) });
					if (FPathViews::IsPathLeaf(RelPath) && !ScanDir->HasScanned())
					{
						SetIsIdle(false);
						ScanDir->MarkFileAlreadyScanned(RelPath);
					}
				}
			}
		}
	}
}

bool FAssetDataDiscovery::TrySetDirectoryProperties(const FString& LocalAbsPath, const FSetPathProperties& InProperties, bool bConfirmedExists)
{
	if (!InProperties.IsSet())
	{
		return false;
	}
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	if (!TrySetDirectoryPropertiesInternal(LocalAbsPath, InProperties, bConfirmedExists))
	{
		return false;
	}
	SetIsIdle(false);
	InvalidateCursor();
	return true;
}

bool FAssetDataDiscovery::TrySetDirectoryPropertiesInternal(const FString& LocalAbsPath, const FSetPathProperties& InProperties, bool bConfirmedExists)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	FMountDir* MountDir = FindContainingMountPoint(LocalAbsPath);
	if (!MountDir)
	{
		UE_LOG(LogAssetRegistry, Warning, TEXT("FAssetDataGatherer::SetDirectoryProperties called on unmounted path %.*s. Call will be ignored."), LocalAbsPath.Len(), *LocalAbsPath);
		return false;
	}

	return MountDir->TrySetDirectoryProperties(LocalAbsPath, InProperties, bConfirmedExists);
}

bool FAssetDataDiscovery::IsWhitelisted(FStringView LocalAbsPath) const
{
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	const FMountDir* MountDir = FindContainingMountPoint(LocalAbsPath);
	return MountDir && MountDir->IsPathWhitelisted(LocalAbsPath);
}

bool FAssetDataDiscovery::IsBlacklisted(FStringView LocalAbsPath) const
{
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	const FMountDir* MountDir = FindContainingMountPoint(LocalAbsPath);
	if (!MountDir)
	{
		// Not mounted, which we report as not blacklisted
		return false;
	}

	return MountDir->IsBlacklisted(LocalAbsPath);
}

bool FAssetDataDiscovery::IsMonitored(FStringView LocalAbsPath) const
{
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	const FMountDir* MountDir = FindContainingMountPoint(LocalAbsPath);
	return MountDir && MountDir->IsMonitored(LocalAbsPath);
}

uint32 FAssetDataDiscovery::GetAllocatedSize() const
{
	auto GetArrayRecursiveAllocatedSize = [](auto Container)
	{
		int32 Result = Container.GetAllocatedSize();
		for (const auto& Value : Container)
		{
			Result += Value.GetAllocatedSize();
		}
		return Result;
	};

	FScopedPause ScopedPause(*this);
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(TreeLock);
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TickScopeLock(&TickLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	FGathererScopeLock ResultsScopeLock(&ResultsLock);

	uint32 Result = 0;
	Result += GetArrayRecursiveAllocatedSize(BlacklistLongPackageNames);
	Result += GetArrayRecursiveAllocatedSize(BlacklistMountRelativePaths);
	Result += GetArrayRecursiveAllocatedSize(DirLongPackageNamesToNotReport);
	if (Thread)
	{
		// TODO: Thread->GetAllocatedSize()
		Result += sizeof(*Thread);
	}

	Result += GetArrayRecursiveAllocatedSize(DiscoveredDirectories);
	Result += GetArrayRecursiveAllocatedSize(DiscoveredFiles);

	Result += MountDirs.GetAllocatedSize();
	for (const TUniquePtr<FMountDir>& Value : MountDirs)
	{
		Result += sizeof(*Value);
		Result += Value->GetAllocatedSize();
	}
	Result += GetArrayRecursiveAllocatedSize(IteratedSubDirs);
	Result += GetArrayRecursiveAllocatedSize(IteratedFiles);
	return Result;
}

void FAssetDataDiscovery::Shrink()
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TickLock);
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	CHECK_IS_LOCKED_CURRENT_THREAD(ResultsLock);
	DirLongPackageNamesToNotReport.Shrink();
	DiscoveredDirectories.Shrink();
	DiscoveredFiles.Shrink();
	MountDirs.Shrink();
	for (TUniquePtr<FMountDir>& MountDir : MountDirs)
	{
		MountDir->Shrink();
	}
	IteratedSubDirs.Shrink();
	IteratedFiles.Shrink();
}

void FAssetDataDiscovery::AddMountPoint(const FString& LocalAbsPath, FStringView LongPackageName)
{
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	SetIsIdle(false);
	AddMountPointInternal(LocalAbsPath, LongPackageName);

	InvalidateCursor();
}

void FAssetDataDiscovery::AddMountPointInternal(const FString& LocalAbsPath, FStringView LongPackageName)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	TArray<FMountDir*> ChildMounts;
	FMountDir* ParentMount = nullptr;
	bool bExists = false;
	for (TUniquePtr<FMountDir>& ExistingMount : MountDirs)
	{
		if (FPathViews::Equals(ExistingMount->GetLocalAbsPath(), LocalAbsPath))
		{
			bExists = true;
			break;
		}
		else if (FPathViews::IsParentPathOf(ExistingMount->GetLocalAbsPath(), LocalAbsPath))
		{
			// Overwrite any earlier ParentMount; later mounts are more direct parents than earlier mounts
			ParentMount = ExistingMount.Get();
		}
		else if (FPathViews::IsParentPathOf(LocalAbsPath, ExistingMount->GetLocalAbsPath()))
		{
			// A mount under the new directory might be a grandchild mount.
			// Don't add it as a child mount unless there is no other mount in between the new mount and the mount.
			FMountDir* ExistingParentMount = ExistingMount->GetParentMount();
			if (!ExistingParentMount || ExistingParentMount == ParentMount)
			{
				ChildMounts.Add(ExistingMount.Get());
			}
		}
	}
	if (bExists)
	{
		return;
	}

	FMountDir& Mount = FindOrAddMountPoint(LocalAbsPath, LongPackageName);
	if (ParentMount)
	{
		FStringView RelPath;
		verify(FPathViews::TryMakeChildPathRelativeTo(LocalAbsPath, ParentMount->GetLocalAbsPath(), RelPath));
		ParentMount->AddChildMount(&Mount);
		for (FMountDir* ChildMount : ChildMounts)
		{
			ParentMount->RemoveChildMount(ChildMount);
		}
	}
	for (FMountDir* ChildMount : ChildMounts)
	{
		Mount.AddChildMount(ChildMount);
		ChildMount->SetParentMount(ParentMount);
	}
}

void FAssetDataDiscovery::RemoveMountPoint(const FString& LocalAbsPath)
{
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	SetIsIdle(false);
	RemoveMountPointInternal(LocalAbsPath);

	InvalidateCursor();
}

void FAssetDataDiscovery::RemoveMountPointInternal(const FString& LocalAbsPath)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	int32 ExistingIndex = FindLowerBoundMountPoint(LocalAbsPath);
	if (ExistingIndex == MountDirs.Num() || !FPathViews::Equals(MountDirs[ExistingIndex]->GetLocalAbsPath(), LocalAbsPath))
	{
		return;
	}
	TUniquePtr<FMountDir> Mount = MoveTemp(MountDirs[ExistingIndex]);
	MountDirs.RemoveAt(ExistingIndex);
	FMountDir* ParentMount = Mount->GetParentMount();

	if (ParentMount)
	{
		for (FMountDir* ChildMount : Mount->GetChildMounts())
		{
			ParentMount->AddChildMount(ChildMount);
			ChildMount->SetParentMount(ParentMount);
		}
		ParentMount->RemoveChildMount(Mount.Get());
	}
	else
	{
		for (FMountDir* ChildMount : Mount->GetChildMounts())
		{
			ChildMount->SetParentMount(nullptr);
		}
	}
}

void FAssetDataDiscovery::OnDirectoryCreated(FStringView LocalAbsPath)
{
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	FMountDir* MountDir = FindContainingMountPoint(LocalAbsPath);
	if (!MountDir || !MountDir->IsMonitored(LocalAbsPath))
	{
		return;
	}

	FStringView MountRelPath;
	verify(FPathViews::TryMakeChildPathRelativeTo(LocalAbsPath, MountDir->GetLocalAbsPath(), MountRelPath));
	TStringBuilder<128> LongPackageName;
	LongPackageName << MountDir->GetLongPackageName();
	FPathViews::AppendPath(LongPackageName, MountRelPath);
	if (FPackageName::DoesPackageNameContainInvalidCharacters(LongPackageName))
	{
		return;
	}

	// Skip reporting the directory if it is in the blacklists of directories to not report
	if (!ShouldDirBeReported(LongPackageName))
	{
		return;
	}

	FDiscoveredPathData DirData;
	DirData.LocalAbsPath = LocalAbsPath;
	DirData.LongPackageName = LongPackageName;
	DirData.RelPath = FPathViews::GetCleanFilename(MountRelPath);

	// Note that we AddDiscovered but do not scan the directory
	// Any files and paths under it will be added by their own event from the directory watcher, so a scan is unnecessary.
	// The directory may also be scanned in the future because a parent directory is still yet pending to scan,
	// we do not try to prevent that wasteful rescan because this is a rare event and it does not cause a behavior problem
	AddDiscovered(TConstArrayView<FDiscoveredPathData>(&DirData, 1), TConstArrayView<FDiscoveredPathData>());
	SetIsIdle(false);
}

void FAssetDataDiscovery::OnFilesCreated(TConstArrayView<FString> LocalAbsPaths)
{
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TreeScopeLock(&TreeLock);
	SetIsIdle(false);
	for (const FString& LocalAbsPath : LocalAbsPaths)
	{
		OnFileCreated(LocalAbsPath);
	}
}

void FAssetDataDiscovery::OnFileCreated(const FString& LocalAbsPath)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	// Detect whether the file should be scanned and if so pass it through to the gatherer
	FMountDir* MountDir = FindContainingMountPoint(LocalAbsPath);
	if (!MountDir || MountDir->IsBlacklisted(LocalAbsPath))
	{
		// The content root of the file is not registered, or the file is blacklisted; ignore it
		return;
	}
	FFileStatData StatData = IFileManager::Get().GetStatData(*LocalAbsPath);
	if (!StatData.bIsValid || StatData.bIsDirectory)
	{
		// The caller has erroneously told us a file exists that doesn't exist (perhaps due to create/delete hysteresis); ignore it
		return;
	}

	FString FileRelPath;
	bool bIsWhitelisted;
	FScanDir* ScanDir = MountDir->GetControllingDir(LocalAbsPath, false /* bIsDirectory */, bIsWhitelisted, FileRelPath);
	if (!ScanDir || !bIsWhitelisted)
	{
		// The new file is in an unmonitored directory; ignore it
		return;
	}

	FStringView RelPathFromParentDir = FPathViews::GetCleanFilename(FileRelPath);
	FStringView FileRelPathNoExt = FPathViews::GetBaseFilenameWithPath(FileRelPath);
	if (!FPackageName::DoesPackageNameContainInvalidCharacters(FileRelPathNoExt) && FPackageName::IsPackageFilename(RelPathFromParentDir))
	{
		TStringBuilder<256> LongPackageName;
		LongPackageName << MountDir->GetLongPackageName();
		FPathViews::AppendPath(LongPackageName, ScanDir->GetMountRelPath());
		FPathViews::AppendPath(LongPackageName, FileRelPathNoExt);
		AddDiscovered(TConstArrayView<FDiscoveredPathData>(), TConstArrayView<FDiscoveredPathData> { FDiscoveredPathData(LocalAbsPath, LongPackageName, RelPathFromParentDir, StatData.ModificationTime) });
		if (FPathViews::IsPathLeaf(FileRelPath))
		{
			ScanDir->MarkFileAlreadyScanned(FileRelPath);
		}
	}
}

FMountDir* FAssetDataDiscovery::FindContainingMountPoint(FStringView LocalAbsPath)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	int32 Index = FindLowerBoundMountPoint(LocalAbsPath);
	// The LowerBound is >= LocalAbsPath, so it is a parentpath of LocalAbsPath only if it is equal to LocalAbsPath
	if (Index < MountDirs.Num() && FPathViews::Equals(MountDirs[Index]->GetLocalAbsPath(), LocalAbsPath))
	{
		return MountDirs[Index].Get();
	}

	// The last element before the lower bound is either (1) an unrelated path and LocalAbsPath does not have a parent
	// (2) a parent path of LocalAbsPath, (3) A sibling path that is a child of an earlier path that is a parent path of LocalAbsPath
	// (4) An unrelated path that is a child of an earlier path, but none of its parents are a parent path of LocalAbsPath
	// Distinguishing between cases (3) and (4) doesn't have a fast algorithm based on sorted paths alone, but we have recorded the parent
	// so we can figure it out that way
	if (Index > 0)
	{
		FMountDir* Previous = MountDirs[Index - 1].Get();
		while (Previous)
		{
			if (FPathViews::IsParentPathOf(Previous->GetLocalAbsPath(), LocalAbsPath))
			{
				return Previous;
			}
			Previous = Previous->GetParentMount();
		}
	}
	return nullptr;
}

const FMountDir* FAssetDataDiscovery::FindContainingMountPoint(FStringView LocalAbsPath) const
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	return const_cast<FAssetDataDiscovery*>(this)->FindContainingMountPoint(LocalAbsPath);
}

FMountDir* FAssetDataDiscovery::FindMountPoint(FStringView LocalAbsPath)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	int32 Index = FindLowerBoundMountPoint(LocalAbsPath);
	if (Index != MountDirs.Num() && FPathViews::Equals(MountDirs[Index]->GetLocalAbsPath(), LocalAbsPath))
	{
		return MountDirs[Index].Get();
	}
	return nullptr;
}

FMountDir& FAssetDataDiscovery::FindOrAddMountPoint(FStringView LocalAbsPath, FStringView LongPackageName)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	int32 Index = FindLowerBoundMountPoint(LocalAbsPath);
	if (Index != MountDirs.Num() && FPathViews::Equals(MountDirs[Index]->GetLocalAbsPath(), LocalAbsPath))
	{
		// Already exists
		return *MountDirs[Index];
	}
	return *MountDirs.EmplaceAt_GetRef(Index, new FMountDir(*this, LocalAbsPath, LongPackageName));
}

int32 FAssetDataDiscovery::FindLowerBoundMountPoint(FStringView LocalAbsPath) const
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TreeLock);
	return Algo::LowerBound(MountDirs, LocalAbsPath, [](const TUniquePtr<FMountDir>& MountDir, FStringView LocalAbsPath)
		{
			return FPathViews::Less(MountDir->GetLocalAbsPath(), LocalAbsPath);
		}
	);
}

void FAssetDataDiscovery::AddDiscovered(TConstArrayView<FDiscoveredPathData> SubDirs, TConstArrayView<FDiscoveredPathData> Files)
{
	// This function is inside the critical section so we have moved filtering results outside of it
	// Caller is responsible for filtering SubDirs and Files by ShouldScan and packagename validity
	FGathererScopeLock ResultsScopeLock(&ResultsLock);
	for (const FDiscoveredPathData& SubDir : SubDirs)
	{
		DiscoveredDirectories.Add(FString(SubDir.LongPackageName));
	}
	for (const FDiscoveredPathData& DiscoveredFile : Files)
	{
		DiscoveredFiles.Emplace(DiscoveredFile); // Emplace passes the FDiscoveredPathData to the FGatheredPathData explicit constructor for it
	}
	NumDiscoveredFiles += Files.Num();
}

bool FAssetDataDiscovery::ShouldDirBeReported(FStringView LongPackageName) const
{
	return !DirLongPackageNamesToNotReport.ContainsByHash(GetTypeHash(LongPackageName), LongPackageName);
}

} // namespace Private
} // namespace AssetDataGather
} // namespace UE

FAssetDataGatherer::FAssetDataGatherer(const TArray<FString>& InBlacklistLongPackageNames, const TArray<FString>& InBlacklistMountRelativePaths, bool bInIsSynchronous)
	: Thread(nullptr)
	, bIsSynchronous(bInIsSynchronous)
	, IsStopped(0)
	, IsPaused(0)
	, bInitialPluginsLoaded(false)
	, bSaveAsyncCacheTriggered(false)
	, SearchStartTime(0)
	, LastCacheWriteTime(0.0)
	, bUseMonolithicCache(false)
	, bUseTickManagedCache(false)
	, bHasLoadedTickManagedCache(false)
	, bDiscoveryIsComplete(false)
	, bIsComplete(false)
	, bIsIdle(false)
	, bFirstTickAfterIdle(true)
	, bFinishedInitialDiscovery(false)
	, WaitBatchCount(0)
	, NumCachedFiles(0)
	, NumUncachedFiles(0)
	, bIsSavingAsyncCache(false)
{
	using namespace UE::AssetDataGather::Private;

	bGatherDependsData = (GIsEditor && !FParse::Param(FCommandLine::Get(), TEXT("NoDependsGathering"))) || FParse::Param(FCommandLine::Get(),TEXT("ForceDependsGathering"));

	bCacheEnabled = !FParse::Param(FCommandLine::Get(), TEXT("NoAssetRegistryCache")) && !FParse::Param(FCommandLine::Get(), TEXT("multiprocess"));

#if !UE_BUILD_SHIPPING
	bool bCommandlineSynchronous;
	if (FParse::Bool(FCommandLine::Get(), TEXT("AssetGatherSync="), bCommandlineSynchronous))
	{
		bIsSynchronous = bCommandlineSynchronous;
	}
#endif
	if (!bIsSynchronous && !FPlatformProcess::SupportsMultithreading())
	{
		bIsSynchronous = true;
		UE_LOG(LogAssetRegistry, Warning, TEXT("Requested asynchronous asset data gather, but threading support is disabled. Performing a synchronous gather instead!"));
	}
	bIsSynchronousTick = bIsSynchronous;

	Discovery = MakeUnique<UE::AssetDataGather::Private::FAssetDataDiscovery>(InBlacklistLongPackageNames, InBlacklistMountRelativePaths, bInIsSynchronous);
}

FAssetDataGatherer::~FAssetDataGatherer()
{
	EnsureCompletion();
	NewCachedAssetDataMap.Empty();
	DiskCachedAssetDataMap.Empty();

	for (FDiskCachedAssetData* AssetData : NewCachedAssetData)
	{
		delete AssetData;
	}
	NewCachedAssetData.Empty();
	for (TPair<int32, FDiskCachedAssetData*> BlockData : DiskCachedAssetBlocks)
	{
		delete[] BlockData.Get<1>();
	}
	DiskCachedAssetBlocks.Empty();
}

void FAssetDataGatherer::SetUseMonolithicCache(bool bInUseMonolithicCache)
{
	FGathererScopeLock ResultsScopeLock(&ResultsLock);
	if (!bCacheEnabled || bUseMonolithicCache == bInUseMonolithicCache)
	{
		return;
	}

	bUseMonolithicCache = bInUseMonolithicCache;
	bHasLoadedTickManagedCache = false;
	LastCacheWriteTime = FPlatformTime::Seconds();
	if (bUseMonolithicCache)
	{
		bUseTickManagedCache = bUseMonolithicCache;
		TickManagedCacheFilename = FPaths::ProjectIntermediateDir() / (bGatherDependsData ? TEXT("CachedAssetRegistry.bin") : TEXT("CachedAssetRegistryNoDeps.bin"));
	}
	else
	{
		bUseTickManagedCache = false;
		TickManagedCacheFilename.Empty();
	}
}

void FAssetDataGatherer::StartAsync()
{
	if (!bIsSynchronous && !Thread)
	{
		Thread = FRunnableThread::Create(this, TEXT("FAssetDataGatherer"), 0, TPri_BelowNormal);
		checkf(Thread, TEXT("Failed to create asset data gatherer thread"));
		Discovery->StartAsync();
	}
}

bool FAssetDataGatherer::Init()
{
	return true;
}

uint32 FAssetDataGatherer::Run()
{
	constexpr double IdleSleepTime = 0.1;
	while (!IsStopped)
	{
		InnerTickLoop(false /* bInIsSynchronousTick */, true /* bContributeToCacheSave */);

		for (;;)
		{
			{
				FGathererScopeLock ResultsScopeLock(&ResultsLock); // bIsIdle requires the lock
				if (IsStopped || bSaveAsyncCacheTriggered || (!IsPaused && !bIsIdle))
				{
					break;
				}
			}
			// No work to do. Sleep for a little and try again later.
			// TODO: Need IsPaused to be a condition variable so we avoid sleeping while waiting for it and then taking a long time to wake after it is unset.
			FPlatformProcess::Sleep(IdleSleepTime);
		}
	}
	return 0;
}

void FAssetDataGatherer::InnerTickLoop(bool bInIsSynchronousTick, bool bContributeToCacheSave)
{
	// Synchronous ticks during Wait contribute to saving of the async cache only if there is no dedicated async thread to do it (bIsSynchronous is true)
	// The dedicated async thread always contributes
	bContributeToCacheSave = !bInIsSynchronousTick || (bIsSynchronous && bContributeToCacheSave);

	FString CacheFilename;
	TArray<TPair<FName, FDiskCachedAssetData*>> AssetsToSave;
	{
		CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
		FGathererScopeLock RunScopeLock(&TickLock);
		TGuardValue<bool> ScopeSynchronousTick(bIsSynchronousTick, bInIsSynchronousTick);
		TRACE_CPUPROFILER_EVENT_SCOPE(FAssetDataGatherer::Tick);
		bool bTickInterruptionEvent = false;
		while (!IsStopped && (bInIsSynchronousTick || !IsPaused) && !bTickInterruptionEvent)
		{
			TickInternal(bTickInterruptionEvent);
		}

		if (bContributeToCacheSave)
		{
			TryReserveSaveAsyncCache(CacheFilename, AssetsToSave);
		}
	}
	SaveCacheFileInternal(CacheFilename, AssetsToSave, true /* bIsAsyncCache */);
}

FAssetDataGatherer::FScopedPause::FScopedPause(const FAssetDataGatherer& InOwner)
	:Owner(InOwner)
{
	if (!Owner.bIsSynchronous)
	{
		Owner.IsPaused++;
	}
}

FAssetDataGatherer::FScopedPause::~FScopedPause()
{
	if (!Owner.bIsSynchronous)
	{
		check(Owner.IsPaused > 0)
		Owner.IsPaused--;
	}
}

void FAssetDataGatherer::Stop()
{
	Discovery->Stop();
	IsStopped++;
}

void FAssetDataGatherer::Exit()
{
}

bool FAssetDataGatherer::IsSynchronous() const
{
	return bIsSynchronous;
}

void FAssetDataGatherer::EnsureCompletion()
{
	Discovery->EnsureCompletion();

	Stop();

	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

void FAssetDataGatherer::TickInternal(bool& bOutIsTickInterrupt)
{
	using namespace UE::AssetDataGather::Private;

	const int32 BatchSize = FTaskGraphInterface::Get().GetNumWorkerThreads() * AssetDataGathererConstants::SingleThreadFilesPerBatch;
	check(BatchSize > 0); // GetNumWorkerThreads should never return 0
	typedef TInlineAllocator<AssetDataGathererConstants::ExpectedMaxBatchSize> FBatchInlineAllocator;

	TArray<FGatheredPathData, FBatchInlineAllocator> LocalFilesToSearch;
	TArray<FAssetData*, FBatchInlineAllocator> LocalAssetResults;
	TArray<FPackageDependencyData, FBatchInlineAllocator> LocalDependencyResults;
	TArray<FString, FBatchInlineAllocator> LocalCookedPackageNamesWithoutAssetDataResults;
	FString LocalCacheFilename;
	double LocalLastCacheWriteTime = 0.0;
	bool bWaitBatchCountDecremented = false;
	bOutIsTickInterrupt = false;

	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);

		if (bFirstTickAfterIdle)
		{
			bFirstTickAfterIdle = false;
			LastCacheWriteTime = FPlatformTime::Seconds();
			SearchStartTime = LastCacheWriteTime;
		}

		IngestDiscoveryResults();

		// Take a batch off of the work list. If we're waiting only on the first WaitBatchCount results don't take more than that
		int32 NumToProcess = FMath::Min<int32>(BatchSize-LocalFilesToSearch.Num(), FilesToSearch.Num());
		if (WaitBatchCount > 0)
		{
			bWaitBatchCountDecremented = true;
			NumToProcess = FMath::Min(NumToProcess, WaitBatchCount);
			WaitBatchCount -= NumToProcess;
			if (WaitBatchCount == 0)
			{
				bOutIsTickInterrupt = true;
			}
		}

		while (NumToProcess > 0)
		{
			LocalFilesToSearch.Add(FilesToSearch.PopFrontValue());
			--NumToProcess;
		}

		// If all work is finished mark idle and exit
		if (LocalFilesToSearch.Num() == 0 && bDiscoveryIsComplete)
		{
			WaitBatchCount = 0; // Clear WaitBatchCount in case it was set higher than FilesToSearch.Num()
			bOutIsTickInterrupt = true;

			if (!bFinishedInitialDiscovery)
			{
				bSaveAsyncCacheTriggered = true;
			}
			SetIsIdle(true);
			return;
		}
		if (bUseTickManagedCache && !bHasLoadedTickManagedCache)
		{
			LocalCacheFilename = TickManagedCacheFilename;
		}
		LocalLastCacheWriteTime = LastCacheWriteTime;
	}

	// Load the async cache if not yet loaded
	if (!LocalCacheFilename.IsEmpty())
	{
		LoadCacheFileInternal(LocalCacheFilename);
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		bHasLoadedTickManagedCache = true;
	}

	struct FReadContext
	{
		FName PackageName;
		FName Extension;
		const FGatheredPathData& AssetFileData;
		TArray<FAssetData*> AssetDataFromFile;
		FPackageDependencyData DependencyData;
		TArray<FString> CookedPackageNamesWithoutAssetData;
		bool bCanAttemptAssetRetry = false;
		bool bResult = false;
		bool bCanceled = false;

		FReadContext(FName InPackageName, FName InExtension, const FGatheredPathData& InAssetFileData)
			: PackageName(InPackageName)
			, Extension(InExtension)
			, AssetFileData(InAssetFileData)
		{
		}
	};

	// Try to read each file in the batch out of the cache, and accumulate a list for more expensive reading of all of the files that are not in the cache 
	TArray<FReadContext> ReadContexts;
	for (const FGatheredPathData& AssetFileData : LocalFilesToSearch)
	{
		const FName PackageName = *AssetFileData.LongPackageName;
		const FName Extension = FName(*FPaths::GetExtension(AssetFileData.LocalAbsPath));

		FDiskCachedAssetData** DiskCachedAssetDataPtr = DiskCachedAssetDataMap.Find(PackageName);
		FDiskCachedAssetData* DiskCachedAssetData = DiskCachedAssetDataPtr ? *DiskCachedAssetDataPtr : nullptr;
		if (DiskCachedAssetData)
		{
			// Check whether we need to invalidate the cached data
			const FDateTime& CachedTimestamp = DiskCachedAssetData->Timestamp;
			if (AssetFileData.PackageTimestamp != CachedTimestamp)
			{
				DiskCachedAssetData = nullptr;
			}
			else if ((DiskCachedAssetData->DependencyData.PackageName != PackageName && DiskCachedAssetData->DependencyData.PackageName != NAME_None) ||
				DiskCachedAssetData->Extension != Extension)
			{
				UE_LOG(LogAssetRegistry, Display, TEXT("Cached dependency data for package '%s' is invalid. Discarding cached data."), *PackageName.ToString());
				DiskCachedAssetData = nullptr;
			}
		}

		if (DiskCachedAssetData)
		{
			// Add the valid cached data to our results, and to the map of data we keep to write out the new version of the cache file
			++NumCachedFiles;

			LocalAssetResults.Reserve(LocalAssetResults.Num() + DiskCachedAssetData->AssetDataList.Num());
			for (const FAssetData& AssetData : DiskCachedAssetData->AssetDataList)
			{
				LocalAssetResults.Add(new FAssetData(AssetData));
			}

			if (bGatherDependsData)
			{
				LocalDependencyResults.Add(DiskCachedAssetData->DependencyData);
			}

			AddToCache(PackageName, DiskCachedAssetData);
		}
		else
		{
			ReadContexts.Emplace(PackageName, Extension, AssetFileData);
		}
	}

	// For all the files not found in the cache, read them from their package files on disk; the file reads are done in parallel
	ParallelFor(ReadContexts.Num(),
		[this, &ReadContexts](int32 Index)
		{
			FReadContext& ReadContext = ReadContexts[Index];
			if (!bIsSynchronousTick && IsPaused)
			{
				ReadContext.bCanceled = true;
				return;
			}
			ReadContext.bResult = ReadAssetFile(ReadContext.AssetFileData.LocalAbsPath, ReadContext.AssetDataFromFile, ReadContext.DependencyData, ReadContext.CookedPackageNamesWithoutAssetData, ReadContext.bCanAttemptAssetRetry);
		},
		EParallelForFlags::Unbalanced | EParallelForFlags::BackgroundPriority
	);

	// Accumulate the results
	bool bHasCancelation = false;
	TArray<FGatheredPathData> LocalFilesToRetry;
	for (FReadContext& ReadContext : ReadContexts)
	{
		if (ReadContext.bCanceled)
		{
			bHasCancelation = true;
		}
		else if (ReadContext.bResult)
		{
			++NumUncachedFiles;

			// Add the results from a cooked package into our results on cooked package
			LocalCookedPackageNamesWithoutAssetDataResults.Append(MoveTemp(ReadContext.CookedPackageNamesWithoutAssetData));
			// Do not add the results from a cooked package into the map of data we keep to write out the new version of the cache file
			bool bCachePackage = bCacheEnabled && LocalCookedPackageNamesWithoutAssetDataResults.Num() == 0;
			if (bCachePackage)
			{
				for (const FAssetData* AssetData : ReadContext.AssetDataFromFile)
				{
					if (!!(AssetData->PackageFlags & PKG_FilterEditorOnly))
					{
						bCachePackage = false;
						break;
					}
				}
			}

			// Add the results from non-cooked packages into the map of data we keep to write out the new version of the cache file 
			if (bCachePackage)
			{
				// Update the cache
				FDiskCachedAssetData* NewData = new FDiskCachedAssetData(ReadContext.AssetFileData.PackageTimestamp, ReadContext.Extension);
				NewData->AssetDataList.Reserve(ReadContext.AssetDataFromFile.Num());
				for (const FAssetData* BackgroundAssetData : ReadContext.AssetDataFromFile)
				{
					NewData->AssetDataList.Add(*BackgroundAssetData);
				}

				// MoveTemp only used if we don't need DependencyData anymore
				if (bGatherDependsData)
				{
					NewData->DependencyData = ReadContext.DependencyData;
				}
				else
				{
					NewData->DependencyData = MoveTemp(ReadContext.DependencyData);
				}

				NewCachedAssetData.Add(NewData);
				AddToCache(ReadContext.PackageName, NewData);
			}

			// Add the results from the package into our output results
			LocalAssetResults.Append(MoveTemp(ReadContext.AssetDataFromFile));
			if (bGatherDependsData)
			{
				LocalDependencyResults.Add(MoveTemp(ReadContext.DependencyData));
			}
		}
		else if (ReadContext.bCanAttemptAssetRetry)
		{
			// If the read temporarily failed, return it to the worklist, pushed to the end
			FGathererScopeLock ResultsScopeLock(&ResultsLock);
			FilesToSearch.Add(ReadContext.AssetFileData);
		}
	}

	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);

		// Submit the results into the thread-shared lists
		AssetResults.Append(MoveTemp(LocalAssetResults));
		DependencyResults.Append(MoveTemp(LocalDependencyResults));
		CookedPackageNamesWithoutAssetDataResults.Append(MoveTemp(LocalCookedPackageNamesWithoutAssetDataResults));

		if (bHasCancelation)
		{
			// If we skipped reading files due to a pause request, push the canceled files back onto the FilesToSearch
			for (int Index = ReadContexts.Num() - 1; Index >= 0; --Index) // AddToFront in reverse order so that the elements are readded in the same order they were popped
			{
				FReadContext& ReadContext = ReadContexts[Index];
				if (ReadContext.bCanceled)
				{
					FilesToSearch.AddFront(ReadContext.AssetFileData);
					if (bWaitBatchCountDecremented)
					{
						++WaitBatchCount;
					}
				}
			}
		}

		if (bUseTickManagedCache && !bIsSavingAsyncCache 
			&& FPlatformTime::Seconds() - LocalLastCacheWriteTime >= AssetDataGathererConstants::MinSecondsToElapseBeforeCacheWrite)
		{
			bSaveAsyncCacheTriggered = true;
			bOutIsTickInterrupt = true;
		}
	}
}

void FAssetDataGatherer::IngestDiscoveryResults()
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TickLock);
	CHECK_IS_LOCKED_CURRENT_THREAD(ResultsLock);
	Discovery->GetAndTrimSearchResults(bDiscoveryIsComplete, DiscoveredPaths, FilesToSearch, NumPathsToSearchAtLastSyncPoint);
}

bool FAssetDataGatherer::ReadAssetFile(const FString& AssetFilename, TArray<FAssetData*>& AssetDataList, FPackageDependencyData& DependencyData, TArray<FString>& CookedPackageNamesWithoutAssetData, bool& OutCanRetry) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FAssetDataGatherer::ReadAssetFile);
	OutCanRetry = false;
	AssetDataList.Reset();

	FPackageReader PackageReader;

	FPackageReader::EOpenPackageResult OpenPackageResult;
	if (!PackageReader.OpenPackageFile(AssetFilename, &OpenPackageResult))
	{
		// If we're missing a custom version, we might be able to load this package later once the module containing that version is loaded...
		//   -	We can only attempt a retry in editors (not commandlets) that haven't yet finished initializing (!GIsRunning), as we 
		//		have no guarantee that a commandlet or an initialized editor is going to load any more modules/plugins
		//   -	Likewise, we can only attempt a retry for asynchronous scans, as during a synchronous scan we won't be loading any 
		//		modules/plugins so it would last forever
		const bool bAllowRetry = GIsEditor && !bInitialPluginsLoaded && !bIsSynchronousTick;
		OutCanRetry = bAllowRetry && OpenPackageResult == FPackageReader::EOpenPackageResult::CustomVersionMissing;
		return false;
	}
	else
	{
		return ReadAssetFile(PackageReader, AssetDataList, (bGatherDependsData ? &DependencyData : nullptr), CookedPackageNamesWithoutAssetData);
	}
}

bool FAssetDataGatherer::ReadAssetFile(FPackageReader& PackageReader, TArray<FAssetData*>& AssetDataList, FPackageDependencyData* DependencyData, TArray<FString>& CookedPackageNamesWithoutAssetData)
{
	if (PackageReader.ReadAssetRegistryDataIfCookedPackage(AssetDataList, CookedPackageNamesWithoutAssetData))
	{
		// Cooked data is special. No further data is found in these packages
		return true;
	}

	if (!PackageReader.ReadAssetRegistryData(AssetDataList))
	{
		if (!PackageReader.ReadAssetDataFromThumbnailCache(AssetDataList))
		{
			// It's ok to keep reading even if the asset registry data doesn't exist yet
			//return false;
		}
	}

	if ( DependencyData )
	{
		if ( !PackageReader.ReadDependencyData(*DependencyData) )
		{
			return false;
		}

		// DEPRECATION_TODO: Remove this fixup-on-load once we bump EUnrealEngineObjectUE4Version VER_UE4_ASSETREGISTRY_DEPENDENCYFLAGS and therefore all projects will resave each ObjectRedirector
		// UObjectRedirectors were originally incorrectly marked as having editor-only imports, since UObjectRedirector is an editor-only class. But UObjectRedirectors are followed during cooking
		// and so their imports should be considered used-in-game. Mark all dependencies in the package as used in game if the package has a UObjectRedirector object
		FName RedirectorClassName = UObjectRedirector::StaticClass()->GetFName();
		if (Algo::AnyOf(AssetDataList, [RedirectorClassName](FAssetData* AssetData) { return AssetData->AssetClass == RedirectorClassName; }))
		{
			TBitArray<>& ImportUsedInGame = DependencyData->ImportUsedInGame;
			for (int32 ImportNum = ImportUsedInGame.Num(), Index = 0; Index < ImportNum; ++Index)
			{
				ImportUsedInGame[Index] = true;
			}
		}
		// END DEPRECATION_TODO
	}

	return true;
}

void FAssetDataGatherer::AddToCache(FName PackageName, FDiskCachedAssetData* DiskCachedAssetData)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TickLock);
	FDiskCachedAssetData*& ValueInMap = NewCachedAssetDataMap.FindOrAdd(PackageName, DiskCachedAssetData);
	if (ValueInMap != DiskCachedAssetData)
	{
		// An updated DiskCachedAssetData for the same package; replace the existing DiskCachedAssetData with the new one.
		// Note that memory management of the DiskCachedAssetData is handled in a separate structure; we do not need to delete the old value here.
		if (DiskCachedAssetData->Extension != ValueInMap->Extension)
		{
			// Two files with the same package name but different extensions, e.g. basename.umap and basename.uasset
			// This is invalid - some systems in the engine (Cooker's FPackageNameCache) assume that package : filename is 1 : 1 - so issue a warning
			// Because it is invalid, we don't fully support it here (our map is keyed only by packagename), and will remove from cache all but the last filename we find with the same packagename
			// TODO: Turn this into a warning once all sample projects have fixed it
			UE_LOG(LogAssetRegistry, Display, TEXT("Multiple files exist with the same package name %s but different extensions (%s and %s). ")
				TEXT("This is invalid and will cause errors; merge or rename or delete one of the files."),
				*PackageName.ToString(), *ValueInMap->Extension.ToString(), *DiskCachedAssetData->Extension.ToString());
		}
		ValueInMap = DiskCachedAssetData;
	}
}

void FAssetDataGatherer::GetAndTrimSearchResults(bool& bOutIsSearching, TRingBuffer<FAssetData*>& OutAssetResults, TRingBuffer<FString>& OutPathResults, TRingBuffer<FPackageDependencyData>& OutDependencyResults,
	TRingBuffer<FString>& OutCookedPackageNamesWithoutAssetDataResults, TArray<double>& OutSearchTimes, int32& OutNumFilesToSearch, int32& OutNumPathsToSearch, bool& OutIsDiscoveringFiles)
{
	FGathererScopeLock ResultsScopeLock(&ResultsLock);

	auto MoveAppendRangeToRingBuffer = [](auto& OutRingBuffer, auto& InArray)
	{
		OutRingBuffer.Reserve(OutRingBuffer.Num() + InArray.Num());
		for (auto& Element : InArray)
		{
			OutRingBuffer.Add(MoveTemp(Element));
		}
		InArray.Reset();
	};

	MoveAppendRangeToRingBuffer(OutAssetResults, AssetResults);
	MoveAppendRangeToRingBuffer(OutPathResults, DiscoveredPaths);
	MoveAppendRangeToRingBuffer(OutDependencyResults, DependencyResults);
	MoveAppendRangeToRingBuffer(OutCookedPackageNamesWithoutAssetDataResults, CookedPackageNamesWithoutAssetDataResults);

	OutSearchTimes.Append(MoveTemp(SearchTimes));
	SearchTimes.Reset();

	OutNumFilesToSearch = FilesToSearch.Num();
	OutNumPathsToSearch = NumPathsToSearchAtLastSyncPoint;
	OutIsDiscoveringFiles = !bDiscoveryIsComplete;

	if (bIsIdle && !bIsComplete)
	{
		bIsComplete = true;
		Shrink();
	}
	bOutIsSearching = !bIsIdle;
}

void FAssetDataGatherer::WaitOnPath(FStringView InPath)
{
	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		if (bIsIdle)
		{
			return;
		}
	}
	FString LocalAbsPath = NormalizeLocalPath(InPath);
	Discovery->SetPropertiesAndWait(LocalAbsPath, false /* bAddToWhitelist */, false /* bForceRescan */, false /* bIgnoreBlacklistScanFilters */);
	WaitOnPathsInternal(TConstArrayView<FString>(&LocalAbsPath, 1), FString(), TArray<FString>());
}

void FAssetDataGatherer::ScanPathsSynchronous(const TArray<FString>& InLocalPaths, bool bForceRescan, bool bIgnoreBlackListScanFilters, const FString& SaveCacheFilename, const TArray<FString>& SaveCacheLongPackageNameDirs)
{
	TArray<FString> LocalAbsPaths;
	LocalAbsPaths.Reserve(InLocalPaths.Num());
	for (const FString& LocalPath : InLocalPaths)
	{
		LocalAbsPaths.Add(NormalizeLocalPath(LocalPath));
	}

	for (const FString& LocalAbsPath : LocalAbsPaths)
	{
		Discovery->SetPropertiesAndWait(LocalAbsPath, true /* bAddToWhitelist */, bForceRescan, bIgnoreBlackListScanFilters);
	}

	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		SetIsIdle(false);
	}

	WaitOnPathsInternal(LocalAbsPaths, SaveCacheFilename, SaveCacheLongPackageNameDirs);
}

void FAssetDataGatherer::WaitOnPathsInternal(TConstArrayView<FString> LocalAbsPaths, const FString& SaveCacheFilename, const TArray<FString>& SaveCacheLongPackageNameDirs)
{
	// Request a halt to the async tick
	FScopedPause ScopedPause(*this);
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	{
		FGathererScopeLock TickScopeLock(&TickLock);

		// Read all results from Discovery into our worklist and then sort our worklist
		{
			FGathererScopeLock ResultsScopeLock(&ResultsLock);
			IngestDiscoveryResults();

			int32 NumDiscoveredPaths;
			SortPathsByPriority(LocalAbsPaths, NumDiscoveredPaths);
			if (NumDiscoveredPaths == 0)
			{
				return;
			}
			WaitBatchCount = NumDiscoveredPaths;
		}
	}

	// We do not contribute to the async cache save if we have been given a modular cache to save 
	bool bContributeToCacheSave = SaveCacheFilename.IsEmpty();

	// Tick until NumDiscoveredPaths have been read
	TRACE_CPUPROFILER_EVENT_SCOPE(FAssetDataGatherer::Tick);
	for (;;)
	{
		InnerTickLoop(true /* bInIsSynchronousTick */, bContributeToCacheSave);
		FGathererScopeLock ResultsScopeLock(&ResultsLock); // WaitBatchCount requires the lock
		if (WaitBatchCount == 0)
		{
			break;
		}
	}

	if (!SaveCacheFilename.IsEmpty())
	{
		TArray<TPair<FName, FDiskCachedAssetData*>> AssetsToSave;
		{
			FGathererScopeLock TickScopeLock(&TickLock);
			GetAssetsToSave(SaveCacheLongPackageNameDirs, AssetsToSave);
		}
		SaveCacheFileInternal(SaveCacheFilename, AssetsToSave, false /* bIsAsyncCacheSave */);
	}
}

void FAssetDataGatherer::WaitForIdle()
{
	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		if (bIsIdle)
		{
			return;
		}
	}
	Discovery->WaitForIdle();
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);

	// Request a halt to the async tick
	FScopedPause ScopedPause(*this);
	// Tick until idle
	for (;;)
	{
		InnerTickLoop(true /* bInIsSynchronousTick */, true /* bContributeToCacheSave */);
		FGathererScopeLock ResultsScopeLock(&ResultsLock); // bIsIdle requires the lock
		if (bIsIdle)
		{
			break;
		}
	}
}

bool FAssetDataGatherer::IsComplete() const
{
	FGathererScopeLock ResultsScopeLock(&ResultsLock);
	return bIsComplete;
}

void FAssetDataGatherer::SetInitialPluginsLoaded()
{
	bInitialPluginsLoaded = true;
}

bool FAssetDataGatherer::IsGatheringDependencies() const
{
	return bGatherDependsData;
}

bool FAssetDataGatherer::IsCacheEnabled() const
{
	return bCacheEnabled;
}

FString FAssetDataGatherer::GetCacheFilename(TConstArrayView<FString> CacheFilePackagePaths)
{
	// Try and build a consistent hash for this input
	// Normalize the paths; removing any trailing /
	TArray<FString> SortedPaths(CacheFilePackagePaths);
	for (FString& PackagePath : SortedPaths)
	{
		while (PackagePath.Len() > 1 && PackagePath.EndsWith(TEXT("/")))
		{
			PackagePath.LeftChopInline(1);
		}
	}

	// Sort the paths
	SortedPaths.StableSort();

	// todo: handle hash collisions?
	uint32 CacheHash = SortedPaths.Num() > 0 ? GetTypeHash(SortedPaths[0]) : 0;
	for (int32 PathIndex = 1; PathIndex < SortedPaths.Num(); ++PathIndex)
	{
		CacheHash = HashCombine(CacheHash, GetTypeHash(SortedPaths[PathIndex]));
	}

	return FPaths::ProjectIntermediateDir() / TEXT("AssetRegistryCache") / FString::Printf(TEXT("%08x%s.bin"), CacheHash, bGatherDependsData ? TEXT("") : TEXT("NoDeps"));
}

void FAssetDataGatherer::LoadCacheFile(FStringView CacheFilename)
{
	FScopedPause ScopedPause(*this);
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TickScopeLock(&TickLock);
	LoadCacheFileInternal(CacheFilename);
}

void FAssetDataGatherer::LoadCacheFileInternal(FStringView CacheFilename)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TickLock);
	if (!bCacheEnabled)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(LoadCacheFile);
	// load the cached data
	TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileReader(*FString(CacheFilename), FILEREAD_Silent));
	if (FileAr && !FileAr->IsError() && FileAr->TotalSize() > 2 * sizeof(uint32))
	{
		uint32 MagicNumber = 0;
		*FileAr << MagicNumber;
		if (!FileAr->IsError() && MagicNumber == AssetDataGathererConstants::CacheSerializationMagic)
		{
			FAssetRegistryVersion::Type RegistryVersion;
			if (FAssetRegistryVersion::SerializeVersion(*FileAr, RegistryVersion) && RegistryVersion == FAssetRegistryVersion::LatestVersion)
			{
				FAssetRegistryReader RegistryReader(*FileAr);
				if (!RegistryReader.IsError())
				{
					SerializeCacheLoad(RegistryReader);

					FGathererScopeLock ResultsScopeLock(&ResultsLock);
					DependencyResults.Reserve(DiskCachedAssetDataMap.Num());
					AssetResults.Reserve(DiskCachedAssetDataMap.Num());
				}
			}
		}
	}
}

void FAssetDataGatherer::TryReserveSaveAsyncCache(FString& OutCacheFilename, TArray<TPair<FName,FDiskCachedAssetData*>>& AssetsToSave)
{
	OutCacheFilename.Reset();
	if (IsStopped)
	{
		return;
	}
	if (!bSaveAsyncCacheTriggered || bIsSavingAsyncCache)
	{
		return;
	}
	CHECK_IS_LOCKED_CURRENT_THREAD(TickLock);
	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		if (bUseTickManagedCache && !TickManagedCacheFilename.IsEmpty())
		{
			OutCacheFilename = TickManagedCacheFilename;
		}
	}
	if (!OutCacheFilename.IsEmpty())
	{
		GetAssetsToSave(TArrayView<const FString>(), AssetsToSave);
		bIsSavingAsyncCache = true;
	}
	bSaveAsyncCacheTriggered = false;
}

void FAssetDataGatherer::GetAssetsToSave(TArrayView<const FString> SaveCacheLongPackageNameDirs, TArray<TPair<FName,FDiskCachedAssetData*>>& OutAssetsToSave)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TickLock);

	OutAssetsToSave.Reset();
	if (SaveCacheLongPackageNameDirs.IsEmpty())
	{
		OutAssetsToSave.Reserve(NewCachedAssetDataMap.Num());
		for (const TPair<FName, FDiskCachedAssetData*>& Pair : NewCachedAssetDataMap)
		{
			OutAssetsToSave.Add(Pair);
		}
	}
	else
	{
		for (const TPair<FName, FDiskCachedAssetData*>& Pair : NewCachedAssetDataMap)
		{
			TStringBuilder<128> PackageNameStr;
			Pair.Key.ToString(PackageNameStr);
			if (Algo::AnyOf(SaveCacheLongPackageNameDirs, [PackageNameSV = FStringView(PackageNameStr)](const FString& SaveCacheLongPackageNameDir)
			{
				return FPathViews::IsParentPathOf(SaveCacheLongPackageNameDir, PackageNameSV);
			}))
			{
				OutAssetsToSave.Add(Pair);
			}
		}
	}
}

void FAssetDataGatherer::SaveCacheFileInternal(const FString& CacheFilename, const TArray<TPair<FName,FDiskCachedAssetData*>>& AssetsToSave, bool bIsAsyncCacheSave)
{
	if (CacheFilename.IsEmpty() || !bCacheEnabled)
	{
		return;
	}
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(TickLock);
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	TRACE_CPUPROFILER_EVENT_SCOPE(SaveCacheFile);
	// Save to a temp file first, then move to the destination to avoid corruption
	FString CacheFilenameStr(CacheFilename);
	FString TempFilename = CacheFilenameStr + TEXT(".tmp");
	TUniquePtr<FArchive> FileAr(IFileManager::Get().CreateFileWriter(*TempFilename, 0));
	if (FileAr)
	{
		int32 MagicNumber = AssetDataGathererConstants::CacheSerializationMagic;
		*FileAr << MagicNumber;

		FAssetRegistryVersion::Type RegistryVersion = FAssetRegistryVersion::LatestVersion;
		FAssetRegistryVersion::SerializeVersion(*FileAr, RegistryVersion);
#if ALLOW_NAME_BATCH_SAVING
		{
			// We might be able to reduce load time by using AssetRegistry::SerializationOptions
			// to save certain common tags as FName.
			FAssetRegistryWriter Ar(FAssetRegistryWriterOptions(), *FileAr);
			SerializeCacheSave(Ar, AssetsToSave);
		}
#else		
		checkf(false, TEXT("Cannot save asset registry cache in this configuration"));
#endif
		// Close file handle before moving temp file to target 
		FileAr.Reset();
		IFileManager::Get().Move(*CacheFilenameStr, *TempFilename);
	}
	else
	{
		UE_LOG(LogAssetRegistry, Error, TEXT("Failed to open file for write %s"), *TempFilename);
	}

	if (bIsAsyncCacheSave)
	{
		FScopedPause ScopedPause(*this);
		FGathererScopeLock TickScopeLock(&TickLock);
		bIsSavingAsyncCache = false;
		LastCacheWriteTime = FPlatformTime::Seconds();
	}
}

void FAssetDataGatherer::SerializeCacheSave(FAssetRegistryWriter& Ar, const TArray<TPair<FName,FDiskCachedAssetData*>>& AssetsToSave)
{
#if ALLOW_NAME_BATCH_SAVING
	using namespace UE::AssetDataGather::Private;

	double SerializeStartTime = FPlatformTime::Seconds();

	// serialize number of objects
	int32 LocalNumAssets = AssetsToSave.Num();
	Ar << LocalNumAssets;

	for (const TPair<FName, FDiskCachedAssetData*>& Pair : AssetsToSave)
	{
		FName AssetName = Pair.Key;
		Ar << AssetName;
		Pair.Value->SerializeForCache(Ar);
	}

	UE_LOG(LogAssetRegistry, Verbose, TEXT("Asset data gatherer serialized in %0.6f seconds"), FPlatformTime::Seconds() - SerializeStartTime);
#endif
}

void FAssetDataGatherer::SerializeCacheLoad(FAssetRegistryReader& Ar)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(TickLock);
	using namespace UE::AssetDataGather::Private;

	double SerializeStartTime = FPlatformTime::Seconds();
	// serialize number of objects
	int32 LocalNumAssets = 0;
	Ar << LocalNumAssets;

	const int32 MinAssetEntrySize = sizeof(int32);
	int32 MaxPossibleNumAssets = (Ar.TotalSize() - Ar.Tell()) / MinAssetEntrySize;
	if (Ar.IsError() || LocalNumAssets < 0 || MaxPossibleNumAssets < LocalNumAssets)
	{
		Ar.SetError();
	}
	else
	{
		FSoftObjectPathSerializationScope SerializationScope(NAME_None, NAME_None, ESoftObjectPathCollectType::NeverCollect, ESoftObjectPathSerializeType::AlwaysSerialize);

		// allocate one single block for all asset data structs (to reduce tens of thousands of heap allocations)
		FName* PackageNameBlock = new FName[LocalNumAssets];
		FDiskCachedAssetData* AssetDataBlock = new FDiskCachedAssetData[LocalNumAssets];
		for (int32 AssetIndex = 0; AssetIndex < LocalNumAssets; ++AssetIndex)
		{
			// Load the name first to add the entry to the tmap below
			Ar << PackageNameBlock[AssetIndex];
			AssetDataBlock[AssetIndex].SerializeForCache(Ar);
			if (Ar.IsError())
			{
				// There was an error reading the cache. Bail out.
				break;
			}
		}

		if (Ar.IsError())
		{
			delete[] AssetDataBlock;
			UE_LOG(LogAssetRegistry, Error, TEXT("There was an error loading the asset registry cache."));
		}
		else
		{
			DiskCachedAssetDataMap.Reserve(DiskCachedAssetDataMap.Num() + LocalNumAssets);
			for (int32 AssetIndex = 0; AssetIndex < LocalNumAssets; ++AssetIndex)
			{
				DiskCachedAssetDataMap.Add(PackageNameBlock[AssetIndex], &AssetDataBlock[AssetIndex]);
			}
			DiskCachedAssetBlocks.Emplace(LocalNumAssets, AssetDataBlock);
		}
		delete[] PackageNameBlock;
	}

	UE_LOG(LogAssetRegistry, Verbose, TEXT("Asset data gatherer serialized in %0.6f seconds"), FPlatformTime::Seconds() - SerializeStartTime);
}

uint32 FAssetDataGatherer::GetAllocatedSize() const
{
	using namespace UE::AssetDataGather::Private;
	auto GetArrayRecursiveAllocatedSize = [](auto Container)
	{
		int32 Result = Container.GetAllocatedSize();
		for (const auto& Value : Container)
		{
			Result += Value.GetAllocatedSize();
		}
		return Result;
	};

	uint32 Result = 0;
	if (Thread)
	{
		// TODO: Add size of Thread->GetAllocatedSize()
		Result += sizeof(*Thread);
	}

	Result += sizeof(*Discovery) + Discovery->GetAllocatedSize();

	FScopedPause ScopedPause(*this);
	CHECK_IS_NOT_LOCKED_CURRENT_THREAD(ResultsLock);
	FGathererScopeLock TickScopeLock(&TickLock);
	FGathererScopeLock ResultsScopeLock(&ResultsLock);

	Result += GetArrayRecursiveAllocatedSize(FilesToSearch);

	Result += AssetResults.GetAllocatedSize();
	FAssetDataTagMapSharedView::FMemoryCounter TagMemoryUsage;
	for (FAssetData* Value : AssetResults)
	{
		Result += sizeof(*Value);
		Result += Value->ChunkIDs.GetAllocatedSize();
		TagMemoryUsage.Include(Value->TagsAndValues);
	}
	Result += TagMemoryUsage.GetFixedSize() + TagMemoryUsage.GetLooseSize();

	Result += GetArrayRecursiveAllocatedSize(DependencyResults);
	Result += GetArrayRecursiveAllocatedSize(CookedPackageNamesWithoutAssetDataResults);
	Result += SearchTimes.GetAllocatedSize();
	Result += GetArrayRecursiveAllocatedSize(DiscoveredPaths);
	Result += TickManagedCacheFilename.GetAllocatedSize();

	Result += NewCachedAssetData.GetAllocatedSize();
	for (const FDiskCachedAssetData* Value : NewCachedAssetData)
	{
		Result += sizeof(*Value);
		Result += Value->GetAllocatedSize();
	}
	Result += DiskCachedAssetBlocks.GetAllocatedSize();
	for (const TPair<int32, FDiskCachedAssetData*>& ArrayData : DiskCachedAssetBlocks)
	{
		Result += ArrayData.Get<0>() * sizeof(FDiskCachedAssetData);
	}
	Result += DiskCachedAssetDataMap.GetAllocatedSize();
	Result += NewCachedAssetDataMap.GetAllocatedSize();

	return Result;
}

void FAssetDataGatherer::Shrink()
{
	CHECK_IS_LOCKED_CURRENT_THREAD(ResultsLock);
	// TODO: Make TRingBuffer::Shrink
	TRingBuffer<UE::AssetDataGather::Private::FGatheredPathData> Buffer;
	Buffer.Reserve(FilesToSearch.Num());
	for (UE::AssetDataGather::Private::FGatheredPathData& File : FilesToSearch)
	{
		Buffer.Add(MoveTemp(File));
	}
	Swap(Buffer, FilesToSearch);
	AssetResults.Shrink();
	DependencyResults.Shrink();
	CookedPackageNamesWithoutAssetDataResults.Shrink();
	SearchTimes.Shrink();
	DiscoveredPaths.Shrink();
}

void FAssetDataGatherer::AddMountPoint(FStringView LocalPath, FStringView LongPackageName)
{
	Discovery->AddMountPoint(NormalizeLocalPath(LocalPath), NormalizeLongPackageName(LongPackageName));
	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		SetIsIdle(false);
	}
}

void FAssetDataGatherer::RemoveMountPoint(FStringView LocalPath)
{
	Discovery->RemoveMountPoint(NormalizeLocalPath(LocalPath));
}

void FAssetDataGatherer::AddRequiredMountPoints(TArrayView<FString> LocalPaths)
{
	TStringBuilder<128> MountPackageName;
	TStringBuilder<128> MountFilePath;
	TStringBuilder<128> RelPath;
	for (const FString& LocalPath : LocalPaths)
	{
		if (FPackageName::TryGetMountPointForPath(LocalPath, MountPackageName, MountFilePath, RelPath))
		{
			Discovery->AddMountPoint(NormalizeLocalPath(MountFilePath), NormalizeLongPackageName(MountPackageName));
		}
	}
	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		SetIsIdle(false);
	}
}

void FAssetDataGatherer::OnDirectoryCreated(FStringView LocalPath)
{
	Discovery->OnDirectoryCreated(NormalizeLocalPath(LocalPath));
	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		SetIsIdle(false);
	}
}

void FAssetDataGatherer::OnFilesCreated(TConstArrayView<FString> LocalPaths)
{
	TArray<FString> LocalAbsPaths;
	LocalAbsPaths.Reserve(LocalPaths.Num());
	for (const FString& LocalPath : LocalPaths)
	{
		LocalAbsPaths.Add(NormalizeLocalPath(LocalPath));
	}
	Discovery->OnFilesCreated(LocalAbsPaths);
	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		SetIsIdle(false);
	}
}

void FAssetDataGatherer::PrioritizeSearchPath(const FString& PathToPrioritize)
{
	using namespace UE::AssetDataGather::Private;

	FString LocalFilenamePathToPrioritize;
	if (FPackageName::TryConvertLongPackageNameToFilename(PathToPrioritize, LocalFilenamePathToPrioritize))
	{
		FSetPathProperties Properties;
		Properties.Priority = EPriority::High;
		SetDirectoryProperties(LocalFilenamePathToPrioritize, Properties);
	}
}

void FAssetDataGatherer::SetDirectoryProperties(FStringView LocalPath, const UE::AssetDataGather::Private::FSetPathProperties& InProperties)
{
	FString LocalAbsPath = NormalizeLocalPath(LocalPath);
	if (LocalAbsPath.Len() == 0)
	{
		return;
	}

	if (!Discovery->TrySetDirectoryProperties(LocalAbsPath, InProperties, false))
	{
		return;
	}

	{
		FGathererScopeLock ResultsScopeLock(&ResultsLock);
		SetIsIdle(false);
		if (InProperties.Priority.IsSet())
		{
			int32 NumPrioritizedPaths;
			SortPathsByPriority(TConstArrayView<FString>(&LocalAbsPath, 1), NumPrioritizedPaths);
		}
	}
}

void FAssetDataGatherer::SortPathsByPriority(TConstArrayView<FString> LocalAbsPathsToPrioritize, int32& OutNumPaths)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(ResultsLock);
	using namespace UE::AssetDataGather::Private;

	// This code needs to be as fast as possible since it is in a critical section!

	// Swap all priority files to the top of the list
	int32 LowestNonPriorityFileIdx = 0;
	int32 NumFilesToSearch = FilesToSearch.Num();
	for (int32 FilenameIdx = 0; FilenameIdx < NumFilesToSearch; ++FilenameIdx)
	{
		for (const FString& LocalAbsPathToPrioritize : LocalAbsPathsToPrioritize)
		{
			if (FPathViews::IsParentPathOf(LocalAbsPathToPrioritize, FilesToSearch[FilenameIdx].LocalAbsPath))
			{
				Swap(FilesToSearch[FilenameIdx], FilesToSearch[LowestNonPriorityFileIdx++]);
				break;
			}
		}
	}
	OutNumPaths = LowestNonPriorityFileIdx;
}

void FAssetDataGatherer::SetIsWhitelisted(FStringView LocalPath, bool bIsWhitelisted)
{
	using namespace UE::AssetDataGather::Private;

	FSetPathProperties Properties;
	Properties.IsWhitelisted = bIsWhitelisted;
	SetDirectoryProperties(LocalPath, Properties);
}

bool FAssetDataGatherer::IsWhitelisted(FStringView LocalPath) const
{
	return Discovery->IsWhitelisted(NormalizeLocalPath(LocalPath));
}

bool FAssetDataGatherer::IsBlacklisted(FStringView LocalPath) const
{
	return Discovery->IsBlacklisted(NormalizeLocalPath(LocalPath));
}

bool FAssetDataGatherer::IsMonitored(FStringView LocalPath) const
{
	return Discovery->IsMonitored(NormalizeLocalPath(LocalPath));
}

void FAssetDataGatherer::SetIsIdle(bool bInIsIdle)
{
	CHECK_IS_LOCKED_CURRENT_THREAD(ResultsLock);
	if (bInIsIdle == bIsIdle)
	{
		return;
	}

	bIsIdle = bInIsIdle;
	if (bIsIdle)
	{
		// bIsComplete will be set in GetAndTrimSearchResults
		double SearchTime = FPlatformTime::Seconds() - SearchStartTime;
		if (!bFinishedInitialDiscovery)
		{
			bFinishedInitialDiscovery = true;

			UE_LOG(LogAssetRegistry, Verbose, TEXT("Initial scan took %0.6f seconds (found %d cached assets, and loaded %d)"), (float)SearchTime, NumCachedFiles, NumUncachedFiles);
		}
		SearchTimes.Add(SearchTime);
	}
	else
	{
		bIsComplete = false;
		bDiscoveryIsComplete = false;
		bFirstTickAfterIdle = true;
	}
}

FString FAssetDataGatherer::NormalizeLocalPath(FStringView LocalPath)
{
	FString LocalAbsPath(LocalPath);
	LocalAbsPath = FPaths::ConvertRelativePathToFull(MoveTemp(LocalAbsPath));
	return LocalAbsPath;
}

FStringView FAssetDataGatherer::NormalizeLongPackageName(FStringView LongPackageName)
{
	// Conform LongPackageName to our internal format, which does not have a terminating redundant /
	if (LongPackageName.EndsWith(TEXT('/')))
	{
		LongPackageName = LongPackageName.LeftChop(1);
	}
	return LongPackageName;
}

