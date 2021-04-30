// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/SharedBuffer.h"
#include "Misc/StringBuilder.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Virtualization/PayloadId.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::Virtualization
{
	
constexpr const uint32 TestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter;

/** This test ensures that ::IsValid returns the correct value dending on how the FPayloadId was created */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPayloadIdTestValidity, TEXT("System.Core.Virtualization.PayloadId.Validity"), TestFlags)
bool FPayloadIdTestValidity::RunTest(const FString& Parameters)
{
	// Create a test buffer for us to use in the tests
	const uint32 TestDataLength = 32;
	uint32 TestDataBuffer[TestDataLength];
	for (uint32 Index = 0; Index < TestDataLength; ++Index)
	{
		TestDataBuffer[Index] = Index;
	}

	// Default constructor
	{
		const FPayloadId EmptyId;
		TestFalse(TEXT("An empty FPayloadId ::IsValid()"), EmptyId.IsValid());
	}

	// FIoHash
	{
		const FIoHash EmptyHash;
		const FPayloadId EmptyHashId(EmptyHash);
		TestFalse(TEXT("A FPayloadId from an empty FIoHash ::IsValid()"), EmptyHashId.IsValid());

		const FIoHash EmptyBufferHash = FIoHash::HashBuffer(nullptr, 0);
		const FPayloadId EmptyBufferHashId(EmptyBufferHash);
		TestTrue(TEXT("A FPayloadId from a FIoHash of an empty buffer ::IsValid()"), EmptyBufferHashId.IsValid());

		const FIoHash BufferHash = FIoHash::HashBuffer(TestDataBuffer, TestDataLength * sizeof(uint32));
		const FPayloadId BufferHashId(BufferHash);
		TestTrue(TEXT("A FPayloadId from a FIoHash of a buffer ::IsValid()"), BufferHashId.IsValid());
	}

	// FGuid
	{
		const FGuid EmptyGuid;
		const FPayloadId EmptyGuidId(EmptyGuid);
		TestFalse(TEXT("A FPayloadId from an invalid FGuid ::IsValid()"), EmptyGuidId.IsValid());

		const FGuid ValidGuid = FGuid::NewGuid();
		const FPayloadId ValidGuidId(ValidGuid);
		TestTrue(TEXT("A FPayloadId from a valid FGuid ::IsValid()"), ValidGuidId.IsValid());
	}

	// FSharedBuffer
	{
		FSharedBuffer NullBuffer;
		const FPayloadId NullBufferId(NullBuffer);
		TestFalse(TEXT("A FPayloadId from a null FSharedBuffer ::IsValid()"), NullBufferId.IsValid());

		FSharedBuffer EmptyBuffer = FSharedBuffer::MakeView(nullptr, 0);
		const FPayloadId EmptyBufferId(EmptyBuffer);
		TestFalse(TEXT("A FPayloadId from an empty FSharedBuffer ::IsValid()"), EmptyBufferId.IsValid());

		FSharedBuffer ValidBuffer = FSharedBuffer::MakeView(TestDataBuffer, TestDataLength);
		const FPayloadId ValidBufferId(ValidBuffer);
		TestTrue(TEXT("A FPayloadId from a valid FSharedBuffer ::IsValid()"), ValidBufferId.IsValid());
	}

	return true;
}

/** This test ensures that the == operator is functioning correctly */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPayloadIdTestEquality, TEXT("System.Core.Virtualization.PayloadId.Equality"), TestFlags)
bool FPayloadIdTestEquality::RunTest(const FString& Parameters)
{
	// Create a test buffer for us to use in the tests
	const uint32 TestDataLength = 32;
	const uint32 TestDataNumBytes = TestDataLength * sizeof(uint32);

	uint32 TestDataBuffer0[TestDataLength];
	uint32 TestDataBuffer1[TestDataLength];
	uint32 TestDataBuffer3[TestDataLength];
	
	for (uint32 Index = 0; Index < TestDataLength; ++Index)
	{
		TestDataBuffer0[Index] = Index;
		TestDataBuffer1[Index] = Index * 2; // Note the first two entries will be the same as 
											// the other buffers
		TestDataBuffer3[Index] = Index;
	}

	const FPayloadId EmptyId0;
	const FPayloadId EmptyId1;

	const FPayloadId BufferId0(FIoHash::HashBuffer(TestDataBuffer0, TestDataNumBytes));
	const FPayloadId BufferId1(FIoHash::HashBuffer(TestDataBuffer1, TestDataNumBytes));
	const FPayloadId BufferId2(FIoHash::HashBuffer(TestDataBuffer3, TestDataNumBytes));

	TestTrue(TEXT("Two empty FPayloadId are equal"), EmptyId0 == EmptyId1);
	TestFalse(TEXT("An empty FPayloadId and a valid FPayloadId are equal"), EmptyId0 == BufferId0);
	TestFalse(TEXT("Two FPayloadId from different buffers with different values are equal"), BufferId0 == BufferId1);
	TestTrue(TEXT("Two FPayloadId from different but identical buffers are equal"), BufferId0 == BufferId2);

	return true;
}

/** This test ensures that the != operator is functioning correctly */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPayloadIdTestInequality, TEXT("System.Core.Virtualization.PayloadId.Inequality"), TestFlags)
bool FPayloadIdTestInequality::RunTest(const FString& Parameters)
{
	// Create a test buffer for us to use in the tests
	const uint32 TestDataLength = 32;
	const uint32 TestDataNumBytes = TestDataLength * sizeof(uint32);

	uint32 TestDataBuffer0[TestDataLength];
	uint32 TestDataBuffer1[TestDataLength];
	uint32 TestDataBuffer3[TestDataLength];

	for (uint32 Index = 0; Index < TestDataLength; ++Index)
	{
		TestDataBuffer0[Index] = Index;
		TestDataBuffer1[Index] = Index * 2; // Note the first two entries will be the same as 
											// the other buffers
		TestDataBuffer3[Index] = Index;
	}

	const FPayloadId EmptyId0;
	const FPayloadId EmptyId1;

	const FPayloadId BufferId0(FIoHash::HashBuffer(TestDataBuffer0, TestDataNumBytes));
	const FPayloadId BufferId1(FIoHash::HashBuffer(TestDataBuffer1, TestDataNumBytes));
	const FPayloadId BufferId2(FIoHash::HashBuffer(TestDataBuffer3, TestDataNumBytes));

	TestFalse(TEXT("Two empty FPayloadId are not equal"), EmptyId0 != EmptyId1);
	TestTrue(TEXT("An empty FPayloadId and a valid FPayloadId are not equal"), EmptyId0 != BufferId0);
	TestTrue(TEXT("Two FPayloadId from different buffers with different values are not equal"), BufferId0 != BufferId1);
	TestFalse(TEXT("Two FPayloadId from different but identical buffers are not equal"), BufferId0 != BufferId2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPayloadIdTestSerialization, TEXT("System.Core.Virtualization.PayloadId.Serialization"), TestFlags)
bool FPayloadIdTestSerialization::RunTest(const FString& Parameters)
{
	auto Serialize = [](FPayloadId InSrc, FPayloadId& OutDst) 
	{
		TArray<uint8> MemoryBuffer;
		const bool bIsArPersistent = true;

		{
			FMemoryWriter WriterAr(MemoryBuffer, bIsArPersistent);
			WriterAr << InSrc;
		}

		{
			FMemoryReader ReaderAr(MemoryBuffer, bIsArPersistent);
			ReaderAr << OutDst;
		}	
	};

	// Serialize an empty FPayloadId
	{
		FPayloadId Source;
		FPayloadId Result;

		Serialize(Source, Result);

		TestTrue(TEXT("Empty FPayloadId serializes to an empty FPayloadId"), Source == Result);
		TestTrue(TEXT("A serialized empty FPayloadId should be invalid"), Result.IsValid() == false);
	}

	// Serialize a valid FPayloadId
	{
		FPayloadId Source(FIoHash(TEXT("73cdaedfeff72f606fc1e73c9751a8418275da58")));
		FPayloadId Result;

		Serialize(Source, Result);

		TestTrue(TEXT("Valid FPayloadId serializes to a valid FPayloadId"), Source == Result);
		TestTrue(TEXT("A serialized FPayloadId should be valid"), Result.IsValid() == true);
	}
	
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPayloadIdTestTypeHash, TEXT("System.Core.Virtualization.PayloadId.TypeHash"), TestFlags)
bool FPayloadIdTestTypeHash::RunTest(const FString& Parameters)
{
	// Create a test buffer for us to use in the tests
	const uint32 TestDataLength = 32;
	const uint32 TestDataNumBytes = TestDataLength * sizeof(uint32);

	uint32 TestDataBuffer0[TestDataLength];
	uint32 TestDataBuffer1[TestDataLength];
	uint32 TestDataBuffer3[TestDataLength];

	for (uint32 Index = 0; Index < TestDataLength; ++Index)
	{
		TestDataBuffer0[Index] = Index;
		TestDataBuffer1[Index] = Index * 2; // Note the first two entries will be the same as 
											// the other buffers
		TestDataBuffer3[Index] = Index;
	}

	// Set up the FPayloadId's for the test
	const FPayloadId EmptyId0;
	const FPayloadId EmptyId1;

	const FPayloadId BufferId0(FIoHash::HashBuffer(TestDataBuffer0, TestDataNumBytes));
	const FPayloadId BufferId1(FIoHash::HashBuffer(TestDataBuffer1, TestDataNumBytes));
	const FPayloadId BufferId2(FIoHash::HashBuffer(TestDataBuffer3, TestDataNumBytes));

	// Test direct use of GetTypeHash
	{
		TestEqual(TEXT("Two empty FPayloadId should have the same hashes"), GetTypeHash(EmptyId1), GetTypeHash(EmptyId0));
		TestNotEqual(TEXT("An empty FPayloadId and a valid FPayloadId should have different hashes"), GetTypeHash(BufferId0), GetTypeHash(EmptyId0));
		TestNotEqual(TEXT("An empty FPayloadId and a valid FPayloadId should have different hashes"), GetTypeHash(BufferId1), GetTypeHash(EmptyId0));
		TestNotEqual(TEXT("An empty FPayloadId and a valid FPayloadId should have different hashes"), GetTypeHash(BufferId2), GetTypeHash(EmptyId0));
		TestNotEqual(TEXT("Two valid FPayloadId created from different buffers should have different hashes"), GetTypeHash(BufferId0), GetTypeHash(BufferId1));
		TestEqual(TEXT("Two valid FPayloadId created from binary equivillant buffers should have the same hashes"), GetTypeHash(BufferId0), GetTypeHash(BufferId2));
	}

	// Test the indirect use of GetTypeHash in practice via TMap
	{
#define TEST_MAP_ENTRY(Key, ExpectedValue) { uint32* Value = PayloadIdMap.Find(Key); \
											if (Value != nullptr) { TestEqual(TEXT("Data stored for key: " #Key), *Value, ExpectedValue); } else \
											{ AddError(TEXT("Unable to find entry for key: " #Key)); }}

		TMap<FPayloadId, uint32> PayloadIdMap;
		
		PayloadIdMap.Add(EmptyId0, 0);
		
		// Test that adding EmptyId0 created a single entry, and that since EmptyId0 and EmptyId1 
		// have the same type hash that we can access the value by both EmptyId0 and EmptyId1
		TestEqual(TEXT("Map Count"), PayloadIdMap.Num(), 1);
		TEST_MAP_ENTRY(EmptyId0, 0);
		TEST_MAP_ENTRY(EmptyId1, 0);

		PayloadIdMap.Add(EmptyId1, 1); // Should replace existing entry for EmptyId0!

		// Test that we still only have a single entry and that adding EmptyId1 replaced the value
		// in the entry for EmptyId0
		TestEqual(TEXT("Map Count"), PayloadIdMap.Num(), 1);
		TEST_MAP_ENTRY(EmptyId0, 1);
		TEST_MAP_ENTRY(EmptyId1, 1);

		PayloadIdMap.Add(BufferId0, 2);
		PayloadIdMap.Add(BufferId1, 3);

		// Test that adding the two new entries worked and that we can access the value of 
		// BufferId0 by BufferId2 since they produce the same type hash
		TestEqual(TEXT("Map Count"), PayloadIdMap.Num(), 3);
		TEST_MAP_ENTRY(EmptyId0, 1);
		TEST_MAP_ENTRY(EmptyId1, 1);
		TEST_MAP_ENTRY(BufferId0, 2);
		TEST_MAP_ENTRY(BufferId1, 3);
		TEST_MAP_ENTRY(BufferId2, 2);

		PayloadIdMap.Add(BufferId2, 4); // Should replace existing entry for BufferId0!

		// Test that adding BufferId2 did not add a new entry but correctly replaced the
		// existing entry for BufferId0
		TestEqual(TEXT("Map Count"), PayloadIdMap.Num(), 3);
		TEST_MAP_ENTRY(EmptyId0, 1);
		TEST_MAP_ENTRY(EmptyId1, 1);
		TEST_MAP_ENTRY(BufferId0, 4);
		TEST_MAP_ENTRY(BufferId1, 3);
		TEST_MAP_ENTRY(BufferId2, 4);

#undef TEST_MAP_ENTRY
	}


	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPayloadIdTestStringBuilder, TEXT("System.Core.Virtualization.PayloadId.StringBuilder"), TestFlags)
bool FPayloadIdTestStringBuilder::RunTest(const FString& Parameters)
{
	// Create a test buffer for us to use in the tests
	const uint32 TestDataLength = 32;
	const uint32 TestDataNumBytes = TestDataLength * sizeof(uint32);

	uint32 TestDataBuffer0[TestDataLength];
	uint32 TestDataBuffer1[TestDataLength];
	uint32 TestDataBuffer3[TestDataLength];

	for (uint32 Index = 0; Index < TestDataLength; ++Index)
	{
		TestDataBuffer0[Index] = Index;
		TestDataBuffer1[Index] = Index * 2; // Note the first two entries will be the same as 
											// the other buffers
		TestDataBuffer3[Index] = Index;
	}

	// Set up the FPayloadId's for the test
	const FPayloadId EmptyId;

	const FPayloadId BufferId0(FIoHash::HashBuffer(TestDataBuffer0, TestDataNumBytes));
	const FPayloadId BufferId1(FIoHash::HashBuffer(TestDataBuffer1, TestDataNumBytes));
	const FPayloadId BufferId2(FIoHash::HashBuffer(TestDataBuffer3, TestDataNumBytes));

	// Test again both TWideStringBuilder and TAnsiStringBuilder
#define TEST_PAYLOAD_ID(BufferToTest) {	TWideStringBuilder<128> WideBuilder; WideBuilder << BufferToTest; \
										TestEqual(TEXT("TWideStringBuilder << and ToString to produce the same result"), *WideBuilder, BufferToTest.ToString());\
										TAnsiStringBuilder<128> AnsiBuilder; AnsiBuilder << BufferToTest; \
										TestEqual(TEXT("TAnsiBuilder << and ToString to produce the same result"), *AnsiBuilder, BufferToTest.ToString()); }

	TEST_PAYLOAD_ID(EmptyId);
	TEST_PAYLOAD_ID(BufferId0);
	TEST_PAYLOAD_ID(BufferId1);
	TEST_PAYLOAD_ID(BufferId2);

#undef TEST_PAYLOAD_ID

	return true;
}

} // namespace UE::Virtualization

#endif //WITH_DEV_AUTOMATION_TESTS
