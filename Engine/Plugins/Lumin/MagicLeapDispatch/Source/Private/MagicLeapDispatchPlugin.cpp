// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IMagicLeapDispatchPlugin.h"
#include "MagicLeapPluginUtil.h"
#include "Lumin/CAPIShims/LuminAPIDispatch.h"

#if PLATFORM_LUMIN
#include "Lumin/LuminPlatformFile.h"
#endif // PLATFORM_LUMIN

DEFINE_LOG_CATEGORY_STATIC(LogMagicLeapDispatch, Log, All);

#if WITH_MLSDK
EMagicLeapDispatchResult MLToUEDispatchResult(MLResult Result)
{
	#define CASE_DISPATCH_RESULT(x) case MLDispatchResult_##x : return EMagicLeapDispatchResult::x;
	#define CASE_ML_RESULT(x) case MLResult_##x : return EMagicLeapDispatchResult::x;

	switch(Result)
	{
		CASE_ML_RESULT(Ok)
		CASE_DISPATCH_RESULT(CannotStartApp)
		CASE_DISPATCH_RESULT(InvalidPacket)
		CASE_DISPATCH_RESULT(NoAppFound)
		CASE_DISPATCH_RESULT(AppPickerDialogFailure)
		CASE_ML_RESULT(AllocFailed)
		CASE_ML_RESULT(InvalidParam)
		CASE_ML_RESULT(UnspecifiedFailure)
		CASE_ML_RESULT(NotImplemented)
	}

	return EMagicLeapDispatchResult::UnspecifiedFailure;
}
#endif // WITH_MLSDK

class FMagicLeapDispatchPlugin : public IMagicLeapDispatchPlugin
{
public:
#if PLATFORM_LUMIN
	virtual EMagicLeapDispatchResult TryOpenApplication(const TArray<FLuminFileInfo>& DispatchFileList) override
	{
		EMagicLeapDispatchResult UEResult = EMagicLeapDispatchResult::NotImplemented;
		IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
		FLuminPlatformFile* LuminPlatformFile = static_cast<FLuminPlatformFile*>(&PlatformFile);

		MLDispatchPacket* Packet = nullptr;
		MLResult Result = MLDispatchAllocateEmptyPacket(&Packet);
		if (Result == MLResult_Ok && Packet != nullptr)
		{
			if (DispatchFileList.Num() > 0)
			{
				Result = MLDispatchAllocateFileInfoList(Packet, DispatchFileList.Num());
				UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchAllocateFileInfoList(%d) failed with error %s"), DispatchFileList.Num(), UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));
				if (Result == MLResult_Ok)
				{
					uint64_t AllocatedSize = 0;
					Result = MLDispatchGetFileInfoListLength(Packet, &AllocatedSize);
					UE_CLOG((AllocatedSize != DispatchFileList.Num()), LogMagicLeapDispatch, Error, TEXT("MLDispatchGetFileInfoListLength does not match expected length. %lld != %d"), AllocatedSize, DispatchFileList.Num());

					uint64_t i = 0;
					for (const FLuminFileInfo& DispatchFileInfo : DispatchFileList)
					{
						MLFileInfo* FileInfo = nullptr;
						Result = MLDispatchGetFileInfoByIndex(Packet, i, &FileInfo);
						UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchGetFileInfoByIndex(%lld) failed with error %s"), i, UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));
						++i;
						if (Result == MLResult_Ok && FileInfo != nullptr)
						{
							if (DispatchFileInfo.MimeType.Len() > 0)
							{
								Result = MLFileInfoSetMimeType(FileInfo, TCHAR_TO_UTF8(*DispatchFileInfo.MimeType));
								UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLFileInfoSetMimeType(%s) failed with error %s"), *DispatchFileInfo.MimeType, UTF8_TO_TCHAR(MLGetResultString(Result)));
							}
							if (DispatchFileInfo.FileName.Len() > 0)
							{
								UE_LOG(LogMagicLeapDispatch, Warning, TEXT("filename = %s"), *DispatchFileInfo.FileName);
								Result = MLFileInfoSetFileName(FileInfo, TCHAR_TO_UTF8(*DispatchFileInfo.FileName));
								UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLFileInfoSetFileName(%s) failed with error %s"), *DispatchFileInfo.FileName, UTF8_TO_TCHAR(MLGetResultString(Result)));
							}
							if (DispatchFileInfo.FileHandle != nullptr)
							{
								UE_LOG(LogMagicLeapDispatch, Warning, TEXT("setting file descriptor"));
								LuminPlatformFile->SetMLFileInfoFD(DispatchFileInfo.FileHandle, FileInfo);
							}

							Result = MLDispatchAddFileInfo(Packet, FileInfo);
							UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchAddFileInfo(%lld) failed with error %s"), i, UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));
						}
					}
				}
			}

			Result = MLDispatchTryOpenApplication(Packet);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchTryOpenApplication() failed with error %s"), UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));
			UEResult = MLToUEDispatchResult(Result);

			Result = MLDispatchReleasePacket(&Packet, true, false);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchReleasePacket() failed with error %s"), UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));
		}
		else
		{
			UEResult = MLToUEDispatchResult(Result);
		}

		return UEResult;
	}
#endif // PLATFORM_LUMIN

private:
	FMagicLeapAPISetup APISetup;
};

IMPLEMENT_MODULE(FMagicLeapDispatchPlugin, MagicLeapDispatch);
