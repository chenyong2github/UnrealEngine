// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/TopLevelAssetPath.h"

#include "Containers/UnrealString.h"

#include "Misc/AsciiSet.h"
#include "Misc/RedirectCollector.h"
#include "UObject/CoreRedirects.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectThreadContext.h"

// The reason behind HACK_HEADER_GENERATOR is that without it UHT is going to 'see' cppstructops for TopLevelAssetPath
// and will not generate temp FTopLevelAssetPath struct for codegen purposes where it can access all of its members
// which are public in the temp struct and private in the actual FTopLevelAssetPath.
// If UHT compiles with UE_IMPLEMENT_STRUCT("/Script/CoreUObject", TopLevelAssetPath) the generated code will fail to compile trying to access private struct members
#if !HACK_HEADER_GENERATOR
template<>
struct TStructOpsTypeTraits<FTopLevelAssetPath> : public TStructOpsTypeTraitsBase2<FTopLevelAssetPath>
{
	enum
	{
		WithStructuredSerializeFromMismatchedTag = true,
	};
};
UE_IMPLEMENT_STRUCT("/Script/CoreUObject", TopLevelAssetPath)
#endif

void FTopLevelAssetPath::AppendString(FStringBuilderBase& Builder) const
{
	if (!IsNull())
	{
		Builder << PackageName;
		if (!AssetName.IsNone())
		{
			Builder << '.' << AssetName;
		}		
	}
}

void FTopLevelAssetPath::AppendString(FString& Builder) const
{
	if (!IsNull())
	{
		PackageName.AppendString(Builder);
		if( !AssetName.IsNone() )
		{
			Builder += TEXT(".");
			AssetName.AppendString(Builder);
		}
	}
}

FString FTopLevelAssetPath::ToString() const
{
	TStringBuilder<256> Builder;
	AppendString(Builder);
	return FString(Builder);
}

void FTopLevelAssetPath::ToString(FString& OutString) const
{
	OutString.Reset();
	AppendString(OutString);
}

bool FTopLevelAssetPath::TrySetPath(FName InPackageName, FName InAssetName)
{
	PackageName = InPackageName;
	AssetName = InAssetName;
	return !PackageName.IsNone();
}

bool FTopLevelAssetPath::TrySetPath(const UObject* InObject)
{
	if (InObject == nullptr)
	{
		Reset();
		return false;
	}
	else if (!InObject->GetOuter())
	{
		check(Cast<UPackage>(InObject) != nullptr);
		PackageName = InObject->GetFName();
		AssetName = FName();
		return true;
	}
	else if (InObject->GetOuter()->GetOuter() != nullptr)
	{
		Reset();
		return false;
	}
	else
	{
		PackageName = InObject->GetOuter()->GetFName();
		AssetName = InObject->GetFName();
		return true;
	}
}

bool FTopLevelAssetPath::TrySetPath(FWideStringView Path)
{
	if (Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::CaseSensitive))
	{
		// Empty path, just empty the pathname.
		Reset();
		return false;
	}
	else
	{
		if (Path[0] != '/' || Path[Path.Len() - 1] == '\'')
		{
			// Possibly an ExportText path. Trim the ClassName.
			Path = FPackageName::ExportTextPathToObjectPath(Path);

			if (Path.IsEmpty() || Path[0] != '/')
			{
				ensureAlwaysMsgf(false, TEXT("Short asset name used to create FTopLevelAssetPath: \"%.*s\""), Path.Len(), Path.GetData());
				Reset();
				return false;
			}
		}

		FAsciiSet Delim1(".");
		FWideStringView PackageNameView = FAsciiSet::FindPrefixWithout(Path, Delim1);
		if (PackageNameView.IsEmpty())
		{
			Reset();
			return false;
		}

		FWideStringView AssetNameView = Path.Mid(PackageNameView.Len() + 1);
		if (AssetNameView.IsEmpty())
		{
			// Reference to a package itself. Iffy, but supported for legacy usage of FSoftObjectPath.
			PackageName = FName(PackageNameView);
			AssetName = FName();
			return true;
		}

		FAsciiSet Delim2(TEXT("." SUBOBJECT_DELIMITER_ANSI));
		if (FAsciiSet::HasAny(AssetNameView, Delim2))
		{
			// Subobject path or is malformed and contains multiple '.' delimiters.
			Reset();
			return false;
		}

		PackageName = FName(PackageNameView);
		AssetName = FName(AssetNameView);
		return true;
	}
	return false;
}

bool FTopLevelAssetPath::TrySetPath(FUtf8StringView Path)
{
	TStringBuilder<FName::StringBufferSize> Wide;
	Wide << Path;
	return TrySetPath(Wide);
}

bool FTopLevelAssetPath::TrySetPath(FAnsiStringView Path)
{
	TStringBuilder<FName::StringBufferSize> Wide;
	Wide << Path;
	return TrySetPath(Wide);
}

bool FTopLevelAssetPath::SerializeFromMismatchedTag(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot)
{
	if (Tag.Type == NAME_NameProperty)
	{
		FName Name;
		Slot << Name;
		
		FNameBuilder NameBuilder(Name);
		TrySetPath(NameBuilder.ToView());

		return true;
	}
	
	if (Tag.Type == NAME_StrProperty)
	{
		FString String;
		Slot << String;

		TrySetPath(String);
		
		return true;
	}

	return false;
}



#if WITH_DEV_AUTOMATION_TESTS 

#include "Misc/AutomationTest.h"

// Combine import/export tests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTopLevelAssetPathTest, "System.Core.Misc.TopLevelAssetPath", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

bool FTopLevelAssetPathTest::RunTest(const FString& Parameters)
{
	FName PackageName("/Path/To/Package");
	FName AssetName("Asset");

	FString AssetPathString(WriteToString<FName::StringBufferSize>(PackageName, '.', AssetName).ToView());

	FTopLevelAssetPath EmptyPath;
	TestEqual(TEXT("Empty path to string is empty string"), EmptyPath.ToString(), FString());

	FTopLevelAssetPath PackagePath;
	TestFalse(TEXT("TrySetPath(NAME_None, NAME_None) fails"), PackagePath.TrySetPath(NAME_None, NAME_None));
	TestTrue(TEXT("TrySetPath(PackageName, NAME_None) succeeds"), PackagePath.TrySetPath(PackageName, NAME_None));
	TestEqual(TEXT("PackagePath to string is PackageName"), PackagePath.ToString(), PackageName.ToString());

	FTopLevelAssetPath AssetPath;
	TestTrue(TEXT("TrySetPath(PackageName, AssetName) succeeds"), AssetPath.TrySetPath(PackageName, AssetName));
	TestEqual(TEXT("AssetPath to string is PackageName.AssetName"), AssetPath.ToString(), AssetPathString);

	FTopLevelAssetPath EmptyPathFromString;
	TestFalse(TEXT("TrySetPath with empty string fails"), EmptyPathFromString.TrySetPath(TEXT("")));
	TestEqual(TEXT("Empty path to string is empty string"), EmptyPathFromString.ToString(), FString());

	FTopLevelAssetPath PackagePathFromString;
	TestTrue(TEXT("TrySetPath(PackageName.ToString()) succeeds"), PackagePath.TrySetPath(PackageName.ToString()));
	TestEqual(TEXT("PackagePath to string is PackageName"), PackagePath.ToString(), PackageName.ToString());

	FTopLevelAssetPath AssetPathFromString;
	TestTrue(TEXT("TrySetPath(AssetPath) succeeds"), PackagePath.TrySetPath(AssetPathString));
	TestEqual(TEXT("AssetPathFromString to string is PackageName.AssetName"), PackagePath.ToString(), AssetPathString);

	FTopLevelAssetPath FailedPath;
	//TestFalse(TEXT("TrySetPath with unrooted path string fails"), FailedPath.TrySetPath("UnrootedPackage/Subfolder")); // after ANY_PACKAGE removal this will assert
	TestEqual(TEXT("Failed set to string is empty string"), FailedPath.ToString(), FString());

	FTopLevelAssetPath SubObjectPath;
	TestFalse(TEXT("TrySetPath with subobject path string fails"), SubObjectPath.TrySetPath("/Path/To/Package.Asset:Subobject"));
	TestEqual(TEXT("Failed set to string is empty string"), SubObjectPath.ToString(), FString());

	FTopLevelAssetPath MalformedPath;
	TestFalse(TEXT("TrySetPath with malformed path string fails"), MalformedPath.TrySetPath("/Path/To/Package.Asset.Malformed"));
	TestEqual(TEXT("Failed set to string is empty string"), MalformedPath.ToString(), FString());

	// Test ExportText path with and without root 
	// Test starting with . 

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
