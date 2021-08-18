// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundUObjectRegistry.h"

#include "Algo/Copy.h"
#include "AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "Engine/AssetManager.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundSettings.h"
#include "MetasoundSource.h"
#include "MetasoundTrace.h"
#include "UObject/Object.h"


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
			auto ParseTypesString = [&](const FName AssetTag, TArray<FName>& OutTypes)
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
	IMetaSoundAssetManager::Set(*this);
	FCoreDelegates::OnPostEngineInit.AddUObject(this, &UMetaSoundAssetSubsystem::PostEngineInit);
}

void UMetaSoundAssetSubsystem::PostEngineInit()
{
	if (UAssetManager* AssetManager = UAssetManager::GetIfValid())
	{
		AssetManager->CallOrRegister_OnCompletedInitialScan(FSimpleMulticastDelegate::FDelegate::CreateUObject(this, &UMetaSoundAssetSubsystem::PostInitAssetScan));
		RebuildBlacklistCache(*AssetManager);
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

	return !AutoUpdateBlacklistCache.Contains(InClassName.GetFullName());
}

void UMetaSoundAssetSubsystem::RebuildBlacklistCache(const UAssetManager& InAssetManager)
{
	const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
	if (Settings->BlacklistCacheChangeID == AutoUpdateBlacklistChangeID)
	{
		return;
	}

	AutoUpdateBlacklistCache.Reset();

	for (const FMetasoundFrontendClassName& ClassName : Settings->AutoUpdateBlacklist)
	{
		AutoUpdateBlacklistCache.Add(ClassName.GetFullName());
	}

	for (const FDefaultMetaSoundAssetAutoUpdateSettings& UpdateSettings : Settings->AutoUpdateAssetBlacklist)
	{
		if (UAssetManager* AssetManager = UAssetManager::GetIfValid())
		{
			FAssetData AssetData;
			if (AssetManager->GetAssetDataForPath(UpdateSettings.MetaSound, AssetData))
			{
				FString AssetClassID;
				if (AssetData.GetTagValue(Metasound::AssetTags::AssetClassID, AssetClassID))
				{
					const FMetasoundFrontendClassName ClassName = { FName(), *AssetClassID, FName() };
					AutoUpdateBlacklistCache.Add(ClassName.GetFullName());
				}
			}
		}
	}

	AutoUpdateBlacklistChangeID = Settings->BlacklistCacheChangeID;
}

void UMetaSoundAssetSubsystem::RescanAutoUpdateBlacklist()
{
	if (const UAssetManager* AssetManager = UAssetManager::GetIfValid())
	{
		RebuildBlacklistCache(*AssetManager);
	}
}

FMetasoundAssetBase* UMetaSoundAssetSubsystem::FindAssetFromKey(const Metasound::Frontend::FNodeRegistryKey& RegistryKey) const
{
	if (const FSoftObjectPath* ObjectPath = FindObjectPathFromKey(RegistryKey))
	{
		return TryLoadAsset(*ObjectPath);
	}

	return nullptr;
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

namespace Metasound
{

	class FMetasoundUObjectRegistry : public IMetasoundUObjectRegistry
	{
		public:
			void RegisterUClassArchetype(TUniquePtr<IMetasoundUObjectRegistryEntry>&& InEntry) override
			{
				if (InEntry.IsValid())
				{
					Frontend::FArchetypeRegistryKey Key = Frontend::GetArchetypeRegistryKey(InEntry->GetArchetypeVersion());

					EntriesByArchetype.Add(Key, InEntry.Get());
					Entries.Add(InEntry.Get());
					Storage.Add(MoveTemp(InEntry));
				}
			}

			TArray<UClass*> GetUClassesForArchetype(const FMetasoundFrontendVersion& InArchetypeVersion) const override
			{
				TArray<UClass*> Classes;

				TArray<const IMetasoundUObjectRegistryEntry*> EntriesForArchetype;
				EntriesByArchetype.MultiFind(Frontend::GetArchetypeRegistryKey(InArchetypeVersion), EntriesForArchetype);

				for (const IMetasoundUObjectRegistryEntry* Entry : EntriesForArchetype)
				{
					if (nullptr != Entry)
					{
						if (UClass* Class = Entry->GetUClass())
						{
							Classes.Add(Class);
						}
					}
				}

				return Classes;
			}

			UObject* NewObject(UClass* InClass, const FMetasoundFrontendDocument& InDocument, const FString& InPath) const override
			{
				TArray<const IMetasoundUObjectRegistryEntry*> EntriesForArchetype;
				EntriesByArchetype.MultiFind(Frontend::GetArchetypeRegistryKey(InDocument.ArchetypeVersion), EntriesForArchetype);

				auto IsChildClassOfRegisteredClass = [&](const IMetasoundUObjectRegistryEntry* Entry)
				{
					return Entry->IsChildClass(InClass);
				};

			
				const IMetasoundUObjectRegistryEntry* const* EntryForClass = EntriesForArchetype.FindByPredicate(IsChildClassOfRegisteredClass);

				if (nullptr != EntryForClass)
				{
					return NewObject(**EntryForClass, InDocument, InPath);
				}

				return nullptr;
			}

			bool IsRegisteredClass(UObject* InObject) const override
			{
				return (nullptr != GetEntryByUObject(InObject));
			}

			FMetasoundAssetBase* GetObjectAsAssetBase(UObject* InObject) const override
			{
				if (const IMetasoundUObjectRegistryEntry* Entry = GetEntryByUObject(InObject))
				{
					return Entry->Cast(InObject);
				}
				return nullptr;
			}

			const FMetasoundAssetBase* GetObjectAsAssetBase(const UObject* InObject) const override
			{
				if (const IMetasoundUObjectRegistryEntry* Entry = GetEntryByUObject(InObject))
				{
					return Entry->Cast(InObject);
				}
				return nullptr;
			}

		private:
			UObject* NewObject(const IMetasoundUObjectRegistryEntry& InEntry, const FMetasoundFrontendDocument& InDocument, const FString& InPath) const
			{
				UPackage* PackageToSaveTo = nullptr;

				if (GIsEditor)
				{
					FText InvalidPathReason;
					bool const bValidPackageName = FPackageName::IsValidLongPackageName(InPath, false, &InvalidPathReason);

					if (!ensureAlwaysMsgf(bValidPackageName, TEXT("Tried to generate a Metasound UObject with an invalid package path/name Falling back to transient package, which means we won't be able to save this asset.")))
					{
						PackageToSaveTo = GetTransientPackage();
					}
					else
					{
						PackageToSaveTo = CreatePackage(*InPath);
					}
				}
				else
				{
					PackageToSaveTo = GetTransientPackage();
				}

				UObject* NewMetasoundObject = InEntry.NewObject(PackageToSaveTo, *InDocument.RootGraph.Metadata.GetClassName().GetFullName().ToString());
				FMetasoundAssetBase* NewAssetBase = InEntry.Cast(NewMetasoundObject);
				if (ensure(nullptr != NewAssetBase))
				{
					NewAssetBase->SetDocument(InDocument);
					NewAssetBase->ConformDocumentToArchetype();
				}

#if WITH_EDITOR
				AsyncTask(ENamedThreads::GameThread, [NewMetasoundObject]()
				{
					FAssetRegistryModule::AssetCreated(NewMetasoundObject);
					NewMetasoundObject->MarkPackageDirty();
					// todo: how do you get the package for a uobject and save it? I forget
				});
#endif

				return NewMetasoundObject;
			}

			const IMetasoundUObjectRegistryEntry* FindEntryByPredicate(TFunction<bool (const IMetasoundUObjectRegistryEntry*)> InPredicate) const
			{
				const IMetasoundUObjectRegistryEntry* const* Entry = Entries.FindByPredicate(InPredicate);

				if (nullptr == Entry)
				{
					return nullptr;
				}

				return *Entry;
			}

			TArray<const IMetasoundUObjectRegistryEntry*> FindEntriesByPredicate(TFunction<bool (const IMetasoundUObjectRegistryEntry*)> InPredicate) const
			{
				TArray<const IMetasoundUObjectRegistryEntry*> FoundEntries;

				Algo::CopyIf(Entries, FoundEntries, InPredicate);

				return FoundEntries;
			}

			const IMetasoundUObjectRegistryEntry* GetEntryByUObject(const UObject* InObject) const
			{
				auto IsChildClassOfRegisteredClass = [&](const IMetasoundUObjectRegistryEntry* Entry)
				{
					if (nullptr == Entry)
					{
						return false;
					}

					return Entry->IsChildClass(InObject);
				};

				return FindEntryByPredicate(IsChildClassOfRegisteredClass);
			}

			TArray<TUniquePtr<IMetasoundUObjectRegistryEntry>> Storage;
			TMultiMap<Frontend::FArchetypeRegistryKey, const IMetasoundUObjectRegistryEntry*> EntriesByArchetype;
			TArray<const IMetasoundUObjectRegistryEntry*> Entries;
	};

	IMetasoundUObjectRegistry& IMetasoundUObjectRegistry::Get()
	{
		static FMetasoundUObjectRegistry Registry;
		return Registry;
	}
}
