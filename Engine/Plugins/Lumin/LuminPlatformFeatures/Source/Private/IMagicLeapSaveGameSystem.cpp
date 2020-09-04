// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMagicLeapSaveGameSystem.h"
#include "GameDelegates.h"
#if WITH_MLSDK
#include "MagicLeapUtil.h"
#include "Lumin/CAPIShims/LuminAPISecureStorage.h"

DEFINE_LOG_CATEGORY_STATIC(LogSecureStorage, Display, All);

//
// Implementation members
//

FMagicLeapSaveGameSystem::FMagicLeapSaveGameSystem()
{

}

FMagicLeapSaveGameSystem::~FMagicLeapSaveGameSystem()
{

}

ISaveGameSystem::ESaveExistsResult FMagicLeapSaveGameSystem::DoesSaveGameExistWithResult(const TCHAR* Name, const int32 UserIndex)
{
	uint8* Bytes = nullptr;
	size_t ArrayNum = 0;
	MLResult Result = MLSecureStorageGetBlob(TCHAR_TO_ANSI(Name), &Bytes, &ArrayNum);

	if (Result == MLResult_Ok)
	{
		MLSecureStorageFreeBlobBuffer(Bytes);
		return ISaveGameSystem::ESaveExistsResult::OK;
	}
	else if (Result == MLSecureStorageResult_BlobNotFound)
	{
	    return ISaveGameSystem::ESaveExistsResult::DoesNotExist;
	}
	return ISaveGameSystem::ESaveExistsResult::UnspecifiedError;
}

bool FMagicLeapSaveGameSystem::SaveGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex, const TArray<uint8>& Data)
{
	MLResult Result = MLSecureStoragePutBlob(TCHAR_TO_ANSI(Name), Data.GetData(), Data.Num());
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogSecureStorage, Error, TEXT("MLSecureStoragePutBlob for key %s failed with error %s"), Name, UTF8_TO_TCHAR(MLSecureStorageGetResultString(Result)));
	}
	return Result == MLResult_Ok;
}

bool FMagicLeapSaveGameSystem::LoadGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex, TArray<uint8>& Data)
{
	uint8* Bytes = nullptr;
	size_t ArrayNum = 0;
	MLResult Result = MLSecureStorageGetBlob(TCHAR_TO_ANSI(Name), &Bytes, &ArrayNum);
	if (Result == MLResult_Ok)
	{
		// Additional validation
		if (!Bytes)
		{
			UE_LOG(LogSecureStorage, Error, TEXT("Error retrieving secure blob with key %s. Blob was null."), Name);
			return false;
		}
        Data = TArray<uint8>(Bytes, ArrayNum);
		MLSecureStorageFreeBlobBuffer(Bytes);
		return true;
	}
	UE_LOG(LogSecureStorage, Error, TEXT("MLSecureStorageGetBlob for key %s failed with error %s"), Name, UTF8_TO_TCHAR(MLSecureStorageGetResultString(Result)));
	return false;
}

bool FMagicLeapSaveGameSystem::DeleteGame(bool bAttemptToUseUI, const TCHAR* Name, const int32 UserIndex)
{
  return MLSecureStorageDeleteBlob(TCHAR_TO_ANSI(Name)) == MLResult_Ok;
}
#endif  // WITH_MLSDK
