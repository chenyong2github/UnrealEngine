// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/DiffPackageWriter.h"

#include "Containers/Map.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/ArchiveStackTrace.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Templates/UniquePtr.h"
#include "UObject/UObjectGlobals.h"

FDiffPackageWriter::FDiffPackageWriter(TUniquePtr<ICookedPackageWriter>&& InInner)
	: Inner(MoveTemp(InInner))
{
	GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxDiffsToLog"), MaxDiffsToLog, GEditorIni);
	// Command line override for MaxDiffsToLog
	FParse::Value(FCommandLine::Get(), TEXT("MaxDiffstoLog="), MaxDiffsToLog);

	bSaveForDiff = FParse::Param(FCommandLine::Get(), TEXT("SaveForDiff"));

	GConfig->GetBool(TEXT("CookSettings"), TEXT("IgnoreHeaderDiffs"), bIgnoreHeaderDiffs, GEditorIni);
	// Command line override for IgnoreHeaderDiffs
	if (bIgnoreHeaderDiffs)
	{
		bIgnoreHeaderDiffs = !FParse::Param(FCommandLine::Get(), TEXT("HeaderDiffs"));
	}
	else
	{
		bIgnoreHeaderDiffs = FParse::Param(FCommandLine::Get(), TEXT("IgnoreHeaderDiffs"));
	}
}

void FDiffPackageWriter::BeginPackage(const FBeginPackageInfo& Info)
{
	bIsDifferent = false;
	bDiffCallstack = false;
	DiffMap.Reset();

	BeginInfo = Info;
	Inner->BeginPackage(Info);
}

void FDiffPackageWriter::BeginDiffCallstack()
{
	bDiffCallstack = true;

	// The contract with the Inner is that Begin is paired with a single commit; send the old commit and the new begin
	FCommitPackageInfo CommitInfo;
	CommitInfo.bSucceeded = true;
	CommitInfo.PackageName = BeginInfo.PackageName;
	CommitInfo.WriteOptions = EWriteOptions::None;
	Inner->CommitPackage(MoveTemp(CommitInfo));
	Inner->BeginPackage(BeginInfo);
}

TFuture<FMD5Hash> FDiffPackageWriter::CommitPackage(FCommitPackageInfo&& Info)
{
	if (bDiffCallstack && bSaveForDiff)
	{
		// Write the package to _ForDiff, but do not write any sidecars
		EnumRemoveFlags(Info.WriteOptions, EWriteOptions::WriteSidecars);
		EnumAddFlags(Info.WriteOptions, EWriteOptions::SaveForDiff);
	}
	else
	{
		EnumRemoveFlags(Info.WriteOptions, EWriteOptions::Write);
	}
	return Inner->CommitPackage(MoveTemp(Info));
}

void FDiffPackageWriter::WritePackageData(const FPackageInfo& Info, FLargeMemoryWriter& ExportsArchive,
	const TArray<FFileRegion>& FileRegions)
{
	Inner->CompleteExportsArchiveForDiff(Info.PackageName, ExportsArchive);

	FArchiveStackTrace& Writer = static_cast<FArchiveStackTrace&>(ExportsArchive);
	ICookedPackageWriter::FPreviousCookedBytesData PreviousInnerData;
	Inner->GetPreviousCookedBytes(Info.PackageName, PreviousInnerData);

	FArchiveStackTrace::FPackageData PreviousPackageData;
	PreviousPackageData.Data = PreviousInnerData.Data.Get();
	PreviousPackageData.Size = PreviousInnerData.Size;
	PreviousPackageData.HeaderSize = PreviousInnerData.HeaderSize;
	PreviousPackageData.StartOffset = PreviousInnerData.StartOffset;

	if (bDiffCallstack)
	{
		TMap<FName, FArchiveDiffStats> PackageDiffStats;
		const TCHAR* CutoffString = TEXT("UEditorEngine::Save()");
		Writer.CompareWith(PreviousPackageData, *BeginInfo.LooseFilePath, Info.HeaderSize, CutoffString,
			MaxDiffsToLog, PackageDiffStats);

		//COOK_STAT(FSavePackageStats::NumberOfDifferentPackages++);
		//COOK_STAT(FSavePackageStats::MergeStats(PackageDiffStats));
	}
	else
	{
		bIsDifferent = !Writer.GenerateDiffMap(PreviousPackageData, Info.HeaderSize, MaxDiffsToLog, DiffMap);
	}

	Inner->WritePackageData(Info, ExportsArchive, FileRegions);
}

TUniquePtr<FLargeMemoryWriter> FDiffPackageWriter::CreateLinkerArchive(FName PackageName, UObject* Asset)
{
	// The entire package will be serialized to memory and then compared against package on disk.
	if (bDiffCallstack)
	{
		// Each difference will be logged with its Serialize call stack trace
		return TUniquePtr<FLargeMemoryWriter>(new FArchiveStackTrace(Asset, *PackageName.ToString(),
			true /* bInCollectCallstacks */, &DiffMap));
	}
	else
	{
		return TUniquePtr<FLargeMemoryWriter>(new FArchiveStackTrace(Asset, *PackageName.ToString(),
			false /* bInCollectCallstacks */));
	}
}