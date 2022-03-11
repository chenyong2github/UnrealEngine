// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundAssetSubsystem.h"

#include "Algo/Sort.h"
#include "Algo/Transform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Engine/AssetManager.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundSettings.h"
#include "MetasoundSource.h"
#include "MetasoundTrace.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/NoExportTypes.h"


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

			// These values are optional and not necessary to return successfully as MetaSounds
			// don't require inputs or outputs for asset tags to be valid (ex. a new MetaSound,
			// non-source asset has no inputs or outputs)
			OutInfo.InputTypes.Reset();
			ParseTypesString(AssetTags::RegistryInputTypes, OutInfo.InputTypes);

			OutInfo.OutputTypes.Reset();
			ParseTypesString(AssetTags::RegistryOutputTypes, OutInfo.OutputTypes);
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

	const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
	if (ensureAlways(Settings))
	{
		SearchAndIterateDirectoryAssets(Settings->DirectoriesToRegister, [this](const FAssetData& AssetData)
		{
			AddOrUpdateAsset(AssetData);
		});
	}
}

void UMetaSoundAssetSubsystem::AddAssetReferences(FMetasoundAssetBase& InAssetBase)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FNodeClassInfo AssetClassInfo = InAssetBase.GetAssetClassInfo();
	const FNodeRegistryKey AssetClassKey = NodeRegistryKey::CreateKey(AssetClassInfo);

	if (!ContainsKey(AssetClassKey))
	{
		AddOrUpdateAsset(*InAssetBase.GetOwningAsset());
		UE_LOG(LogMetaSound, Verbose, TEXT("Adding asset '%s' to MetaSoundAsset registry."), *InAssetBase.GetOwningAssetName());
	}

	bool bLoadFromPathCache = false;
	const TSet<FString>& ReferencedAssetClassKeys = InAssetBase.GetReferencedAssetClassKeys();
	for (const FString& ReferencedAssetClassKey : ReferencedAssetClassKeys)
	{
		if (!ContainsKey(ReferencedAssetClassKey))
		{
			UE_LOG(LogMetaSound, Verbose, TEXT("Missing referenced class '%s' asset entry."), *ReferencedAssetClassKey);
			bLoadFromPathCache = true;
		}
	}

	// All keys are loaded
	if (!bLoadFromPathCache)
	{
		return;
	}

	UE_LOG(LogMetaSound, Verbose, TEXT("Attempting preemptive reference load..."));

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
				UE_LOG(LogMetaSound, Verbose,
					TEXT("Preemptive load of class '%s' from hint path '%s' due to early "
						"registration request (asset scan likely not complete)."),
					*ClassKey,
					*AssetClassPath.ToString());

				UObject* MetaSoundObject = MetaSoundAsset->GetOwningAsset();
				if (ensureAlways(MetaSoundObject))
				{
					AddOrUpdateAsset(*MetaSoundObject);
				}
			}
		}
	}
}

Metasound::Frontend::FNodeRegistryKey UMetaSoundAssetSubsystem::AddOrUpdateAsset(const UObject& InObject)
{
	using namespace Metasound;
	using namespace Metasound::AssetSubsystemPrivate;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::AddOrUpdateAsset);

	const FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InObject);
	check(MetaSoundAsset);

	FNodeClassInfo ClassInfo = MetaSoundAsset->GetAssetClassInfo();
	const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(ClassInfo);

	if (NodeRegistryKey::IsValid(RegistryKey))
	{
		PathMap.FindOrAdd(RegistryKey) = InObject.GetPathName();
	}

	return RegistryKey;
}

Metasound::Frontend::FNodeRegistryKey UMetaSoundAssetSubsystem::AddOrUpdateAsset(const FAssetData& InAssetData)
{
	using namespace Metasound;
	using namespace Metasound::AssetSubsystemPrivate;
	using namespace Metasound::Frontend;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundAssetSubsystem::AddOrUpdateAsset);

	FNodeClassInfo ClassInfo;
	bool bClassInfoFound = GetAssetClassInfo(InAssetData, ClassInfo);
	if (!bClassInfoFound)
	{
		UObject* Object = nullptr;

		FSoftObjectPath Path(InAssetData.ObjectPath);
		if (InAssetData.IsAssetLoaded())
		{
			Object = Path.ResolveObject();
			UE_LOG(LogMetaSound, Verbose, TEXT("Adding loaded asset '%s' to MetaSoundAsset registry."), *Object->GetName());
		}
		else
		{
			Object = Path.TryLoad();
			UE_LOG(LogMetaSound, Verbose, TEXT("Loaded asset '%s' and adding to MetaSoundAsset registry."), *Object->GetName());
		}

		if (Object)
		{
			return AddOrUpdateAsset(*Object);
		}
	}

	if (ClassInfo.AssetClassID.IsValid())
	{
		const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(ClassInfo);
		if (NodeRegistryKey::IsValid(RegistryKey))
		{
			PathMap.FindOrAdd(RegistryKey) = InAssetData.ObjectPath;
		}

		return RegistryKey;
	}

	// Invalid ClassID means the node could not be registered.
	// Let caller report or ensure as necessary.
	return NodeRegistryKey::GetInvalid();
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

	for (const FMetasoundFrontendClassName& ClassName : Settings->AutoUpdateDenylist)
	{
		AutoUpdateDenyListCache.Add(ClassName.GetFullName());
	}

	for (const FDefaultMetaSoundAssetAutoUpdateSettings& UpdateSettings : Settings->AutoUpdateAssetDenylist)
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

void UMetaSoundAssetSubsystem::RemoveAsset(const UObject& InObject)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	if (const FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(&InObject))
	{
		const FNodeClassInfo ClassInfo = MetaSoundAsset->GetAssetClassInfo();
		FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ClassInfo);
		PathMap.Remove(RegistryKey);
	}
}

void UMetaSoundAssetSubsystem::RemoveAsset(const FAssetData& InAssetData)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FNodeClassInfo ClassInfo;
	if (ensureAlways(AssetSubsystemPrivate::GetAssetClassInfo(InAssetData, ClassInfo)))
	{
		FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(ClassInfo);
		PathMap.Remove(RegistryKey);
	}
}

void UMetaSoundAssetSubsystem::RenameAsset(const FAssetData& InAssetData, bool bInReregisterWithFrontend)
{
	auto PerformRename = [this, &InAssetData]()
	{
		RemoveAsset(InAssetData);
		ResetAssetClassDisplayName(InAssetData);
		AddOrUpdateAsset(InAssetData);
	};

	if (bInReregisterWithFrontend)
	{
		FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(InAssetData.GetAsset());
		check(MetaSoundAsset);

		MetaSoundAsset->UnregisterGraphWithFrontend();
		PerformRename();
		MetaSoundAsset->RegisterGraphWithFrontend();
	}
	else
	{
		PerformRename();
	}
}

void UMetaSoundAssetSubsystem::ResetAssetClassDisplayName(const FAssetData& InAssetData)
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
	FMetasoundFrontendGraphClass& Class = MetaSoundAsset->GetDocumentChecked().RootGraph;

#if WITH_EDITOR
	Class.Metadata.SetDisplayName(FText());
#endif // WITH_EDITOR
}

void UMetaSoundAssetSubsystem::SearchAndIterateDirectoryAssets(const TArray<FDirectoryPath>& InDirectories, TFunctionRef<void(const FAssetData&)> InFunction)
{
	if (InDirectories.IsEmpty())
	{
		return;
	}

	UAssetManager& AssetManager = UAssetManager::Get();

	FAssetManagerSearchRules Rules;
	for (const FDirectoryPath& Path : InDirectories)
	{
		Rules.AssetScanPaths.Add(*Path.Path);
	}

	Metasound::IMetasoundUObjectRegistry::Get().IterateRegisteredUClasses([&](UClass& RegisteredClass)
	{
		Rules.AssetBaseClass = &RegisteredClass;
		TArray<FAssetData> MetaSoundAssets;
		AssetManager.SearchAssetRegistryPaths(MetaSoundAssets, Rules);
		for (const FAssetData& AssetData : MetaSoundAssets)
		{
			InFunction(AssetData);
		}
	});
}

void UMetaSoundAssetSubsystem::RegisterAssetClassesInDirectories(const TArray<FMetaSoundAssetDirectory>& InDirectories)
{
	TArray<FDirectoryPath> Directories;
	Algo::Transform(InDirectories, Directories, [](const FMetaSoundAssetDirectory& AssetDir) { return AssetDir.Directory; });

	SearchAndIterateDirectoryAssets(Directories, [this](const FAssetData& AssetData)
	{
		AddOrUpdateAsset(AssetData);
		FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(AssetData.GetAsset());
		check(MetaSoundAsset);

		Metasound::Frontend::FMetaSoundAssetRegistrationOptions RegOptions;
		if (const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>())
		{
			RegOptions.bAutoUpdateLogWarningOnDroppedConnection = Settings->bAutoUpdateLogWarningOnDroppedConnection;
		}
		MetaSoundAsset->RegisterGraphWithFrontend(RegOptions);
	});
}

void UMetaSoundAssetSubsystem::UnregisterAssetClassesInDirectories(const TArray<FMetaSoundAssetDirectory>& InDirectories)
{
	TArray<FDirectoryPath> Directories;
	Algo::Transform(InDirectories, Directories, [](const FMetaSoundAssetDirectory& AssetDir) { return AssetDir.Directory; });

	SearchAndIterateDirectoryAssets(Directories, [this](const FAssetData& AssetData)
	{
		using namespace Metasound;
		using namespace Metasound::Frontend;

		if (AssetData.IsAssetLoaded())
		{
			FMetasoundAssetBase* MetaSoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(AssetData.GetAsset());
			check(MetaSoundAsset);
			MetaSoundAsset->UnregisterGraphWithFrontend();

			RemoveAsset(AssetData);
		}
		else
		{
			FNodeClassInfo AssetClassInfo;
			if (ensureAlways(AssetSubsystemPrivate::GetAssetClassInfo(AssetData, AssetClassInfo)))
			{
				const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(AssetClassInfo);
				const bool bIsRegistered = FMetasoundFrontendRegistryContainer::Get()->IsNodeRegistered(RegistryKey);
				if (bIsRegistered)
				{
					FMetasoundFrontendRegistryContainer::Get()->UnregisterNode(RegistryKey);
					PathMap.Remove(RegistryKey);
				}
			}
		}
	});
}
