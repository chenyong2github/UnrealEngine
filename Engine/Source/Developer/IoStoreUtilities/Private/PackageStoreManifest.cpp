// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageStoreManifest.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"

FPackageStoreManifest::FPackageStoreManifest(const FString& InCookedOutputPath)
	: CookedOutputPath(InCookedOutputPath)
{
	FPaths::NormalizeFilename(CookedOutputPath);
}

void FPackageStoreManifest::BeginPackage(FName InputPackageName)
{
	FScopeLock Lock(&CriticalSection);
	FPackageInfo& PackageInfo = MainPackageInfoByNameMap.FindOrAdd(InputPackageName);
	PackageInfo.PackageName = InputPackageName;
	if (PackageInfo.PackageChunkId.IsValid())
	{
		FileNameByChunkIdMap.Remove(PackageInfo.PackageChunkId);
	}
	for (const FIoChunkId& BulkDataChunkId : PackageInfo.BulkDataChunkIds)
	{
		FileNameByChunkIdMap.Remove(BulkDataChunkId);
	}
	PackageInfo.BulkDataChunkIds.Reset();
	OptionalPackageInfoByNameMap.Remove(InputPackageName);
}

void FPackageStoreManifest::AddPackageData(FName InputPackageName, FName OutputPackageName, const FString& FileName, const FIoChunkId& ChunkId)
{
	FScopeLock Lock(&CriticalSection);
	FPackageInfo* PackageInfo = GetPackageInfo_NoLock(InputPackageName, OutputPackageName);
	check(PackageInfo);
	PackageInfo->PackageChunkId = ChunkId;
	if (!FileName.IsEmpty())
	{
		FileNameByChunkIdMap.Add(ChunkId, FileName);
	}
}

void FPackageStoreManifest::AddBulkData(FName InputPackageName, FName OutputPackageName, const FString& FileName, const FIoChunkId& ChunkId)
{
	FScopeLock Lock(&CriticalSection);
	FPackageInfo* PackageInfo = GetPackageInfo_NoLock(InputPackageName, OutputPackageName);
	check(PackageInfo);
	PackageInfo->BulkDataChunkIds.Add(ChunkId);
	if (!FileName.IsEmpty())
	{
		FileNameByChunkIdMap.Add(ChunkId, FileName);
	}
}

FIoStatus FPackageStoreManifest::Save(const TCHAR* Filename) const
{
	FScopeLock Lock(&CriticalSection);
	TStringBuilder<64> ChunkIdStringBuilder;
	auto ChunkIdToString = [&ChunkIdStringBuilder](const FIoChunkId& ChunkId)
	{
		ChunkIdStringBuilder.Reset();
		ChunkIdStringBuilder << ChunkId;
		return *ChunkIdStringBuilder;
	};

	FString JsonTcharText;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonTcharText);
	Writer->WriteObjectStart();

	if (ZenServerInfo)
	{
		Writer->WriteObjectStart(TEXT("ZenServer"));
		Writer->WriteObjectStart(TEXT("Settings"));
		ZenServerInfo->Settings.WriteToJson(*Writer);
		Writer->WriteObjectEnd();
		Writer->WriteValue(TEXT("ProjectId"), ZenServerInfo->ProjectId);
		Writer->WriteValue(TEXT("OplogId"), ZenServerInfo->OplogId);
		Writer->WriteObjectEnd();
	}
	
	Writer->WriteArrayStart(TEXT("Files"));
	for (const auto& KV : FileNameByChunkIdMap)
	{
		Writer->WriteObjectStart();
		FString RelativePath = KV.Value;
		FPaths::MakePathRelativeTo(RelativePath, *CookedOutputPath);
		Writer->WriteValue(TEXT("Path"), RelativePath);
		Writer->WriteValue(TEXT("ChunkId"), ChunkIdToString(KV.Key));
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();

	auto WritePackageInfoObject = [Writer, &ChunkIdToString](const FPackageInfo& PackageInfo, const TCHAR* Name = nullptr)
	{
		if (Name)
		{
			Writer->WriteObjectStart(Name);
		}
		else
		{
			Writer->WriteObjectStart();
		}
		Writer->WriteValue(TEXT("Name"), PackageInfo.PackageName.ToString());
		Writer->WriteValue(TEXT("PackageChunkId"), ChunkIdToString(PackageInfo.PackageChunkId));
		if (!PackageInfo.BulkDataChunkIds.IsEmpty())
		{
			Writer->WriteArrayStart(TEXT("BulkDataChunkIds"));
			for (const FIoChunkId& ChunkId : PackageInfo.BulkDataChunkIds)
			{
				Writer->WriteValue(ChunkIdToString(ChunkId));
			}
			Writer->WriteArrayEnd();
		}
		Writer->WriteObjectEnd();
	};

	Writer->WriteArrayStart(TEXT("Packages"));
	for (const auto& PackageNameInfoPair : MainPackageInfoByNameMap)
	{
		const FPackageInfo& PackageInfo = PackageNameInfoPair.Value;
		WritePackageInfoObject(PackageInfo);
	}
	Writer->WriteArrayEnd();

	Writer->WriteArrayStart(TEXT("OptionalPackages"));
	for (const auto& PackageNameInfoPair : OptionalPackageInfoByNameMap)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("PackageName"), PackageNameInfoPair.Key.ToString());
		{
			const FPackageInfo& PackageInfo = PackageNameInfoPair.Value;
			WritePackageInfoObject(PackageInfo, TEXT("PackageInfo"));
		}
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();


	Writer->WriteObjectEnd();
	Writer->Close();

	if (!FFileHelper::SaveStringToFile(JsonTcharText, Filename))
	{
		return FIoStatus(EIoErrorCode::FileOpenFailed);
	}

	return FIoStatus::Ok;
}

FIoStatus FPackageStoreManifest::Load(const TCHAR* Filename)
{
	FScopeLock Lock(&CriticalSection);
	MainPackageInfoByNameMap.Empty();
	OptionalPackageInfoByNameMap.Empty();
	FileNameByChunkIdMap.Empty();

	auto ChunkIdFromString = [](const FString& ChunkIdString)
	{
		FStringView ChunkIdStringView(*ChunkIdString, 24);
		uint8 Data[12];
		UE::String::HexToBytes(ChunkIdStringView, Data);
		FIoChunkId ChunkId;
		ChunkId.Set(Data, 12);
		return ChunkId;
	};

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, Filename))
	{
		return FIoStatus(EIoErrorCode::FileOpenFailed);
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return FIoStatus(EIoErrorCode::Unknown);
	}

	TSharedPtr<FJsonValue> ZenServerValue = JsonObject->Values.FindRef(TEXT("ZenServer"));
	if (ZenServerValue)
	{
		ZenServerInfo = MakeUnique<FZenServerInfo>();
		TSharedPtr<FJsonObject> ZenServerObject = ZenServerValue->AsObject();

		TSharedPtr<FJsonValue> SettingsValue = ZenServerObject->Values.FindRef(TEXT("Settings"));
		if (SettingsValue)
		{
			TSharedPtr<FJsonObject> SettingsObject = SettingsValue->AsObject();
			ZenServerInfo->Settings.ReadFromJson(*SettingsObject);
		}
		ZenServerInfo->ProjectId = ZenServerObject->Values.FindRef(TEXT("ProjectId"))->AsString();
		ZenServerInfo->OplogId = ZenServerObject->Values.FindRef(TEXT("OplogId"))->AsString();
	}

	TSharedPtr<FJsonValue> FilesArrayValue = JsonObject->Values.FindRef(TEXT("Files"));
	TArray<TSharedPtr<FJsonValue>> FilesArray = FilesArrayValue->AsArray();
	FileNameByChunkIdMap.Reserve(FilesArray.Num());
	for (const TSharedPtr<FJsonValue>& FileValue : FilesArray)
	{
		TSharedPtr<FJsonObject> FileObject = FileValue->AsObject();
		FIoChunkId ChunkId = ChunkIdFromString(FileObject->Values.FindRef(TEXT("ChunkId"))->AsString());
		FString RelativePath = FileObject->Values.FindRef(TEXT("Path"))->AsString();
		FileNameByChunkIdMap.Add(ChunkId, FPaths::Combine(CookedOutputPath, RelativePath));
	}


	auto ReadPackageInfo = [&ChunkIdFromString](TSharedPtr<FJsonObject> PackageObject, FPackageInfo& PackageInfo)
	{
		check(!PackageInfo.PackageName.IsNone());
		PackageInfo.PackageChunkId = ChunkIdFromString(PackageObject->Values.FindRef(TEXT("PackageChunkId"))->AsString());

		TSharedPtr<FJsonValue> BulkDataChunkIdsValue = PackageObject->Values.FindRef(TEXT("BulkDataChunkIds"));
		if (BulkDataChunkIdsValue.IsValid())
		{
			TArray<TSharedPtr<FJsonValue>> BulkDataChunkIdsArray = BulkDataChunkIdsValue->AsArray();
			PackageInfo.BulkDataChunkIds.Reserve(BulkDataChunkIdsArray.Num());
			for (const TSharedPtr<FJsonValue>& BulkDataChunkIdValue : BulkDataChunkIdsArray)
			{
				PackageInfo.BulkDataChunkIds.Add(ChunkIdFromString(BulkDataChunkIdValue->AsString()));
			}
		}
	};

	TArray<TSharedPtr<FJsonValue>> PackagesArray = JsonObject->Values.FindRef(TEXT("Packages"))->AsArray();
	MainPackageInfoByNameMap.Reserve(PackagesArray.Num());
	for (const TSharedPtr<FJsonValue>& PackageValue : PackagesArray)
	{
		TSharedPtr<FJsonObject> PackageObject = PackageValue->AsObject();
		FName PackageName = FName(PackageObject->Values.FindRef(TEXT("Name"))->AsString());

		FPackageInfo& PackageInfo = MainPackageInfoByNameMap.FindOrAdd(PackageName);
		PackageInfo.PackageName = PackageName;
		ReadPackageInfo(PackageObject, PackageInfo);
	}

	TArray<TSharedPtr<FJsonValue>> OptionalPackagesArray = JsonObject->Values.FindRef(TEXT("OptionalPackages"))->AsArray();
	OptionalPackageInfoByNameMap.Reserve(OptionalPackagesArray.Num());
	for (const TSharedPtr<FJsonValue>& PackageNameInfoPair : OptionalPackagesArray)
	{
		TSharedPtr<FJsonObject> PairObject = PackageNameInfoPair->AsObject();
		FName InputPackageName = FName(PairObject->Values.FindRef(TEXT("PackageName"))->AsString());

		TSharedPtr<FJsonObject> PackageObject = PairObject->Values.FindRef(TEXT("PackageInfo"))->AsObject();
		FName OutputPackageName = FName(PackageObject->Values.FindRef(TEXT("Name"))->AsString());

		FPackageInfo PackageInfo;
		PackageInfo.PackageName = OutputPackageName;
		ReadPackageInfo(PackageObject, PackageInfo);
		OptionalPackageInfoByNameMap.Add(InputPackageName, MoveTemp(PackageInfo));
	}

	return FIoStatus::Ok;
}

TArray<FPackageStoreManifest::FFileInfo> FPackageStoreManifest::GetFiles() const
{
	FScopeLock Lock(&CriticalSection);
	TArray<FFileInfo> Files;
	Files.Reserve(FileNameByChunkIdMap.Num());
	for (const auto& KV : FileNameByChunkIdMap)
	{
		Files.Add({ KV.Value, KV.Key });
	}
	return Files;
}

TArray<FPackageStoreManifest::FPackageInfo> FPackageStoreManifest::GetPackages() const
{
	FScopeLock Lock(&CriticalSection);
	TArray<FPackageInfo> Packages;
	MainPackageInfoByNameMap.GenerateValueArray(Packages);
	TArray<FPackageInfo> OptionalPackages;
	OptionalPackageInfoByNameMap.GenerateValueArray(OptionalPackages);
	Packages.Append(MoveTemp(OptionalPackages));
	return Packages;
}

FPackageStoreManifest::FZenServerInfo& FPackageStoreManifest::EditZenServerInfo()
{
	FScopeLock Lock(&CriticalSection);
	if (!ZenServerInfo)
	{
		ZenServerInfo = MakeUnique<FZenServerInfo>();
	}
	return *ZenServerInfo;
}

const FPackageStoreManifest::FZenServerInfo* FPackageStoreManifest::ReadZenServerInfo() const
{
	FScopeLock Lock(&CriticalSection);
	return ZenServerInfo.Get();
}

FPackageStoreManifest::FPackageInfo* FPackageStoreManifest::GetPackageInfo_NoLock(FName InputPackageName, FName OutputPackageName)
{
	FPackageInfo* PackageInfo = nullptr;
	if (InputPackageName == OutputPackageName)
	{
		PackageInfo = MainPackageInfoByNameMap.Find(InputPackageName);
	}
	else
	{
		TArray<FPackageInfo*, TInlineAllocator<2>> PackageInfos;
		OptionalPackageInfoByNameMap.MultiFindPointer(InputPackageName, PackageInfos);
		FPackageInfo** PackageInfoPtr = PackageInfos.FindByPredicate([OutputPackageName](FPackageInfo* Info)
			{
				return Info->PackageName == OutputPackageName;
			});
		// If we didn't find any, create one
		if (PackageInfoPtr == nullptr)
		{
			PackageInfo = &OptionalPackageInfoByNameMap.Add(InputPackageName);
			PackageInfo->PackageName = OutputPackageName;
		}
		else
		{
			PackageInfo = *PackageInfoPtr;
		}
	}
	return PackageInfo;
}

