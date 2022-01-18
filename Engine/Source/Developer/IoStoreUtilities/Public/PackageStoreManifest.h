// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoDispatcher.h"
#include "ZenServerInterface.h"

class FPackageStoreManifest
{
public:
	struct FFileInfo
	{
		FString FileName;
		FIoChunkId ChunkId;
	};

	struct FPackageInfo
	{
		FName PackageName;
		FIoChunkId PackageChunkId;
		TArray<FIoChunkId> BulkDataChunkIds;
	};
	
	struct FZenServerInfo
	{
		UE::Zen::FServiceSettings Settings;
		FString ProjectId;
		FString OplogId;
	};

	IOSTOREUTILITIES_API FPackageStoreManifest(const FString& CookedOutputPath);
	IOSTOREUTILITIES_API ~FPackageStoreManifest() = default;

	IOSTOREUTILITIES_API void BeginPackage(FName InputPackageName);
	IOSTOREUTILITIES_API void AddPackageData(FName InputPackageName, FName OutputPackageName, const FString& FileName, const FIoChunkId& ChunkId);
	IOSTOREUTILITIES_API void AddBulkData(FName InputPackageName, FName OutputPackageName, const FString& FileName, const FIoChunkId& ChunkId);

	IOSTOREUTILITIES_API FIoStatus Save(const TCHAR* Filename) const;
	IOSTOREUTILITIES_API FIoStatus Load(const TCHAR* Filename);

	IOSTOREUTILITIES_API TArray<FFileInfo> GetFiles() const;
	IOSTOREUTILITIES_API TArray<FPackageInfo> GetPackages() const;

	IOSTOREUTILITIES_API FZenServerInfo& EditZenServerInfo();
	IOSTOREUTILITIES_API const FZenServerInfo* ReadZenServerInfo() const;

private:
	FPackageInfo* GetPackageInfo_NoLock(FName InputPackageName, FName OutputPackageName);

	mutable FCriticalSection CriticalSection;
	FString CookedOutputPath;
	/** Main Package Output Info map */
	TMap<FName, FPackageInfo> MainPackageInfoByNameMap;
	/** Additionally Generated Package Info Output map */
	TMultiMap<FName, FPackageInfo> OptionalPackageInfoByNameMap;

	TMap<FIoChunkId, FString> FileNameByChunkIdMap;
	TUniquePtr<FZenServerInfo> ZenServerInfo;
};
