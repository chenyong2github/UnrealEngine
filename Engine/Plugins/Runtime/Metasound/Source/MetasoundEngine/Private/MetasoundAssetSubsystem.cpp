// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundAssetSubsystem.h"

#include "Algo/Transform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Engine/AssetManager.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundSettings.h"
#include "MetasoundSource.h"
#include "MetasoundTrace.h"


namespace Metasound
{
	namespace AssetSubsystemPrivate
	{
		bool GetAssetClassInfo(const FAssetData& InAssetData, Frontend::FNodeClassInfo& OutInfo)
		{
			using namespace Metasound;
			using namespace Metasound::Frontend;

			bool bSuccess = true;

			OutInfo.Type = EMetasoundFrontendClassType::External;
			OutInfo.AssetPath = InAssetData.ObjectPath;

			FString AssetClassID;
			bSuccess &= InAssetData.GetTagValue(AssetTags::AssetClassID, AssetClassID);
			OutInfo.AssetClassID = FGuid(AssetClassID);
			OutInfo.ClassName = FMetasoundFrontendClassName(FName(), *AssetClassID, FName());

			int32 RegistryVersionMajor = 0;
			bSuccess &= InAssetData.GetTagValue(AssetTags::RegistryVersionMajor, RegistryVersionMajor);
			OutInfo.Version.Major = RegistryVersionMajor;

			int32 RegistryVersionMinor = 0;
			bSuccess &= InAssetData.GetTagValue(AssetTags::RegistryVersionMinor, RegistryVersionMinor);
			OutInfo.Version.Minor = RegistryVersionMinor;

#if WITH_EDITORONLY_DATA
			auto ParseTypesString = [&](const FName AssetTag, TSet<FName>& OutTypes)
			{
				FString TypesString;
				if (InAssetData.GetTagValue(AssetTag, TypesString))
				{
					TArray<FString> DataTypeStrings;
					TypesString.ParseIntoArray(DataTypeStrings, *AssetTags::ArrayDelim);
					Algo::Transform(DataTypeStrings, OutTypes, [](const FString& DataType) { return *DataType; });
					return true;
				}

				return false;
			};

			OutInfo.InputTypes.Reset();
			bSuccess &= ParseTypesString(AssetTags::RegistryInputTypes, OutInfo.InputTypes);

			OutInfo.OutputTypes.Reset();
			bSuccess &= ParseTypesString(AssetTags::RegistryOutputTypes, OutInfo.OutputTypes);
#endif // WITH_EDITORONLY_DATA

			return bSuccess;
		}
	}
}

void UMetaSoundAssetSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	using namespace Metasound::Frontend;

	IMetaSoundAssetManager::Set(*this);
	FCoreDelegates::OnPostEngineInit.AddUObject(this, &UMetaSoundAssetSubsystem::PostEngineInit);
}

void UMetaSoundAssetSubsystem::PostEngineInit()
{
	if (UAssetManager* AssetManager = UAssetManager::GetIfValid())
	{
		AssetManager->CallOrRegister_OnCompletedInitialScan(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UMetaSoundAssetSubsystem::PostInitAssetScan));
		RebuildDenyListCache(*AssetManager);
	}
	else
	{
		UE_LOG(LogMetaSound, Error, TEXT("Cannot initialize MetaSoundAssetSubsystem: Enable AssetManager or disable MetaSound plugin"));
	}
}

void UMetaSoundAssetSubsystem::PostInitAssetScan()
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::PostInitAssetScan);

	UAssetManager& AssetManager = UAssetManager::Get();

	FAssetManagerSearchRules Rules;
	Rules.AssetScanPaths.Add(TEXT("/Game"));

	Rules.AssetBaseClass = UMetaSound::StaticClass();
	TArray<FAssetData> MetaSoundAssets;
	AssetManager.SearchAssetRegistryPaths(MetaSoundAssets, Rules);
	for (const FAssetData& AssetData : MetaSoundAssets)
	{
		AddOrUpdateAsset(AssetData, false /* bRegisterWithFrontend */);
	}

	Rules.AssetBaseClass = UMetaSoundSource::StaticClass();
	TArray<FAssetData> MetaSoundSourceAssets;
	AssetManager.SearchAssetRegistryPaths(MetaSoundSourceAssets, Rules);
	for (const FAssetData& AssetData : MetaSoundSourceAssets)
	{
		AddOrUpdateAsset(AssetData, false /* bRegisterWithFrontend */);
	}
}

void UMetaSoundAssetSubsystem::AddAssetReferences(const FMetasoundAssetBase& InAssetBase)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	bool bLoadFromPathCache = false;
	const TSet<FString>& AssetClassKeys = InAssetBase.GetReferencedAssetClassKeys();
	for (const FString& AssetClassKey : AssetClassKeys)
	{
		if (!ContainsKey(AssetClassKey))
		{
			UE_LOG(LogMetaSound, Log, TEXT("Missing referenced class '%s' asset entry."), *AssetClassKey);
			bLoadFromPathCache = true;
		}
	}

	// All keys are loaded
	if (!bLoadFromPathCache)
	{
		return;
	}

	UE_LOG(LogMetaSound, Log, TEXT("Attempting preemptive reference load..."));

	// If keys are not loaded, iterate class cache paths to prime asset manager with
	// hint paths as the registration is getting called before asset manager has 
	// completed initial scan.
	const TSet<FSoftObjectPath>& AssetClassCache = InAssetBase.GetReferencedAssetClassCache();
	for (const FSoftObjectPath& AssetClassPath : AssetClassCache)
	{
		if (FMetasoundAssetBase* MetaSoundAsset = TryLoadAsset(AssetClassPath))
		{
			FNodeClassInfo ClassInfo = MetaSoundAsset->GetAssetClassInfo();
			const FNodeRegistryKey ClassKey = NodeRegistryKey::CreateKey(ClassInfo);
			if (!ContainsKey(ClassKey))
			{
				UE_LOG(LogMetaSound, Log,
					TEXT("Preemptive load of class '%s' from hint path '%s' due to early "
						"registration request (asset scan likely not complete)."),
					*ClassKey,
					*AssetClassPath.ToString());
				constexpr bool bRegisterWithFrontend = false;

				UObject* MetaSoundObject = MetaSoundAsset->GetOwningAsset();
				if (ensureAlways(MetaSoundObject))
				{
					AddOrUpdateAsset(*MetaSoundObject, bRegisterWithFrontend);
				}
			}
		}
	}
}

void UMetaSoundAssetSubsystem::AddOrUpdateAsset(UObject& InObject, bool bInRegisterWithFrontend)
{
	using namespace Metasound;
	using namespace Metasound::AssetSubsystemPrivate;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::AddOrUpdateAsset);

	FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InObject);
	check(MetaSoundAsset);

	if (bInRegisterWithFrontend)
	{
		MetaSoundAsset->RegisterGraphWithFrontend();
	}

	FNodeClassInfo ClassInfo = MetaSoundAsset->GetAssetClassInfo();
	const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(ClassInfo);
	PathMap.FindOrAdd(RegistryKey) = InObject.GetPathName();
}

void UMetaSoundAssetSubsystem::AddOrUpdateAsset(const FAssetData& InAssetData, bool bInRegisterWithFrontend)
{
	using namespace Metasound;
	using namespace Metasound::AssetSubsystemPrivate;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::AddOrUpdateAsset);

	FNodeClassInfo ClassInfo;
	bool bClassInfoFound = GetAssetClassInfo(InAssetData, ClassInfo);
	if (!bClassInfoFound || bInRegisterWithFrontend)
	{
		UObject* Object = nullptr;

		FSoftObjectPath Path(InAssetData.ObjectPath);
		if (InAssetData.IsAssetLoaded())
		{
			Object = Path.ResolveObject();
		}
		else
		{
			if (!bInRegisterWithFrontend)
			{
				UE_LOG(LogMetaSound, Warning,
					TEXT("Failed to find serialized MetaSound asset registry data for asset '%s'. "
						"Forcing synchronous load which increases load times. Re-save asset to avoid this."),
					*InAssetData.ObjectPath.ToString());
			}
			Object = Path.TryLoad();
		}

		if (Object)
		{
			AddOrUpdateAsset(*Object, bInRegisterWithFrontend);
			return;
		}
	}

	const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(ClassInfo);
	PathMap.FindOrAdd(RegistryKey) = InAssetData.ObjectPath;
}

bool UMetaSoundAssetSubsystem::CanAutoUpdate(const FMetasoundFrontendClassName& InClassName) const
{
	const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
	if (!Settings->bAutoUpdateEnabled)
	{
		return false;
	}

	return !AutoUpdateDenyListCache.Contains(InClassName.GetFullName());
}

bool UMetaSoundAssetSubsystem::ContainsKey(const Metasound::Frontend::FNodeRegistryKey& InRegistryKey) const
{
	return PathMap.Contains(InRegistryKey);
}

void UMetaSoundAssetSubsystem::RebuildDenyListCache(const UAssetManager& InAssetManager)
{
	using namespace Metasound::Frontend;

	const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
	if (Settings->DenyListCacheChangeID == AutoUpdateDenyListChangeID)
	{
		return;
	}

	AutoUpdateDenyListCache.Reset();

	for (const FMetasoundFrontendClassName& ClassName : Settings->AutoUpdateBlacklist)
	{
		AutoUpdateDenyListCache.Add(ClassName.GetFullName());
	}

	for (const FDefaultMetaSoundAssetAutoUpdateSettings& UpdateSettings : Settings->AutoUpdateAssetBlacklist)
	{
		if (UAssetManager* AssetManager = UAssetManager::GetIfValid())
		{
			FAssetData AssetData;
			if (AssetManager->GetAssetDataForPath(UpdateSettings.MetaSound, AssetData))
			{
				FString AssetClassID;
				if (AssetData.GetTagValue(AssetTags::AssetClassID, AssetClassID))
				{
					const FMetasoundFrontendClassName ClassName = { FName(), *AssetClassID, FName() };
					AutoUpdateDenyListCache.Add(ClassName.GetFullName());
				}
			}
		}
	}

	AutoUpdateDenyListChangeID = Settings->DenyListCacheChangeID;
}

TSet<Metasound::Frontend::FNodeRegistryKey> UMetaSoundAssetSubsystem::GetReferencedKeys(const FMetasoundAssetBase& InAssetBase) const
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::GetReferencedKeys);
	using namespace Metasound::Frontend;

	TSet<FNodeRegistryKey> OutKeys;
	const FMetasoundFrontendDocument& Document = InAssetBase.GetDocumentChecked();
	for (const FMetasoundFrontendClass& Class : Document.Dependencies)
	{
		const FNodeRegistryKey Key = NodeRegistryKey::CreateKey(Class.Metadata);
		if (ContainsKey(Key))
		{
			OutKeys.Add(Key);
		}
	}
	return OutKeys;
}

void UMetaSoundAssetSubsystem::RescanAutoUpdateDenyList()
{
	if (const UAssetManager* AssetManager = UAssetManager::GetIfValid())
	{
		RebuildDenyListCache(*AssetManager);
	}
}

FMetasoundAssetBase* UMetaSoundAssetSubsystem::TryLoadAssetFromKey(const Metasound::Frontend::FNodeRegistryKey& RegistryKey) const
{
	if (const FSoftObjectPath* ObjectPath = FindObjectPathFromKey(RegistryKey))
	{
		return TryLoadAsset(*ObjectPath);
	}

	return nullptr;
}

bool UMetaSoundAssetSubsystem::TryLoadReferencedAssets(const FMetasoundAssetBase& InAssetBase, TArray<FMetasoundAssetBase*>& OutReferencedAssets) const
{
	using namespace Metasound::Frontend;

	bool bSucceeded = true;
	OutReferencedAssets.Reset();

	TArray<FMetasoundAssetBase*> ReferencedAssets;
	const TSet<FString>& AssetClassKeys = InAssetBase.GetReferencedAssetClassKeys();
	for (const FNodeRegistryKey& Key : AssetClassKeys)
	{
		if (FMetasoundAssetBase* MetaSound = TryLoadAssetFromKey(Key))
		{
			OutReferencedAssets.Add(MetaSound);
		}
		else
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to find referenced MetaSound asset with key '%s'"), *Key);
			bSucceeded = false;
		}
	}

	return bSucceeded;
}

const FSoftObjectPath* UMetaSoundAssetSubsystem::FindObjectPathFromKey(const Metasound::Frontend::FNodeRegistryKey& InRegistryKey) const
{
	return PathMap.Find(InRegistryKey);
}

FMetasoundAssetBase* UMetaSoundAssetSubsystem::TryLoadAsset(const FSoftObjectPath& InObjectPath) const
{
	return Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(InObjectPath.TryLoad());
}

void UMetaSoundAssetSubsystem::RemoveAsset(UObject& InObject, bool bInUnregisterWithFrontend)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	if (FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InObject))
	{
		const FNodeClassInfo ClassInfo = MetaSoundAsset->GetAssetClassInfo();
		FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ClassInfo);
		PathMap.Remove(RegistryKey);

		if (bInUnregisterWithFrontend)
		{
			MetaSoundAsset->UnregisterGraphWithFrontend();
		}
	}
}

void UMetaSoundAssetSubsystem::RemoveAsset(const FAssetData& InAssetData, bool bInUnregisterWithFrontend)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FNodeClassInfo ClassInfo;
	if (ensureAlways(AssetSubsystemPrivate::GetAssetClassInfo(InAssetData, ClassInfo)))
	{
		FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ClassInfo);
		PathMap.Remove(RegistryKey);

		UObject* Object = InAssetData.GetAsset();
		if (Object && bInUnregisterWithFrontend)
		{
			FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
			check(MetaSoundAsset);

			MetaSoundAsset->UnregisterGraphWithFrontend();
		}
	}
}

void UMetaSoundAssetSubsystem::RenameAsset(const FAssetData& InAssetData, bool bInReregisterWithFrontend)
{
	RemoveAsset(InAssetData, bInReregisterWithFrontend);
	SynchronizeAssetClassDisplayName(InAssetData);
	AddOrUpdateAsset(InAssetData, bInReregisterWithFrontend);
}

void UMetaSoundAssetSubsystem::SynchronizeAssetClassDisplayName(const FAssetData& InAssetData)
{
	UObject* Object = nullptr;
	FSoftObjectPath Path(InAssetData.ObjectPath);
	if (InAssetData.IsAssetLoaded())
	{
		Object = Path.ResolveObject();
	}
	else
	{
		Object = Path.TryLoad();
	}

	FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
	check(MetaSoundAsset);
	Metasound::Frontend::FSynchronizeAssetClassDisplayName(InAssetData.AssetName).Transform(MetaSoundAsset->GetDocumentHandle());
}
