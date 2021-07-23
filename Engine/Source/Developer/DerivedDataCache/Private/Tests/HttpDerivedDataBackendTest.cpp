// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpDerivedDataBackend.h"

#include "Async/ParallelFor.h"
#include "DerivedDataBackendInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/SecureHash.h"

// Test is targeted at HttpDerivedDataBackend but with some backend test interface it could be generalized
// to function against all backends.

#if WITH_DEV_AUTOMATION_TESTS && WITH_HTTP_DDC_BACKEND

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

// Helper function to create a number of dummy cache keys for testing
TArray<FString> CreateTestCacheKeys(UE::DerivedData::Backends::FHttpDerivedDataBackend* InTestBackend, uint32 InNumKeys)
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

IMPLEMENT_HTTPDERIVEDDATA_AUTOMATION_TEST(FConcurrentCachedDataProbablyExistsBatch, TEXT(".FConcurrentCachedDataProbablyExistsBatch"), EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConcurrentCachedDataProbablyExistsBatch::RunTest(const FString& Parameters)
{
	UE::DerivedData::Backends::FHttpDerivedDataBackend* TestBackend = GetTestBackend();

	const int32 ParallelTasks = 32;
	const uint32 Iterations = 20;
	const uint32 KeysInBatch = 4;

	TArray<FString> Keys = CreateTestCacheKeys(TestBackend, KeysInBatch);

	std::atomic<uint32> MismatchedResults = 0;
	ParallelFor(ParallelTasks,
		[&](int32 TaskIndex)
		{
			for (uint32 Iteration = 0; Iteration < Iterations; ++Iteration)
			{
				TConstArrayView<FString> BatchView = MakeArrayView(Keys.GetData(), KeysInBatch);
				TBitArray<> Result = TestBackend->CachedDataProbablyExistsBatch(BatchView);
				if (Result.CountSetBits() != BatchView.Num())
				{
					MismatchedResults.fetch_add(BatchView.Num() - Result.CountSetBits(), std::memory_order_relaxed);
				}
			}
		}
	);
	TestEqual(TEXT("Concurrent calls to CachedDataProbablyExistsBatch for a batch of keys that were put are not reliably found"), MismatchedResults, 0);

	return true;
}

// This test validate that batch requests wont mismatch head and get request for the same keys in the same batch
IMPLEMENT_HTTPDERIVEDDATA_AUTOMATION_TEST(FConcurrentExistsAndGetForSameKeyBatch, TEXT(".FConcurrentExistsAndGetForSameKeyBatch"), EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FConcurrentExistsAndGetForSameKeyBatch::RunTest(const FString& Parameters)
{
	UE::DerivedData::Backends::FHttpDerivedDataBackend* TestBackend = GetTestBackend();

	const int32 ParallelTasks = 32;
	const uint32 Iterations = 20;
	const uint32 KeysInBatch = 4;

	TArray<FString> Keys = CreateTestCacheKeys(TestBackend, KeysInBatch);

	ParallelFor(ParallelTasks,
		[&](int32 TaskIndex)
		{
			for (uint32 Iteration = 0; Iteration < Iterations; ++Iteration)
			{
				for (int32 KeyIndex = 0; KeyIndex < Keys.Num(); ++KeyIndex)
				{
					if ((TaskIndex % 2) ^ (KeyIndex % 2))
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

}
#endif // WITH_DEV_AUTOMATION_TESTS
