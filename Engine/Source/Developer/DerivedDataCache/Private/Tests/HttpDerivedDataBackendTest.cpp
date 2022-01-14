// Copyright Epic Games, Inc. All Rights Reserved.

#include "Async/ParallelFor.h"
#include "DerivedDataBackendInterface.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataCacheRecord.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataValue.h"
#include "DerivedDataValueId.h"
#include "Memory/CompositeBuffer.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/CompactBinaryWriter.h"
#include "ZenServerInterface.h"

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

namespace UE::DerivedData::CacheStore::Http
{
FDerivedDataBackendInterface* GetAnyHttpDerivedDataBackend(
	FString& OutDomain,
	FString& OutOAuthProvider,
	FString& OutOAuthClientId,
	FString& OutOAuthSecret,
	FString& OutNamespace,
	FString& OutStructuredNamespace);
} // UE::DerivedData::CacheStore::Http

namespace UE::DerivedData::CacheStore::ZenCache
{
FDerivedDataBackendInterface* CreateZenDerivedDataBackend(const TCHAR* NodeName, const TCHAR* ServiceUrl, const TCHAR* Namespace);
} // UE::DerivedData::CacheStore::ZenCache

namespace UE::DerivedData::CacheStore::Http
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
		if (FDerivedDataBackendInterface* Backend = GetTestBackend())
		{
			return true;
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

	FDerivedDataBackendInterface* GetTestBackend() const
	{
		static FDerivedDataBackendInterface* CachedBackend = UE::DerivedData::CacheStore::Http::GetAnyHttpDerivedDataBackend(
			TestDomain, TestOAuthProvider, TestOAuthClientId, TestOAuthSecret, TestNamespace, TestStructuredNamespace);
		return CachedBackend;
	}

	bool GetRecords(TConstArrayView<UE::DerivedData::FCacheRecord> Records, UE::DerivedData::FCacheRecordPolicy Policy, TArray<UE::DerivedData::FCacheRecord>& OutRecords)
	{
		using namespace UE::DerivedData;
		UE::DerivedData::ICacheStore* TestBackend = GetTestBackend();

		TArray<FCacheGetRequest> Requests;
		Requests.Reserve(Records.Num());

		for (int32 RecordIndex = 0; RecordIndex < Records.Num(); ++RecordIndex)
		{
			const FCacheRecord& Record = Records[RecordIndex];
			Requests.Add({ {TEXT("FHttpDerivedDataTestBase")}, Record.GetKey(), Policy, static_cast<uint64>(RecordIndex) });
		}

		struct FGetOutput
		{
			FCacheRecord Record;
			EStatus Status = EStatus::Error;
		};

		TArray<TOptional<FGetOutput>> GetOutputs;
		GetOutputs.SetNum(Records.Num());
		FRequestOwner RequestOwner(EPriority::Blocking);
		TestBackend->Get(Requests, RequestOwner, [&GetOutputs](FCacheGetResponse&& Response)
			{
				FCacheRecordBuilder RecordBuilder(Response.Record.GetKey());

				if (Response.Record.GetMeta())
				{
					RecordBuilder.SetMeta(FCbObject::Clone(Response.Record.GetMeta()));
				}

				for (const FValueWithId& Value : Response.Record.GetValues())
				{
					if (Value)
					{
						RecordBuilder.AddValue(Value);
					}
				}
				GetOutputs[Response.UserData].Emplace(FGetOutput{ RecordBuilder.Build(), Response.Status });
			});
		RequestOwner.Wait();

		for (int32 RecordIndex = 0; RecordIndex < Records.Num(); ++RecordIndex)
		{
			FGetOutput& ReceivedOutput = GetOutputs[RecordIndex].GetValue();
			if (ReceivedOutput.Status != EStatus::Ok)
			{
				return false;
			}
			OutRecords.Add(MoveTemp(ReceivedOutput.Record));
		}

		return true;
	}

	bool GetRecordChunks(TConstArrayView<UE::DerivedData::FCacheRecord> Records, UE::DerivedData::FCacheRecordPolicy Policy, uint64 Offset, uint64 Size, TArray<FSharedBuffer>& OutChunks)
	{
		using namespace UE::DerivedData;
		UE::DerivedData::ICacheStore* TestBackend = GetTestBackend();

		TArray<FCacheChunkRequest> Requests;

		int32 OverallIndex = 0;
		for (int32 RecordIndex = 0; RecordIndex < Records.Num(); ++RecordIndex)
		{
			const FCacheRecord& Record = Records[RecordIndex];
			TConstArrayView<FValueWithId> Values = Record.GetValues();
			for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
			{
				const FValueWithId& Value = Values[ValueIndex];
				Requests.Add({ {TEXT("FHttpDerivedDataTestBase")}, Record.GetKey(),  Value.GetId(), Offset, Size, Value.GetRawHash(), Policy.GetValuePolicy(Value.GetId()), static_cast<uint64>(OverallIndex) });
				++OverallIndex;
			}
		}

		struct FGetChunksOutput
		{
			FSharedBuffer Chunk;
			EStatus Status = EStatus::Error;
		};

		TArray<TOptional<FGetChunksOutput>> GetOutputs;
		GetOutputs.SetNum(Requests.Num());
		FRequestOwner RequestOwner(EPriority::Blocking);
		TestBackend->GetChunks(Requests, RequestOwner, [&GetOutputs](FCacheChunkResponse&& Response)
			{
				GetOutputs[Response.UserData].Emplace(FGetChunksOutput { Response.RawData, Response.Status });
			});
		RequestOwner.Wait();

		for (int32 RequestIndex = 0; RequestIndex < Requests.Num(); ++RequestIndex)
		{
			FGetChunksOutput& ReceivedOutput = GetOutputs[RequestIndex].GetValue();
			if (ReceivedOutput.Status != EStatus::Ok)
			{
				return false;
			}
			OutChunks.Add(MoveTemp(ReceivedOutput.Chunk));
		}

		return true;

	}
	
	void ValidateRecords(const TCHAR* Name, TConstArrayView<UE::DerivedData::FCacheRecord> RecordsToTest, TConstArrayView<UE::DerivedData::FCacheRecord> ReferenceRecords, UE::DerivedData::FCacheRecordPolicy Policy)
	{
		using namespace UE::DerivedData;

		if (!TestEqual(FString::Printf(TEXT("%s::Record quantity"), Name), RecordsToTest.Num(), ReferenceRecords.Num()))
		{
			return;
		}

		for (int32 RecordIndex = 0; RecordIndex < RecordsToTest.Num(); ++RecordIndex)
		{
			const FCacheRecord& ExpectedRecord = ReferenceRecords[RecordIndex];
			const FCacheRecord& RecordToTest = RecordsToTest[RecordIndex];

			if (EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipMeta))
			{
				TestTrue(FString::Printf(TEXT("%s::Get meta null"), Name), !RecordToTest.GetMeta());
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s::Get meta equality"), Name), ExpectedRecord.GetMeta().Equals(RecordToTest.GetMeta()));
			}

			TestEqual(FString::Printf(TEXT("%s::Get value quantity"), Name), ExpectedRecord.GetValues().Num(), RecordToTest.GetValues().Num());

			const TConstArrayView<FValueWithId> ExpectedValues = ExpectedRecord.GetValues();
			const TConstArrayView<FValueWithId> ReceivedValues = RecordToTest.GetValues();
			for (int32 ValueIndex = 0; ValueIndex < ExpectedValues.Num(); ++ValueIndex)
			{
				if (EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipData))
				{
					TestTrue(FString::Printf(TEXT("%s::Get value[%d] !HasData"), Name, ValueIndex), !ReceivedValues[ValueIndex].HasData());
				}
				else
				{
					TestTrue(FString::Printf(TEXT("%s::Get value[%d] HasData"), Name, ValueIndex), ReceivedValues[ValueIndex].HasData());
					TestTrue(FString::Printf(TEXT("%s::Get value[%d] equality"), Name, ValueIndex), ExpectedValues[ValueIndex] == ReceivedValues[ValueIndex]);
					TestTrue(FString::Printf(TEXT("%s::Get value[%d] data equality"), Name, ValueIndex), FIoHash::HashBuffer(ReceivedValues[ValueIndex].GetData().GetCompressed()) == FIoHash::HashBuffer(ExpectedValues[ValueIndex].GetData().GetCompressed()));
				}
			}
		}
	}

	void ValidateRecordChunks(const TCHAR* Name, TConstArrayView<FSharedBuffer> RecordChunksToTest, TConstArrayView<UE::DerivedData::FCacheRecord> ReferenceRecords, UE::DerivedData::FCacheRecordPolicy Policy, uint64 Offset, uint64 Size)
	{
		using namespace UE::DerivedData;

		int32 TotalChunks = 0;
		for (int32 RecordIndex = 0; RecordIndex < ReferenceRecords.Num(); ++RecordIndex)
		{
			const FCacheRecord& Record = ReferenceRecords[RecordIndex];
			TConstArrayView<FValueWithId> Values = Record.GetValues();
			for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
			{
				++TotalChunks;
			}
		}

		if (!TestEqual(FString::Printf(TEXT("%s::Chunk quantity"), Name), RecordChunksToTest.Num(), TotalChunks))
		{
			return;
		}

		int32 ChunkIndex = 0;
		for (int32 RecordIndex = 0; RecordIndex < ReferenceRecords.Num(); ++RecordIndex)
		{
			const FCacheRecord& ExpectedRecord = ReferenceRecords[RecordIndex];

			const TConstArrayView<FValueWithId> ExpectedValues = ExpectedRecord.GetValues();
			for (int32 ValueIndex = 0; ValueIndex < ExpectedValues.Num(); ++ValueIndex)
			{
				const FSharedBuffer& ChunkToTest = RecordChunksToTest[ChunkIndex];

				if (EnumHasAnyFlags(Policy.GetRecordPolicy(), ECachePolicy::SkipData))
				{
					TestTrue(FString::Printf(TEXT("%s::Get chunk[%d] IsNull"), Name, ChunkIndex), ChunkToTest.IsNull());
				}
				else
				{
					FSharedBuffer ReferenceBuffer = ExpectedValues[ValueIndex].GetData().Decompress();
					FMemoryView ReferenceView = ReferenceBuffer.GetView().Mid(Offset, Size);
					TestTrue(FString::Printf(TEXT("%s::Get chunk[%d] data equality"), Name, ChunkIndex), ReferenceView.EqualBytes(ChunkToTest.GetView()));
				}
				++ChunkIndex;
			}
		}
	}

	TArray<UE::DerivedData::FCacheRecord> GetAndValidateRecords(const TCHAR* Name, TConstArrayView<UE::DerivedData::FCacheRecord> Records, UE::DerivedData::FCacheRecordPolicy Policy)
	{
		using namespace UE::DerivedData;
		TArray<FCacheRecord> ReceivedRecords;
		bool bGetSuccessful = GetRecords(Records, Policy, ReceivedRecords);
		TestTrue(FString::Printf(TEXT("%s::Get status"), Name), bGetSuccessful);

		if (!bGetSuccessful)
		{
			return TArray<FCacheRecord>();
		}

		ValidateRecords(Name, ReceivedRecords, Records, Policy);
		return ReceivedRecords;
	}

	TArray<FSharedBuffer> GetAndValidateRecordChunks(const TCHAR* Name, TConstArrayView<UE::DerivedData::FCacheRecord> Records, UE::DerivedData::FCacheRecordPolicy Policy, uint64 Offset, uint64 Size)
	{
		using namespace UE::DerivedData;
		TArray<FSharedBuffer> ReceivedChunks;
		bool bGetSuccessful = GetRecordChunks(Records, Policy, Offset, Size, ReceivedChunks);
		TestTrue(FString::Printf(TEXT("%s::GetChunks status"), Name), bGetSuccessful);

		if (!bGetSuccessful)
		{
			return TArray<FSharedBuffer>();
		}

		ValidateRecordChunks(Name, ReceivedChunks, Records, Policy, Offset, Size);
		return ReceivedChunks;
	}

	TArray<UE::DerivedData::FCacheRecord> GetAndValidateRecordsAndChunks(const TCHAR* Name, TConstArrayView<UE::DerivedData::FCacheRecord> Records, UE::DerivedData::FCacheRecordPolicy Policy)
	{
		GetAndValidateRecordChunks(Name, Records, Policy, 5, 5);
		return GetAndValidateRecords(Name, Records, Policy);
	}

protected:
	static inline FString TestDomain;
	static inline FString TestOAuthProvider;
	static inline FString TestOAuthClientId;
	static inline FString TestOAuthSecret;
	static inline FString TestNamespace;
	static inline FString TestStructuredNamespace;
};

// Helper function to create a number of dummy cache keys for testing
TArray<FString> CreateTestCacheKeys(FDerivedDataBackendInterface* InTestBackend, uint32 InNumKeys)
{
	TArray<FString> Keys;
	TArray<uint8> KeyContents;
	KeyContents.Add(42);

	FSHA1 HashState;
	HashState.Update(KeyContents.GetData(), KeyContents.Num());
	HashState.Final();
	uint8 Hash[FSHA1::DigestSize];
	HashState.GetHash(Hash);
	const FString HashString = BytesToHex(Hash, FSHA1::DigestSize);

	for (uint32 KeyIndex = 0; KeyIndex < InNumKeys; ++KeyIndex)
	{
		FString NewKey = FString::Printf(TEXT("__AutoTest_Dummy_%u__%s"), KeyIndex, *HashString);
		Keys.Add(NewKey);
		InTestBackend->PutCachedData(*NewKey, KeyContents, false);
	}
	return Keys;
}

TArray<UE::DerivedData::FCacheRecord> CreateTestCacheRecords(UE::DerivedData::ICacheStore* InTestBackend, uint32 InNumKeys, uint32 InNumValues, FCbObject MetaContents = FCbObject(), const char* BucketName = nullptr)
{
	using namespace UE::DerivedData;
	FCacheBucket TestCacheBucket(BucketName ? BucketName : "AutoTestDummy");

	TArray<FCacheRecord> CacheRecords;
	TArray<FCachePutRequest> PutRequests;
	PutRequests.Reserve(InNumKeys);

	for (uint32 KeyIndex = 0; KeyIndex < InNumKeys; ++KeyIndex)
	{
		FIoHashBuilder HashBuilder;

		TArray<FSharedBuffer> Values;
		for (uint32 ValueIndex = 0; ValueIndex < InNumValues; ++ValueIndex)
		{
			TArray<uint8> ValueContents;
			// Add N zeroed bytes where N corresponds to the value index times 10.
			const int32 NumBytes = (ValueIndex+1)*10;
			ValueContents.AddUninitialized(NumBytes);
			for (int32 ContentIndex = 0; ContentIndex < NumBytes; ++ContentIndex)
			{
				ValueContents[ContentIndex] = (uint8)(KeyIndex + ContentIndex);
			}
			Values.Emplace(MakeSharedBufferFromArray(MoveTemp(ValueContents)));
			HashBuilder.Update(Values.Last().GetView());
		}

		FCacheKey Key;
		Key.Bucket = TestCacheBucket;
		Key.Hash = HashBuilder.Finalize();

		FCacheRecordBuilder RecordBuilder(Key);

		for (const FSharedBuffer& ValueBuffer : Values)
		{
			FIoHash ValueHash(FIoHash::HashBuffer(ValueBuffer));
			RecordBuilder.AddValue(FValueId::FromHash(ValueHash), ValueBuffer);
		}

		if (MetaContents)
		{
			RecordBuilder.SetMeta(MoveTemp(MetaContents));
		}

		PutRequests.Add({ {TEXT("AutoTest")}, RecordBuilder.Build(), ECachePolicy::Default, KeyIndex });
	}

	FRequestOwner Owner(EPriority::Blocking);
	InTestBackend->Put(PutRequests, Owner, [&CacheRecords, &PutRequests] (FCachePutResponse&& Response)
	{
		check(Response.Status == EStatus::Ok);
		CacheRecords.Add(PutRequests[Response.UserData].Record);
	});
	Owner.Wait();

	return CacheRecords;
}

IMPLEMENT_HTTPDERIVEDDATA_AUTOMATION_TEST(FConcurrentCachedDataProbablyExistsBatch, TEXT(".FConcurrentCachedDataProbablyExistsBatch"), EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConcurrentCachedDataProbablyExistsBatch::RunTest(const FString& Parameters)
{
	FDerivedDataBackendInterface* TestBackend = GetTestBackend();

	const int32 ThreadCount = 64;
	const double Duration = 10;
	const uint32 KeysInBatch = 4;

	TArray<FString> Keys = CreateTestCacheKeys(TestBackend, KeysInBatch);

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

// This test validate that batch requests wont mismatch head and get request for the same keys in the same batch
IMPLEMENT_HTTPDERIVEDDATA_AUTOMATION_TEST(FConcurrentExistsAndGetForSameKeyBatch, TEXT(".FConcurrentExistsAndGetForSameKeyBatch"), EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConcurrentExistsAndGetForSameKeyBatch::RunTest(const FString& Parameters)
{
	FDerivedDataBackendInterface* TestBackend = GetTestBackend();

	const int32 ParallelTasks = 32;
	const uint32 Iterations = 20;
	const uint32 KeysInBatch = 4;

	TArray<FString> Keys = CreateTestCacheKeys(TestBackend, KeysInBatch);
	// Add some non valid keys by just using guids
	for (int32 Index = 0; Index < KeysInBatch; ++Index)
	{
		Keys.Add(FGuid::NewGuid().ToString());
	}
	
	ParallelFor(ParallelTasks,
		[&](int32 TaskIndex)
		{
			for (uint32 Iteration = 0; Iteration < Iterations; ++Iteration)
			{
				for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
				{
					if ((Iteration % 2) ^ (KeyIndex % 2))
					{
						TestBackend->CachedDataProbablyExists(*Keys[KeyIndex]);
					}
					else
					{
						TArray<uint8> OutData;
						TestBackend->GetCachedData(*Keys[KeyIndex], OutData);
					}
				}
			}
		}
	);
	return true;
}

// Tests basic functionality for structured cache operations
IMPLEMENT_HTTPDERIVEDDATA_AUTOMATION_TEST(CacheStore, TEXT(".CacheStore"), EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool CacheStore::RunTest(const FString& Parameters)
{
	using namespace UE::DerivedData;
	FDerivedDataBackendInterface* TestBackend = GetTestBackend();

#if UE_WITH_ZEN
	using namespace UE::Zen;
	FServiceSettings ZenTestServiceSettings;
	FServiceAutoLaunchSettings& ZenTestAutoLaunchSettings = ZenTestServiceSettings.SettingsVariant.Get<FServiceAutoLaunchSettings>();
	ZenTestAutoLaunchSettings.DataPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineSavedDir(), "ZenUnitTest"));
	ZenTestAutoLaunchSettings.ExtraArgs = FString::Printf(TEXT("--http asio --upstream-jupiter-url \"%s\" --upstream-jupiter-oauth-url \"%s\" --upstream-jupiter-oauth-clientid \"%s\" --upstream-jupiter-oauth-clientsecret \"%s\" --upstream-jupiter-namespace-ddc \"%s\" --upstream-jupiter-namespace \"%s\""),
		*TestDomain,
		*TestOAuthProvider,
		*TestOAuthClientId,
		*TestOAuthSecret,
		*TestNamespace,
		*TestStructuredNamespace
	);
	ZenTestAutoLaunchSettings.DesiredPort = 13337; // Avoid the normal default port
	ZenTestAutoLaunchSettings.bShowConsole = true;
	ZenTestAutoLaunchSettings.bLimitProcessLifetime = true;

	FScopeZenService ScopeZenService(MoveTemp(ZenTestServiceSettings));
	TUniquePtr<FDerivedDataBackendInterface> ZenIntermediaryBackend(UE::DerivedData::CacheStore::ZenCache::CreateZenDerivedDataBackend(TEXT("Test"), ScopeZenService.GetInstance().GetURL(), *TestNamespace));
	auto WaitForZenPushToUpstream = [](FDerivedDataBackendInterface* ZenBackend, TConstArrayView<FCacheRecord> Records)
	{
		// TODO: Expecting a legitimate means to wait for zen to finish pushing records to its upstream in the future
		FPlatformProcess::Sleep(1.0f);
	};
#endif // UE_WITH_ZEN

	const uint32 RecordsInBatch = 3;

	{
		TArray<FCacheRecord> PutRecords = CreateTestCacheRecords(TestBackend, RecordsInBatch, 1);
		TArray<FCacheRecord> RecievedRecords = GetAndValidateRecordsAndChunks(TEXT("SimpleValue"), PutRecords, ECachePolicy::Default);
		TArray<FCacheRecord> RecievedRecordsSkipMeta = GetAndValidateRecordsAndChunks(TEXT("SimpleValueSkipMeta"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipMeta);
		TArray<FCacheRecord> RecievedRecordsSkipData = GetAndValidateRecordsAndChunks(TEXT("SimpleValueSkipData"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipData);

#if UE_WITH_ZEN
		if (ZenIntermediaryBackend)
		{
			TArray<FCacheRecord> PutRecordsZen = CreateTestCacheRecords(ZenIntermediaryBackend.Get(), RecordsInBatch, 1, FCbObject(), "AutoTestDummyZen");
			WaitForZenPushToUpstream(ZenIntermediaryBackend.Get(), PutRecordsZen);
			ValidateRecords(TEXT("SimpleValueZenAndDirect"), GetAndValidateRecords(TEXT("SimpleValueZen"), PutRecordsZen, ECachePolicy::Default), RecievedRecords, ECachePolicy::Default);
			ValidateRecords(TEXT("SimpleValueSkipMetaZenAndDirect"), GetAndValidateRecords(TEXT("SimpleValueSkipMetaZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipMeta), RecievedRecordsSkipMeta, ECachePolicy::Default | ECachePolicy::SkipMeta);
			ValidateRecords(TEXT("SimpleValueSkipDataZenAndDirect"), GetAndValidateRecords(TEXT("SimpleValueSkipDataZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipData), RecievedRecordsSkipData, ECachePolicy::Default | ECachePolicy::SkipData);
		}
#endif // UE_WITH_ZEN
	}

	{
		TCbWriter<64> MetaWriter;
		MetaWriter.BeginObject();
		MetaWriter.AddInteger("MetaKey"_ASV, 42);
		MetaWriter.EndObject();
		FCbObject MetaObject = MetaWriter.Save().AsObject();

		TArray<FCacheRecord> PutRecords = CreateTestCacheRecords(TestBackend, RecordsInBatch, 1, MetaObject);
		TArray<FCacheRecord> RecievedRecords = GetAndValidateRecordsAndChunks(TEXT("SimpleValueWithMeta"), PutRecords, ECachePolicy::Default);
		TArray<FCacheRecord> RecievedRecordsSkipMeta = GetAndValidateRecordsAndChunks(TEXT("SimpleValueWithMetaSkipMeta"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipMeta);
		TArray<FCacheRecord> RecievedRecordsSkipData = GetAndValidateRecordsAndChunks(TEXT("SimpleValueWithMetaSkipData"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipData);

#if UE_WITH_ZEN
		if (ZenIntermediaryBackend)
		{
			TArray<FCacheRecord> PutRecordsZen = CreateTestCacheRecords(ZenIntermediaryBackend.Get(), RecordsInBatch, 1, MetaObject, "AutoTestDummyZen");
			WaitForZenPushToUpstream(ZenIntermediaryBackend.Get(), PutRecordsZen);
			ValidateRecords(TEXT("SimpleValueWithMetaZenAndDirect"), GetAndValidateRecords(TEXT("SimpleValueWithMetaZen"), PutRecordsZen, ECachePolicy::Default), RecievedRecords, ECachePolicy::Default);
			ValidateRecords(TEXT("SimpleValueWithMetaSkipMetaZenAndDirect"), GetAndValidateRecords(TEXT("SimpleValueWithMetaSkipMetaZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipMeta), RecievedRecordsSkipMeta, ECachePolicy::Default | ECachePolicy::SkipMeta);
			ValidateRecords(TEXT("SimpleValueWithMetaSkipDataZenAndDirect"), GetAndValidateRecords(TEXT("SimpleValueWithMetaSkipDataZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipData), RecievedRecordsSkipData, ECachePolicy::Default | ECachePolicy::SkipData);
		}
#endif // UE_WITH_ZEN
	}

	{
		TArray<FCacheRecord> PutRecords = CreateTestCacheRecords(TestBackend, RecordsInBatch, 5);
		TArray<FCacheRecord> RecievedRecords = GetAndValidateRecordsAndChunks(TEXT("MultiValue"), PutRecords, ECachePolicy::Default);
		TArray<FCacheRecord> RecievedRecordsSkipMeta = GetAndValidateRecordsAndChunks(TEXT("MultiValueSkipMeta"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipMeta);
		TArray<FCacheRecord> RecievedRecordsSkipData = GetAndValidateRecordsAndChunks(TEXT("MultiValueSkipData"), PutRecords, ECachePolicy::Default | ECachePolicy::SkipData);

#if UE_WITH_ZEN
		if (ZenIntermediaryBackend)
		{
			TArray<FCacheRecord> PutRecordsZen = CreateTestCacheRecords(ZenIntermediaryBackend.Get(), RecordsInBatch, 5, FCbObject(), "AutoTestDummyZen");
			WaitForZenPushToUpstream(ZenIntermediaryBackend.Get(), PutRecordsZen);
			ValidateRecords(TEXT("MultiValueZenAndDirect"), GetAndValidateRecords(TEXT("MultiValueZen"), PutRecordsZen, ECachePolicy::Default), RecievedRecords, ECachePolicy::Default);
			ValidateRecords(TEXT("MultiValueSkipMetaZenAndDirect"), GetAndValidateRecords(TEXT("MultiValueSkipMetaZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipMeta), RecievedRecordsSkipMeta, ECachePolicy::Default | ECachePolicy::SkipMeta);
			ValidateRecords(TEXT("MultiValueSkipDataZenAndDirect"), GetAndValidateRecords(TEXT("MultiValueSkipDataZen"), PutRecordsZen, ECachePolicy::Default | ECachePolicy::SkipData), RecievedRecordsSkipData, ECachePolicy::Default | ECachePolicy::SkipData);
		}
#endif // UE_WITH_ZEN
	}

	return true;
}

} // UE::DerivedData::CacheStore::Http

#endif // #if WITH_DEV_AUTOMATION_TESTS && WITH_HTTP_DDC_BACKEND
