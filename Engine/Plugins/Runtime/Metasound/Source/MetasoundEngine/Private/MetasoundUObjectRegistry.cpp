// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundUObjectRegistry.h"

#include "Algo/Copy.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "UObject/Object.h"

namespace Metasound
{
	class FMetasoundUObjectRegistry : public IMetasoundUObjectRegistry
	{
		public:
			void RegisterUClassArchetype(TUniquePtr<IMetasoundUObjectRegistryEntry>&& InEntry) override
			{
				if (InEntry.IsValid())
				{
					FName ArchetypeName = InEntry->GetArchetypeName();

					EntriesByArchetype.Add(ArchetypeName, InEntry.Get());
					Entries.Add(InEntry.Get());
					Storage.Add(MoveTemp(InEntry));
				}
			}

			TArray<UClass*> GetUClassesForArchetype(const FName& InArchetypeName) const override
			{
				TArray<UClass*> Classes;

				TArray<const IMetasoundUObjectRegistryEntry*> EntriesForArchetype;
				EntriesByArchetype.MultiFind(InArchetypeName, EntriesForArchetype);

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

			UObject* NewObject(UClass* InClass, const FMetasoundFrontendDocument& InDocument, const FMetasoundFrontendArchetype& InArchetype, const FString& InPath) const override
			{
				auto IsChildClassOfRegisteredClass = [&](const IMetasoundUObjectRegistryEntry* Entry)
				{
					return Entry->IsChildClass(InClass);
				};

				TArray<const IMetasoundUObjectRegistryEntry*> EntriesForClass = FindEntriesByPredicate(IsChildClassOfRegisteredClass);

				for (const IMetasoundUObjectRegistryEntry* Entry : EntriesForClass)
				{
					if (Entry->GetArchetypeName() == InArchetype.Name)
					{
						return NewObject(*Entry, InDocument, InPath);
					}
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

				UObject* NewMetasoundObject = InEntry.NewObject(PackageToSaveTo, *InDocument.RootGraph.Metadata.ClassName.GetFullName().ToString());
				FMetasoundAssetBase* NewAssetBase = InEntry.Cast(NewMetasoundObject);
				if (ensure(nullptr != NewAssetBase))
				{
					NewAssetBase->SetDocument(InDocument);

					const FMetasoundFrontendArchetype& Archetype = NewAssetBase->GetArchetype();
					if (ensure(NewAssetBase->IsArchetypeSupported(Archetype)))
					{
						NewAssetBase->ConformDocumentToArchetype();
					}
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
			TMultiMap<FName, const IMetasoundUObjectRegistryEntry*> EntriesByArchetype;
			TArray<const IMetasoundUObjectRegistryEntry*> Entries;
	};

	IMetasoundUObjectRegistry& IMetasoundUObjectRegistry::Get()
	{
		static FMetasoundUObjectRegistry Registry;
		return Registry;
	}
}
