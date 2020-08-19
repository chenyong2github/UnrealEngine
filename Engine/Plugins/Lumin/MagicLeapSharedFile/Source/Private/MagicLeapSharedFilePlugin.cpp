// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapSharedFilePlugin.h"
#include "Lumin/CAPIShims/LuminAPISharedFile.h"
#include "Lumin/CAPIShims/LuminAPIFileInfo.h"
#include "Stats/Stats.h"

#if PLATFORM_LUMIN
#include "Lumin/LuminPlatformFile.h"
#endif // PLATFORM_LUMIN

DEFINE_LOG_CATEGORY(LogMagicLeapSharedFile);


#if WITH_MLSDK
bool GetFilenamesFromSharedFileList_Impl(MLSharedFileList* SharedFileList, TArray<FString>& OutSharedFileList)
{
	MLResult Result = MLResult_UnspecifiedFailure;
	OutSharedFileList.Empty();

	if (SharedFileList != nullptr)
	{
		MLHandle ListLength = 0;
		Result = MLSharedFileGetListLength(SharedFileList, &ListLength);
		UE_CLOG(MLResult_Ok != Result, LogMagicLeapSharedFile, Error, TEXT("MLSharedFileGetListLength failed with error '%s'"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));
		if (Result == MLResult_Ok)
		{
			for (MLHandle i = 0; i < ListLength; ++i)
			{
				MLFileInfo* FileInfo = nullptr;
				Result = MLSharedFileGetMLFileInfoByIndex(SharedFileList, i, &FileInfo);
				UE_CLOG(MLResult_Ok != Result, LogMagicLeapSharedFile, Error, TEXT("MLSharedFileGetMLFileInfoByIndex failed with error '%s'"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));
				if (Result == MLResult_Ok && FileInfo != nullptr)
				{
					const char* Filename = nullptr;
					Result = MLFileInfoGetFileName(FileInfo, &Filename);
					UE_CLOG(MLResult_Ok != Result, LogMagicLeapSharedFile, Error, TEXT("MLFileInfoGetFileName failed with error '%s'"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));
					if (Result == MLResult_Ok && Filename != nullptr)
					{
						OutSharedFileList.Add(UTF8_TO_TCHAR(Filename));
					}
				}
			}
		}

		Result = MLSharedFileListRelease(&SharedFileList);
		UE_CLOG(MLResult_Ok != Result, LogMagicLeapSharedFile, Error, TEXT("MLSharedFileListRelease failed with error '%s'"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));
	}

	return MLResult_Ok == Result;
}

void onFilesPicked(MLSharedFileList *list, void *context)
{
	check(context != nullptr);
	FMagicLeapSharedFilePlugin* Plugin = reinterpret_cast<FMagicLeapSharedFilePlugin*>(context);
	Plugin->GetFileNamesFromSharedFileList(reinterpret_cast<void*>(list));
}
#endif // WITH_MLSDK

void FMagicLeapSharedFilePlugin::StartupModule()
{
	IMagicLeapSharedFilePlugin::StartupModule();

	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapSharedFilePlugin::Tick);
}

void FMagicLeapSharedFilePlugin::ShutdownModule()
{
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IMagicLeapSharedFilePlugin::ShutdownModule();
}

IFileHandle* FMagicLeapSharedFilePlugin::SharedFileRead(const FString& FileName)
{
#if PLATFORM_LUMIN
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	FLuminPlatformFile* LuminPlatformFile = static_cast<FLuminPlatformFile*>(&PlatformFile);
	return LuminPlatformFile->SharedFileOpenRead(*FileName);
#else
	return nullptr;
#endif // PLATFORM_LUMIN
}

IFileHandle* FMagicLeapSharedFilePlugin::SharedFileWrite(const FString& FileName)
{
#if PLATFORM_LUMIN
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	FLuminPlatformFile* LuminPlatformFile = static_cast<FLuminPlatformFile*>(&PlatformFile);
	return LuminPlatformFile->SharedFileOpenWrite(*FileName);
#else
	return nullptr;
#endif // PLATFORM_LUMIN
}

bool FMagicLeapSharedFilePlugin::SharedFileListAccessibleFiles(TArray<FString>& OutSharedFileList)
{
	OutSharedFileList.Empty();
#if WITH_MLSDK
	MLSharedFileList* SharedFileList = nullptr;
	MLResult Result = MLSharedFileListAccessibleFiles(&SharedFileList);
	UE_CLOG(MLResult_Ok != Result, LogMagicLeapSharedFile, Error, TEXT("MLSharedFileListAccessibleFiles failed with error '%s'"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));
	if (Result == MLResult_Ok && SharedFileList != nullptr)
	{
		return GetFilenamesFromSharedFileList_Impl(SharedFileList, OutSharedFileList);
	}
#endif // WITH_MLSDK

	return false;
}

bool FMagicLeapSharedFilePlugin::SharedFilePickAsync(const FMagicLeapFilesPickedResultDelegate& InResultDelegate)
{
#if WITH_MLSDK
	ResultDelegate = InResultDelegate;

	{
		FScopeLock Lock(&Mutex);
		bWaitingForDelegateResult = true;
	}

	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);

	MLResult Result = MLSharedFilePick(onFilesPicked, this);
	if (MLResult_Ok != Result)
	{
		FScopeLock Lock(&Mutex);
		bWaitingForDelegateResult = true;
		FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
		UE_LOG(LogMagicLeapSharedFile, Error, TEXT("MLSharedFilePick failed with error '%s'"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)));
	}

	return Result == MLResult_Ok;
#else
	return false;
#endif // WITH_MLSDK
}

bool FMagicLeapSharedFilePlugin::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMagicLeapSharedFilePlugin_Tick);

	bool bWaitingForDelegateResult_Cached;
	FMagicLeapFilesPickedResultDelegate ResultDelegate_Cached;

	{
		FScopeLock Lock(&Mutex);
		bWaitingForDelegateResult_Cached = bWaitingForDelegateResult;
		ResultDelegate_Cached = ResultDelegate;
	}
	
	if (!bWaitingForDelegateResult_Cached)
	{
		FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
		ResultDelegate_Cached.ExecuteIfBound(PickedFileList);
	}

	return true;
}

void FMagicLeapSharedFilePlugin::GetFileNamesFromSharedFileList(void* SharedFiles)
{
#if WITH_MLSDK
	FScopeLock Lock(&Mutex);
	GetFilenamesFromSharedFileList_Impl(static_cast<MLSharedFileList*>(SharedFiles), PickedFileList);
	bWaitingForDelegateResult = false;
#endif // WITH_MLSDK
}

IMPLEMENT_MODULE(FMagicLeapSharedFilePlugin, MagicLeapSharedFile);
