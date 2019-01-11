// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HttpModule.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "HttpManager.h"
#include "Http.h"
#include "NullHttp.h"
#include "HttpTests.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"

DEFINE_LOG_CATEGORY(LogHttp);

// FHttpModule

IMPLEMENT_MODULE(FHttpModule, HTTP);

FHttpModule* FHttpModule::Singleton = NULL;

static bool ShouldLaunchUrl(const TCHAR* Url)
{
	FString SchemeName;
	if (FParse::SchemeNameFromURI(Url, SchemeName) && (SchemeName == TEXT("http") || SchemeName == TEXT("https")))
	{
		FHttpManager& HttpManager = FHttpModule::Get().GetHttpManager();
		return HttpManager.IsDomainAllowed(Url);
	}

	return true;
}

void FHttpModule::UpdateConfigs()
{
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpTimeout"), HttpTimeout, GEngineIni);
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpConnectionTimeout"), HttpConnectionTimeout, GEngineIni);
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpReceiveTimeout"), HttpReceiveTimeout, GEngineIni);
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpSendTimeout"), HttpSendTimeout, GEngineIni);
	GConfig->GetInt(TEXT("HTTP"), TEXT("HttpMaxConnectionsPerServer"), HttpMaxConnectionsPerServer, GEngineIni);
	GConfig->GetBool(TEXT("HTTP"), TEXT("bEnableHttp"), bEnableHttp, GEngineIni);
	GConfig->GetBool(TEXT("HTTP"), TEXT("bUseNullHttp"), bUseNullHttp, GEngineIni);
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpDelayTime"), HttpDelayTime, GEngineIni);
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpThreadActiveFrameTimeInSeconds"), HttpThreadActiveFrameTimeInSeconds, GEngineIni);
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpThreadActiveMinimumSleepTimeInSeconds"), HttpThreadActiveMinimumSleepTimeInSeconds, GEngineIni);
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpThreadIdleFrameTimeInSeconds"), HttpThreadIdleFrameTimeInSeconds, GEngineIni);
	GConfig->GetFloat(TEXT("HTTP"), TEXT("HttpThreadIdleMinimumSleepTimeInSeconds"), HttpThreadIdleMinimumSleepTimeInSeconds, GEngineIni);

	AllowedDomains.Empty();
	GConfig->GetArray(TEXT("HTTP"), TEXT("AllowedDomains"), AllowedDomains, GEngineIni);
}

void FHttpModule::StartupModule()
{	
	Singleton = this;

	MaxReadBufferSize = 256 * 1024;
	HttpTimeout = 300.0f;
	HttpConnectionTimeout = -1;
	HttpReceiveTimeout = HttpConnectionTimeout;
	HttpSendTimeout = HttpConnectionTimeout;
	HttpMaxConnectionsPerServer = 16;
	bEnableHttp = true;
	bUseNullHttp = false;
	HttpDelayTime = 0;
	HttpThreadActiveFrameTimeInSeconds = 1.0f / 200.0f; // 200Hz
	HttpThreadActiveMinimumSleepTimeInSeconds = 0.0f;
	HttpThreadIdleFrameTimeInSeconds = 1.0f / 30.0f; // 30Hz
	HttpThreadIdleMinimumSleepTimeInSeconds = 0.0f;	

	// override the above defaults from configs
	UpdateConfigs();

	if (!FParse::Value(FCommandLine::Get(), TEXT("httpproxy="), ProxyAddress))
	{
		if (!GConfig->GetString(TEXT("HTTP"), TEXT("HttpProxyAddress"), ProxyAddress, GEngineIni))
		{
			if (TOptional<FString> OperatingSystemProxyAddress = FPlatformHttp::GetOperatingSystemProxyAddress())
			{
				ProxyAddress = MoveTemp(OperatingSystemProxyAddress.GetValue());
			}
		}
	}

	// Initialize FPlatformHttp after we have read config values
	FPlatformHttp::Init();

	HttpManager = FPlatformHttp::CreatePlatformHttpManager();
	if (NULL == HttpManager)
	{
		// platform does not provide specific HTTP manager, use generic one
		HttpManager = new FHttpManager();
	}
	HttpManager->Initialize();

	bSupportsDynamicProxy = HttpManager->SupportsDynamicProxy();

	FCoreDelegates::ShouldLaunchUrl.BindStatic(ShouldLaunchUrl);
}

void FHttpModule::PostLoadCallback()
{

}

void FHttpModule::PreUnloadCallback()
{
}

void FHttpModule::ShutdownModule()
{
	FCoreDelegates::ShouldLaunchUrl.Unbind();

	if (HttpManager != nullptr)
	{
		// block on any http requests that have already been queued up
		HttpManager->Flush(true);
	}

	// at least on Linux, the code in HTTP manager (e.g. request destructors) expects platform to be initialized yet
	delete HttpManager;	// can be passed NULLs

	FPlatformHttp::Shutdown();

	HttpManager = nullptr;
	Singleton = nullptr;
}

bool FHttpModule::HandleHTTPCommand(const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (FParse::Command(&Cmd, TEXT("TEST")))
	{
		int32 Iterations=1;
		FString IterationsStr;
		FParse::Token(Cmd, IterationsStr, true);
		if (!IterationsStr.IsEmpty())
		{
			Iterations = FCString::Atoi(*IterationsStr);
		}		
		FString Url;
		FParse::Token(Cmd, Url, true);
		if (Url.IsEmpty())
		{
			Url = TEXT("http://www.google.com");
		}		
		FHttpTest* HttpTest = new FHttpTest(TEXT("GET"),TEXT(""),Url,Iterations);
		HttpTest->Run();
	}
	else if (FParse::Command(&Cmd, TEXT("DUMPREQ")))
	{
		GetHttpManager().DumpRequests(Ar);
	}
	else if (FParse::Command(&Cmd, TEXT("FLUSH")))
	{
		GetHttpManager().Flush(false);
	}
#if !UE_BUILD_SHIPPING
	else if (FParse::Command(&Cmd, TEXT("FILEUPLOAD")))
	{
		FString UploadUrl, UploadFilename;
		bool bIsCmdOk = FParse::Token(Cmd, UploadUrl, false);
		bIsCmdOk &= FParse::Token(Cmd, UploadFilename, false);
		if (bIsCmdOk)
		{
			FString HttpMethod;
			if (!FParse::Token(Cmd, HttpMethod, false))
			{
				HttpMethod = TEXT("PUT");
			}

			TSharedRef<IHttpRequest> Request = CreateRequest();
			Request->SetURL(UploadUrl);
			Request->SetVerb(HttpMethod);
			Request->SetHeader(TEXT("Content-Type"), TEXT("application/x-uehttp-upload-test"));
			Request->SetContentAsStreamedFile(UploadFilename);
			Request->ProcessRequest();
		}
		else
		{
			UE_LOG(LogHttp, Warning, TEXT("Command expects params <upload url> <upload filename> [http verb]"))
		}
	}
#endif
	return true;
}

bool FHttpModule::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	// Ignore any execs that don't start with HTTP
	if (FParse::Command(&Cmd, TEXT("HTTP")))
	{
		return HandleHTTPCommand( Cmd, Ar );
	}
	return false;
}

FHttpModule& FHttpModule::Get()
{
	if (Singleton == NULL)
	{
		check(IsInGameThread());
		FModuleManager::LoadModuleChecked<FHttpModule>("HTTP");
	}
	check(Singleton != NULL);
	return *Singleton;
}

TSharedRef<IHttpRequest> FHttpModule::CreateRequest()
{
	if (bUseNullHttp)
	{
		return TSharedRef<IHttpRequest>(new FNullHttpRequest());
	}
	else
	{
		// Create the platform specific Http request instance
		return TSharedRef<IHttpRequest>(FPlatformHttp::ConstructRequest());
	}
}
