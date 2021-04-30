// Copyright Epic Games, Inc. All Rights Reserved.

#include "Algo/AllOf.h"
#include "Memory/SharedBuffer.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Templates/UniquePtr.h"
#include "Virtualization/VirtualizationUtilities.h"
#include "Virtualization/VirtualizedBulkData.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA

namespace UE::Virtualization
{
	
constexpr const uint32 TestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter;

/**
 * This test creates a very basic VirtualizedBulkData object with in memory payload and validates that we are able to retrieve the 
 * payload via both TFuture and callback methods. It then creates copies of the object and makes sure that we can get the payload from
 * the copies, even when the original source object has been reset.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestBasic, TEXT("System.Core.Virtualization.BulkData.Basic"), TestFlags)
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
			FSharedBuffer RetrievedBuffer = BulkDataToValidate.GetPayload().Get();
			TestEqual(FString::Printf(TEXT("%s buffer length"),Label), (int64)RetrievedBuffer.GetSize(), BufferSize);
			TestTrue(FString::Printf(TEXT("SourceBuffer values == %s values"),Label), FMemory::Memcmp(SourceBuffer.Get(), RetrievedBuffer.GetData(), BufferSize) == 0);
		};

	// Create a basic bulkdata (but retain ownership of the buffer!)
	FByteVirtualizedBulkData BulkData;
	BulkData.UpdatePayload(FSharedBuffer::MakeView(SourceBuffer.Get(), BufferSize));

	// Test accessing the data from the bulkdata object
	ValidateBulkData(BulkData, TEXT("Retrieved"));

	// Create a new bulkdata object via the copy constructor
	FByteVirtualizedBulkData BulkDataCopy(BulkData);

	// Create a new bulkdata object and copy by assignment (note we assign some junk data that will get overwritten)
	FByteVirtualizedBulkData BulkDataAssignment;
	BulkDataAssignment.UpdatePayload(FUniqueBuffer::Alloc(128).MoveToShared());
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestEmpty, TEXT("System.Core.Virtualization.BulkData.Empty"), TestFlags)
bool FVirtualizationWrapperTestEmpty::RunTest(const FString& Parameters)
{
	FByteVirtualizedBulkData BulkData;

	// Validate the general accessors
	TestEqual(TEXT("Return value of ::GetBulkDataSize()"), BulkData.GetPayloadSize(), (int64)0);
	TestTrue(TEXT("Payload key is invalid"), !BulkData.GetPayloadId().IsValid());
	TestFalse(TEXT("Return value of ::IsDataLoaded()"), BulkData.IsDataLoaded());

	// Validate the payload accessors
	FSharedBuffer Payload = BulkData.GetPayload().Get();
	TestTrue(TEXT("The payload from the GetPayload TFuture is null"), Payload.IsNull());

	return true;
}

/**
 * Test the various methods for updating the payload that a FVirtualizedUntypedBulkData owns
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestUpdatePayload, TEXT("System.Core.Virtualization.BulkData.UpdatePayload"), TestFlags)
bool FVirtualizationWrapperTestUpdatePayload::RunTest(const FString& Parameters)
{
	// Create a memory buffer of all zeros
	const int64 BufferSize = 1024;
	TUniquePtr<uint8[]> OriginalData = MakeUnique<uint8[]>(BufferSize);
	FMemory::Memzero(OriginalData.Get(), BufferSize);

	// Pass the buffer to to bulkdata but retain ownership
	FByteVirtualizedBulkData BulkData;
	BulkData.UpdatePayload(FSharedBuffer::MakeView(OriginalData.Get(), BufferSize));

	// Access the payload, edit it and push it back into the bulkdata object
	{
		// The payload should be the same size and same contents as the original buffer but a different
		// memory address since we retained ownership in the TUniquePtr, so the bulkdata object should 
		// have created it's own copy.
		FSharedBuffer Payload = BulkData.GetPayload().Get();
		TestEqual(TEXT("Payload length"), (int64)Payload.GetSize(), BufferSize);
		TestNotEqual(TEXT("OriginalData and the payload should have different memory addresses"), (uint8*)OriginalData.Get(), (uint8*)Payload.GetData());
		TestTrue(TEXT("Orginal buffer == Payload data"), FMemory::Memcmp(OriginalData.Get(), Payload.GetData(), Payload.GetSize()) == 0);

		// Make a copy of the payload that we can edit
		const uint8 NewValue = 255;
		FSharedBuffer EditedPayload;
		{
			FUniqueBuffer EditablePayload = FUniqueBuffer::Clone(Payload);
			FMemory::Memset(EditablePayload.GetData(), NewValue, EditablePayload.GetSize());
			EditedPayload = EditablePayload.MoveToShared();
		}

		// Update the bulkdata object with the new edited payload
		BulkData.UpdatePayload(EditedPayload);

		Payload = BulkData.GetPayload().Get();
		TestEqual(TEXT("Updated payload length"), (int64)Payload.GetSize(), BufferSize);
		TestEqual(TEXT("Payload and EditablePayload should have the same memory addresses"), (uint8*)Payload.GetData(), (uint8*)EditedPayload.GetData());

		const bool bAllElementsCorrect = Algo::AllOf(TArrayView<uint8>((uint8*)Payload.GetData(), Payload.GetSize()), [NewValue](uint8 Val)
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
		BulkData.UpdatePayload(FSharedBuffer::TakeOwnership(OriginalData.Release(), BufferSize, FMemory::Free));

		FSharedBuffer Payload = BulkData.GetPayload().Get();
		TestEqual(TEXT("Updated payload length"), (int64)Payload.GetSize(), BufferSize);
		TestEqual(TEXT("Payload and OriginalDataPtr should have the same memory addresses"), (uint8*)Payload.GetData(), OriginalDataPtr);

		// The original data was all zeros, so we can test for that to make sure that the contents are correct.
		const bool bAllElementsCorrect = Algo::AllOf(TArrayView<uint8>((uint8*)Payload.GetData(), Payload.GetSize()), [](uint8 Val)
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestReaderWriter, TEXT("System.Core.Virtualization.BulkData.Reader/Writer"), TestFlags)
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

/**
 * This set of tests validate that the BulkData's identifier works how we expect it too. It should remain unique in all cases except
 * move semantics.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVirtualizationWrapperTestIdentifiers, TEXT("System.Core.Virtualization.BulkData.Identifiers"), TestFlags)
bool FVirtualizationWrapperTestIdentifiers::RunTest(const FString& Parameters)
{
	// Some basic tests with an invalid id
	{
		FByteVirtualizedBulkData BulkData;
		TestFalse(TEXT("BulkData with no payload should return an invalid identifier"), BulkData.GetIdentifier().IsValid());

		FByteVirtualizedBulkData CopiedBulkData(BulkData);
		TestFalse(TEXT("Copying a bulkdata with an invalid id should result in an invalid id"), CopiedBulkData.GetIdentifier().IsValid());

		FByteVirtualizedBulkData AssignedBulkData; 
		AssignedBulkData = BulkData;
		TestFalse(TEXT("Assigning a a bulkdata with an invalid id should result in an invalid id"), AssignedBulkData.GetIdentifier().IsValid());
		
		// Check that we did not change the initial object at any point
		TestFalse(TEXT("Being copied and assigned to other objects should not affect the identifier"), BulkData.GetIdentifier().IsValid());
	}

	// Some basic tests with a valid id
	{
		FByteVirtualizedBulkData BulkData;
		BulkData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared()); // Assigning this payload should cause BulkData to gain an identifier
		TestTrue(TEXT("BulkData with a payload should returns a valid identifier"), BulkData.GetIdentifier().IsValid());

		const FGuid OriginalGuid = BulkData.GetIdentifier();

		FByteVirtualizedBulkData CopiedBulkData(BulkData);
		TestNotEqual(TEXT("Copying a bulkdata with a valid id should result in a unique identifier"), BulkData.GetIdentifier(), CopiedBulkData.GetIdentifier());

		FByteVirtualizedBulkData AssignedBulkData;
		AssignedBulkData = BulkData;
		TestNotEqual(TEXT("Assignment operator creates different identifiers"), BulkData.GetIdentifier(), AssignedBulkData.GetIdentifier());

		// Check that we did not change the initial object at any point
		TestEqual(TEXT("Being copied and assigned to other objects should not affect the identifier"), BulkData.GetIdentifier(), OriginalGuid);

		// Now that AssignedBulkData has a valid identifier, make sure that it is not changed if we assign something else to it.
		const FGuid OriginalAssignedGuid = AssignedBulkData.GetIdentifier();
		AssignedBulkData = CopiedBulkData;
		TestEqual(TEXT("Being copied and assigned to other objects should not affect the identifier"), AssignedBulkData.GetIdentifier(), OriginalAssignedGuid);
	}

	// Test move constructor
	{	
		FVirtualizedUntypedBulkData BulkData;
		BulkData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());

		FGuid OriginalGuid = BulkData.GetIdentifier();

		FVirtualizedUntypedBulkData MovedBulkData = MoveTemp(BulkData);

		TestEqual(TEXT("Move constructor should preserve the identifier"), MovedBulkData.GetIdentifier(), OriginalGuid);
	}

	// Test move assignment
	{
		FVirtualizedUntypedBulkData BulkData;
		BulkData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());

		FGuid OriginalGuid = BulkData.GetIdentifier();

		FVirtualizedUntypedBulkData MovedBulkData;
		MovedBulkData = MoveTemp(BulkData);

		TestEqual(TEXT("Move assignment should preserve the identifier"), MovedBulkData.GetIdentifier(), OriginalGuid);
	}

	// Check that resizing an array will not change the internals
	{
		const uint32 NumToTest = 10;

		TArray<FByteVirtualizedBulkData> BulkDataArray;
		TArray<FGuid> GuidArray;

		for (uint32 Index = 0; Index < NumToTest; ++Index)
		{
			BulkDataArray.Add(FByteVirtualizedBulkData());

			// Leave some with invalid ids and some with valid ones
			if (Index % 2 == 0)
			{
				BulkDataArray[Index].UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());
			}

			GuidArray.Add(BulkDataArray[Index].GetIdentifier());
		}

		// Force an internal reallocation and make sure that the identifiers are unchanged.
		// Note that it is possible that the allocation is just resized and not reallocated.
		BulkDataArray.Reserve(BulkDataArray.Max() * 4);

		for (uint32 Index = 0; Index < NumToTest; ++Index)
		{
			TestEqual(TEXT(""), BulkDataArray[Index].GetIdentifier(), GuidArray[Index]);
		}

		// Now insert a new item, moving all of the existing entries and make sure that
		// the identifiers are unchanged.
		BulkDataArray.Insert(FByteVirtualizedBulkData(), 0);

		for (uint32 Index = 0; Index < NumToTest; ++Index)
		{
			TestEqual(TEXT(""), BulkDataArray[Index+1].GetIdentifier(), GuidArray[Index]);
		}
	}

	// Test that adding a payload to a reset bulkdata object or one that has had a zero length payload applied
	// will correctly show the original id once it has a valid payload.
	{
		FByteVirtualizedBulkData BulkData;
		BulkData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());
		
		const FGuid OriginalGuid = BulkData.GetIdentifier();

		BulkData.Reset();
		TestFalse(TEXT("BulkData with no payload should return an invalid identifier"), BulkData.GetIdentifier().IsValid());

		BulkData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());
		TestEqual(TEXT("Removing a payload then adding a new one should return the original identifier"), BulkData.GetIdentifier(), OriginalGuid);

		BulkData.UpdatePayload(FUniqueBuffer::Alloc(0).MoveToShared());
		TestFalse(TEXT("Setting a zero length payload should return an invalid identifier"), BulkData.GetIdentifier().IsValid());
		
		BulkData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());
		TestEqual(TEXT("Restoring a payload should return the original identifier"), BulkData.GetIdentifier(), OriginalGuid);
	}

	// Test that serialization does not change the identifier (in this case serializing to and from a memory buffer) 
	{
		FByteVirtualizedBulkData SrcData;
		SrcData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());

		TArray<uint8> MemoryBuffer;
		const bool bIsArPersistent = true;

		FByteVirtualizedBulkData DstData;

		// Serialize the SourceBuffer to BulkData 
		{
			FMemoryWriter WriterAr(MemoryBuffer, bIsArPersistent);
			SrcData.Serialize(WriterAr, nullptr);
		}

		// Serialize BulkData back to ReplicatedBuffer
		{
			FMemoryReader ReaderAr(MemoryBuffer, bIsArPersistent);
			DstData.Serialize(ReaderAr, nullptr);
		}

		TestEqual(TEXT("Serialization should preserve the identifier"), SrcData.GetIdentifier(), DstData.GetIdentifier());
	}

	// Test that serialization does not change the identifier (in this case serializing to and from a memory buffer) 
	{
		FByteVirtualizedBulkData SrcData;
		SrcData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());

		const FGuid OriginalIdentifier = SrcData.GetIdentifier();
		SrcData.UpdatePayload(FUniqueBuffer::Alloc(0).MoveToShared());

		TArray<uint8> MemoryBuffer;
		const bool bIsArPersistent = true;

		FByteVirtualizedBulkData DstData;

		// Serialize the SourceBuffer to BulkData 
		{
			FMemoryWriter WriterAr(MemoryBuffer, bIsArPersistent);
			SrcData.Serialize(WriterAr, nullptr);
		}

		// Serialize BulkData back to ReplicatedBuffer
		{
			FMemoryReader ReaderAr(MemoryBuffer, bIsArPersistent);
			DstData.Serialize(ReaderAr, nullptr);
		}

		TestFalse(TEXT("After serialization the identifier should be invalid"), DstData.GetIdentifier().IsValid());

		DstData.UpdatePayload(FUniqueBuffer::Alloc(32).MoveToShared());
		TestEqual(TEXT("After adding a new payload the object should have the original identifier"), DstData.GetIdentifier(), OriginalIdentifier);
	}

	return true;
}

} // namespace UE::Virtualization

#endif //WITH_DEV_AUTOMATION_TESTS && WITH_EDITORONLY_DATA
