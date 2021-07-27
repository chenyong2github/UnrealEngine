// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpDerivedDataBackend.h"

#include "Async/ParallelFor.h"
#include "DerivedDataBackendInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/SecureHash.h"

// Test is targeted at HttpDerivedDataBackend but with some backend test interface it could be generalized
// to function against all backends.

#if WITH_DEV_AUTOMATION_TESTS && WITH_HTTP_DDC_BACKEND

DEFINE_LOG_CATEGORY_STATIC(LogHttpDerivedDataBackendTests, Log, All);

#define TEST_NAME_ROOT TEXT("System.DerivedDataCache.HttpDerivedDataBackend")

#define IMPLEMENT_HTTPDERIVEDDATA_AUTOMATION_TEST( TClass, PrettyName, TFlags ) \
	IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(TClass, FHttpDerivedDataTestBase, TEST_NAME_ROOT PrettyName, TFlags) \
	void TClass::GetTests(TArray<FString>& OutBeautifiedNames, TArray <FString>& OutTestCommands) const \
	{ \
		if (CheckPrequisites()) \
		{ \
			OutBeautifiedNames.Add(TEST_NAME_ROOT PrettyName); \
			OutTestCommands.Add(FString()); \
		} \
	}

namespace HttpDerivedDataBackendTest
{

class FHttpDerivedDataTestBase : public FAutomationTestBase
{
public:
	FHttpDerivedDataTestBase(const FString& InName, const bool bInComplexTask)
	: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	bool CheckPrequisites() const
	{
		if (UE::DerivedData::Backends::FHttpDerivedDataBackend* Backend = GetTestBackend())
		{
			if (Backend->IsUsable())
			{
				return true;
			}
		}
		return false;
	}

protected:
	void ConcurrentTestWithStats(TFunctionRef<void()> TestFunction, int32 ThreadCount, double Duration)
	{
		std::atomic<uint64> Requests{ 0 };
		std::atomic<uint64> MaxLatency{ 0 };
		std::atomic<uint64> TotalMS{ 0 };
		std::atomic<uint64> TotalRequests{ 0 };

		FEvent* StartEvent = FPlatformProcess::GetSynchEventFromPool(true);
		FEvent* LastEvent = FPlatformProcess::GetSynchEventFromPool(true);
		std::atomic<double> StopTime{ 0.0 };
		std::atomic<uint64> ActiveCount{ 0 };

		for (int32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
		{
			ActiveCount++;
			Async(
				ThreadIndex < FTaskGraphInterface::Get().GetNumWorkerThreads() ? EAsyncExecution::TaskGraph : EAsyncExecution::Thread,
				[&]()
				{
					// No false start, wait until everyone is ready before starting the test
					StartEvent->Wait();

					while (FPlatformTime::Seconds() < StopTime.load(std::memory_order_relaxed))
					{
						uint64 Before = FPlatformTime::Cycles64();
						TestFunction();
						uint64 Delta = FPlatformTime::Cycles64() - Before;
						Requests++;
						TotalMS += FPlatformTime::ToMilliseconds64(Delta);
						TotalRequests++;

						// Compare exchange loop until we either succeed to set the maximum value
						// or we bail out because we don't have the maximum value anymore.
						while (true)
						{
							uint64 Snapshot = MaxLatency.load();
							if (Delta > Snapshot)
							{
								// Only do the exchange if the value has not changed since we confirmed
								// we had a bigger one.
								if (MaxLatency.compare_exchange_strong(Snapshot, Delta))
								{
									// Exchange succeeded
									break;
								}
							}
							else
							{
								// We don't have the maximum
								break;
							}
						}
					}
					
					if (--ActiveCount == 0)
					{
						LastEvent->Trigger();
					}
				}
			);
		}

		StopTime = FPlatformTime::Seconds() + Duration;

		// GO!
		StartEvent->Trigger();

		while (FPlatformTime::Seconds() < StopTime)
		{
			FPlatformProcess::Sleep(1.0f);

			if (TotalRequests)
			{
				UE_LOG(LogHttpDerivedDataBackendTests, Display, TEXT("RPS: %llu, AvgLatency: %.02f ms, MaxLatency: %.02f s"), Requests.exchange(0), double(TotalMS) / TotalRequests, FPlatformTime::ToSeconds(MaxLatency));
			}
			else
			{
				UE_LOG(LogHttpDerivedDataBackendTests, Display, TEXT("RPS: %llu, AvgLatency: N/A, MaxLatency: %.02f s"), Requests.exchange(0), FPlatformTime::ToSeconds(MaxLatency));
			}
		}

		LastEvent->Wait();

		FPlatformProcess::ReturnSynchEventToPool(StartEvent);
		FPlatformProcess::ReturnSynchEventToPool(LastEvent);
	}

	UE::DerivedData::Backends::FHttpDerivedDataBackend* GetTestBackend() const
	{
		static UE::DerivedData::Backends::FHttpDerivedDataBackend* CachedBackend = FetchTestBackend_Internal();
		return CachedBackend;
	}

private:
	UE::DerivedData::Backends::FHttpDerivedDataBackend* FetchTestBackend_Internal() const
	{
		return UE::DerivedData::Backends::FHttpDerivedDataBackend::GetAny();
	}
};

IMPLEMENT_HTTPDERIVEDDATA_AUTOMATION_TEST(FConcurrentCachedDataProbablyExistsBatch, TEXT(".ConcurrentCachedDataProbablyExistsBatch"), EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConcurrentCachedDataProbablyExistsBatch::RunTest(const FString& Parameters)
{
	UE::DerivedData::Backends::FHttpDerivedDataBackend* TestBackend = GetTestBackend();

	const int32  ThreadCount = 64;
	const double Duration = 10;
	const uint32 KeysInBatch = 4;

	TArray<FString> Keys;
	TArray<uint8> KeyContents;
	KeyContents.Add(42);

	FSHA1 HashState;
	HashState.Update(KeyContents.GetData(), KeyContents.Num());
	HashState.Final();
	uint8 Hash[FSHA1::DigestSize];
	HashState.GetHash(Hash);
	const FString HashString = BytesToHex(Hash, FSHA1::DigestSize);

	for (uint32 KeyIndex = 0; KeyIndex < KeysInBatch; ++KeyIndex)
	{
		FString NewKey = FString::Printf(TEXT("__AutoTest_Dummy_%u__%s"), KeyIndex, *HashString);
		Keys.Add(NewKey);
		TestBackend->PutCachedData(*NewKey, KeyContents, false);
	}

	std::atomic<uint32> MismatchedResults = 0;

	ConcurrentTestWithStats(
		[&]()
		{
			TConstArrayView<FString> BatchView = MakeArrayView(Keys.GetData(), KeysInBatch);
			TBitArray<> Result = TestBackend->CachedDataProbablyExistsBatch(BatchView);
			if (Result.CountSetBits() != BatchView.Num())
			{
				MismatchedResults.fetch_add(BatchView.Num() - Result.CountSetBits(), std::memory_order_relaxed);
			}
		},
		ThreadCount,
		Duration
	);

	TestEqual(TEXT("Concurrent calls to CachedDataProbablyExistsBatch for a batch of keys that were put are not reliably found"), MismatchedResults, 0);

	return true;
}

}
#endif // #if WITH_DEV_AUTOMATION_TESTS && WITH_HTTP_DDC_BACKEND
