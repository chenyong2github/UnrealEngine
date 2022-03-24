// Copyright Epic Games, Inc. All Rights Reserved.

#include "PathTests.h"
#include "Containers/StringView.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"
#include "TestHarness.h"


void TestViewTransform(FStringView(*InFunction)(const FStringView& InPath), const FStringView& InPath, const TCHAR* InExpected)
{
	const FStringView Actual = InFunction(InPath);
	TestEqual(FString::Printf(TEXT("Failed on path '%.*s' (got '%.*s', expected '%s')."),
		InPath.Len(), InPath.GetData(), Actual.Len(), Actual.GetData(), InExpected), Actual,InExpected);
}

static const TCHAR* BoolToString(bool bValue)
{
	return bValue ? TEXT("true") : TEXT("false");
}


TEST_CASE("Core::Misc::FPathViews::Collapse Directories", "[Core][Misc][Smoke]")
{
	TestCollapseRelativeDirectories<FPathViews, TStringBuilder<64>>();
}

TEST_CASE("Core::Misc::FPathViews::Remove Duplicate Slashes", "[Core][Misc][Smoke]")
{
	TestRemoveDuplicateSlashes<FPathViews, TStringBuilder<64>>();
}

TEST_CASE("Core::Misc::FPathViews::Get Clean Filename", "[Core][Misc][Smoke]")
{
	auto RunGetCleanFilenameTest = [](const TCHAR* InPath, const TCHAR* InExpected)
	{
		TestViewTransform(FPathViews::GetCleanFilename, InPath, InExpected);
	};

	RunGetCleanFilenameTest(TEXT(""), TEXT(""));
	RunGetCleanFilenameTest(TEXT(".txt"), TEXT(".txt"));
	RunGetCleanFilenameTest(TEXT(".tar.gz"), TEXT(".tar.gz"));
	RunGetCleanFilenameTest(TEXT(".tar.gz/"), TEXT(""));
	RunGetCleanFilenameTest(TEXT(".tar.gz\\"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("File.tar.gz"), TEXT("File.tar.gz"));
	RunGetCleanFilenameTest(TEXT("File.tar.gz/"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("File.tar.gz\\"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("C:/Folder/"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("C:/Folder/File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("C:/Folder/File.tar.gz"), TEXT("File.tar.gz"));
	RunGetCleanFilenameTest(TEXT("C:/Folder/First.Last/File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("File.tar.gz"));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\First.Last\\"), TEXT(""));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("File"));
	RunGetCleanFilenameTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("File.tar.gz"));
}

TEST_CASE("Core::Misc::FPathViews::Get Base Filename", "[Core][Misc][Smoke]")
{
	auto RunGetBaseFilenameTest = [](const TCHAR* InPath, const TCHAR* InExpected, const TCHAR* InExpectedWithPath)
	{
		const FStringView Path = InPath;
		TestViewTransform(FPathViews::GetBaseFilename, Path, InExpected);
		TestViewTransform(FPathViews::GetBaseFilenameWithPath, Path, InExpectedWithPath);
	};

	RunGetBaseFilenameTest(TEXT(""), TEXT(""), TEXT(""));
	RunGetBaseFilenameTest(TEXT(".txt"), TEXT(""), TEXT(""));
	RunGetBaseFilenameTest(TEXT(".tar.gz"), TEXT(".tar"), TEXT(".tar"));
	RunGetBaseFilenameTest(TEXT(".tar.gz/"), TEXT(""), TEXT(".tar.gz/"));
	RunGetBaseFilenameTest(TEXT(".tar.gz\\"), TEXT(""), TEXT(".tar.gz\\"));
	RunGetBaseFilenameTest(TEXT("File"), TEXT("File"), TEXT("File"));
	RunGetBaseFilenameTest(TEXT("File.txt"), TEXT("File"), TEXT("File"));
	RunGetBaseFilenameTest(TEXT("File.tar.gz"), TEXT("File.tar"), TEXT("File.tar"));
	RunGetBaseFilenameTest(TEXT("File.tar.gz/"), TEXT(""), TEXT("File.tar.gz/"));
	RunGetBaseFilenameTest(TEXT("File.tar.gz\\"), TEXT(""), TEXT("File.tar.gz\\"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/"), TEXT(""), TEXT("C:/Folder/"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/File"), TEXT("File"), TEXT("C:/Folder/File"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/File.tar.gz"), TEXT("File.tar"), TEXT("C:/Folder/File.tar"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/First.Last/File"), TEXT("File"), TEXT("C:/Folder/First.Last/File"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/First.Last/File.txt"), TEXT("File"), TEXT("C:/Folder/First.Last/File"));
	RunGetBaseFilenameTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("File.tar"), TEXT("C:/Folder/First.Last/File.tar"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\"), TEXT(""), TEXT("C:\\Folder\\"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\File"), TEXT("File"), TEXT("C:\\Folder\\File"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\First.Last\\"), TEXT(""), TEXT("C:\\Folder\\First.Last\\"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("File"), TEXT("C:\\Folder\\First.Last\\File"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\First.Last\\File.txt"), TEXT("File"), TEXT("C:\\Folder\\First.Last\\File"));
	RunGetBaseFilenameTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("File.tar"), TEXT("C:\\Folder\\First.Last\\File.tar"));
}

TEST_CASE("Core::Misc::FPathViews::Get Path", "[Core][Misc][Smoke]")
{
	auto RunGetPathTest = [](const TCHAR* InPath, const TCHAR* InExpected)
	{
		TestViewTransform(FPathViews::GetPath, InPath, InExpected);
	};

	RunGetPathTest(TEXT(""), TEXT(""));
	RunGetPathTest(TEXT(".txt"), TEXT(""));
	RunGetPathTest(TEXT(".tar.gz"), TEXT(""));
	RunGetPathTest(TEXT(".tar.gz/"), TEXT(".tar.gz"));
	RunGetPathTest(TEXT(".tar.gz\\"), TEXT(".tar.gz"));
	RunGetPathTest(TEXT("File"), TEXT(""));
	RunGetPathTest(TEXT("File.txt"), TEXT(""));
	RunGetPathTest(TEXT("File.tar.gz"), TEXT(""));
	RunGetPathTest(TEXT("File.tar.gz/"), TEXT("File.tar.gz"));
	RunGetPathTest(TEXT("File.tar.gz\\"), TEXT("File.tar.gz"));
	RunGetPathTest(TEXT("C:/Folder/"), TEXT("C:/Folder"));
	RunGetPathTest(TEXT("C:/Folder/File"), TEXT("C:/Folder"));
	RunGetPathTest(TEXT("C:/Folder/File.tar.gz"), TEXT("C:/Folder"));
	RunGetPathTest(TEXT("C:/Folder/First.Last/File"), TEXT("C:/Folder/First.Last"));
	RunGetPathTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("C:/Folder/First.Last"));
	RunGetPathTest(TEXT("C:\\Folder\\"), TEXT("C:\\Folder"));
	RunGetPathTest(TEXT("C:\\Folder\\File"), TEXT("C:\\Folder"));
	RunGetPathTest(TEXT("C:\\Folder\\First.Last\\"), TEXT("C:\\Folder\\First.Last"));
	RunGetPathTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("C:\\Folder\\First.Last"));
	RunGetPathTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("C:\\Folder\\First.Last"));
}


TEST_CASE("Core::Misc::FPathViews::Get Extension", "[Core][Misc][Smoke]")
{
	auto RunGetExtensionTest = [](const TCHAR* InPath, const TCHAR* InExpectedExt, const TCHAR* InExpectedExtDot)
	{
		const FStringView Path = InPath;
		TestViewTransform([](const FStringView& InPath2) { return FPathViews::GetExtension(InPath2, /*bIncludeDot*/ false); }, Path, InExpectedExt);
		TestViewTransform([](const FStringView& InPath2) { return FPathViews::GetExtension(InPath2, /*bIncludeDot*/ true); }, Path, InExpectedExtDot);
	};

	RunGetExtensionTest(TEXT(""), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT(".txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT(".tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT(".tar.gz/"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT(".tar.gz\\"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("File.tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT("File.tar.gz/"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("File.tar.gz\\"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:/Folder/File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:\\Folder\\File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:/Folder/File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("C:\\Folder\\File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("C:/Folder/File.tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT("C:\\Folder\\File.tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT("C:/Folder/First.Last/File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT(""), TEXT(""));
	RunGetExtensionTest(TEXT("C:/Folder/First.Last/File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("C:\\Folder\\First.Last\\File.txt"), TEXT("txt"), TEXT(".txt"));
	RunGetExtensionTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("gz"), TEXT(".gz"));
	RunGetExtensionTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("gz"), TEXT(".gz"));
}

TEST_CASE("Core::Misc::FPathViews::Get Path Leaf", "[Core][Misc][Smoke]")
{
	auto RunGetPathLeafTest = [](const TCHAR* InPath, const TCHAR* InExpected)
	{
		TestViewTransform(FPathViews::GetPathLeaf, InPath, InExpected);
	};

	RunGetPathLeafTest(TEXT(""), TEXT(""));
	RunGetPathLeafTest(TEXT(".txt"), TEXT(".txt"));
	RunGetPathLeafTest(TEXT(".tar.gz"), TEXT(".tar.gz"));
	RunGetPathLeafTest(TEXT(".tar.gz/"), TEXT(".tar.gz"));
	RunGetPathLeafTest(TEXT(".tar.gz\\"), TEXT(".tar.gz"));
	RunGetPathLeafTest(TEXT("File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("File.txt"), TEXT("File.txt"));
	RunGetPathLeafTest(TEXT("File.tar.gz"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("File.tar.gz/"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("File.tar.gz\\"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("C:/Folder/"), TEXT("Folder"));
	RunGetPathLeafTest(TEXT("C:/Folder/File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("C:/Folder/File.tar.gz"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("C:/Folder/First.Last/File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("File.tar.gz"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\"), TEXT("Folder"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\First.Last\\"), TEXT("First.Last"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("File"));
	RunGetPathLeafTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("File.tar.gz"));
}

TEST_CASE("Core::Misc::FPathViews::Is Path Leaf", "[Core][Misc][Smoke]")
{
	auto RunIsPathLeafTest = [](const TCHAR* InPath, bool bExpected)
	{
		bool bResult = FPathViews::IsPathLeaf(InPath);
		TestFalse(FString::Printf(TEXT("GetPathLeaf(\"%s\") == %s, expected %s."), InPath, BoolToString(bResult), BoolToString(bExpected)), (bResult != bExpected));
	};

	RunIsPathLeafTest(TEXT(""), true);
	RunIsPathLeafTest(TEXT(".txt"), true);
	RunIsPathLeafTest(TEXT(".tar.gz"), true);
	RunIsPathLeafTest(TEXT(".tar.gz/"), true);
	RunIsPathLeafTest(TEXT(".tar.gz\\"), true);
	RunIsPathLeafTest(TEXT("File"), true);
	RunIsPathLeafTest(TEXT("File.txt"), true);
	RunIsPathLeafTest(TEXT("File.tar.gz"), true);
	RunIsPathLeafTest(TEXT("File.tar.gz/"), true);
	RunIsPathLeafTest(TEXT("File.tar.gz\\"), true);
	RunIsPathLeafTest(TEXT("//"), true);
	RunIsPathLeafTest(TEXT("\\\\"), true);
	RunIsPathLeafTest(TEXT("/"), true);
	RunIsPathLeafTest(TEXT("\\"), true);
	RunIsPathLeafTest(TEXT("C:/"), true);
	RunIsPathLeafTest(TEXT("C:\\"), true);
	RunIsPathLeafTest(TEXT("//Folder"), false);
	RunIsPathLeafTest(TEXT("\\\\Folder"), false);
	RunIsPathLeafTest(TEXT("/Folder"), false);
	RunIsPathLeafTest(TEXT("\\Folder"), false);
	RunIsPathLeafTest(TEXT("C:/Folder"), false);
	RunIsPathLeafTest(TEXT("C:\\Folder"), false);
	RunIsPathLeafTest(TEXT("C:/Folder"), false);
	RunIsPathLeafTest(TEXT("C:\\Folder"), false);
}


TEST_CASE("Core::Misc::FPathViews::Split", "[Core][Misc][Smoke]")
{
	auto RunSplitTest = [](const TCHAR* InPath, const TCHAR* InExpectedPath, const TCHAR* InExpectedName, const TCHAR* InExpectedExt)
	{
		FStringView SplitPath, SplitName, SplitExt;
		FPathViews::Split(InPath, SplitPath, SplitName, SplitExt);

		TestFalse(FString::Printf(TEXT("Failed to split path '%s' (got ('%.*s', '%.*s', '%.*s'), expected ('%s', '%s', '%s'))."), InPath,
			SplitPath.Len(), SplitPath.GetData(), SplitName.Len(), SplitName.GetData(), SplitExt.Len(), SplitExt.GetData(),
			InExpectedPath, InExpectedName, InExpectedExt),
			(SplitPath != InExpectedPath || SplitName != InExpectedName || SplitExt != InExpectedExt) );
	};

	RunSplitTest(TEXT(""), TEXT(""), TEXT(""), TEXT(""));
	RunSplitTest(TEXT(".txt"), TEXT(""), TEXT(""), TEXT("txt"));
	RunSplitTest(TEXT(".tar.gz"), TEXT(""), TEXT(".tar"), TEXT("gz"));
	RunSplitTest(TEXT(".tar.gz/"), TEXT(".tar.gz"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT(".tar.gz\\"), TEXT(".tar.gz"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("File"), TEXT(""), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("File.txt"), TEXT(""), TEXT("File"), TEXT("txt"));
	RunSplitTest(TEXT("File.tar.gz"), TEXT(""), TEXT("File.tar"), TEXT("gz"));
	RunSplitTest(TEXT("File.tar.gz/"), TEXT("File.tar.gz"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("File.tar.gz\\"), TEXT("File.tar.gz"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("C:/Folder/"), TEXT("C:/Folder"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("C:/Folder/File"), TEXT("C:/Folder"), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("C:/Folder/File.txt"), TEXT("C:/Folder"), TEXT("File"), TEXT("txt"));
	RunSplitTest(TEXT("C:/Folder/File.tar.gz"), TEXT("C:/Folder"), TEXT("File.tar"), TEXT("gz"));
	RunSplitTest(TEXT("C:/Folder/First.Last/File"), TEXT("C:/Folder/First.Last"), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("C:/Folder/First.Last/File.txt"), TEXT("C:/Folder/First.Last"), TEXT("File"), TEXT("txt"));
	RunSplitTest(TEXT("C:/Folder/First.Last/File.tar.gz"), TEXT("C:/Folder/First.Last"), TEXT("File.tar"), TEXT("gz"));
	RunSplitTest(TEXT("C:\\Folder\\"), TEXT("C:\\Folder"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("C:\\Folder\\File"), TEXT("C:\\Folder"), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("C:\\Folder\\First.Last\\"), TEXT("C:\\Folder\\First.Last"), TEXT(""), TEXT(""));
	RunSplitTest(TEXT("C:\\Folder\\First.Last\\File"), TEXT("C:\\Folder\\First.Last"), TEXT("File"), TEXT(""));
	RunSplitTest(TEXT("C:\\Folder\\First.Last\\File.txt"), TEXT("C:\\Folder\\First.Last"), TEXT("File"), TEXT("txt"));
	RunSplitTest(TEXT("C:\\Folder\\First.Last\\File.tar.gz"), TEXT("C:\\Folder\\First.Last"), TEXT("File.tar"), TEXT("gz"));
}


TEST_CASE("Core::Misc::FPathViews::Append", "[Core][Misc][Smoke]")
{
	TStringBuilder<256> Path;

	FPathViews::Append(Path, TEXT("A"), TEXT(""));
	TestEqual(TEXT("FPathViews::Append('A', '')"), FStringView(Path), TEXTVIEW("A/"));
	Path.Reset();

	FPathViews::Append(Path, TEXT(""), TEXT("B"));
	TestEqual(TEXT("FPathViews::Append('', 'B')"), FStringView(Path), TEXTVIEW("B"));
	Path.Reset();

	FPathViews::Append(Path, TEXT("/"), TEXT("B"));
	TestEqual(TEXT("FPathViews::Append('/', 'B')"), FStringView(Path), TEXTVIEW("/B"));
	Path.Reset();

	FPathViews::Append(Path, TEXT("A"), TEXT("B"));
	TestEqual(TEXT("FPathViews::Append('A', 'B')"), FStringView(Path), TEXTVIEW("A/B"));
	Path.Reset();

	FPathViews::Append(Path, TEXT("A/"), TEXT("B"));
	TestEqual(TEXT("FPathViews::Append('A/', 'B')"), FStringView(Path), TEXTVIEW("A/B"));
	Path.Reset();

	FPathViews::Append(Path, TEXT("A\\"), TEXT("B"));
	TestEqual(TEXT("FPathViews::Append('A\\', 'B')"), FStringView(Path), TEXTVIEW("A\\B"));
	Path.Reset();

	FPathViews::Append(Path, TEXT("A/B"), TEXT("C/D"));
	TestEqual(TEXT("FPathViews::Append('A/B', 'C/D')"), FStringView(Path), TEXTVIEW("A/B/C/D"));
	Path.Reset();

	FPathViews::Append(Path, TEXT("A/"), TEXT("B"), TEXT("C/"), TEXT("D"));
	TestEqual(TEXT("FPathViews::Append('A/', 'B', 'C/', 'D')"), FStringView(Path), TEXTVIEW("A/B/C/D"));
	Path.Reset();

	FPathViews::Append(Path, TEXT("A/"), 16, TEXT("B"));
	TestEqual(TEXT("FPathViews::Append('A/', 16, 'B')"), FStringView(Path), TEXTVIEW("A/16/B"));
	Path.Reset();
}

TEST_CASE("Core::Misc::FPathViews::Change Extension", "[Core][Misc][Smoke]")
{
	auto RunChangeExtensionTest = [](const TCHAR* InPath, const TCHAR* InNewExt, const TCHAR* InExpectedPath)
	{
		// Run test
		const FString NewPath = FPathViews::ChangeExtension(InPath, InNewExt);
		TestFalse(FString::Printf(TEXT("Path '%s' failed to change the extension (got '%s', expected '%s')."), InPath, *NewPath, InExpectedPath), (NewPath != InExpectedPath));
	};

	RunChangeExtensionTest(nullptr, nullptr, TEXT(""));
	RunChangeExtensionTest(TEXT(""), TEXT(""), TEXT(""));
	RunChangeExtensionTest(TEXT(""), TEXT(".txt"), TEXT(""));
	RunChangeExtensionTest(TEXT("file"), TEXT("log"), TEXT("file"));
	RunChangeExtensionTest(TEXT("file.txt"), TEXT("log"), TEXT("file.log"));
	RunChangeExtensionTest(TEXT("file.tar.gz"), TEXT("gz2"), TEXT("file.tar.gz2"));
	RunChangeExtensionTest(TEXT("file.txt"), TEXT(""), TEXT("file"));
	RunChangeExtensionTest(TEXT("C:/Folder/file"), TEXT("log"), TEXT("C:/Folder/file"));
	RunChangeExtensionTest(TEXT("C:/Folder/file.txt"), TEXT("log"), TEXT("C:/Folder/file.log"));
	RunChangeExtensionTest(TEXT("C:/Folder/file.tar.gz"), TEXT("gz2"), TEXT("C:/Folder/file.tar.gz2"));
	RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file"), TEXT("log"), TEXT("C:/Folder/First.Last/file"));
	RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file.txt"), TEXT("log"), TEXT("C:/Folder/First.Last/file.log"));
	RunChangeExtensionTest(TEXT("C:/Folder/First.Last/file.tar.gz"), TEXT("gz2"), TEXT("C:/Folder/First.Last/file.tar.gz2"));
}

TEST_CASE("Core::Misc::FPathViews::Equals And Less", "[Core][Misc][Smoke]")
{
	auto RunEqualsLessTest = [](const TCHAR* A, const TCHAR* B, int Expected)
	{
		bool bEqual = FPathViews::Equals(A, B);
		bool bALessThanB = FPathViews::Less(A, B);
		bool bBLessThanA = FPathViews::Less(B, A);
		
		TestFalse(FString::Printf(TEXT("Equals(%s,%s) == %s, expected %s"), A, B, BoolToString(bEqual), BoolToString(Expected == 0)), (bEqual != (Expected == 0)));
		TestFalse(FString::Printf(TEXT("Less(%s,%s) == %s, expected %s"), A, B, BoolToString(bALessThanB), BoolToString(Expected < 0)), (bALessThanB != Expected < 0));
		TestFalse(FString::Printf(TEXT("Less(%s,%s) == %s, expected %s"), B, A, BoolToString(bBLessThanA), BoolToString(Expected > 0)), (bBLessThanA != Expected > 0));
	};

	RunEqualsLessTest(TEXT("A"), TEXT("B"), -1);
	RunEqualsLessTest(TEXT("A"), TEXT("b"), -1);
	RunEqualsLessTest(TEXT("a"), TEXT("B"), -1);
	RunEqualsLessTest(TEXT("A"), TEXT("A"), 0);
	RunEqualsLessTest(TEXT("A"), TEXT("a/"), 0);
	RunEqualsLessTest(TEXT("A"), TEXT("a\\"), 0);
	RunEqualsLessTest(TEXT("A"), TEXT("abby"), -1);
	RunEqualsLessTest(TEXT("a"), TEXT("Abby"), -1);
	RunEqualsLessTest(TEXT("\\A/B"), TEXT("/A\\B/"), 0);
	//RunEqualsLessTest(TEXT("../../../Engine"), TEXT("C:/Engine"), 0); // Detecting relpath == abspath is not yet implemented
	//RunEqualsLessTest(TEXT("C:/A/B"), TEXT("C:/A/../A/./B"), 0); // Collapsing .. and . is not yet implemented
	RunEqualsLessTest(TEXT("/"), TEXT("/"), 0);
	RunEqualsLessTest(TEXT("/"), TEXT("//"), -1);
	RunEqualsLessTest(TEXT("/"), TEXT("C:/"), -1); // int('/') == 47 < int('C') == 67
	RunEqualsLessTest(TEXT("/"), TEXT("A"), -1);
	RunEqualsLessTest(TEXT("//"), TEXT("//"), 0);
	RunEqualsLessTest(TEXT("//"), TEXT("C:/"), -1);
	RunEqualsLessTest(TEXT("//"), TEXT("A"), -1);
	RunEqualsLessTest(TEXT("C:/"), TEXT("C:/"), 0);
	RunEqualsLessTest(TEXT("C:/"), TEXT("C"), 1);
}


TEST_CASE("Core::Misc::FPathViews::Try Make Child Path Relative To", "[Core][Misc][Smoke]")
{
	auto RunRelChildTest = [](const TCHAR* Child, const TCHAR* Parent, bool bExpectedIsChild, const TCHAR* ExpectedRelPath)
	{
		FStringView ActualRelPath;
		bool bActualIsChild = FPathViews::TryMakeChildPathRelativeTo(Child, Parent, ActualRelPath);
		bool bActualIsParent = FPathViews::IsParentPathOf(Parent, Child);
		
		TestFalse(FString::Printf(TEXT("TryMakeChildPathRelativeTo(\"%s\", \"%s\") returned (%s, \"%.*s\"), expected (%s, \"%s\")."), Child, Parent,
			BoolToString(bActualIsChild), ActualRelPath.Len(), ActualRelPath.GetData(), BoolToString(bExpectedIsChild), ExpectedRelPath),
			(bExpectedIsChild != bActualIsChild || !ActualRelPath.Equals(ExpectedRelPath, ESearchCase::IgnoreCase)));
			
		TestFalse(FString::Printf(TEXT("IsParentPathOf(\"%s\", \"%s\") returned %s, expected %s."), Child, Parent,
			BoolToString(bActualIsParent), BoolToString(bExpectedIsChild)), (bActualIsParent != bExpectedIsChild));
	};

	RunRelChildTest(TEXT("A/B"), TEXT("A"), true, TEXT("B"));
	RunRelChildTest(TEXT("A\\B"), TEXT("A"), true, TEXT("B"));
	RunRelChildTest(TEXT("A/B"), TEXT("A/"), true, TEXT("B"));
	RunRelChildTest(TEXT("A\\B"), TEXT("A/"), true, TEXT("B"));
	RunRelChildTest(TEXT("A/B"), TEXT("A\\"), true, TEXT("B"));
	RunRelChildTest(TEXT("A\\B"), TEXT("A\\"), true, TEXT("B"));
	RunRelChildTest(TEXT("A"), TEXT("A"), true, TEXT(""));
	RunRelChildTest(TEXT("A/"), TEXT("A"), true, TEXT(""));
	RunRelChildTest(TEXT("A\\"), TEXT("A"), true, TEXT(""));
	RunRelChildTest(TEXT("A"), TEXT("A/"), true, TEXT(""));
	RunRelChildTest(TEXT("A"), TEXT("A\\"), true, TEXT(""));
	RunRelChildTest(TEXT("../A"), TEXT(".."), true, TEXT("A"));
	RunRelChildTest(TEXT("/A/B"), TEXT("/A"), true, TEXT("B"));
	RunRelChildTest(TEXT("../A/B"), TEXT("../A"), true, TEXT("B"));
	RunRelChildTest(TEXT("../"), TEXT(".."), true, TEXT(""));
	RunRelChildTest(TEXT("C:/"), TEXT("C:/"), true, TEXT(""));
	RunRelChildTest(TEXT("C:/A"), TEXT("C:/"), true, TEXT("A"));
	RunRelChildTest(TEXT("//A"), TEXT("//A"), true, TEXT(""));
	RunRelChildTest(TEXT("//A/"), TEXT("//A"), true, TEXT(""));
	RunRelChildTest(TEXT("//A"), TEXT("//A/"), true, TEXT(""));
	RunRelChildTest(TEXT("//A/"), TEXT("//A/"), true, TEXT(""));
	RunRelChildTest(TEXT("//A/B"), TEXT("//A"), true, TEXT("B"));
	RunRelChildTest(TEXT("//A/B/"), TEXT("//A"), true, TEXT("B/"));
	RunRelChildTest(TEXT("//A/B"), TEXT("//A/"), true, TEXT("B"));
	RunRelChildTest(TEXT("//A/B/"), TEXT("//A/"), true, TEXT("B/"));

	RunRelChildTest(TEXT("//A/BFoo"), TEXT("//A/B"), false, TEXT(""));
	RunRelChildTest(TEXT("//A/C"), TEXT("//A/B"), false, TEXT(""));
	RunRelChildTest(TEXT("//A/C"), TEXT("C:/A"), false, TEXT(""));
	RunRelChildTest(TEXT("//A/C"), TEXT("/A"), false, TEXT(""));
	RunRelChildTest(TEXT("C:/A/BFoo"), TEXT("C:/A/B"), false, TEXT(""));
	RunRelChildTest(TEXT("C:/A/C"), TEXT("C:/A/B"), false, TEXT(""));
	RunRelChildTest(TEXT("C:/A/C"), TEXT("//A"), false, TEXT(""));
	RunRelChildTest(TEXT("C:/A/C"), TEXT("/A"), false, TEXT(""));
	RunRelChildTest(TEXT("/A/BFoo"), TEXT("/A/B"), false, TEXT(""));
	RunRelChildTest(TEXT("/A/C"), TEXT("/A/B"), false, TEXT(""));
	RunRelChildTest(TEXT("/A/C"), TEXT("C:/A"), false, TEXT(""));
	RunRelChildTest(TEXT("/A/C"), TEXT("//A"), false, TEXT(""));

	RunRelChildTest(TEXT("/"), TEXT("/"), true, TEXT(""));
	RunRelChildTest(TEXT("/"), TEXT("//"), false, TEXT(""));
	RunRelChildTest(TEXT("/"), TEXT("C"), false, TEXT(""));
	RunRelChildTest(TEXT("/"), TEXT("C:/"), false, TEXT(""));
	RunRelChildTest(TEXT("//"), TEXT("/"), false, TEXT(""));
	RunRelChildTest(TEXT("//"), TEXT("//"), true, TEXT(""));
	RunRelChildTest(TEXT("//"), TEXT("C"), false, TEXT(""));
	RunRelChildTest(TEXT("//"), TEXT("C:/"), false, TEXT(""));
	RunRelChildTest(TEXT("C"), TEXT("/"), false, TEXT(""));
	RunRelChildTest(TEXT("C"), TEXT("//"), false, TEXT(""));
	RunRelChildTest(TEXT("C"), TEXT("C"), true, TEXT(""));
	RunRelChildTest(TEXT("C"), TEXT("C:/"), false, TEXT(""));
	RunRelChildTest(TEXT("C:/"), TEXT("/"), false, TEXT(""));
	RunRelChildTest(TEXT("C:/"), TEXT("//"), false, TEXT(""));
	RunRelChildTest(TEXT("C:/"), TEXT("C"), false, TEXT(""));
	RunRelChildTest(TEXT("C:/"), TEXT("C:/"), true, TEXT(""));

	RunRelChildTest(TEXT("C:/A/B/C"), TEXT("C:/A"), true, TEXT("B/C"));
	RunRelChildTest(TEXT("C:/A"), TEXT("C:/A/B/C"), false, TEXT(""));

	// Correctly handle paths with invalid duplicate slashes
	RunRelChildTest(TEXT("///A"), TEXT("///"), true, TEXT("A"));
	RunRelChildTest(TEXT("////////////A"), TEXT("///"), true, TEXT("A"));
	RunRelChildTest(TEXT("///A"), TEXT("//"), true, TEXT("A"));
	RunRelChildTest(TEXT("////////////A"), TEXT("/"), false, TEXT(""));
	RunRelChildTest(TEXT("C://A"), TEXT("C://"), true, TEXT("A"));
	RunRelChildTest(TEXT("C://////////A"), TEXT("C://"), true, TEXT("A"));
	RunRelChildTest(TEXT("C://A"), TEXT("C:/"), true, TEXT("A"));
	RunRelChildTest(TEXT("C://////////A"), TEXT("C:/"), true, TEXT("A"));
	RunRelChildTest(TEXT("A//B"), TEXT("A//"), true, TEXT("B"));
	RunRelChildTest(TEXT("A///////////B"), TEXT("A//"), true, TEXT("B"));
	RunRelChildTest(TEXT("A//B"), TEXT("A/"), true, TEXT("B"));
	RunRelChildTest(TEXT("A///////////B"), TEXT("A/"), true, TEXT("B"));
	RunRelChildTest(TEXT("A//B"), TEXT("A"), true, TEXT("B"));
	RunRelChildTest(TEXT("A///////////B"), TEXT("A"), true, TEXT("B"));
}


TEST_CASE("Core::Misc::FPathViews::Is Relative Path", "[Core][Misc][Smoke]")
{
	auto RunRelTest = [](const TCHAR* A, bool bExpected)
	{
		bool bActual = FPathViews::IsRelativePath(A);
		TestFalse(FString::Printf(TEXT("IsRelativePath(\"%s\") == %s, expected %s."), A, BoolToString(bActual), BoolToString(bExpected)), (bActual != bExpected));
	};

	RunRelTest(TEXT("A"), true);
	RunRelTest(TEXT("A/"), true);
	RunRelTest(TEXT("A/B"), true);
	RunRelTest(TEXT("A\\B"), true);
	RunRelTest(TEXT("/A"), false);
	RunRelTest(TEXT("\\A"), false);
	RunRelTest(TEXT("/A/B"), false);
	RunRelTest(TEXT("//A"), false);
	RunRelTest(TEXT("\\\\A"), false);
	RunRelTest(TEXT("//A/B"), false);
	RunRelTest(TEXT("C:/A"), false);
	RunRelTest(TEXT("C:\\A"), false);
	RunRelTest(TEXT("C:/A/B"), false);
}


TEST_CASE("Core::Misc::FPathViews::Split First Component", "[Core][Misc][Smoke]")
{
	auto RunSplitFirstTest = [](const TCHAR* FullPath, const TCHAR* ExpectedFirst, const TCHAR* ExpectedRemaining)
	{
		FStringView ActualFirst;
		FStringView ActualRemaining;
		FPathViews::SplitFirstComponent(FullPath, ActualFirst, ActualRemaining);
		
		TestFalse(FString::Printf(TEXT("SplitFirstComponent(\"%s\") == (\"%.*s\", \"%.*s\"), expected (\"%s\", \"%s\")."),
				FullPath, ActualFirst.Len(), ActualFirst.GetData(), ActualRemaining.Len(), ActualRemaining.GetData(),
				ExpectedFirst, ExpectedRemaining),
				(!ActualFirst.Equals(ExpectedFirst, ESearchCase::IgnoreCase) || !ActualRemaining.Equals(ExpectedRemaining, ESearchCase::IgnoreCase)) );
	};

	RunSplitFirstTest(TEXT(""), TEXT(""), TEXT(""));

	RunSplitFirstTest(TEXT("A"), TEXT("A"), TEXT(""));
	RunSplitFirstTest(TEXT("A/"), TEXT("A"), TEXT(""));
	RunSplitFirstTest(TEXT("A\\"), TEXT("A"), TEXT(""));
	RunSplitFirstTest(TEXT("A/B"), TEXT("A"), TEXT("B"));
	RunSplitFirstTest(TEXT("A\\B"), TEXT("A"), TEXT("B"));
	RunSplitFirstTest(TEXT("A/B/"), TEXT("A"), TEXT("B/"));
	RunSplitFirstTest(TEXT("A\\B\\"), TEXT("A"), TEXT("B\\"));
	RunSplitFirstTest(TEXT("A/B/C"), TEXT("A"), TEXT("B/C"));
	RunSplitFirstTest(TEXT("A\\B\\C"), TEXT("A"), TEXT("B\\C"));

	RunSplitFirstTest(TEXT("/A"), TEXT("/"), TEXT("A"));
	RunSplitFirstTest(TEXT("\\A"), TEXT("\\"), TEXT("A"));
	RunSplitFirstTest(TEXT("/A/"), TEXT("/"), TEXT("A/"));
	RunSplitFirstTest(TEXT("\\A\\"), TEXT("\\"), TEXT("A\\"));
	RunSplitFirstTest(TEXT("/A/B"), TEXT("/"), TEXT("A/B"));
	RunSplitFirstTest(TEXT("\\A\\B"), TEXT("\\"), TEXT("A\\B"));

	RunSplitFirstTest(TEXT("//A"), TEXT("//"), TEXT("A"));
	RunSplitFirstTest(TEXT("\\\\A"), TEXT("\\\\"), TEXT("A"));
	RunSplitFirstTest(TEXT("//A/"), TEXT("//"), TEXT("A/"));
	RunSplitFirstTest(TEXT("\\\\A\\"), TEXT("\\\\"), TEXT("A\\"));
	RunSplitFirstTest(TEXT("//A/B"), TEXT("//"), TEXT("A/B"));
	RunSplitFirstTest(TEXT("\\\\A\\B"), TEXT("\\\\"), TEXT("A\\B"));

	RunSplitFirstTest(TEXT("C:/A"), TEXT("C:/"), TEXT("A"));
	RunSplitFirstTest(TEXT("C:\\A"), TEXT("C:\\"), TEXT("A"));
	RunSplitFirstTest(TEXT("C:/A/"), TEXT("C:/"), TEXT("A/"));
	RunSplitFirstTest(TEXT("C:\\A\\"), TEXT("C:\\"), TEXT("A\\"));
	RunSplitFirstTest(TEXT("C:/A/B"), TEXT("C:/"), TEXT("A/B"));
	RunSplitFirstTest(TEXT("C:\\A\\B"), TEXT("C:\\"), TEXT("A\\B"));

	// Correctly handle paths with invalid duplicate slashes
	RunSplitFirstTest(TEXT("///A"), TEXT("//"), TEXT("A"));
	RunSplitFirstTest(TEXT("////////////A"), TEXT("//"), TEXT("A"));
	RunSplitFirstTest(TEXT("///A"), TEXT("//"), TEXT("A"));
	RunSplitFirstTest(TEXT("C://A"), TEXT("C:/"), TEXT("A"));
	RunSplitFirstTest(TEXT("C://////////A"), TEXT("C:/"), TEXT("A"));
	RunSplitFirstTest(TEXT("A//B"), TEXT("A"), TEXT("B"));
	RunSplitFirstTest(TEXT("A///////////B"), TEXT("A"), TEXT("B"));
}


TEST_CASE("Core::Misc::FPathViews::Append Path", "[Core][Misc][Smoke]")
{
	auto RunAppendTest = [](const TCHAR* Base, const TCHAR* Append, const TCHAR* ExpectedNewBase)
	{
		TStringBuilder<128> BaseBuilder;
		BaseBuilder << Base;
		FPathViews::AppendPath(BaseBuilder, Append);
		TestFalse(FString::Printf(TEXT("AppendPath(\"%s\", \"%s\") == \"%.*s\", expected \"%s\"."),
				Base, Append, BaseBuilder.Len(), BaseBuilder.GetData(), ExpectedNewBase),
				(!FStringView(BaseBuilder).Equals(ExpectedNewBase, ESearchCase::IgnoreCase)));
	};

	RunAppendTest(TEXT(""), TEXT("A"), TEXT("A"));
	RunAppendTest(TEXT(""), TEXT("A/B"), TEXT("A/B"));
	RunAppendTest(TEXT(""), TEXT("A\\B"), TEXT("A\\B"));

	RunAppendTest(TEXT("Root"), TEXT("A"), TEXT("Root/A"));
	RunAppendTest(TEXT("Root"), TEXT("A/B"), TEXT("Root/A/B"));
	RunAppendTest(TEXT("Root"), TEXT("A\\B"), TEXT("Root/A\\B"));

	RunAppendTest(TEXT("/"), TEXT("A"), TEXT("/A"));
	RunAppendTest(TEXT("/"), TEXT("A/B"), TEXT("/A/B"));
	RunAppendTest(TEXT("/"), TEXT("A\\B"), TEXT("/A\\B"));
	RunAppendTest(TEXT("\\"), TEXT("A"), TEXT("\\A"));
	RunAppendTest(TEXT("\\"), TEXT("A/B"), TEXT("\\A/B"));
	RunAppendTest(TEXT("\\"), TEXT("A\\B"), TEXT("\\A\\B"));

	RunAppendTest(TEXT("/Root"), TEXT("A"), TEXT("/Root/A"));
	RunAppendTest(TEXT("/Root"), TEXT("A/B"), TEXT("/Root/A/B"));
	RunAppendTest(TEXT("/Root"), TEXT("A\\B"), TEXT("/Root/A\\B"));
	RunAppendTest(TEXT("\\Root"), TEXT("A"), TEXT("\\Root/A"));
	RunAppendTest(TEXT("\\Root"), TEXT("A/B"), TEXT("\\Root/A/B"));
	RunAppendTest(TEXT("\\Root"), TEXT("A\\B"), TEXT("\\Root/A\\B"));
	RunAppendTest(TEXT("/Root/"), TEXT("A"), TEXT("/Root/A"));
	RunAppendTest(TEXT("/Root/"), TEXT("A/B"), TEXT("/Root/A/B"));
	RunAppendTest(TEXT("/Root/"), TEXT("A\\B"), TEXT("/Root/A\\B"));
	RunAppendTest(TEXT("\\Root\\"), TEXT("A"), TEXT("\\Root\\A"));
	RunAppendTest(TEXT("\\Root\\"), TEXT("A/B"), TEXT("\\Root\\A/B"));
	RunAppendTest(TEXT("\\Root\\"), TEXT("A\\B"), TEXT("\\Root\\A\\B"));

	RunAppendTest(TEXT("//"), TEXT("A"), TEXT("//A"));
	RunAppendTest(TEXT("//"), TEXT("A/B"), TEXT("//A/B"));
	RunAppendTest(TEXT("//"), TEXT("A\\B"), TEXT("//A\\B"));
	RunAppendTest(TEXT("\\\\"), TEXT("A"), TEXT("\\\\A"));
	RunAppendTest(TEXT("\\\\"), TEXT("A/B"), TEXT("\\\\A/B"));
	RunAppendTest(TEXT("\\\\"), TEXT("A\\B"), TEXT("\\\\A\\B"));

	RunAppendTest(TEXT("//Root"), TEXT("A"), TEXT("//Root/A"));
	RunAppendTest(TEXT("//Root"), TEXT("A/B"), TEXT("//Root/A/B"));
	RunAppendTest(TEXT("//Root"), TEXT("A\\B"), TEXT("//Root/A\\B"));
	RunAppendTest(TEXT("\\\\Root"), TEXT("A"), TEXT("\\\\Root/A"));
	RunAppendTest(TEXT("\\\\Root"), TEXT("A/B"), TEXT("\\\\Root/A/B"));
	RunAppendTest(TEXT("\\\\Root"), TEXT("A\\B"), TEXT("\\\\Root/A\\B"));
	RunAppendTest(TEXT("//Root/"), TEXT("A"), TEXT("//Root/A"));
	RunAppendTest(TEXT("//Root/"), TEXT("A/B"), TEXT("//Root/A/B"));
	RunAppendTest(TEXT("//Root/"), TEXT("A\\B"), TEXT("//Root/A\\B"));
	RunAppendTest(TEXT("\\\\Root\\"), TEXT("A"), TEXT("\\\\Root\\A"));
	RunAppendTest(TEXT("\\\\Root\\"), TEXT("A/B"), TEXT("\\\\Root\\A/B"));
	RunAppendTest(TEXT("\\\\Root\\"), TEXT("A\\B"), TEXT("\\\\Root\\A\\B"));

	RunAppendTest(TEXT("C:/"), TEXT("A"), TEXT("C:/A"));
	RunAppendTest(TEXT("C:/"), TEXT("A/B"), TEXT("C:/A/B"));
	RunAppendTest(TEXT("C:/"), TEXT("A\\B"), TEXT("C:/A\\B"));
	RunAppendTest(TEXT("C:\\"), TEXT("A"), TEXT("C:\\A"));
	RunAppendTest(TEXT("C:\\"), TEXT("A/B"), TEXT("C:\\A/B"));
	RunAppendTest(TEXT("C:\\"), TEXT("A\\B"), TEXT("C:\\A\\B"));

	RunAppendTest(TEXT("C:/Root"), TEXT("A"), TEXT("C:/Root/A"));
	RunAppendTest(TEXT("C:/Root"), TEXT("A/B"), TEXT("C:/Root/A/B"));
	RunAppendTest(TEXT("C:/Root"), TEXT("A\\B"), TEXT("C:/Root/A\\B"));
	RunAppendTest(TEXT("C:\\Root"), TEXT("A"), TEXT("C:\\Root/A"));
	RunAppendTest(TEXT("C:\\Root"), TEXT("A/B"), TEXT("C:\\Root/A/B"));
	RunAppendTest(TEXT("C:\\Root"), TEXT("A\\B"), TEXT("C:\\Root/A\\B"));
	RunAppendTest(TEXT("C:/Root/"), TEXT("A"), TEXT("C:/Root/A"));
	RunAppendTest(TEXT("C:/Root/"), TEXT("A/B"), TEXT("C:/Root/A/B"));
	RunAppendTest(TEXT("C:/Root/"), TEXT("A\\B"), TEXT("C:/Root/A\\B"));
	RunAppendTest(TEXT("C:\\Root\\"), TEXT("A"), TEXT("C:\\Root\\A"));
	RunAppendTest(TEXT("C:\\Root\\"), TEXT("A/B"), TEXT("C:\\Root\\A/B"));
	RunAppendTest(TEXT("C:\\Root\\"), TEXT("A\\B"), TEXT("C:\\Root\\A\\B"));

	// No matter the prefix, appending a rooted path should result in only the rooted path
	for (const TCHAR* Prefix : { TEXT(""),
		TEXT("/"), TEXT("\\"), TEXT("/Root"), TEXT("\\Root"), TEXT("/Root/"), TEXT("\\Root\\"),
		TEXT("//"), TEXT("\\\\"), TEXT("//Root"), TEXT("\\\\Root"), TEXT("//Root/"), TEXT("\\\\Root\\"),
		TEXT("C:/"), TEXT("C:\\"), TEXT("C:/Root"), TEXT("C:\\Root"), TEXT("C:/Root/"), TEXT("C:\\Root\\")})
	{
		RunAppendTest(Prefix, TEXT("/A"), TEXT("/A"));
		RunAppendTest(Prefix, TEXT("\\A"), TEXT("\\A"));
		RunAppendTest(Prefix, TEXT("/A/B"), TEXT("/A/B"));
		RunAppendTest(Prefix, TEXT("\\A\\B"), TEXT("\\A\\B"));

		RunAppendTest(Prefix, TEXT("//A"), TEXT("//A"));
		RunAppendTest(Prefix, TEXT("\\\\A"), TEXT("\\\\A"));
		RunAppendTest(Prefix, TEXT("//A/B"), TEXT("//A/B"));
		RunAppendTest(Prefix, TEXT("\\\\A\\B"), TEXT("\\\\A\\B"));

		RunAppendTest(Prefix, TEXT("C:/A"), TEXT("C:/A"));
		RunAppendTest(Prefix, TEXT("C:\\A"), TEXT("C:\\A"));
		RunAppendTest(Prefix, TEXT("C:/A/B"), TEXT("C:/A/B"));
		RunAppendTest(Prefix, TEXT("C:\\A\\B"), TEXT("C:\\A\\B"));
	}
}

TEST_CASE("Core::Misc::FPathViews::To Absolute", "[Core][Misc][Smoke]")
{
	using namespace PathTest;

	for (FTestPair Pair : ExpectedRelativeToAbsolutePaths)
	{
		TStringBuilder<64> ActualAppend;
		FPathViews::ToAbsolutePath(BaseDir, Pair.Input, ActualAppend);
		TestEqual(TEXT("ToAbsolutePath"), ActualAppend.ToView(), Pair.Expected);

		TStringBuilder<64> ActualInline;
		ActualInline << Pair.Input;
		FPathViews::ToAbsolutePathInline(BaseDir, ActualInline);	
		TestEqual(TEXT("ToAbsolutePathInline"), ActualInline.ToView(), Pair.Expected);
		
		const FStringView Original = TEXTVIEW("\\\\la/./.././la////");
		TStringBuilder<64> ActualNondestructive;
		ActualNondestructive << Original;
		FPathViews::ToAbsolutePath(BaseDir, Pair.Input, ActualNondestructive);
		TestEqual(TEXT("ToAbsolutePath non-destructive append"), ActualNondestructive.ToView().Left(Original.Len()), Original);
		TestEqual(TEXT("ToAbsolutePath non-destructive append"), ActualNondestructive.ToView().RightChop(Original.Len()), Pair.Expected);
	}
}
