// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetData.h"

#include "Algo/Sort.h"
#include "AssetRegistry/ARFilter.h"
#include "Containers/Set.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"
#include "Serialization/CustomVersion.h"
#include "String/Find.h"
#include "UObject/PropertyPortFlags.h"

DEFINE_LOG_CATEGORY(LogAssetData);

IMPLEMENT_STRUCT(ARFilter);
IMPLEMENT_STRUCT(AssetData);

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
	: PackageName(InPackageName)
	, PackagePath(InPackagePath)
	, AssetName(InAssetName)
	, AssetClass(InAssetClass)
	, ChunkIDs(MoveTemp(InChunkIDs))
	, PackageFlags(InPackageFlags)
{
	SetTagsAndAssetBundles(MoveTemp(InTags));

	FNameBuilder ObjectPathStr(PackageName);
	ObjectPathStr << TEXT('.');
	AssetName.AppendString(ObjectPathStr);
	ObjectPath = FName(FStringView(ObjectPathStr));
}

FAssetData::FAssetData(const FString& InLongPackageName, const FString& InObjectPath, FName InAssetClass, FAssetDataTagMap InTags, TArrayView<const int32> InChunkIDs, uint32 InPackageFlags)
	: ObjectPath(*InObjectPath)
	, PackageName(*InLongPackageName)
	, AssetClass(InAssetClass)
	, ChunkIDs(MoveTemp(InChunkIDs))
	, PackageFlags(InPackageFlags)
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
		AssetClass = InAsset->GetClass()->GetFName();
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
