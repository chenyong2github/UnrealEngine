// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#include "AsyncIODelete.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAsyncIODeleteTest, "System.Core.Misc.AsyncIODelete", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);
bool FAsyncIODeleteTest::RunTest(const FString& Parameters)
{
	FString TestRoot = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("AsyncIODelete"), TEXT(""));
	IFileManager& FileManager = IFileManager::Get();
	const bool bApplyToTreeTrue = true;
	ON_SCOPE_EXIT
	{
		const bool bRequireExists = false;
		FileManager.DeleteDirectory(*TestRoot, bRequireExists, bApplyToTreeTrue);
	};

	FString TempRoot = FPaths::Combine(TestRoot, TEXT("TempRoot"));
	FString TempRoot2 = FPaths::Combine(TestRoot, TEXT("TempRoot2"));
	FString TempRoot3 = FPaths::Combine(TestRoot, TEXT("TempRoot3"));
	FString TempRoot4 = FPaths::Combine(TestRoot, TEXT("TempRoot4"));
	FString TestFile1 = FPaths::Combine(TestRoot, TEXT("TestFile1"));
	FString TestDir1 = FPaths::Combine(TestRoot, TEXT("TestDir1"));
	int32 NumFiles = 0;
	int32 NumDirs = 0;
	const TCHAR* TestText = TEXT("Test");
	auto CreateTestPathsToDelete = [=, &TestFile1, &TestDir1, &FileManager]()
	{
		FFileHelper::SaveStringToFile(TestText, *TestFile1);
		FileManager.MakeDirectory(*TestDir1, bApplyToTreeTrue);
	};
	IPlatformFile::FDirectoryVisitorFunc CountFilesAndDirs = [&NumFiles, &NumDirs](const TCHAR* VisitFilename, bool VisitIsDir)
	{
		NumFiles += VisitIsDir ? 1 : 0;
		NumDirs += VisitIsDir ? 0 : 1;
		return true;
	};
	auto TestTempRootCountsEqual = [&](const FString& RootDir, int32 ExpectedFileCount, int32 ExpectedDirCount, const TCHAR* TestDescription)
	{
		NumFiles = 0;
		NumDirs = 0;
		FileManager.IterateDirectory(*RootDir, CountFilesAndDirs);
		TestTrue(TestDescription, NumFiles == ExpectedFileCount && NumDirs == ExpectedDirCount);
	};
	auto TestRequestedPathsDeleted = [&](const TCHAR* TestDescription)
	{
		TestTrue(TestDescription, !FileManager.FileExists(*TestFile1) && !FileManager.DirectoryExists(*TestDir1));
	};

	FileManager.MakeDirectory(*TestRoot);

	const bool bVerbose = true;
	auto StartSection = [bVerbose](const TCHAR* Section)
	{
		if (bVerbose)
		{
			UE_LOG(LogCore, Display, TEXT("%s"), Section);
		}
	};
	const float MaxWaitTime = 5.0f;
	auto WaitForAllTasksAndVerify = [this, MaxWaitTime](FAsyncIODelete& InAsyncIODelete)
	{
		bool WaitResult = InAsyncIODelete.WaitForAllTasks(MaxWaitTime);
		TestTrue(TEXT("WaitForAllTasks timed out"), WaitResult);
	};
	{
		FAsyncIODelete AsyncIODelete(TempRoot);

		StartSection(TEXT("Waiting for tasks to complete when none have been launched should succeed"));
		WaitForAllTasksAndVerify(AsyncIODelete);

		StartSection(TEXT("Moving file and directory from the source location should be finished by the time DeleteFile/DeleteDirectory returns"));
		CreateTestPathsToDelete();
		AsyncIODelete.DeleteFile(TestFile1);
		AsyncIODelete.DeleteDirectory(TestDir1);
		TestRequestedPathsDeleted(TEXT("AsyncIODelete::Delete should have moved the deleted paths before returning."));

		StartSection(TEXT("Deleting the temporary files/directories should be finished before WaitForAllTasks returns"));
		WaitForAllTasksAndVerify(AsyncIODelete);
		TestTempRootCountsEqual(TempRoot, 0, 0, TEXT("AsyncIODelete should have deleted the moved paths before WaitForAllTasks returned."));

		StartSection(TEXT("Two FAsyncIODelete constructed at once should be legal, as long as they have different TempRoots"));
		FAsyncIODelete AsyncIODelete2(TempRoot2);

		StartSection(TEXT("Use the pause feature to verify that the paths are indeed moved into the TempRoot"));
		AsyncIODelete2.SetDeletesPaused(true);
		CreateTestPathsToDelete();
		AsyncIODelete2.DeleteFile(TestFile1);
		AsyncIODelete2.DeleteDirectory(TestDir1);
		TestRequestedPathsDeleted(TEXT("AsyncIODelete::Delete should have moved the deleted paths before returning even when paused."));
		WaitForAllTasksAndVerify(AsyncIODelete2);
		TestTempRootCountsEqual(TempRoot2, 1, 1, TEXT("AsyncIODelete should not have deleted the moved paths because it is paused."));
		AsyncIODelete2.SetDeletesPaused(false);
		WaitForAllTasksAndVerify(AsyncIODelete2);
		TestTempRootCountsEqual(TempRoot2, 0, 0, TEXT("AsyncIODelete should have deleted the moved paths after unpausing."));

		StartSection(TEXT("Verify Teardown() deletes the TempRoot and Setup() creates it"));
		AsyncIODelete2.Teardown();
		TestTrue(TEXT("AsyncIODelete::Teardown should have deleted its TempRoot."), !FileManager.DirectoryExists(*TempRoot2));
		AsyncIODelete2.Setup();
		TestTrue(TEXT("AsyncIODelete::Setup should have created its TempRoot."), FileManager.DirectoryExists(*TempRoot2));

		StartSection(TEXT("Manual setup works as long as you call SetTempRoot before Setup"));
		FAsyncIODelete AsyncIODelete3;
		AsyncIODelete3.SetTempRoot(TempRoot3);
		AsyncIODelete3.Setup();
		TestTrue(TEXT("Setup should have created the TempRoot."), FileManager.DirectoryExists(*TempRoot3));

		StartSection(TEXT("Check that even after Setup, waiting for tasks to complete when none have been launched should succeed"));
		WaitForAllTasksAndVerify(AsyncIODelete);

		StartSection(TEXT("Changing TempRoot and then deleting a file works"));
		CreateTestPathsToDelete();
		AsyncIODelete3.DeleteFile(TestFile1);
		AsyncIODelete3.DeleteDirectory(TestDir1);
		AsyncIODelete3.SetTempRoot(TempRoot4);
		TestTrue(TEXT("SetTempRoot should have deleted the old TempRoot."), !FileManager.DirectoryExists(*TempRoot3));
		CreateTestPathsToDelete();
		AsyncIODelete3.DeleteFile(TestFile1);
		AsyncIODelete3.DeleteDirectory(TestDir1);
		TestRequestedPathsDeleted(TEXT("AsyncIODelete::Delete should have worked after changing the TempRoot."));
		TestTrue(TEXT("Delete should have created the new TempRoot after SetTempRoot."), FileManager.DirectoryExists(*TempRoot4));
		WaitForAllTasksAndVerify(AsyncIODelete3);
		TestTempRootCountsEqual(TempRoot4, 0, 0, TEXT("AsyncIODelete::Delete should have created the tasks to delete moved files after changing the TempRoot."));

		StartSection(TEXT("Attempting to delete a parent directory of the temproot, the temproot itself, or a child inside of it fails"));
		FString SubDirInTempRoot4 = FPaths::Combine(TempRoot4, TEXT("SubDir"));
		FileManager.MakeDirectory(*SubDirInTempRoot4, bApplyToTreeTrue); // Note it's illegal to add files into TempRoot, but we're not currently checking for it and we're not colliding with the DeleteN paths AsyncIODelete uses, so this breaking of the rule will not cause problems
		TestFalse(TEXT("AsyncIODelete should refuse to delete a parent of its TempRoot."), AsyncIODelete3.DeleteDirectory(TestRoot));
		TestFalse(TEXT("AsyncIODelete should refuse to delete its TempRoot."), AsyncIODelete3.DeleteDirectory(TempRoot4));
		TestFalse(TEXT("AsyncIODelete should refuse to delete a child of its TempRoot."), AsyncIODelete3.DeleteDirectory(SubDirInTempRoot4));
	}

	TestTrue(TEXT("AsyncIODelete destructor should have deleted its TempRoot."),
		!FileManager.DirectoryExists(*TempRoot) && !FileManager.DirectoryExists(*TempRoot2) && !FileManager.DirectoryExists(*TempRoot3) && !FileManager.DirectoryExists(*TempRoot4));

	return true;
}