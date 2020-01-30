// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapSecureStorage.h"
#include "IMagicLeapSecureStoragePlugin.h"
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "Lumin/CAPIShims/LuminAPISecureStorage.h"

class FMagicLeapSecureStoragePlugin : public IMagicLeapSecureStoragePlugin
{
};

IMPLEMENT_MODULE(FMagicLeapSecureStoragePlugin, MagicLeapSecureStorage);

//////////////////////////////////////////////////////////////////////////

template<>
bool UMagicLeapSecureStorage::PutSecureBlob<FString>(const FString& Key, const FString* DataToStore)
{
#if WITH_MLSDK
	return MLSecureStoragePutBlob(TCHAR_TO_ANSI(*Key), reinterpret_cast<const uint8*>(TCHAR_TO_ANSI(*(*DataToStore))), DataToStore->Len() + 1) == MLResult_Ok;
#else
	return false;
#endif
}

template<>
bool UMagicLeapSecureStorage::GetSecureBlob<FString>(const FString& Key, FString& DataToRetrieve)
{
	uint8* blob = nullptr;
	size_t blobLength = 0;

	bool bResult = false;
#if WITH_MLSDK
	MLResult Result = MLSecureStorageGetBlob(TCHAR_TO_ANSI(*Key), &blob, &blobLength);
	if (Result == MLResult_Ok)
	{
		if (blob == nullptr)
		{
			UE_LOG(LogSecureStorage, Error, TEXT("Error retrieving secure blob with key %s"), *Key);
			bResult = false;
		}
		else
		{
			bResult = true;
			DataToRetrieve = FString(ANSI_TO_TCHAR(reinterpret_cast<ANSICHAR*>(blob)));
			// Replace with library function call when it comes online.
			MLSecureStorageFreeBlobBuffer(blob);
		}
	}
	else
	{
		bResult = false;
		UE_LOG(LogSecureStorage, Error, TEXT("MLSecureStorageGetBlob for key %s failed with error %s"), *Key, UTF8_TO_TCHAR(MLSecureStorageGetResultString(Result)));
	}
#endif //WITH_MLSDK

	return bResult;
}

bool UMagicLeapSecureStorage::PutSecureBool(const FString& Key, bool DataToStore)
{
	return PutSecureBlob<bool>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::PutSecureByte(const FString& Key, uint8 DataToStore)
{
	return PutSecureBlob<uint8>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::PutSecureInt(const FString& Key, int32 DataToStore)
{
	return PutSecureBlob<int32>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::PutSecureInt64(const FString& Key, int64 DataToStore)
{
	return PutSecureBlob<int64>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::PutSecureFloat(const FString& Key, float DataToStore)
{
	return PutSecureBlob<float>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::PutSecureString(const FString& Key, const FString& DataToStore)
{
	return PutSecureBlob<FString>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::PutSecureVector(const FString& Key, const FVector& DataToStore)
{
	return PutSecureBlob<FVector>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::PutSecureRotator(const FString& Key, const FRotator& DataToStore)
{
	return PutSecureBlob<FRotator>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::PutSecureTransform(const FString& Key, const FTransform& DataToStore)
{
	return PutSecureBlob<FTransform>(Key, &DataToStore);
}

bool UMagicLeapSecureStorage::GetSecureBool(const FString& Key, bool& DataToRetrieve)
{
	return GetSecureBlob<bool>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::GetSecureByte(const FString& Key, uint8& DataToRetrieve)
{
	return GetSecureBlob<uint8>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::GetSecureInt(const FString& Key, int32& DataToRetrieve)
{
	return GetSecureBlob<int32>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::GetSecureInt64(const FString& Key, int64& DataToRetrieve)
{
	return GetSecureBlob<int64>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::GetSecureFloat(const FString& Key, float& DataToRetrieve)
{
	return GetSecureBlob<float>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::GetSecureString(const FString& Key, FString& DataToRetrieve)
{
	return GetSecureBlob<FString>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::GetSecureVector(const FString& Key, FVector& DataToRetrieve)
{
	return GetSecureBlob<FVector>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::GetSecureRotator(const FString& Key, FRotator& DataToRetrieve)
{
	return GetSecureBlob<FRotator>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::GetSecureTransform(const FString& Key, FTransform& DataToRetrieve)
{
	return GetSecureBlob<FTransform>(Key, DataToRetrieve);
}

bool UMagicLeapSecureStorage::PutSecureSaveGame(const FString& Key, USaveGame* ObjectToStore)
{
	TArray<uint8> Bytes;
	bool bSuccess = UGameplayStatics::SaveGameToMemory(ObjectToStore, Bytes);

	if (!bSuccess || Bytes.Num() == 0)
	{
		UE_LOG(LogSecureStorage, Error, TEXT("Serialization of `%s` was unsuccessful."), *Key);
		return false;
	}

	return PutSecureBlobImpl(Key, Bytes.GetData(), Bytes.Num());
}

bool UMagicLeapSecureStorage::GenericPutSecureArray(const FString& Key, const FArrayProperty* ArrayProperty, void* TargetArray)
{

	FScriptArrayHelper ArrayHelper(ArrayProperty, TargetArray);
	const auto& ElementSize = ArrayProperty->Inner->ElementSize;
	size_t ByteNum = ElementSize * ArrayHelper.Num();

	// Contiguous memory to send to SecureStorage
	TArray<uint8> Bytes;
	Bytes.SetNum(ByteNum);

	// Generate the contiguous array from the FArrayProperty
	for (int32 i = 0; i < ArrayHelper.Num(); ++i)
	{
		ArrayProperty->Inner->CopySingleValueFromScriptVM(Bytes.GetData() + i * ElementSize, ArrayHelper.GetRawPtr(i));
	}

	return PutSecureBlobImpl(Key, Bytes.GetData(), ByteNum);

}

bool UMagicLeapSecureStorage::GetSecureSaveGame(const FString& Key, USaveGame*& ObjectToRetrieve)
{
	uint8* Data;
	size_t ArrayNum;
	bool bSuccess = GetSecureBlobImpl(Key, Data, ArrayNum);

	TArray<uint8> Bytes(Data, ArrayNum);
	ObjectToRetrieve = UGameplayStatics::LoadGameFromMemory(Bytes);

	if (!ObjectToRetrieve)
	{
		UE_LOG(LogSecureStorage, Error, TEXT("Deserialization of `%s` was unsuccessful."), *Key);

		if (bSuccess) 
		{
			FreeBlobBufferImpl(Data);
		}

		return false;
	}

	return bSuccess;
}

bool UMagicLeapSecureStorage::GenericGetSecureArray(const FString& Key, FArrayProperty* ArrayProperty, void* TargetArray)
{

	FScriptArrayHelper ArrayHelper(ArrayProperty, TargetArray);

	uint8* Data;
	size_t ArrayNum;
	bool bSuccess = GetSecureBlobImpl(Key, Data, ArrayNum);

	//Early out to avoid processing
	if (!bSuccess)
	{
		return false;
	}

	const int32& ElementSize = ArrayProperty->Inner->ElementSize;

	// Without type metadata, validate that the returned byte count is divisible by the requested element type.
	if (ArrayNum % ElementSize)
	{
		UE_LOG(LogSecureStorage, Error, TEXT("Size of blob data %s does not match the size of requested data type."), *Key);
		FreeBlobBufferImpl(Data);
		return false;
	}

	// Convert the byte size to the requested type size
	ArrayHelper.Resize(ArrayNum / ElementSize);

	// Copy the contiguous array into the property value locations
	for (int32 i = 0; i < ArrayHelper.Num(); ++i)
	{
		ArrayProperty->Inner->CopySingleValueToScriptVM(ArrayHelper.GetRawPtr(i), Data + i * ElementSize);
	}

	return true;

}

bool UMagicLeapSecureStorage::DeleteSecureData(const FString& Key)
{
#if WITH_MLSDK
	return MLSecureStorageDeleteBlob(TCHAR_TO_ANSI(*Key)) == MLResult_Ok;
#else
	return false;
#endif //WITH_MLSDK
}

bool UMagicLeapSecureStorage::PutSecureBlobImpl(const FString& Key, const uint8* DataToStore, size_t DataTypeSize)
{
#if WITH_MLSDK

	MLResult Result = MLSecureStoragePutBlob(TCHAR_TO_ANSI(*Key), DataToStore, DataTypeSize);

	if (Result != MLResult_Ok)
	{
		UE_LOG(LogSecureStorage, Error, TEXT("MLSecureStoragePutBlob for key %s failed with error %s"), *Key, UTF8_TO_TCHAR(MLSecureStorageGetResultString(Result)));
	}

	return Result == MLResult_Ok;
#else
	return false;
#endif //WITH_MLSDK
}

bool UMagicLeapSecureStorage::GetSecureBlobImpl(const FString& Key, uint8*& DataToRetrieve, size_t& DataTypeSize) 
{

	DataToRetrieve = nullptr;
	DataTypeSize = 0;

#if WITH_MLSDK
	MLResult Result = MLSecureStorageGetBlob(TCHAR_TO_ANSI(*Key), &DataToRetrieve, &DataTypeSize);
	if (Result == MLResult_Ok)
	{
		// Additional validation
		if (!DataToRetrieve)
		{
			UE_LOG(LogSecureStorage, Error, TEXT("Error retrieving secure blob with key %s. Blob was null."), *Key);
			return false;
		}

		return true;
	}
	
	if (Result != MLSecureStorageResult_BlobNotFound)
	{
		UE_LOG(LogSecureStorage, Error, TEXT("MLSecureStorageGetBlob for key %s failed with error %s"), *Key, UTF8_TO_TCHAR(MLSecureStorageGetResultString(Result)));
	}

#endif //WITH_MLSDK

	return false;
}

void UMagicLeapSecureStorage::FreeBlobBufferImpl(uint8* Buffer)
{
#if WITH_MLSDK
	MLSecureStorageFreeBlobBuffer(Buffer);
#endif //WITH_MLSDK
}
