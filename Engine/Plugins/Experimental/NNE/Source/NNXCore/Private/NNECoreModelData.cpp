// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNECoreModelData.h"

#include "NNECore.h"
#include "NNXCore.h"
#include "NNXRuntimeFormat.h"
#include "NNXModelOptimizerInterface.h"
#include "NNECoreAttributeMap.h"
#include "Serialization/CustomVersion.h"
#include "UObject/WeakInterfacePtr.h"

#if WITH_EDITOR
#include "Misc/Guid.h"
#include "Containers/StringFwd.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCache.h"
#include "Memory/SharedBuffer.h"
#include "DerivedDataRequestOwner.h"
#include "Internationalization/TextLocalizationResource.h"
#endif

const FGuid UNNEModelData::GUID(0x9513202e, 0xeba1b279, 0xf17fe5ba, 0xab90c3f2);
FCustomVersionRegistration NNEModelDataVersion(UNNEModelData::GUID, 0, TEXT("NNEModelDataVersion"));

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

inline TArray<uint8> Create(const FString& RuntimeName, FString FileType, const TArray<uint8>& FileData)
{
	NNX::IRuntime* NNXRuntime = NNX::GetRuntime(RuntimeName);
	if (NNXRuntime)
	{
		return NNXRuntime->CreateModelData(FileType, FileData);
	}

	TWeakInterfacePtr<INNERuntime> NNERuntime = UE::NNECore::GetRuntime<INNERuntime>(RuntimeName);
	if (NNERuntime.IsValid())
	{
		return NNERuntime->CreateModelData(FileType, FileData);
	}
	else
	{
		UE_LOG(LogNNX, Error, TEXT("UNNEModelData: No runtime '%s' found. Valid runtimes are: "), *RuntimeName);
		TArrayView<TWeakInterfacePtr<INNERuntime>> Runtimes = UE::NNECore::GetAllRuntimes();
		for (int i = 0; i < Runtimes.Num(); i++)
		{
			UE_LOG(LogNNX, Error, TEXT("- %s"), *Runtimes[i]->GetRuntimeName());
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
#if WITH_EDITOR

	using namespace NNX;

	// Check if we have a local cache hit
	TArray<uint8>* LocalData = ModelData.Find(RuntimeName);
	if (LocalData)
	{
		return TConstArrayView<uint8>(LocalData->GetData(), LocalData->Num());
	}

	// Check if we have a remote cache hit
	FSharedBuffer RemoteData = GetFromDDC(FileDataId, RuntimeName);
	if (RemoteData.GetSize() > 0)
	{
		ModelData.Add(RuntimeName, TArray<uint8>((uint8*)RemoteData.GetData(), RemoteData.GetSize()));
		
		TArray<uint8>* CachedRemoteData = ModelData.Find(RuntimeName);
		return TConstArrayView<uint8>(CachedRemoteData->GetData(), CachedRemoteData->Num());
	}

	// Try to create the model
	TArray<uint8> CreatedData = Create(RuntimeName, FileType, FileData);
	if (CreatedData.Num() < 1)
	{
		return {};
	}

	// Cache the model and put it into ddc
	ModelData.Add(RuntimeName, CreatedData);

	FSharedBuffer SharedBuffer = MakeSharedBufferFromArray(MoveTemp(CreatedData));
	PutIntoDDC(FileDataId, RuntimeName, SharedBuffer);

	TArray<uint8>* CachedCreatedData = ModelData.Find(RuntimeName);
	return TConstArrayView<uint8>(CachedCreatedData->GetData(), CachedCreatedData->Num());

#else

	TArray<uint8>* Data = ModelData.Find(RuntimeName);
	if (Data)
	{
		return TConstArrayView<uint8>(Data->GetData(), Data->Num());
	}
	return {};

#endif
}

void UNNEModelData::Serialize(FArchive& Ar)
{
	using namespace NNX;

	// Store the asset version (no effect in load)
	Ar.UsingCustomVersion(UNNEModelData::GUID);

	// Only one version is supported for now
	check(Ar.CustomVer(UNNEModelData::GUID) == 0);

	// Recreate each model data when cooking
	if (Ar.IsCooking() && Ar.IsSaving())
	{
		ModelData.Empty();
		TArray<NNX::IRuntime*> Runtimes = GetAllRuntimes();
		for (int i = 0; i < Runtimes.Num(); i++)
		{
			TArray<uint8> CreatedData = Create(Runtimes[i]->GetRuntimeName(), FileType, FileData);
			if (CreatedData.Num() > 0)
			{
				ModelData.Add(Runtimes[i]->GetRuntimeName(), CreatedData);
#if WITH_EDITOR
				FSharedBuffer SharedBuffer = MakeSharedBufferFromArray(MoveTemp(CreatedData));
				PutIntoDDC(FileDataId, Runtimes[i]->GetRuntimeName(), SharedBuffer);
#endif
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

		UE_LOG(LogTemp, Display, TEXT("UNNEModelData: Serialized data of %d runtimes"), NumItems);
	}
	else
	{
		int32 NumItems = 0;

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

			UE_LOG(LogTemp, Display, TEXT("UNNEModelData: Deserialized data of %d runtimes"), NumItems);
		}
	}
}