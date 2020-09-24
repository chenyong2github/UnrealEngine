// Copyright Epic Games, Inc. All Rights Reserved.

#include "IMagicLeapDispatchPlugin.h"
#include "Containers/Ticker.h"
#include "Containers/Queue.h"
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
	void StartupModule() override
	{
		TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapDispatchPlugin::Tick);
	}

	void ShutdownModule() override
	{
		FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	}

	bool Tick(float DeltaTime)
	{
#if WITH_MLSDK
		OAuthResponse IncomingResponse;
		if (IncomingResponses.Dequeue(IncomingResponse))
		{
			if (IncomingResponse.bRedirect)
			{
				OAuthMetadata.RedirectDelegate.ExecuteIfBound(IncomingResponse.Response);
			}
			else
			{
				OAuthMetadata.CancelDelegate.ExecuteIfBound(IncomingResponse.Response);
			}
			MLResult Result = MLDispatchOAuthUnregisterSchema(TCHAR_TO_UTF8(*OAuthMetadata.RedirectURI));
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchOAuthUnregisterSchema failed with error %s"), UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));
			Result = MLDispatchOAuthUnregisterSchema(TCHAR_TO_UTF8(*OAuthMetadata.CancelURI));
			UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchOAuthUnregisterSchema failed with error %s"), UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));
		}
#endif // WITH_MLSDK
		
		return true;
	}

#if WITH_MLSDK
	struct FOAuthMetadata
	{
		FString RedirectURI;
		FString CancelURI;
		FMagicLeapOAuthSchemaHandler RedirectDelegate;
		FMagicLeapOAuthSchemaHandler CancelDelegate;
	};

	FOAuthMetadata OAuthMetadata;

	struct OAuthResponse
	{
		FString Response;
		bool bRedirect;
	};

	TQueue<OAuthResponse, EQueueMode::Spsc> IncomingResponses;

	static void RedirectUriCb(MLDispatchOAuthResponse* Response)
	{
		if (Response)
		{
			FMagicLeapDispatchPlugin* This = static_cast<FMagicLeapDispatchPlugin*>(Response->context);
			if (This)
			{
				This->IncomingResponses.Enqueue({ UTF8_TO_TCHAR(Response->response), true });
			}
		}
	}

	static void CancelUriCb(MLDispatchOAuthResponse* Response)
	{
		if (Response)
		{
			FMagicLeapDispatchPlugin* This = static_cast<FMagicLeapDispatchPlugin*>(Response->context);
			if (This)
			{
				This->IncomingResponses.Enqueue({ UTF8_TO_TCHAR(Response->response), false });
			}
		}
	}
#endif // WITH_MLSDK

	EMagicLeapDispatchResult OpenOAuthWindow(const FString& OAuthURL, const FString& RedirectURI, const FString& CancelURI,
		const FMagicLeapOAuthSchemaHandler& RedirectUriDelegate, const FMagicLeapOAuthSchemaHandler CancelUriDelegate)
	{
#if WITH_MLSDK
		if (!TickDelegateHandle.IsValid())
		{
			TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);
		}

		OAuthMetadata.RedirectURI = RedirectURI;
		OAuthMetadata.CancelURI = CancelURI;
		OAuthMetadata.RedirectDelegate = RedirectUriDelegate;
		OAuthMetadata.CancelDelegate = CancelUriDelegate;

		MLDispatchOAuthCallbacks RedirectCallbacks = {};
		MLDispatchOAuthCallbacksInit(&RedirectCallbacks);
		RedirectCallbacks.oauth_schema_handler = RedirectUriCb;
		MLResult Result = MLDispatchOAuthRegisterSchemaEx(TCHAR_TO_UTF8(*RedirectURI), &RedirectCallbacks, this);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchOAuthRegisterSchemaEx() failed with error %s"), UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));

		MLDispatchOAuthCallbacks CancelCallbacks = {};
		MLDispatchOAuthCallbacksInit(&CancelCallbacks);
		CancelCallbacks.oauth_schema_handler = CancelUriCb;
		Result = MLDispatchOAuthRegisterSchemaEx(TCHAR_TO_UTF8(*CancelURI), &CancelCallbacks, this);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchOAuthRegisterSchemaEx() failed with error %s"), UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));

		Result = MLDispatchOAuthOpenWindow(TCHAR_TO_UTF8(*OAuthURL), TCHAR_TO_UTF8(*CancelURI));
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapDispatch, Error, TEXT("MLDispatchOAuthOpenWindow() failed with error %s"), UTF8_TO_TCHAR(MLDispatchGetResultString(Result)));

		return MLToUEDispatchResult(Result);
#else
		return EMagicLeapDispatchResult::NotImplemented;
#endif // WITH_MLSDK
	}

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
	FTickerDelegate TickDelegate;
	FDelegateHandle TickDelegateHandle;
};

IMPLEMENT_MODULE(FMagicLeapDispatchPlugin, MagicLeapDispatch);
