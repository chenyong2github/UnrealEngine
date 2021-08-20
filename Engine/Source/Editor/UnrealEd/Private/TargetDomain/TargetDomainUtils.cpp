// Copyright Epic Games, Inc. All Rights Reserved.

#include "TargetDomain/TargetDomainUtils.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Cooker/PackageBuildDependencyTracker.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "IO/IoHash.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Serialization/PackageWriter.h"

namespace UE::TargetDomain
{

bool TryCreateKey(FName PackageName, TArrayView<FName> SortedBuildDependencies, FIoHash* OutHash, FString* OutErrorMessage)
{
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (!AssetRegistry)
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("AssetRegistry is unavailable.");
		return false;
	}
	FCbWriter KeyBuilder;
	UE::EditorDomain::EPackageDigestResult Result;
	FString ErrorMessage;
	bool bBlacklisted;
	Result = UE::EditorDomain::AppendPackageDigest(*AssetRegistry, PackageName, KeyBuilder, bBlacklisted, ErrorMessage);
	if (Result != UE::EditorDomain::EPackageDigestResult::Success)
	{
		if (OutErrorMessage) *OutErrorMessage = MoveTemp(ErrorMessage);
		return false;
	}

	for (FName DependencyName : SortedBuildDependencies)
	{
		Result = UE::EditorDomain::AppendPackageDigest(*AssetRegistry, DependencyName, KeyBuilder, bBlacklisted, ErrorMessage);
		if (Result != UE::EditorDomain::EPackageDigestResult::Success)
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = FString::Printf(TEXT("Could not create PackageDigest for %s: %s"),
					*DependencyName.ToString(), *ErrorMessage);
			}
			return false;
		}
	}

	if (OutHash)
	{
		*OutHash = KeyBuilder.Save().GetRangeHash();
	}
	return true;
}

bool TryCollectKeyAndDependencies(UPackage* Package, const ITargetPlatform* TargetPlatform, FIoHash* OutHash, TArray<FName>* OutBuildDependencies,
	TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage)
{
	if (!Package)
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("Invalid null package.");
		return false;
	}

	FName PackageName = Package->GetFName();
	TSet<FName> BuildDependencies;
	TSet<FName> RuntimeOnlyDependencies;

	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (!AssetRegistry)
	{
		if (OutErrorMessage) *OutErrorMessage = TEXT("AssetRegistry is unavailable.");
		return false;
	}

	TArray<FName> AssetDependencies;
	AssetRegistry->GetDependencies(PackageName, AssetDependencies, UE::AssetRegistry::EDependencyCategory::Package,
		UE::AssetRegistry::EDependencyQuery::Game);

	FPackageBuildDependencyTracker& Tracker = FPackageBuildDependencyTracker::Get();
	TArray<FBuildDependencyAccessData> AccessDatas = Tracker.GetAccessDatas(PackageName);

	BuildDependencies.Reserve(AccessDatas.Num());
	for (FBuildDependencyAccessData& AccessData : AccessDatas)
	{
		if (AccessData.TargetPlatform == TargetPlatform || AccessData.TargetPlatform == nullptr)
		{
			BuildDependencies.Add(AccessData.ReferencedPackage);
		}
	}

	RuntimeOnlyDependencies.Reserve(AccessDatas.Num());
	for (FName DependencyName : AssetDependencies)
	{
		if (!BuildDependencies.Contains(DependencyName))
		{
			RuntimeOnlyDependencies.Add(DependencyName);
		}
	}

	TArray<FName> SortedBuild;
	SortedBuild = BuildDependencies.Array();
	SortedBuild.Sort(FNameLexicalLess());
	TArray<FName> SortedRuntimeOnly;
	SortedRuntimeOnly = RuntimeOnlyDependencies.Array();
	SortedRuntimeOnly.Sort(FNameLexicalLess());

	if (!TryCreateKey(PackageName, SortedBuild, OutHash, OutErrorMessage))
	{
		return false;
	}

	if (OutBuildDependencies)
	{
		*OutBuildDependencies = MoveTemp(SortedBuild);
	}
	if (OutRuntimeOnlyDependencies)
	{
		*OutRuntimeOnlyDependencies = MoveTemp(SortedRuntimeOnly);
	}
	if (OutErrorMessage)
	{
		OutErrorMessage->Reset();
	}
	
	return true;
}

FCbObject CollectDependenciesObject(UPackage* Package, const ITargetPlatform* TargetPlatform, FString* ErrorMessage)
{
	FIoHash TargetDomainKey;
	TArray<FName> BuildDependencies;
	TArray<FName> RuntimeOnlyDependencies;
	if (!UE::TargetDomain::TryCollectKeyAndDependencies(Package, TargetPlatform, &TargetDomainKey, &BuildDependencies, &RuntimeOnlyDependencies, ErrorMessage))
	{
		return FCbObject();
	}

	FCbWriter Writer;
	Writer.BeginObject();
	Writer << "targetdomainkey" << TargetDomainKey;
	TStringBuilder<128> PackageNameBuffer;
	if (!BuildDependencies.IsEmpty())
	{
		Writer.BeginArray("builddependencies");
		for (FName DependencyName : BuildDependencies)
		{
			DependencyName.ToString(PackageNameBuffer);
			Writer << PackageNameBuffer;
		}
		Writer.EndArray();
	}
	if (!RuntimeOnlyDependencies.IsEmpty())
	{
		Writer.BeginArray("runtimeonlydependencies");
		for (FName DependencyName : RuntimeOnlyDependencies)
		{
			DependencyName.ToString(PackageNameBuffer);
			Writer << PackageNameBuffer;
		}
		Writer.EndArray();
	}
	Writer.EndObject();
	return Writer.Save().AsObject();
}


bool TryFetchKeyAndDependencies(ICookedPackageWriter* PackageWriter, FName PackageName, const ITargetPlatform* TargetPlatform,
	FIoHash* OutHash, TArray<FName>* OutBuildDependencies, TArray<FName>* OutRuntimeOnlyDependencies, FString* OutErrorMessage)
{
	FCbObject DependenciesObj = PackageWriter->GetTargetDomainDependencies(PackageName);
	FIoHash StoredKey = DependenciesObj["targetdomainkey"].AsHash();
	if (StoredKey.IsZero())
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Dependencies not in oplog.");
		}
		return false;
	}

	TArray<FName> BuildDependencies;
	if (OutBuildDependencies)
	{
		OutBuildDependencies->Reset();
	}
	else
	{
		OutBuildDependencies = &BuildDependencies;
	}

	for (FCbFieldView DepObj : DependenciesObj["builddependencies"])
	{
		if (FString DependencyName(DepObj.AsString()); !DependencyName.IsEmpty())
		{
			OutBuildDependencies->Add(FName(*DependencyName));
		}
	}

	FIoHash CurrentKey;
	if (!TryCreateKey(PackageName, *OutBuildDependencies, &CurrentKey, OutErrorMessage))
	{
		return false;
	}

	if (StoredKey != CurrentKey)
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Stored key does not match current key.");
		}
		return false;
	}

	if (OutRuntimeOnlyDependencies)
	{
		for (FCbFieldView DepObj : DependenciesObj["runtimeonlydependencies"])
		{
			if (FString DependencyName(DepObj.AsString()); !DependencyName.IsEmpty())
			{
				OutRuntimeOnlyDependencies->Add(FName(*DependencyName));
			}
		}
	}

	return true;
}

bool IsIterativeEnabled(FName PackageName)
{
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	if (!AssetRegistry)
	{
		return false;
	}
	TOptional<FAssetPackageData> PackageDataOpt = AssetRegistry->GetAssetPackageDataCopy(PackageName);
	if (!PackageDataOpt)
	{
		return false;
	}
	FAssetPackageData& PackageData = *PackageDataOpt;

	UE::EditorDomain::FClassDigestMap& ClassDigests = UE::EditorDomain::GetClassDigests();
	FReadScopeLock ClassDigestsScopeLock(ClassDigests.Lock);
	for (FName ClassName : PackageData.ImportedClasses)
	{
		UE::EditorDomain::FClassDigestData* ExistingData = ClassDigests.Map.Find(ClassName);
		if (!ExistingData)
		{
			// All allowlisted classes are added to ClassDigests at startup, so if the class is not in ClassDigests,
			// it is not allowlisted
			return false;
		}
		if (!ExistingData->bTargetIterativeEnabled)
		{
			return false;
		}
	}
	return true;
}

} // namespace UE::TargetDomain

