// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/SavePackage.h"
#include "Serialization/AsyncLoading2.h"
#include "IO/IoDispatcher.h"

class FIoStoreWriterContext;
class FIoStoreWriter;
class FPackageStoreOptimizer;
class FPackageStorePackage;
class FPackageStoreContainerHeaderEntry;

class FPackageStoreWriter
	: public IPackageStoreWriter
{
public:
	IOSTOREUTILITIES_API FPackageStoreWriter(const FString& OutputPath, const ITargetPlatform* TargetPlatform);
	IOSTOREUTILITIES_API ~FPackageStoreWriter();
	IOSTOREUTILITIES_API void WritePackage(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;
	IOSTOREUTILITIES_API void Finalize() override;

private:
	struct FPendingPackage
	{
		FIoBuffer CookedExportsBuffer;
		FString FileName;
		TArray<FFileRegion> FileRegions;
	};

	void Flush(bool bAllowMissingImports);

	TUniquePtr<FPackageStoreOptimizer> PackageStoreOptimizer;
	TUniquePtr<FIoStoreWriterContext> IoStoreWriterContext;
	TUniquePtr<FIoStoreWriter> IoStoreWriter;
	FIoContainerId ContainerId = FIoContainerId::FromName(TEXT("global"));
	TMap<FPackageStorePackage*, FPendingPackage> PendingPackagesMap;
	TArray<FPackageStoreContainerHeaderEntry> CompletedPackages;
};