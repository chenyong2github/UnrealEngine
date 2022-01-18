// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "Hash/Blake3.h"
#include "Misc/StringBuilder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCacheStoreTest, "System.DerivedDataCache.CacheStore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter);

// TODO: remove this ifdef and enable the code it surrounds when we submit zenserver.exe
// with the change to HandleRpcGetCachePayloads to support the ValueAPI
#define ZENSERVER_SUPPORTS_VALUEAPI 0

bool FCacheStoreTest::RunTest(const FString& Parameters)
{
	using namespace UE::DerivedData;
	ICache& Cache = GetCache();

	FRequestOwner Owner(EPriority::Blocking);
	FCacheBucket DDCTestBucket(TEXT("DDCTest"));

	// NumRequests/2 must be larger than the batch size in ZenDerivedData,
	// and NumRequests >= 8 so that we get two results for each possible combination of flags
	const int32 NumRequests = 24;
	TArray<FCachePutRequest> PutRequests;
	TArray<FCachePutValueRequest> PutValueRequests;
	TArray<FCacheGetRequest> GetRequests;
	TArray<FCacheGetValueRequest> GetValueRequests;
	TArray<FCacheGetChunkRequest> ChunkRequests;
	struct FTestData
	{
		FValue Value;
		uint8 ByteValue;
		bool bGetRequestsData = true;
		bool bUseValueAPI = true;
		bool bReceivedPut = false;
		bool bReceivedGet = false;
		bool bReceivedPutValue = false;
		bool bReceivedGetValue = false;
		bool bReceivedChunk = false;
	};

	TMap<uint64, FTestData> TestDatas;
	FValueId ValueId = FValueId::FromName(TEXT("ValueName"));
	for (int32 n = 0; n < NumRequests; ++n)
	{
		FBlake3 KeyWriter;
		KeyWriter.Update(*TestName, TestName.Len() * sizeof(TestName[0]));
		KeyWriter.Update(&n, sizeof(n));
		FIoHash KeyHash = KeyWriter.Finalize();
		FCacheKey Key{ DDCTestBucket, KeyHash };
		FSharedString Name(WriteToString<16>(TEXT("Request"), n));
		uint64 UserData = (uint64)n;
		FTestData& TestData = TestDatas.FindOrAdd(UserData);

		ECachePolicy PutPolicy = ECachePolicy::Default;
		ECachePolicy GetPolicy = ECachePolicy::Default;
		TestData.bGetRequestsData = (n & 0x1) == 0;
		TestData.bUseValueAPI = (n & 0x2) != 0;
		if (!TestData.bGetRequestsData)
		{
			GetPolicy |= ECachePolicy::SkipData;
		}
		TestData.ByteValue = (uint8)n;
		TestData.Value = FValue::Compress(FSharedBuffer::MakeView(&TestData.ByteValue, 1));

		if (!TestData.bUseValueAPI)
		{
			FCacheRecordBuilder Builder(Key);
			Builder.AddValue(ValueId, TestData.Value);

			PutRequests.Add(FCachePutRequest{ Name, Builder.Build(), FCacheRecordPolicy(PutPolicy), UserData });
			GetRequests.Add(FCacheGetRequest{ Name, Key, FCacheRecordPolicy(GetPolicy), UserData });
		}
		else
		{
			PutValueRequests.Add(FCachePutValueRequest{ Name, Key, TestData.Value, PutPolicy, UserData });
			GetValueRequests.Add(FCacheGetValueRequest{ Name, Key, GetPolicy, UserData });
		}
		ChunkRequests.Add(FCacheGetChunkRequest{ Name, Key, TestData.bUseValueAPI ? FValueId() : ValueId,
			0, TestData.Value.GetRawSize(), FIoHash(), GetPolicy, UserData });
	}

	{
		FRequestBarrier Barrier(Owner);
		Cache.Put(PutRequests, Owner, [&TestDatas, this](FCachePutResponse&& Response)
			{
				FTestData* TestData = TestDatas.Find(Response.UserData);
				if (TestTrue(TEXT("Valid UserData in Put Callback"), TestData != nullptr))
				{
					TestData->bReceivedPut = true;
				}
			});
		Cache.PutValue(PutValueRequests, Owner, [&TestDatas, this](FCachePutValueResponse&& Response)
			{
				FTestData* TestData = TestDatas.Find(Response.UserData);
				if (TestTrue(TEXT("Valid UserData in PutValue Callback"), TestData != nullptr))
				{
					TestData->bReceivedPutValue = true;
				}
			});
	}
	Owner.Wait();
	for (TPair<uint64, FTestData>& Pair : TestDatas)
	{
		int32 n = (int32)Pair.Key;
		if (!Pair.Value.bUseValueAPI)
		{
			TestTrue(*WriteToString<16>(TEXT("Put %d received"), n), Pair.Value.bReceivedPut);
		}
		else
		{
			TestTrue(*WriteToString<16>(TEXT("PutValue %d received"), n), Pair.Value.bReceivedPutValue);
		}

	}

	{
		FRequestBarrier Barrier(Owner);
		Cache.Get(GetRequests, Owner, [&TestDatas, &ValueId, this](FCacheGetResponse&& Response)
			{
				FTestData* TestData = TestDatas.Find(Response.UserData);
				int32 n = (int32)Response.UserData;
				if (TestTrue(TEXT("Valid UserData in Get Callback"), TestData != nullptr))
				{
					TestData->bReceivedGet = true;
					if (TestTrue(*WriteToString<32>(TEXT("Get %d succeeded"), n), Response.Status == EStatus::Ok))
					{
						FCacheRecord& Record = Response.Record;
						TConstArrayView<FValueWithId> Values = Record.GetValues();
						if (TestEqual(*WriteToString<32>(TEXT("Get %d ValuesLen"), n), Values.Num(), 1))
						{
							const FValueWithId& ActualValue = Values[0];
							TestEqual(*WriteToString<32>(TEXT("Get %d ValueId"), n), ActualValue.GetId(), ValueId);
							FValue& ExpectedValue = TestData->Value;
							TestEqual(*WriteToString<32>(TEXT("Get %d Hash"), n),
								ActualValue.GetRawHash(), ExpectedValue.GetRawHash());
							TestEqual(*WriteToString<32>(TEXT("Get %d Size"), n),
								ActualValue.GetRawSize(), ExpectedValue.GetRawSize());

							if (TestData->bGetRequestsData)
							{
								const FCompressedBuffer& Compressed = ActualValue.GetData();
								FSharedBuffer Buffer = Compressed.Decompress();
								TestEqual(*WriteToString<32>(TEXT("Get %d Data Size"), n),
									Buffer.GetSize(), ActualValue.GetRawSize());
								if (Buffer.GetSize())
								{
									TestEqual(*WriteToString<32>(TEXT("Get %d Data Equals"), n),
										((const uint8*)Buffer.GetData())[0], TestData->ByteValue);
								}
							}
						}
					}
				}
			});

		Cache.GetValue(GetValueRequests, Owner, [&TestDatas, this](FCacheGetValueResponse&& Response)
			{
				FTestData* TestData = TestDatas.Find(Response.UserData);
				int32 n = (int32)Response.UserData;
				if (TestTrue(TEXT("Valid UserData in GetValue Callback"), TestData != nullptr))
				{
					TestData->bReceivedGetValue = true;
					if (TestTrue(*WriteToString<32>(TEXT("GetValue %d succeeded"), n), Response.Status == EStatus::Ok))
					{
						FValue& ActualValue = Response.Value;
						FValue& ExpectedValue = TestData->Value;
						TestEqual(*WriteToString<32>(TEXT("GetValue %d Hash"), n),
							ActualValue.GetRawHash(), ExpectedValue.GetRawHash());
						TestEqual(*WriteToString<32>(TEXT("GetValue %d Size"), n),
							ActualValue.GetRawSize(), ExpectedValue.GetRawSize());

						if (TestData->bGetRequestsData)
						{
							const FCompressedBuffer& Compressed = ActualValue.GetData();
							FSharedBuffer Buffer = Compressed.Decompress();
							TestEqual(*WriteToString<32>(TEXT("GetValue %d Data Size"), n),
								Buffer.GetSize(), ActualValue.GetRawSize());
							if (Buffer.GetSize())
							{
								TestEqual(*WriteToString<32>(TEXT("GetValue %d Data Equals"), n),
									((const uint8*)Buffer.GetData())[0], TestData->ByteValue);
							}
						}
					}
				}
			});

#if ZENSERVER_SUPPORTS_VALUEAPI
		Cache.GetChunks(ChunkRequests, Owner, [&TestDatas, this](FCacheGetChunkResponse&& Response)
			{
				FTestData* TestData = TestDatas.Find(Response.UserData);
				int32 n = (int32)Response.UserData;
				if (TestTrue(TEXT("Valid UserData in GetChunks Callback"), TestData != nullptr))
				{
					TestData->bReceivedChunk = true;
					if (TestTrue(*WriteToString<32>(TEXT("GetChunks %d succeeded"), n), Response.Status == EStatus::Ok))
					{
						FValue& ExpectedValue = TestData->Value;
						TestEqual(*WriteToString<32>(TEXT("GetChunks %d Hash"), n),
							Response.RawHash, ExpectedValue.GetRawHash());
						TestEqual(*WriteToString<32>(TEXT("GetChunks %d Size"), n),
							Response.RawSize, ExpectedValue.GetRawSize());
						if (TestData->bGetRequestsData)
						{
							FSharedBuffer Buffer = Response.RawData;
							TestEqual(*WriteToString<32>(TEXT("GetChunks %d Data Size"), n),
								Buffer.GetSize(), Response.RawSize);
							if (Buffer.GetSize())
							{
								TestEqual(*WriteToString<32>(TEXT("GetChunks %d Data Equals"), n),
									((const uint8*)Buffer.GetData())[0], TestData->ByteValue);
							}
						}
					}
				}
			});
#endif
	}
	Owner.Wait();
	for (TPair<uint64, FTestData>& Pair : TestDatas)
	{
		int32 n = (int32)Pair.Key;
		if (!Pair.Value.bUseValueAPI)
		{
			TestTrue(*WriteToString<16>(TEXT("Get %d received"), n), Pair.Value.bReceivedGet);
		}
		else
		{
			TestTrue(*WriteToString<16>(TEXT("GetValue %d received"), n), TestDatas[n].bReceivedGetValue);
		}
#if ZENSERVER_SUPPORTS_VALUEAPI
		TestTrue(*WriteToString<16>(TEXT("GetChunk %d received"), n), TestDatas[n].bReceivedChunk);
#endif
	}

	return true;
}

#endif