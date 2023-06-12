// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEModelData.h"

#include "NNE.h"
#include "NNEAttributeMap.h"
#include "NNEModelOptimizerInterface.h"
#include "NNERuntimeFormat.h"
#include "Serialization/CustomVersion.h"
#include "UObject/WeakInterfacePtr.h"
#include "EditorFramework/AssetImportData.h"

#if WITH_EDITOR
#include "Containers/StringFwd.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Memory/SharedBuffer.h"
#include "Misc/Guid.h"
#endif

enum Type
{
	Initial = 0,
	TargetRuntimesAndAssetImportData = 1,
	// -----<new versions can be added before this line>-------------------------------------------------
	// - this needs to be the last line (see note below)
	VersionPlusOne,
	LatestVersion = VersionPlusOne - 1
};

const FGuid UNNEModelData::GUID(0x9513202e, 0xeba1b279, 0xf17fe5ba, 0xab90c3f2);
FCustomVersionRegistration NNEModelDataVersion(UNNEModelData::GUID, LatestVersion, TEXT("NNEModelDataVersion"));//Always save with the latest version

#if WITH_EDITOR

inline UE::DerivedData::FCacheKey CreateCacheKey(const FGuid& FileDataId, const FString& RuntimeName)
{
	FString GuidString = FileDataId.ToString(EGuidFormats::Digits);
	return { UE::DerivedData::FCacheBucket(FWideStringView(*GuidString)), FIoHash::HashBuffer(MakeMemoryView(FTCHARToUTF8(RuntimeName))) };
}

inline FSharedBuffer GetFromDDC(const FGuid& FileDataId, const FString& RuntimeName)
{
	UE::DerivedData::FCacheGetValueRequest GetRequest;
	GetRequest.Name = FString("Get-") + RuntimeName + FString("-") + FileDataId.ToString(EGuidFormats::Digits);
	GetRequest.Key = CreateCacheKey(FileDataId,  RuntimeName);
	FSharedBuffer RawDerivedData;
	UE::DerivedData::FRequestOwner BlockingGetOwner(UE::DerivedData::EPriority::Blocking);
	UE::DerivedData::GetCache().GetValue({ GetRequest }, BlockingGetOwner, [&RawDerivedData](UE::DerivedData::FCacheGetValueResponse&& Response)
		{
			RawDerivedData = Response.Value.GetData().Decompress();
		});
	BlockingGetOwner.Wait();
	return RawDerivedData;
}

inline void PutIntoDDC(const FGuid& FileDataId, const FString& RuntimeName, FSharedBuffer& Data)
{
	UE::DerivedData::FCachePutValueRequest PutRequest;
	PutRequest.Name = FString("Put-") + RuntimeName + FString("-") + FileDataId.ToString(EGuidFormats::Digits);
	PutRequest.Key = CreateCacheKey(FileDataId, RuntimeName);
	PutRequest.Value = UE::DerivedData::FValue::Compress(Data);
	UE::DerivedData::FRequestOwner BlockingPutOwner(UE::DerivedData::EPriority::Blocking);
	UE::DerivedData::GetCache().PutValue({ PutRequest }, BlockingPutOwner);
	BlockingPutOwner.Wait();
}

#endif

inline TArray<uint8> CreateRuntimeDataBlob(const FString& RuntimeName, FString FileType, const TArray<uint8>& FileData)
{
	TWeakInterfacePtr<INNERuntime> NNERuntime = UE::NNECore::GetRuntime<INNERuntime>(RuntimeName);
	if (NNERuntime.IsValid())
	{
		return NNERuntime->CreateModelData(FileType, FileData);
	}
	else
	{
		UE_LOG(LogNNE, Error, TEXT("UNNEModelData: No runtime '%s' found. Valid runtimes are: "), *RuntimeName);
		TArrayView<TWeakInterfacePtr<INNERuntime>> Runtimes = UE::NNECore::GetAllRuntimes();
		for (int i = 0; i < Runtimes.Num(); i++)
		{
			UE_LOG(LogNNE, Error, TEXT("- %s"), *Runtimes[i]->GetRuntimeName());
		}
		return {};
	}
}

void UNNEModelData::Init(const FString& Type, TConstArrayView<uint8> Buffer)
{
	FileType = Type;
	FileData = Buffer;
	FPlatformMisc::CreateGuid(FileDataId);
	ModelData.Empty();
}

TConstArrayView<uint8> UNNEModelData::GetModelData(const FString& RuntimeName)
{
#if WITH_EDITORONLY_DATA
	// Check model data is supporting the requested target runtime
	TArrayView<const FString> TargetRuntimesNames = GetTargetRuntimes();
	if (!TargetRuntimesNames.IsEmpty() && !TargetRuntimesNames.Contains(RuntimeName))
	{
		UE_LOG(LogNNE, Error, TEXT("UNNEModelData: Runtime '%s' is not among the target runtimes. Target runtimes are: "), *RuntimeName);
		for (const FString& TargetRuntimesName : TargetRuntimesNames)
		{
			UE_LOG(LogNNE, Error, TEXT("- %s"), *TargetRuntimesName);
		}
		return {};
	}
#endif //WITH_EDITORONLY_DATA

	// Check if we have a local cache hit
	TArray<uint8>* LocalData = ModelData.Find(RuntimeName);
	if (LocalData)
	{
		return TConstArrayView<uint8>(LocalData->GetData(), LocalData->Num());
	}
	
#if WITH_EDITOR
	// Check if we have a remote cache hit
	FSharedBuffer RemoteData = GetFromDDC(FileDataId, RuntimeName);
	if (RemoteData.GetSize() > 0)
	{
		ModelData.Add(RuntimeName, TArray<uint8>((uint8*)RemoteData.GetData(), RemoteData.GetSize()));
		
		TArray<uint8>* CachedRemoteData = ModelData.Find(RuntimeName);
		return TConstArrayView<uint8>(CachedRemoteData->GetData(), CachedRemoteData->Num());
	}
#endif //WITH_EDITOR

	// Try to create the model
	TArray<uint8> CreatedData = CreateRuntimeDataBlob(RuntimeName, FileType, FileData);
	if (CreatedData.Num() < 1)
	{
		return {};
	}

	// Cache the model
	ModelData.Add(RuntimeName, CreatedData);

#if WITH_EDITOR
	// And put it into DDC
	FSharedBuffer SharedBuffer = MakeSharedBufferFromArray(MoveTemp(CreatedData));
	PutIntoDDC(FileDataId, RuntimeName, SharedBuffer);
#endif //WITH_EDITOR
	
	TArray<uint8>* CachedCreatedData = ModelData.Find(RuntimeName);
	return TConstArrayView<uint8>(CachedCreatedData->GetData(), CachedCreatedData->Num());
}

void UNNEModelData::Serialize(FArchive& Ar)
{
	// Store the asset version (no effect in load)
	Ar.UsingCustomVersion(UNNEModelData::GUID);

#if WITH_EDITORONLY_DATA
	// Recreate each model data when cooking
	if (Ar.IsCooking() && Ar.IsSaving())
	{
		ModelData.Reset();

		TArray<FString, TInlineAllocator<10>> CookedRuntimeNames;
		CookedRuntimeNames.Append(GetTargetRuntimes());

		//No target runtime means all currently registered ones.
		if (GetTargetRuntimes().IsEmpty())
		{
			for (const TWeakInterfacePtr<INNERuntime>& Runtime : UE::NNECore::GetAllRuntimes())
			{
				CookedRuntimeNames.Add(Runtime->GetRuntimeName());
			}
		}

		for (const FString& RuntimeName : CookedRuntimeNames)
		{
			TArray<uint8> CreatedData = CreateRuntimeDataBlob(RuntimeName, FileType, FileData);
			if (CreatedData.Num() > 0)
			{
				ModelData.Add(RuntimeName, CreatedData);
#if WITH_EDITOR
				FSharedBuffer SharedBuffer = MakeSharedBufferFromArray(MoveTemp(CreatedData));
				PutIntoDDC(FileDataId, RuntimeName, SharedBuffer);
#endif //WITH_EDITOR
			}
		}

		// Dummy data for fields not required in the game
		TArray<uint8> EmptyData;
		TArray<FString> RuntimeNames;
		ModelData.GetKeys(RuntimeNames);
		int32 NumItems = RuntimeNames.Num();

		Ar << FileType;
		Ar << EmptyData;
		Ar << FileDataId;
		Ar << NumItems;

		for (int i = 0; i < NumItems; i++)
		{
			Ar << RuntimeNames[i];
			Ar << ModelData[RuntimeNames[i]];
		}
	}
	else
#endif //WITH_EDITORONLY_DATA
	{
		int32 NumItems = 0;

#if WITH_EDITORONLY_DATA
		if (Ar.CustomVer(UNNEModelData::GUID) >= TargetRuntimesAndAssetImportData)
		{
			Ar << TargetRuntimes;
			Ar << AssetImportData;
		}
		else 
		{
			// AssetImportData should always be valid
			AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
		}
#endif //WITH_EDITORONLY_DATA

		Ar << FileType;
		Ar << FileData;
		Ar << FileDataId;
		Ar << NumItems;

		if (Ar.IsLoading())
		{
			for (int i = 0; i < NumItems; i++)
			{
				FString Name;
				Ar << Name;
				TArray<uint8> Data;
				Ar << Data;
				ModelData.Add(Name, MoveTemp(Data));
			}
		}
	}
}

#if WITH_EDITORONLY_DATA
namespace UE::NNECore::ModelDataHelpers
{
	FString GetRuntimesAsString(TArrayView<const FString> Runtimes)
	{
		if (Runtimes.Num() == 0)
		{
			return TEXT("All");
		}

		FString RuntimesAsOneString;
		bool bIsFirstRuntime = true;

		for (const FString& Runtime : Runtimes)
		{
			if (!bIsFirstRuntime)
			{
				RuntimesAsOneString += TEXT(", ");
			}
			RuntimesAsOneString += Runtime;
			bIsFirstRuntime = false;
		}
		return RuntimesAsOneString;
	}
} // UE::NNECore::ModelDataHelpers

void UNNEModelData::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	Super::PostInitProperties();
}

void UNNEModelData::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}

	OutTags.Add(FAssetRegistryTag("TargetRuntimes", UE::NNECore::ModelDataHelpers::GetRuntimesAsString(GetTargetRuntimes()), FAssetRegistryTag::TT_Alphabetical));

	Super::GetAssetRegistryTags(OutTags);
}

void UNNEModelData::SetTargetRuntimes(TArrayView<const FString> RuntimeNames)
{
	TargetRuntimes = RuntimeNames;

	TArray<FString, TInlineAllocator<10>> CookedRuntimes;
	ModelData.GetKeys(CookedRuntimes);
	for (const FString& Runtime : CookedRuntimes)
	{
		if (!TargetRuntimes.Contains(Runtime))
		{
			ModelData.Remove(Runtime);
		}
	}
	ModelData.Compact();
}

#endif //WITH_EDITORONLY_DATA