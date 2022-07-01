// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetData.h"

#include "Algo/IsSorted.h"
#include "Algo/Sort.h"
#include "AssetRegistry/ARFilter.h"
#include "Containers/Set.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"
#include "Serialization/CustomVersion.h"
#include "String/Find.h"
#include "UObject/PropertyPortFlags.h"

DEFINE_LOG_CATEGORY(LogAssetData);

UE_IMPLEMENT_STRUCT("/Script/CoreUObject", ARFilter);
UE_IMPLEMENT_STRUCT("/Script/CoreUObject", AssetData);

// Register Asset Registry version
const FGuid FAssetRegistryVersion::GUID(0x717F9EE7, 0xE9B0493A, 0x88B39132, 0x1B388107);
FCustomVersionRegistration GRegisterAssetRegistryVersion(FAssetRegistryVersion::GUID, FAssetRegistryVersion::LatestVersion, TEXT("AssetRegistry"));

namespace UE { namespace AssetData { namespace Private {

const FName GAssetBundleDataName("AssetBundleData");

static TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe> ParseAssetBundles(const TCHAR* Text, const FAssetData& Context)
{
	// Register that the SoftObjectPaths we read in the FAssetBundleEntry::BundleAssets are non-package data and don't need to be tracked
	FSoftObjectPathSerializationScope SerializationScope(NAME_None, NAME_None, ESoftObjectPathCollectType::NonPackage, ESoftObjectPathSerializeType::AlwaysSerialize);

	FAssetBundleData Temp;
	if (!Temp.ImportTextItem(Text, PPF_None, nullptr, (FOutputDevice*)GWarn))
	{
		// Native UScriptStruct isn't available during early cooked asset registry preloading.
		// Preloading should not require this fallback.
		
		UScriptStruct& Struct = *TBaseStructure<FAssetBundleData>::Get();
		Struct.ImportText(Text, &Temp, nullptr, PPF_None, (FOutputDevice*)GWarn, 
							[&]() { return Context.AssetName.ToString(); });
	}
	
	if (Temp.Bundles.Num() > 0)
	{
		return TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe>(new FAssetBundleData(MoveTemp(Temp)));
	}

	return TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe>();
}

}}} // end namespace UE::AssetData::Private

FAssetData::FAssetData(FName InPackageName, FName InPackagePath, FName InAssetName, FName InAssetClass, FAssetDataTagMap InTags, TArrayView<const int32> InChunkIDs, uint32 InPackageFlags)
	: FAssetData(InPackageName, InPackagePath, InAssetName, FAssetData::TryConvertShortClassNameToPathName(InAssetClass), InTags, InChunkIDs, InPackageFlags)
{
}

FAssetData::FAssetData(const FString& InLongPackageName, const FString& InObjectPath, FName InAssetClass, FAssetDataTagMap InTags, TArrayView<const int32> InChunkIDs, uint32 InPackageFlags)
	: FAssetData(InLongPackageName, InObjectPath, FAssetData::TryConvertShortClassNameToPathName(InAssetClass), InTags, InChunkIDs, InPackageFlags)
{
}

FAssetData::FAssetData(FName InPackageName, FName InPackagePath, FName InAssetName, FTopLevelAssetPath InAssetClassPathName, FAssetDataTagMap InTags, TArrayView<const int32> InChunkIDs, uint32 InPackageFlags)
	: PackageName(InPackageName)
	, PackagePath(InPackagePath)
	, AssetName(InAssetName)
	, AssetClassPath(InAssetClassPathName)
	, PackageFlags(InPackageFlags)
	, ChunkIDs(MoveTemp(InChunkIDs))
{
	SetTagsAndAssetBundles(MoveTemp(InTags));

	FNameBuilder ObjectPathStr(PackageName);
	ObjectPathStr << TEXT('.');
	AssetName.AppendString(ObjectPathStr);
	ObjectPath = FName(FStringView(ObjectPathStr));
}

FAssetData::FAssetData(const FString& InLongPackageName, const FString& InObjectPath, FTopLevelAssetPath InAssetClassPathName, FAssetDataTagMap InTags, TArrayView<const int32> InChunkIDs, uint32 InPackageFlags)
	: ObjectPath(*InObjectPath)
	, PackageName(*InLongPackageName)
	, AssetClassPath(InAssetClassPathName)
	, PackageFlags(InPackageFlags)
	, ChunkIDs(MoveTemp(InChunkIDs))
{
	SetTagsAndAssetBundles(MoveTemp(InTags));

	PackagePath = FName(*FPackageName::GetLongPackagePath(InLongPackageName));

	// Find the object name from the path, FPackageName::ObjectPathToObjectName(InObjectPath)) doesn't provide what we want here
	int32 CharPos = InObjectPath.FindLastCharByPredicate([](TCHAR Char)
	{
		return Char == ':' || Char == '.';
	});
	AssetName = FName(*InObjectPath.Mid(CharPos + 1));
}

FAssetData::FAssetData(const UObject* InAsset, FAssetData::ECreationFlags InCreationFlags)
{
	if (InAsset != nullptr)
	{
#if WITH_EDITORONLY_DATA
		// ClassGeneratedBy TODO: This may be wrong in cooked builds
		const UClass* InClass = Cast<UClass>(InAsset);
		if (InClass && InClass->ClassGeneratedBy && !EnumHasAnyFlags(InCreationFlags, FAssetData::ECreationFlags::AllowBlueprintClass))
		{
			// For Blueprints, the AssetData refers to the UBlueprint and not the UBlueprintGeneratedClass
			InAsset = InClass->ClassGeneratedBy;
		}
#endif

		const UPackage* Package = InAsset->GetPackage();

		PackageName = Package->GetFName();
		PackagePath = FName(*FPackageName::GetLongPackagePath(Package->GetName()));
		AssetName = InAsset->GetFName();
		AssetClassPath = InAsset->GetClass()->GetPathName();
		ObjectPath = FName(*InAsset->GetPathName());

		if (!EnumHasAnyFlags(InCreationFlags, FAssetData::ECreationFlags::SkipAssetRegistryTagsGathering))
		{
			InAsset->GetAssetRegistryTags(*this);
		}

		ChunkIDs = Package->GetChunkIDs();
		PackageFlags = Package->GetPackageFlags();
	}
}

bool FAssetData::IsUAsset(UObject* InAsset)
{
	if (InAsset == nullptr)
	{
		return false;
	}

	const UPackage* Package = InAsset->GetPackage();

	TStringBuilder<FName::StringBufferSize> AssetNameStrBuilder;
	InAsset->GetPathName(Package, AssetNameStrBuilder);

	TStringBuilder<FName::StringBufferSize> PackageNameStrBuilder;
	Package->GetFName().AppendString(PackageNameStrBuilder);

	return DetectIsUAssetByNames(PackageNameStrBuilder, AssetNameStrBuilder);
}

bool FAssetData::IsTopLevelAsset() const
{
	int32 SubObjectIndex;
	FStringView(WriteToString<256>(ObjectPath)).FindChar(SUBOBJECT_DELIMITER_CHAR, SubObjectIndex);
	return SubObjectIndex == INDEX_NONE;
}

bool FAssetData::IsTopLevelAsset(UObject* Object)
{
	if (!Object)
	{
		return false;
	}
	UObject* Outer = Object->GetOuter();
	if (!Outer)
	{
		return false;
	}
	return Outer->IsA<UPackage>();
}

void FAssetData::SetTagsAndAssetBundles(FAssetDataTagMap&& Tags)
{
	using namespace UE::AssetData::Private;

	for (TPair<FName, FString>& Tag : Tags)
	{
		check(!Tag.Key.IsNone() && !Tag.Value.IsEmpty());
	}

	FString AssetBundles;
	if (Tags.RemoveAndCopyValue(GAssetBundleDataName, AssetBundles))
	{
		TaggedAssetBundles = ParseAssetBundles(*AssetBundles, *this);
	}
	else
	{
		TaggedAssetBundles.Reset();
	}

	TagsAndValues = Tags.Num() > 0 ? FAssetDataTagMapSharedView(MoveTemp(Tags)) : FAssetDataTagMapSharedView();
}

FPrimaryAssetId FAssetData::GetPrimaryAssetId() const
{
	FName PrimaryAssetType = GetTagValueRef<FName>(FPrimaryAssetId::PrimaryAssetTypeTag);
	FName PrimaryAssetName = GetTagValueRef<FName>(FPrimaryAssetId::PrimaryAssetNameTag);

	if (!PrimaryAssetType.IsNone() && !PrimaryAssetName.IsNone())
	{
		return FPrimaryAssetId(PrimaryAssetType, PrimaryAssetName);
	}

	return FPrimaryAssetId();
}

void FAssetData::SerializeForCacheInternal(FArchive& Ar, FAssetRegistryVersion::Type Version, void (*SerializeTagsAndBundles)(FArchive& , FAssetData&))
{
	// Serialize out the asset info
	Ar << ObjectPath;
	Ar << PackagePath;

	// Serialize the asset class.
	if (Version >= FAssetRegistryVersion::ClassPaths)
	{
		Ar << AssetClassPath;
	}
	else
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Ar << AssetClass;
		AssetClassPath = FAssetData::TryConvertShortClassNameToPathName(AssetClass, ELogVerbosity::NoLogging);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// These are derived from ObjectPath, we manually serialize them because they get pooled
	Ar << PackageName;
	Ar << AssetName;

	SerializeTagsAndBundles(Ar , *this);

	if (Ar.IsSaving() && ChunkIDs.Num() > 1)
	{
		TArray<int32> SortedChunkIDs(ChunkIDs);
		Algo::Sort(SortedChunkIDs);
		Ar << SortedChunkIDs;
	}
	else
	{
		Ar << ChunkIDs;
	}
	Ar << PackageFlags;
}

FTopLevelAssetPath FAssetData::TryConvertShortClassNameToPathName(FName InClassName, ELogVerbosity::Type FailureMessageVerbosity /*= ELogVerbosity::Warning*/)
{
	FTopLevelAssetPath ClassPath;
	if (!InClassName.IsNone())
	{
		FString ClassNameString(InClassName.ToString());
		ELogVerbosity::Type AmbiguousMessageVerbosity = (FailureMessageVerbosity == ELogVerbosity::NoLogging || FailureMessageVerbosity > ELogVerbosity::Warning) ?
			FailureMessageVerbosity : ELogVerbosity::Warning;
		ClassPath = UClass::TryConvertShortTypeNameToPathName<UStruct>(ClassNameString, AmbiguousMessageVerbosity, TEXT("AssetRegistry trying to convert short name to path name"));
		if (ClassPath.IsNull())
		{
			// In some cases the class name stored in asset registry tags have been redirected with ini class redirects
			FString RedirectedName = FLinkerLoad::FindNewPathNameForClass(ClassNameString, false);
			if (!FPackageName::IsShortPackageName(RedirectedName))
			{
				ClassPath = FTopLevelAssetPath(RedirectedName);
			}
			else
			{
				ClassPath = UClass::TryConvertShortTypeNameToPathName<UStruct>(RedirectedName, AmbiguousMessageVerbosity, TEXT("AssetRegistry trying to convert redirected short name to path name"));
			}

			if (ClassPath.IsNull())
			{
				// Fallback to a fake name but at least the class name will be preserved
				ClassPath = FTopLevelAssetPath(TEXT("/Unknown"), InClassName);
#if !NO_LOGGING
				if (FailureMessageVerbosity != ELogVerbosity::NoLogging)
				{
					FMsg::Logf(__FILE__, __LINE__, LogAssetData.GetCategoryName(), FailureMessageVerbosity, TEXT("Failed to convert deprecated short class name \"%s\" to path name. Using \"%s\""), *InClassName.ToString(), *ClassPath.ToString());
				}
#endif
			}
		}
	}
	return ClassPath;
}

bool FAssetRegistryVersion::SerializeVersion(FArchive& Ar, FAssetRegistryVersion::Type& Version)
{
	FGuid Guid = FAssetRegistryVersion::GUID;

	if (Ar.IsLoading())
	{
		Version = FAssetRegistryVersion::PreVersioning;
	}

	Ar << Guid;

	if (Ar.IsError())
	{
		return false;
	}

	if (Guid == FAssetRegistryVersion::GUID)
	{
		int32 VersionInt = Version;
		Ar << VersionInt;
		Version = (FAssetRegistryVersion::Type)VersionInt;

		Ar.SetCustomVersion(Guid, VersionInt, TEXT("AssetRegistry"));
	}
	else
	{
		return false;
	}

	return !Ar.IsError();
}

void FAssetPackageData::SerializeForCacheInternal(FArchive& Ar, FAssetPackageData& PackageData, FAssetRegistryVersion::Type Version)
{
	Ar << PackageData.DiskSize;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Ar << PackageData.PackageGuid;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (Version >= FAssetRegistryVersion::AddedCookedMD5Hash)
	{
		Ar << PackageData.CookedHash;
	}
	if (Version >= FAssetRegistryVersion::AddedChunkHashes)
	{
		Ar << PackageData.ChunkHashes;
	}
	if (Version >= FAssetRegistryVersion::WorkspaceDomain)
	{
		if (Version >= FAssetRegistryVersion::PackageFileSummaryVersionChange)
		{
			Ar << PackageData.FileVersionUE;
		}
		else
		{
			int32 UE4Version;
			Ar << UE4Version;

			PackageData.FileVersionUE = FPackageFileVersion::CreateUE4Version(UE4Version);
		}

		Ar << PackageData.FileVersionLicenseeUE;
		Ar << PackageData.Flags;
		Ar << PackageData.CustomVersions;
	}
	if (Version >= FAssetRegistryVersion::PackageImportedClasses)
	{
		if (Ar.IsSaving() && !Algo::IsSorted(PackageData.ImportedClasses, FNameLexicalLess()))
		{
			Algo::Sort(PackageData.ImportedClasses, FNameLexicalLess());
		}
		Ar << PackageData.ImportedClasses;
	}
}

COREUOBJECT_API void FAssetPackageData::SerializeForCache(FArchive& Ar)
{
	// Calling with hard-coded version and using force-inline on SerializeForCacheInternal eliminates the cost of its if-statements
	SerializeForCacheInternal(Ar, *this, FAssetRegistryVersion::LatestVersion);
}

COREUOBJECT_API void FAssetPackageData::SerializeForCacheOldVersion(FArchive& Ar, FAssetRegistryVersion::Type Version)
{
	SerializeForCacheInternal(Ar, *this, Version);
}

void FARFilter::PostSerialize(const FArchive& Ar)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if WITH_EDITORONLY_DATA

	auto ConvertShortClassNameToPathName = [](FName ShortClassFName)
	{
		FTopLevelAssetPath ClassPathName;
		if (ShortClassFName != NAME_None)
		{
			FString ShortClassName = ShortClassFName.ToString();
			ClassPathName = UClass::TryConvertShortTypeNameToPathName<UStruct>(*ShortClassName, ELogVerbosity::Warning, TEXT("FARFilter::PostSerialize"));
			UE_CLOG(ClassPathName.IsNull(), LogAssetData, Error, TEXT("Failed to convert short class name %s to class path name."), *ShortClassName);
		}
		return ClassPathName;
	};

	for (FName ClassFName : ClassNames)
	{
		FTopLevelAssetPath ClassPathName = ConvertShortClassNameToPathName(ClassFName);
		ClassPaths.Add(ClassPathName);
	}
	for (FName ClassFName : RecursiveClassesExclusionSet)
	{
		FTopLevelAssetPath ClassPathName = ConvertShortClassNameToPathName(ClassFName);
		RecursiveClassPathsExclusionSet.Add(ClassPathName);
	}

	ClassNames.Empty();
	RecursiveClassPathsExclusionSet.Empty();

#endif // WITH_EDITORONLY_DATA
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

namespace UE
{
namespace AssetRegistry
{

uint32 GetTypeHash(const TArray<FPackageCustomVersion>& Versions)
{
	constexpr uint32 HashPrime = 23;
	uint32 Hash = 0;
	for (const FPackageCustomVersion& Version : Versions)
	{
		Hash = Hash * HashPrime + GetTypeHash(Version.Key);
		Hash = Hash * HashPrime + Version.Version;
	}
	return Hash;
}

class FPackageCustomVersionRegistry
{
public:
	FPackageCustomVersionsHandle FindOrAdd(TArray<FPackageCustomVersion>&& InVersions)
	{
		FPackageCustomVersionsHandle Result;
		Algo::Sort(InVersions);
		uint32 Hash = GetTypeHash(InVersions);
		{
			FReadScopeLock ScopeLock(Lock);
			TArray<FPackageCustomVersion>* Existing = RegisteredValues.FindByHash(Hash, InVersions);
			if (Existing)
			{
				// We return a TArrayView with a pointer to the allocation managed by the element in the Set
				// The element in the set may be destroyed and a moved copy recreated when the set changes size,
				// but since TSet uses move constructors during the resize, the allocation will be unchanged,
				// so we can safely refer to it from external handles.
				Result.Ptr = TConstArrayView<FPackageCustomVersion>(*Existing);
				return Result;
			}
		}
		{
			FWriteScopeLock ScopeLock(Lock);
			TArray<FPackageCustomVersion>& Existing = RegisteredValues.FindOrAddByHash(Hash, MoveTemp(InVersions));
			Result.Ptr = TConstArrayView<FPackageCustomVersion>(Existing);
			return Result;
		}
	}

private:
	TSet<TArray<FPackageCustomVersion>> RegisteredValues;
	FRWLock Lock;
} GFPackageCustomVersionRegistry;

FPackageCustomVersionsHandle FPackageCustomVersionsHandle::FindOrAdd(TConstArrayView<FCustomVersion> InVersions)
{
	TArray<FPackageCustomVersion> PackageFormat;
	PackageFormat.Reserve(InVersions.Num());
	for (const FCustomVersion& Version : InVersions)
	{
		PackageFormat.Emplace(Version.Key, Version.Version);
	}
	return GFPackageCustomVersionRegistry.FindOrAdd(MoveTemp(PackageFormat));
}

FPackageCustomVersionsHandle FPackageCustomVersionsHandle::FindOrAdd(TConstArrayView<FPackageCustomVersion> InVersions)
{
	return GFPackageCustomVersionRegistry.FindOrAdd(TArray<FPackageCustomVersion>(InVersions));
}

FPackageCustomVersionsHandle FPackageCustomVersionsHandle::FindOrAdd(TArray<FPackageCustomVersion>&& InVersions)
{
	return GFPackageCustomVersionRegistry.FindOrAdd(MoveTemp(InVersions));
}

FArchive& operator<<(FArchive& Ar, UE::AssetRegistry::FPackageCustomVersionsHandle& Handle)
{
	using namespace UE::AssetRegistry;

	if (Ar.IsLoading())
	{
		int32 NumCustomVersions;
		Ar << NumCustomVersions;
		TArray<UE::AssetRegistry::FPackageCustomVersion> CustomVersions;
		CustomVersions.SetNum(NumCustomVersions);
		for (UE::AssetRegistry::FPackageCustomVersion& CustomVersion : CustomVersions)
		{
			Ar << CustomVersion;
		}
		Handle = FPackageCustomVersionsHandle::FindOrAdd(MoveTemp(CustomVersions));
	}
	else
	{
		TConstArrayView<UE::AssetRegistry::FPackageCustomVersion> CustomVersions = Handle.Get();
		int32 NumCustomVersions = CustomVersions.Num();
		Ar << NumCustomVersions;
		for (UE::AssetRegistry::FPackageCustomVersion CustomVersion : CustomVersions)
		{
			Ar << CustomVersion;
		}
	}
	return Ar;
}

}
}
