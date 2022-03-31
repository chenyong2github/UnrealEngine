// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "TestFixtures/CoreTestFixture.h"

// These file tests are designed to ensure expected file writing behavior, as well as cross-platform consistency
TEST_CASE_METHOD(FCoreTestFixture, "Core::Misc::File::File Truncate", "[Core][Misc][Smoke]")
{
	const FString TempFilename = FPaths::CreateTempFilename(*FPaths::EngineIntermediateDir());
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	ON_SCOPE_EXIT
	{
		// Delete temp file
		PlatformFile.DeleteFile(*TempFilename);
	};

	{
		INFO("Open Test File")

		// Open a test file
		if (TUniquePtr<IFileHandle> TestFile = TUniquePtr<IFileHandle>(PlatformFile.OpenWrite(*TempFilename, /*bAppend*/false, /*bAllowRead*/true)))
		{
			// Append 4 int32 values of incrementing value to this file
			int32 Val = 1;
			TestFile->Write((const uint8*)&Val, sizeof(Val));

			++Val;
			TestFile->Write((const uint8*)&Val, sizeof(Val));

			// Tell here, so we can move back and truncate after writing
			const int64 ExpectedTruncatePos = TestFile->Tell();
			++Val;
			TestFile->Write((const uint8*)&Val, sizeof(Val));

			// Tell here, so we can attempt to read here after truncation
			const int64 TestReadPos = TestFile->Tell();
			++Val;
			TestFile->Write((const uint8*)&Val, sizeof(Val));

			// Validate that the Tell position is at the end of the file, and that the size is reported correctly	
			{
				INFO("File was not the expected size");

				const int64 ActualEOFPos = TestFile->Tell();
				const int64 ExpectedEOFPos = (sizeof(int32) * 4);		
				CHECK(ActualEOFPos == ExpectedEOFPos);

				const int64 ActualFileSize = TestFile->Size();
				CHECK(ActualFileSize == ExpectedEOFPos);

			}

			// Truncate the file at our test pos
			{
				INFO("File truncation request failed");
				CHECK(TestFile->Truncate(ExpectedTruncatePos));
			}
			
			// Validate that the size is reported correctly
			{
				INFO("File was not the expected size after truncation");

				const int64 ActualFileSize = TestFile->Size();
				CHECK(ActualFileSize == ExpectedTruncatePos);
			}

			// Validate that we can't read past the truncation point
			{
				int32 Dummy = 0;
				INFO("File read seek outside the truncated range");
				CHECK_FALSE((TestFile->Seek(TestReadPos) && TestFile->Read((uint8*)&Dummy, sizeof(Dummy))));
			}
		}
		else
		{
			FAIL_CHECK();
		}
	}
}


TEST_CASE_METHOD(FCoreTestFixture, "Core::Misc::File::File Append", "[Core][Misc][Smoke]")
{
	const FString TempFilename = FPaths::CreateTempFilename(*FPaths::EngineIntermediateDir());
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	ON_SCOPE_EXIT
	{
		// Delete temp file
		PlatformFile.DeleteFile(*TempFilename);
	};

	// Scratch data for testing
	uint8 One = 1;
	TArray<uint8> TestData;
	
	// Check a new file can be created
	{
		INFO("File creation")

		// Check a new file can be created
		if (TUniquePtr<IFileHandle> TestFile = TUniquePtr<IFileHandle>(PlatformFile.OpenWrite(*TempFilename, /*bAppend*/false, /*bAllowRead*/true)))
		{
			TestData.AddZeroed(64);

			TestFile->Write(TestData.GetData(), TestData.Num());
		}
		else
		{
			FAIL_CHECK();
		}

		// Confirm same data
		{
			INFO("Confirm same data")

			TArray<uint8> ReadData;

			CHECK(FFileHelper::LoadFileToArray(ReadData, *TempFilename));
			CHECK(ReadData == TestData);
		}
	}

	{
		INFO("File append")

		// Using append flag should open the file, and writing data immediately should append to the end.
		// We should also be capable of seeking writing.
		if (TUniquePtr<IFileHandle> TestFile = TUniquePtr<IFileHandle>(PlatformFile.OpenWrite(*TempFilename, /*bAppend*/true, /*bAllowRead*/true)))
		{
			// Validate the file actually opened in append mode correctly
			{
				INFO("File did not seek to the end when opening");

				const int64 ActualEOFPos = TestFile->Tell();
				const int64 ExpectedEOFPos = TestFile->Size();
	
				CHECK(ActualEOFPos == ExpectedEOFPos);
			}

			TestData.Add(One);
			TestData[10] = One;

			TestFile->Write(&One, 1);
			TestFile->Seek(10);
			TestFile->Write(&One, 1);
		}
		else
		{
			FAIL_CHECK();
		}

		// Confirm same data
		{
			INFO("Confirm same data")

			TArray<uint8> ReadData;

			CHECK(FFileHelper::LoadFileToArray(ReadData, *TempFilename));
			CHECK(ReadData==TestData);
		}
	}

	// No append should clobber existing file
	{
		INFO("File clobber")

		if (TUniquePtr<IFileHandle> TestFile = TUniquePtr<IFileHandle>(PlatformFile.OpenWrite(*TempFilename, /*bAppend*/false, /*bAllowRead*/true)))
		{
			TestData.Reset();
			TestData.Add(One);

			TestFile->Write(&One, 1);
		}
		else
		{
			FAIL_CHECK();
		}

		// Confirm same data
		{
			INFO("Confirm Same Data")

			TArray<uint8> ReadData;

			CHECK(FFileHelper::LoadFileToArray(ReadData, *TempFilename));
			CHECK(ReadData == TestData);
		}
	}
}

TEST_CASE_METHOD(FCoreTestFixture, "Core::Misc::File::Shrink Buffers", "[Core][Misc][Smoke]")
{
	const FString TempFilename = FPaths::CreateTempFilename(*FPaths::EngineIntermediateDir());
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	ON_SCOPE_EXIT
	{
		// Delete temp file
		PlatformFile.DeleteFile(*TempFilename);
	};

	// Scratch data for testing
	uint8 One = 1;
	TArray<uint8> TestData;

	// Check a new file can be created
	{
		INFO("Check a new file can be created");

		if (TUniquePtr<IFileHandle> TestFile = TUniquePtr<IFileHandle>(PlatformFile.OpenWrite(*TempFilename, /*bAppend*/false, /*bAllowRead*/true)))
		{
			for (uint8 i = 0; i < 64; ++i)
			{
				TestData.Add(i);
			}

			TestFile->Write(TestData.GetData(), TestData.Num());
		}
		else
		{
			FAIL_CHECK();
		}

		// Confirm same data
		{
			INFO("Confirm same data");
			TArray<uint8> ReadData;
			CHECK(FFileHelper::LoadFileToArray(ReadData, *TempFilename));
			CHECK(ReadData == TestData);
		}
	}

	// Using ShrinkBuffers should not disrupt our read position in the file
	{
		INFO("Using ShrinkBuffers should not disrupt our read position in the file");

		if (TUniquePtr<IFileHandle> TestFile = TUniquePtr<IFileHandle>(PlatformFile.OpenRead(*TempFilename, /*bAllowWrite*/false)))
		{
			// Validate the file actually opened and is of the right size

			INFO("Validate the file actually opened and is of the right size");
			CHECK(static_cast<decltype(TestFile->Size())>(TestData.Num()));

			const int32 FirstHalfSize = TestData.Num() / 2;
			const int32 SecondHalfSize = TestData.Num() - FirstHalfSize;

			TArray<uint8> FirstHalfReadData;
			FirstHalfReadData.AddUninitialized(FirstHalfSize);
			CHECK(TestFile->Read(FirstHalfReadData.GetData(), FirstHalfReadData.Num()));

			for (int32 i = 0; i < FirstHalfSize; ++i)
			{
				CHECK(FirstHalfReadData[i]==TestData[i]);
			}

			TestFile->ShrinkBuffers();

			TArray<uint8> SecondHalfReadData;
			SecondHalfReadData.AddUninitialized(SecondHalfSize);
			CHECK(TestFile->Read(SecondHalfReadData.GetData(), SecondHalfReadData.Num()));

			for (int32 i = 0; i < SecondHalfSize; ++i)
			{
				CHECK(SecondHalfReadData[i]==TestData[FirstHalfSize + i]);
			}
		}
		else
		{
			FAIL_CHECK();
		}
	}
}
