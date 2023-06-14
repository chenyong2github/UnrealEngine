// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HttpManager.h"
#include "Http.h"
#include "Misc/CommandLine.h"
#include "TestHarness.h"

/**
 *  HTTP Tests
 *  -----------------------------------------------------------------------------------------------
 *
 *  PURPOSE:
 *
 *	Integration Tests to make sure all kinds of HTTP client features in C++ work well on different platforms,
 *  including but not limited to error handing, retrying, threading, streaming, SSL and profiling.
 *
 *  Refer to WebTests/README.md for more info about how to run these tests
 * 
 *  -----------------------------------------------------------------------------------------------
 */

#define HTTP_TAG "[HTTP]"
#define HTTP_TIME_DIFF_TOLERANCE 0.5f

class FHttpModuleTestFixture
{
public:
	FHttpModuleTestFixture()
		: WebServerIp(TEXT("127.0.0.1"))
	{
		ParseSettingsFromCommandLine();

		HttpModule = new FHttpModule();
		IModuleInterface* Module = HttpModule;
		Module->StartupModule();
	}

	virtual ~FHttpModuleTestFixture()
	{
		IModuleInterface* Module = HttpModule;
		Module->ShutdownModule();
		delete Module;
	}

	void ParseSettingsFromCommandLine()
	{
		FParse::Value(FCommandLine::Get(), TEXT("web_server_ip"), WebServerIp);
	}

	const FString UrlWithInvalidPortToTestConnectTimeout() const { return FString::Format(TEXT("http://{0}:{1}"), { *WebServerIp, 8765 }); }
	const FString UrlBase() const { return FString::Format(TEXT("http://{0}:{1}"), { *WebServerIp, 8000 }); }
	const FString UrlHttpTests() const { return FString::Format(TEXT("{0}/webtests/httptests"), { *UrlBase() }); }
	const FString UrlToTestMethods() const { return FString::Format(TEXT("{0}/methods"), { *UrlHttpTests() }); }

	FString WebServerIp;
	FHttpModule* HttpModule;
};

TEST_CASE_METHOD(FHttpModuleTestFixture, "Shutdown http module without issue when there are ongoing http requests.", HTTP_TAG)
{
	uint32 ChunkSize = 1024 * 1024;
	TArray<uint8> DataChunk;
	DataChunk.SetNum(ChunkSize);
	FMemory::Memset(DataChunk.GetData(), 'd', ChunkSize);

	for (int32 i = 0; i < 10; ++i)
	{
		IHttpRequest* LeakingHttpRequest = FPlatformHttp::ConstructRequest(); // Leaking in purpose to make sure it's ok

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
		HttpRequest->SetURL(UrlToTestMethods());
		HttpRequest->SetVerb(TEXT("PUT"));
		// TODO: Use some shared data, like cookie, openssl session etc.
		HttpRequest->SetContent(DataChunk);
		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
		});
		HttpRequest->ProcessRequest();
	}

	HttpModule->GetHttpManager().Tick(0.0f);
}

class FWaitUntilQuitFromTestFixture : public FHttpModuleTestFixture
{
public:
	FWaitUntilQuitFromTestFixture()
	{
	}

	~FWaitUntilQuitFromTestFixture()
	{
		WaitUntilAllHttpRequestsComplete();
	}

	void WaitUntilAllHttpRequestsComplete()
	{
		while (!bQuitRequested)
		{
			HttpModule->GetHttpManager().Tick(TickFrequency);
			FPlatformProcess::Sleep(TickFrequency);
		}
	}

	float TickFrequency = 1.0f / 60; /*60 FPS*/;
	bool bQuitRequested = false;
};

TEST_CASE_METHOD(FWaitUntilQuitFromTestFixture, "Http request can be reused", HTTP_TAG)
{
	TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());
	HttpRequest->SetVerb(TEXT("POST"));

	HttpRequest->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		CHECK(HttpResponse->GetResponseCode() == 200);

		uint32 Chunks = 3;
		uint32 ChunkSize = 1024;
		HttpRequest->SetURL(FString::Format(TEXT("{0}/streaming_download/{1}/{2}/"), { *UrlHttpTests(), Chunks, ChunkSize }));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->OnProcessRequestComplete().BindLambda([this, Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			bQuitRequested = true;
		});
		HttpRequest->ProcessRequest();
	});
	HttpRequest->ProcessRequest();
}

class FWaitUntilCompleteHttpFixture : public FHttpModuleTestFixture
{
public:
	FWaitUntilCompleteHttpFixture()
	{
		HttpModule->GetHttpManager().SetRequestAddedDelegate(FHttpManagerRequestAddedDelegate::CreateRaw(this, &FWaitUntilCompleteHttpFixture::OnRequestAdded));
		HttpModule->GetHttpManager().SetRequestCompletedDelegate(FHttpManagerRequestCompletedDelegate::CreateRaw(this, &FWaitUntilCompleteHttpFixture::OnRequestCompleted));
	}

	~FWaitUntilCompleteHttpFixture()
	{
		WaitUntilAllHttpRequestsComplete();

		HttpModule->GetHttpManager().SetRequestAddedDelegate(FHttpManagerRequestAddedDelegate());
		HttpModule->GetHttpManager().SetRequestCompletedDelegate(FHttpManagerRequestCompletedDelegate());
	}

	void OnRequestAdded(const FHttpRequestRef& Request)
	{
		++OngoingRequests;
	}

	void OnRequestCompleted(const FHttpRequestRef& Request)
	{
		if (ensure(OngoingRequests > 0))
		{
			--OngoingRequests;
		}
	}

	void WaitUntilAllHttpRequestsComplete()
	{
		while (!bRunningThreadRequest && OngoingRequests != 0)
		{
			HttpModule->GetHttpManager().Tick(TickFrequency);
			FPlatformProcess::Sleep(TickFrequency);
		}
	}

	uint32 OngoingRequests = 0;
	float TickFrequency = 1.0f / 60; /*60 FPS*/;
	bool bRunningThreadRequest = false;
};

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http Methods", HTTP_TAG)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(UrlToTestMethods());

	SECTION("GET")
	{
		HttpRequest->SetVerb(TEXT("GET"));
	}
	SECTION("POST")
	{
		HttpRequest->SetVerb(TEXT("POST"));
	}
	SECTION("PUT")
	{
		HttpRequest->SetVerb(TEXT("PUT"));
	}
	SECTION("DELETE")
	{
		HttpRequest->SetVerb(TEXT("DELETE"));
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Get large response content without chunks", HTTP_TAG)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(FString::Format(TEXT("{0}/get_large_response_without_chunks/{1}/"), { *UrlHttpTests(), 1024 * 1024/*bytes_number*/}));
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request connect timeout", HTTP_TAG)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(UrlWithInvalidPortToTestConnectTimeout());
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetTimeout(7);
	FDateTime StartTime = FDateTime::Now();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);
		CHECK(HttpResponse == nullptr);
		// TODO: For now curl impl is using customized timeout instead of relying on native http timeout, 
		// which doesn't get CURLE_COULDNT_CONNECT. Enable this after switching to native http timeout
		//CHECK(HttpRequest->GetStatus() == EHttpRequestStatus::Failed_ConnectionError);
		FTimespan Timespan = FDateTime::Now() - StartTime;
		float DurationInSeconds = Timespan.GetTotalSeconds();
		CHECK(FMath::IsNearlyEqual(DurationInSeconds, 7, HTTP_TIME_DIFF_TOLERANCE));
	});
	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http download", HTTP_TAG)
{
	uint32 Chunks = 3;
	uint32 ChunkSize = 1024*1024;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(FString::Format(TEXT("{0}/streaming_download/{1}/{2}/"), { *UrlHttpTests(), Chunks, ChunkSize }));
	HttpRequest->SetVerb(TEXT("GET"));

	TSharedRef<int64> TotalBytesReceived = MakeShared<int64>(0);

	SECTION("Success without stream provided")
	{
		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
		});
	}
	SECTION("Success with customized stream")
	{
		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			FTestHttpReceiveStream(TSharedRef<int64> InTotalBytesReceived)
				: TotalBytesReceived(InTotalBytesReceived)
			{
			}

			virtual void Serialize(void* V, int64 Length) override
			{
				*TotalBytesReceived += Length;
			}

			TSharedRef<int64> TotalBytesReceived;
		};

		TSharedRef<FTestHttpReceiveStream> Stream = MakeShared<FTestHttpReceiveStream>(TotalBytesReceived);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(Stream));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(*TotalBytesReceived == Chunks * ChunkSize);
		});
	}
	SECTION("Success with customized stream delegate")
	{
		FHttpRequestStreamDelegate Delegate;
		Delegate.BindLambda([TotalBytesReceived](void* Ptr, int64 Length) {
			*TotalBytesReceived += Length;
			return true;
		});
		CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegate(Delegate));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetResponseCode() == 200);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(*TotalBytesReceived == Chunks * ChunkSize);
		});
	}
	SECTION("Use customized stream to receive response body but failed when serialize")
	{
		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			FTestHttpReceiveStream(TSharedRef<int64> InTotalBytesReceived)
				: TotalBytesReceived(InTotalBytesReceived)
			{
			}

			virtual void Serialize(void* V, int64 Length) override
			{
				*TotalBytesReceived += Length;
				SetError();
			}

			TSharedRef<int64> TotalBytesReceived;
		};

		TSharedRef<FTestHttpReceiveStream> Stream = MakeShared<FTestHttpReceiveStream>(TotalBytesReceived);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(Stream));

		HttpRequest->OnProcessRequestComplete().BindLambda([ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!bSucceeded);
			CHECK(HttpResponse != nullptr);
			CHECK(*TotalBytesReceived <= ChunkSize);
		});
	}
	SECTION("Use customized stream delegate to receive response body but failed when call")
	{
		FHttpRequestStreamDelegate Delegate;
		Delegate.BindLambda([TotalBytesReceived](void* Ptr, int64 Length) {
			*TotalBytesReceived += Length;
			return false;
		});
		CHECK(HttpRequest->SetResponseBodyReceiveStreamDelegate(Delegate));

		HttpRequest->OnProcessRequestComplete().BindLambda([ChunkSize, TotalBytesReceived](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!bSucceeded);
			CHECK(HttpResponse != nullptr);
			CHECK(*TotalBytesReceived <= ChunkSize);
		});
	}
	SECTION("Success with file stream to receive response body")
	{
		FString Filename = FString(FPlatformProcess::UserSettingsDir()) / TEXT("TestStreamDownload.dat");
		FArchive* RawFile = IFileManager::Get().CreateFileWriter(*Filename);
		CHECK(RawFile != nullptr);
		TSharedRef<FArchive> FileToWrite = MakeShareable(RawFile);
		CHECK(HttpRequest->SetResponseBodyReceiveStream(FileToWrite));

		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize, Filename, FileToWrite](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(bSucceeded);
			REQUIRE(HttpResponse != nullptr);
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(HttpResponse->GetContent().IsEmpty());
			CHECK(HttpResponse->GetResponseCode() == 200);

			FileToWrite->FlushCache();
			FileToWrite->Close();

			TSharedRef<FArchive, ESPMode::ThreadSafe> FileToRead = MakeShareable(IFileManager::Get().CreateFileReader(*Filename));
			CHECK(FileToRead->TotalSize() == Chunks * ChunkSize);

			IFileManager::Get().Delete(*Filename);
		});
	}

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Can run parallel stream download requests", HTTP_TAG)
{
	uint32 Chunks = 5;
	uint32 ChunkSize = 1024*1024;

	for (int i = 0; i < 3; ++i)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
		HttpRequest->SetURL(FString::Format(TEXT("{0}/streaming_download/{1}/{2}/"), { *UrlHttpTests(), Chunks, ChunkSize }));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->OnProcessRequestComplete().BindLambda([Chunks, ChunkSize](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(HttpResponse->GetContentLength() == Chunks * ChunkSize);
			CHECK(bSucceeded);
			CHECK(HttpResponse->GetResponseCode() == 200);
		});
		HttpRequest->ProcessRequest();
	}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Streaming http upload - gold path.", HTTP_TAG)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(FString::Format(TEXT("{0}/streaming_upload"), { *UrlHttpTests() }));
	HttpRequest->SetVerb(TEXT("POST"));

	const char* BoundaryLabel = "test_http_boundary";
	HttpRequest->SetHeader(TEXT("Content-Type"), FString::Format(TEXT("multipart/form-data; boundary={0}"), { BoundaryLabel }));

	// Not really reading file here in order to simplify the test flow. It will be sent by chunks in http request
	const uint32 FileSize = 10*1024*1024;
	char* FileData = (char*)FMemory::Malloc(FileSize + 1);
	FMemory::Memset(FileData, 'd', FileSize);
	FileData[FileSize + 1] = '\0';

	TArray<uint8> ContentData;
	const int32 ContentMaxSize = FileSize + 256/*max length of format string*/;
	ContentData.Reserve(ContentMaxSize);
	const int32 ContentLength = FCStringAnsi::Snprintf(
		(char*)ContentData.GetData(),
		ContentMaxSize,
		"--%s\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"bigfile.zip\"\r\n"
		"Content-Type: application/octet-stream\r\n\r\n"
		"%s\r\n"
		"--%s--",
		BoundaryLabel, FileData, BoundaryLabel);

	FMemory::Free(FileData);

	CHECK(ContentLength > 0);
	CHECK(ContentLength < ContentMaxSize);
	ContentData.SetNumUninitialized(ContentLength);
	HttpRequest->SetContent(MoveTemp(ContentData));

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(bSucceeded);
		REQUIRE(HttpResponse != nullptr);
		CHECK(HttpResponse->GetResponseCode() == 200);
	});
	HttpRequest->ProcessRequest();
}

class FWaitThreadedHttpFixture : public FWaitUntilCompleteHttpFixture, public FRunnable
{
public:
	DECLARE_DELEGATE(FRunActualTestCodeDelegate);

	FWaitThreadedHttpFixture()
	{
		bRunningThreadRequest = true;
	}

	// FRunnable interface
	virtual uint32 Run() override
	{
		ThreadCallback.ExecuteIfBound();
		bRunningThreadRequest = false;
		return 0;
	}

	void StartTestHttpThread()
	{
		RunnableThread = TSharedPtr<FRunnableThread>(FRunnableThread::Create(this, TEXT("Test Http Thread")));
	}

	FRunActualTestCodeDelegate ThreadCallback;
	TSharedPtr<FRunnableThread> RunnableThread;
};

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Http streaming download request can work in non game thread", HTTP_TAG)
{
	ThreadCallback.BindLambda([this]() {
		TSharedRef<IHttpRequest> HttpRequest = HttpModule->CreateRequest();
		HttpRequest->SetURL(FString::Format(TEXT("{0}/streaming_download/{1}/{2}/"), { *UrlHttpTests(), 3/*chunks*/, 1024/*chunk_size*/}));
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);

		class FTestHttpReceiveStream final : public FArchive
		{
		public:
			virtual void Serialize(void* V, int64 Length) override
			{
				// No matter what's the thread policy, Serialize always get called in http thread.
				CHECK(!IsInGameThread());
			}
		};
		CHECK(HttpRequest->SetResponseBodyReceiveStream(MakeShared<FTestHttpReceiveStream>()));

		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			// EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread was used, so not in game thread here
			CHECK(!IsInGameThread());
			CHECK(bSucceeded);
			CHECK(HttpResponse->GetResponseCode() == 200);
		});

		HttpRequest->ProcessRequest();
	});

	StartTestHttpThread();
}

namespace UE
{
namespace TestHttp
{

void SetupURLRequestFilter(FHttpModule* HttpModule)
{
	// Pre check will fail when domain is not allowed
	UE::Core::FURLRequestFilter::FRequestMap SchemeMap;
	SchemeMap.Add(TEXT("http"), TArray<FString>{TEXT("epicgames.com")});
	UE::Core::FURLRequestFilter Filter{SchemeMap};
	HttpModule->GetHttpManager().SetURLRequestFilter(Filter);
}

}
}

TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Http request pre check will fail", HTTP_TAG)
{
	// Pre check will fail when domain is not allowed
	UE::TestHttp::SetupURLRequestFilter(HttpModule);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetURL(UrlToTestMethods());

	SECTION("on game thread")
	{
		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(IsInGameThread());
			CHECK(!bSucceeded);
		});
	}
	SECTION("on http thread")
	{
		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
			CHECK(!IsInGameThread());
			CHECK(!bSucceeded);
		});
	}

	HttpRequest->ProcessRequest();
}

TEST_CASE_METHOD(FWaitThreadedHttpFixture, "Threaded http request pre check will fail", HTTP_TAG)
{
	ThreadCallback.BindLambda([this]() {
		// Pre check will fail when domain is not allowed
		UE::TestHttp::SetupURLRequestFilter(HttpModule);

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
		HttpRequest->SetVerb(TEXT("GET"));
		HttpRequest->SetURL(UrlToTestMethods());

		SECTION("on game thread")
		{
			HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(IsInGameThread());
				CHECK(!bSucceeded);
			});
		}
		SECTION("on http thread")
		{
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
			HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
				CHECK(!IsInGameThread());
				CHECK(!bSucceeded);
			});
		}

		HttpRequest->ProcessRequest();
	});

	StartTestHttpThread();
}


TEST_CASE_METHOD(FWaitUntilCompleteHttpFixture, "Cancel http request connect before timeout", HTTP_TAG)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = HttpModule->CreateRequest();
	HttpRequest->SetURL(UrlWithInvalidPortToTestConnectTimeout());
	HttpRequest->SetVerb(TEXT("GET"));
	HttpRequest->SetTimeout(7);
	FDateTime StartTime = FDateTime::Now();
	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSucceeded) {
		CHECK(!bSucceeded);

		FTimespan Timespan = FDateTime::Now() - StartTime;
		float DurationInSeconds = Timespan.GetTotalSeconds();
		CHECK(DurationInSeconds < 2);
	});
	HttpRequest->ProcessRequest();
	FPlatformProcess::Sleep(0.5);
	HttpRequest->CancelRequest();
}
// TODO: Add cancel test, with multiple cancel calls
