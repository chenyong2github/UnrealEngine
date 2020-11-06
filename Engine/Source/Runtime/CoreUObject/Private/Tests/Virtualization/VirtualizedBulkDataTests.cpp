// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/VirtualizedBulkData.h"
#include "Virtualization/VirtualizationUtilities.h"
#include "Memory/SharedBuffer.h"
#include "Templates/UniquePtr.h"
#include "Algo/AllOf.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

constexpr const uint32 TestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter;

/**
 * This test creates a very basic VirtualizedBulkData object with in memory payload and validates that we are able to retrieve the 
 * payload via both TFuture and callback methods. It then creates copies of the object and makes sure that we can get the payload from
 * the copies, even when the original source object has been reset.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestBasic, TEXT("System.Core.VirtualizedBulkData.Basic"), TestFlags)
bool FVirtualizationWrapperTestBasic::RunTest(const FString& Parameters)
{
	const int64 BufferSize = 1024;
	TUniquePtr<uint8[]> SourceBuffer = MakeUnique<uint8[]>(BufferSize);
	
	// Write some values to SourceBuffer
	for (int64 Index = 0; Index < BufferSize; ++Index)
	{
		SourceBuffer[Index] = (uint8)(FMath::Rand() % 255);
	}

	auto ValidateBulkData = [this, &SourceBuffer, BufferSize](const FVirtualizedUntypedBulkData& BulkDataToValidate, const TCHAR* Label)
		{
			FSharedBufferConstPtr RetrievedBuffer = BulkDataToValidate.GetData().Get();
			TestEqual(FString::Printf(TEXT("%s buffer length"),Label), (int64)RetrievedBuffer->GetSize(), BufferSize);
			TestTrue(FString::Printf(TEXT("SourceBuffer values == %s values"),Label), FMemory::Memcmp(SourceBuffer.Get(), RetrievedBuffer->GetData(), BufferSize) == 0);
		};

	// Create a basic bulkdata (but retain ownership of the buffer!)
	FByteVirtualizedBulkData BulkData;
	BulkData.UpdatePayload(FSharedBuffer::MakeView(SourceBuffer.Get(), BufferSize).ToSharedRef());

	// Test access via callback
	BulkData.GetData([this,BufferSize,&SourceBuffer](FSharedBufferConstPtr Payload)
		{
			TestEqual(TEXT("Callback buffer length"), (int64)Payload->GetSize(), BufferSize);
			TestTrue(TEXT("SourceBuffer values == Callback values"), FMemory::Memcmp(SourceBuffer.Get(), Payload->GetData(), BufferSize) == 0);
		});

	// Test accessing the data from the bulkdata object
	ValidateBulkData(BulkData, TEXT("Retrieved"));

	// Create a new bulkdata object via the copy constructor
	FByteVirtualizedBulkData BulkDataCopy(BulkData);

	// Create a new bulkdata object and copy by assignment (note we assign some junk data that will get overwritten)
	FByteVirtualizedBulkData BulkDataAssignment;
	BulkDataAssignment.UpdatePayload(FSharedBuffer::Alloc(128).ToSharedRef());
	BulkDataAssignment = BulkData;

	// Test both bulkdata objects
	ValidateBulkData(BulkDataCopy, TEXT("Copy Constructor"));
	ValidateBulkData(BulkDataAssignment, TEXT("Copy Assignment"));

	// Should not affect BulkDataAssignment/BulkDataCopy 
	BulkData.Reset();

	// Test both bulkdata objects again now that we reset the data
	ValidateBulkData(BulkDataCopy, TEXT("Copy Constructor (after data reset)"));
	ValidateBulkData(BulkDataAssignment, TEXT("Copy Assignment (after data reset)"));

	return true;
}

/**
 * This test will validate how VirtualizedBulkData behaves when it has no associated payload and make sure 
 * that our assumptions are correct.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestEmpty, TEXT("System.Core.VirtualizedBulkData.Empty"), TestFlags)
bool FVirtualizationWrapperTestEmpty::RunTest(const FString& Parameters)
{
	FByteVirtualizedBulkData BulkData;

	// Validate the general accessors
	TestEqual(TEXT("Return value of ::GetBulkDataSize()"), BulkData.GetBulkDataSize(), (int64)0);
	TestTrue(TEXT("Payload key is invalid"), !BulkData.GetKey().IsValid());
	TestFalse(TEXT("Return value of ::IsDataLoaded()"), BulkData.IsDataLoaded());

	// Validate the payload accessors
	FSharedBufferConstPtr Payload = BulkData.GetData().Get();
	TestFalse(TEXT("The payload from the GetData TFuture is invalid"), Payload.IsValid());

	BulkData.GetData([this](FSharedBufferConstPtr Payload) 
		{
			TestFalse(TEXT("The payload via the GetData callback is invalid"), Payload.IsValid());
		});

	return true;
}

/**
 * Test the various methods for updating the payload that a FVirtualizedUntypedBulkData owns
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestUpdatePayload, TEXT("System.Core.VirtualizedBulkData.UpdatePayload"), TestFlags)
bool FVirtualizationWrapperTestUpdatePayload::RunTest(const FString& Parameters)
{
	// Create a memory buffer of all zeros
	const int64 BufferSize = 1024;
	TUniquePtr<uint8[]> OriginalData = MakeUnique<uint8[]>(BufferSize);
	FMemory::Memzero(OriginalData.Get(), BufferSize);

	// Pass the buffer to to bulkdata but retain ownership
	FByteVirtualizedBulkData BulkData;
	BulkData.UpdatePayload(FSharedBuffer::MakeView(OriginalData.Get(), BufferSize).ToSharedRef());

	// Access the payload, edit it and push it back into the bulkdata object
	{
		// The payload should be the same size and same contents as the original buffer but a different
		// memory address since we retained ownership in the TUniquePtr, so the bulkdata object should 
		// have created it's own copy.
		FSharedBufferConstPtr Payload = BulkData.GetData().Get();
		TestEqual(TEXT("Payload length"), (int64)Payload->GetSize(), BufferSize);
		TestNotEqual(TEXT("OriginalData and the payload should have different memory addresses"), (uint8*)OriginalData.Get(), (uint8*)Payload->GetData());
		TestTrue(TEXT("Orginal buffer == Payload data"), FMemory::Memcmp(OriginalData.Get(), Payload->GetData(), Payload->GetSize()) == 0);

		// Make a copy of the payload that we can edit
		FSharedBufferRef EditablePayload = FSharedBuffer::Clone(*Payload).ToSharedRef();

		const uint8 NewValue = 255;
		FMemory::Memset(EditablePayload->GetData(), NewValue, EditablePayload->GetSize());

		// Update the bulkdata object with the new edited payload
		BulkData.UpdatePayload(EditablePayload);

		Payload = BulkData.GetData().Get();
		TestEqual(TEXT("Updated payload length"), (int64)Payload->GetSize(), BufferSize);
		TestEqual(TEXT("Payload and EditablePayload should have the same memory addresses"), (uint8*)Payload->GetData(), (uint8*)EditablePayload->GetData());

		const bool bAllElementsCorrect = Algo::AllOf(TArrayView<uint8>((uint8*)Payload->GetData(), Payload->GetSize()), [NewValue](uint8 Val)
			{ 
				return Val == NewValue; 
			});

		TestTrue(TEXT("All payload elements correctly updated"), bAllElementsCorrect);
	}

	{
		// Store the original data pointer address so we can test against it later, we should not actually use
		// this pointer though as once we pass it to the bulkdata object we cannot be sure what happens to it.
		uint8* OriginalDataPtr = OriginalData.Get();

		// Update the bulkdata object with the original data but this time we give ownership of the buffer to
		// the bulkdata object.
		BulkData.UpdatePayload(FSharedBuffer::TakeOwnership(OriginalData.Release(), BufferSize, FMemory::Free).ToSharedRef());

		FSharedBufferConstPtr Payload = BulkData.GetData().Get();
		TestEqual(TEXT("Updated payload length"), (int64)Payload->GetSize(), BufferSize);
		TestEqual(TEXT("Payload and OriginalDataPtr should have the same memory addresses"), (uint8*)Payload->GetData(), OriginalDataPtr);

		// The original data was all zeros, so we can test for that to make sure that the contents are correct.
		const bool bAllElementsCorrect = Algo::AllOf(TArrayView<uint8>((uint8*)Payload->GetData(), Payload->GetSize()), [](uint8 Val)
			{
				return Val == 0;
			});

		TestTrue(TEXT("All payload elements correctly updated"), bAllElementsCorrect);
	}

	return true;
}

/**
 * This test will create a buffer, then serialize it to a VirtualizedBulkData object via FVirtualizedBulkDataWriter.
 * Then we will serialize the VirtualizedBulkData object back to a second buffer and compare the results.
 * If the reader and writers are working then the ReplicatedBuffer should be the same as the original SourceBuffer.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestReaderWriter, TEXT("System.Core.VirtualizedBulkData.Reader/Writer"), TestFlags)
bool FVirtualizationWrapperTestReaderWriter::RunTest(const FString& Parameters)
{
	const int64 BufferSize = 1024;

	TUniquePtr<uint8[]> SourceBuffer = MakeUnique<uint8[]>(BufferSize);
	TUniquePtr<uint8[]> ReplicatedBuffer = MakeUnique<uint8[]>(BufferSize);

	// Write some values to SourceBuffer
	for (int64 Index = 0; Index < BufferSize; ++Index)
	{
		SourceBuffer[Index] = (uint8)(FMath::Rand() % 255);
	}

	FByteVirtualizedBulkData BulkData;

	// Serialize the SourceBuffer to BulkData 
	{
		FVirtualizedBulkDataWriter WriterAr(BulkData);
		WriterAr.Serialize(SourceBuffer.Get(), BufferSize);
	}

	// Serialize BulkData back to ReplicatedBuffer
	{
		FVirtualizedBulkDataReader ReaderAr(BulkData);
		ReaderAr.Serialize(ReplicatedBuffer.Get(), BufferSize);
	}

	// Now test that the buffer was restored to the original values
	const bool bMemCmpResult = FMemory::Memcmp(SourceBuffer.Get(), ReplicatedBuffer.Get(), BufferSize) == 0;
	TestTrue(TEXT("SourceBuffer values == ReplicatedBuffer values"), bMemCmpResult);

	// Test writing nothing to an empty bulkdata object and then reading that bulkdata object
	// to make sure that we deal with null buffers properly.
	{
		FByteVirtualizedBulkData EmptyBulkData;
		FVirtualizedBulkDataWriter WriterAr(EmptyBulkData);
		FVirtualizedBulkDataReader ReaderAr(EmptyBulkData);
	}
	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA
