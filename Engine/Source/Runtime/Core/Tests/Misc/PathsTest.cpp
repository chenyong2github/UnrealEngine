// Copyright Epic Games, Inc. All Rights Reserved.

#include "PathTests.h"
#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Containers/UnrealString.h"
#include "Misc/Paths.h"
#include "TestHarness.h"

const FStringView PathTest::BaseDir = TEXTVIEW("/root");

const PathTest::FTestPair PathTest::ExpectedRelativeToAbsolutePaths[10] =
{
	{ TEXTVIEW(""),					TEXTVIEW("/root/") },
	{ TEXTVIEW("dir"),				TEXTVIEW("/root/dir") },
	{ TEXTVIEW("/groot"),			TEXTVIEW("/groot") },
	{ TEXTVIEW("/groot/"),			TEXTVIEW("/groot/") },
	{ TEXTVIEW("/r/dir"),			TEXTVIEW("/r/dir") },
	{ TEXTVIEW("/r/dir"),			TEXTVIEW("/r/dir") },
	{ TEXTVIEW("C:\\"),				TEXTVIEW("C:/") },
	{ TEXTVIEW("C:\\A\\B"),			TEXTVIEW("C:/A/B") },
	{ TEXTVIEW("a/b/../c"),			TEXTVIEW("/root/a/c") },
	{ TEXTVIEW("/a/b/../c"),		TEXTVIEW("/a/c") },
};

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Misc::FPaths::Smoke Test", "[Core][Misc][Smoke]")
{
	TestCollapseRelativeDirectories<FPaths, FString>(*this);

	// Extension texts
	{
		auto RunGetExtensionTest = [this](const TCHAR* InPath, const TCHAR* InExpectedExt)
		{
			// Run test
			const FString Ext = FPaths::GetExtension(FString(InPath));
			TEST_TRUE(FString::Printf(TEXT("Path '%s' failed to get the extension (got '%s', expected '%s')."), InPath, *Ext, InExpectedExt), Ext == InExpectedExt);
		};

		RunGetExtensionTest(TEXT("file"),									TEXT(""));
		RunGetExtensionTest(TEXT("file.txt"),								TEXT("txt"));
		RunGetExtensionTest(TEXT("file.tar.gz"),							TEXT("gz"));
		RunGetExtensionTest(TEXT("C:/Folder/file"),							TEXT(""));
		RunGetExtensionTest(TEXT("C:/Folder/file.txt"),						TEXT("txt"));
		RunGetExtensionTest(TEXT("C:/Folder/file.tar.gz"),					TEXT("gz"));
		RunGetExtensionTest(TEXT("C:/Folder/First.Last/file"),				TEXT(""));
		RunGetExtensionTest(TEXT("C:/Folder/First.Last/file.txt"),			TEXT("txt"));
		RunGetExtensionTest(TEXT("C:/Folder/First.Last/file.tar.gz"),		TEXT("gz"));

		auto RunSetExtensionTest = [this](const TCHAR* InPath, const TCHAR* InNewExt, const FString& InExpectedPath)
		{
			// Run test
			const FString NewPath = FPaths::SetExtension(FString(InPath), FString(InNewExt));	
			TEST_TRUE(FString::Printf(TEXT("Path '%s' failed to set the extension (got '%s', expected '%s')."), InPath, *NewPath, *InExpectedPath), NewPath == InExpectedPath);
		};

		RunSetExtensionTest(TEXT("file"),									TEXT("log"),	TEXT("file.log"));
		RunSetExtensionTest(TEXT("file.txt"),								TEXT("log"),	TEXT("file.log"));
		RunSetExtensionTest(TEXT("file.tar.gz"),							TEXT("gz2"),	TEXT("file.tar.gz2"));
		RunSetExtensionTest(TEXT("C:/Folder/file"),							TEXT("log"),	TEXT("C:/Folder/file.log"));
		RunSetExtensionTest(TEXT("C:/Folder/file.txt"),						TEXT("log"),	TEXT("C:/Folder/file.log"));
		RunSetExtensionTest(TEXT("C:/Folder/file.tar.gz"),					TEXT("gz2"),	TEXT("C:/Folder/file.tar.gz2"));
		RunSetExtensionTest(TEXT("C:/Folder/First.Last/file"),				TEXT("log"),	TEXT("C:/Folder/First.Last/file.log"));
		RunSetExtensionTest(TEXT("C:/Folder/First.Last/file.txt"),			TEXT("log"),	TEXT("C:/Folder/First.Last/file.log"));
		RunSetExtensionTest(TEXT("C:/Folder/First.Last/file.tar.gz"),		TEXT("gz2"),	TEXT("C:/Folder/First.Last/file.tar.gz2"));

		auto RunChangeExtensionTest = [this](const TCHAR* InPath, const TCHAR* InNewExt, const FString& InExpectedPath)
		{
			// Run test
			const FString NewPath = FPaths::ChangeExtension(FString(InPath), FString(InNewExt));
			TEST_TRUE(FString::Printf(TEXT("Path '%s' failed to change the extension (got '%s', expected '%s')."), InPath, *NewPath, *InExpectedPath), NewPath == InExpectedPath);

		};

		RunChangeExtensionTest(TEXT("file"),								TEXT("log"),	TEXT("file"));
		RunChangeExtensionTest(TEXT("file.txt"),							TEXT("log"),	TEXT("file.log"));
		RunChangeExtensionTest(TEXT("file.tar.gz"),							TEXT("gz2"),	TEXT("file.tar.gz2"));
		RunChangeExtensionTest(TEXT("C:/Folder/file"),						TEXT("log"),	TEXT("C:/Folder/file"));
		RunChangeExtensionTest(TEXT("C:/Folder/file.txt"),					TEXT("log"),	TEXT("C:/Folder/file.log"));
		RunChangeExtensionTest(TEXT("C:/Folder/file.tar.gz"),				TEXT("gz2"),	TEXT("C:/Folder/file.tar.gz2"));
		RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file"),			TEXT("log"),	TEXT("C:/Folder/First.Last/file"));
		RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file.txt"),		TEXT("log"),	TEXT("C:/Folder/First.Last/file.log"));
		RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file.tar.gz"),	TEXT("gz2"),	TEXT("C:/Folder/First.Last/file.tar.gz2"));
	}

	// IsUnderDirectory
	{
		auto RunIsUnderDirectoryTest = [this](const TCHAR* InPath1, const TCHAR* InPath2, bool ExpectedResult)
		{
			// Run test
			bool Result = FPaths::IsUnderDirectory(FString(InPath1), FString(InPath2));
			TEST_TRUE(FString::Printf(TEXT("FPaths::IsUnderDirectory('%s', '%s') != %s."), InPath1, InPath2, ExpectedResult ? TEXT("true") : TEXT("false")), Result == ExpectedResult);
		};

		RunIsUnderDirectoryTest(TEXT("C:/Folder"),			TEXT("C:/FolderN"), false);
		RunIsUnderDirectoryTest(TEXT("C:/Folder1"),			TEXT("C:/Folder2"), false);
		RunIsUnderDirectoryTest(TEXT("C:/Folder"),			TEXT("C:/Folder/SubDir"), false);

		RunIsUnderDirectoryTest(TEXT("C:/Folder"),			TEXT("C:/Folder"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/File"),		TEXT("C:/Folder"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/File"),		TEXT("C:/Folder/"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/"),			TEXT("C:/Folder"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/"),			TEXT("C:/Folder/"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/Subdir/"),	TEXT("C:/Folder"), true);
		RunIsUnderDirectoryTest(TEXT("C:/Folder/Subdir/"),	TEXT("C:/Folder/"), true);
	}

	TestRemoveDuplicateSlashes<FPaths, FString>(*this);

	// ConvertRelativePathToFull
	{
		using namespace PathTest;

		for (FTestPair Pair : ExpectedRelativeToAbsolutePaths)
		{
			FString Actual = FPaths::ConvertRelativePathToFull(FString(BaseDir), FString(Pair.Input));
			TEST_EQUAL(TEXT("ConvertRelativePathToFull"), FStringView(Actual), Pair.Expected);
		}
	}
}

