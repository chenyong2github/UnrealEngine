// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendSearchEngine.h"

#include "Algo/Transform.h"
#include "HAL/IConsoleManager.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistryTransaction.h"
#include "MetasoundTrace.h"
#include "MetasoundAssetManager.h"

static int32 ClearMetaSoundFrontendSearchEngineCacheCVar = 0;
FAutoConsoleVariableRef CVarClearMetaSoundFrontendSearchEngineCache(
	TEXT("au.Debug.MetaSounds.SearchEngine.ClearCache"),
	ClearMetaSoundFrontendSearchEngineCacheCVar,
	TEXT("If true, flags the MetaSound registry SearchEngine cache to be cleared on subsequent query.\n")
	TEXT("0: Do not clear, !0: Flag to clear"),
	ECVF_Default);


namespace Metasound
{
	namespace Frontend
	{
		namespace SearchEngineQuerySteps
		{
			class FInterfaceRegistryTransactionSource : public IFrontendQuerySource
			{
			public:
				FInterfaceRegistryTransactionSource()
				: CurrentTransactionID(GetOriginRegistryTransactionID())
				{
				}

				void Stream(TArray<FFrontendQueryEntry>& OutEntries) override
				{
					auto AddEntry = [&OutEntries](const FInterfaceRegistryTransaction& InTransaction)
					{
						OutEntries.Emplace(FFrontendQueryEntry::FValue(TInPlaceType<FInterfaceRegistryTransaction>(), InTransaction));
					};
					
					IInterfaceRegistry::Get().ForEachRegistryTransactionSince(CurrentTransactionID, &CurrentTransactionID, AddEntry);
				}

				void Reset() override
				{
					CurrentTransactionID = GetOriginRegistryTransactionID();
				}

			private:
				FRegistryTransactionID CurrentTransactionID;
			};

			class FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys : public IFrontendQueryMapStep
			{
			public:
				FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FInterfaceRegistryKey RegistryKey;
					if (ensure(InEntry.Value.IsType<FInterfaceRegistryTransaction>()))
					{
						RegistryKey = InEntry.Value.Get<FInterfaceRegistryTransaction>().GetInterfaceRegistryKey();
					}
					return FFrontendQueryEntry::FKey{RegistryKey};
				}
			};

			class FReduceInterfaceRegistryTransactionsToCurrentStatus : public IFrontendQueryReduceStep
			{
			public:
				void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry* const>& InEntries, FReduceOutputView& OutResult) const override
				{
					int32 State = 0;
					FFrontendQueryEntry* FinalEntry = nullptr;

					for (FFrontendQueryEntry* Entry : InEntries)
					{
						if (ensure(Entry->Value.IsType<FInterfaceRegistryTransaction>()))
						{
							const FInterfaceRegistryTransaction& Transaction = Entry->Value.Get<FInterfaceRegistryTransaction>();
							
							switch (Transaction.GetTransactionType())
							{
								case FInterfaceRegistryTransaction::ETransactionType::InterfaceRegistration:
									State++;
									FinalEntry = Entry;
									break;

								case FInterfaceRegistryTransaction::ETransactionType::InterfaceUnregistration:
									State--;
									break;

								default:
									break;
							}
						}
					}

					if ((nullptr != FinalEntry) && (State > 0))
					{
						OutResult.Add(*FinalEntry);
					}
				}
			};

			class FReduceInterfaceRegistryTransactionsBySupportedClassName : public IFrontendQueryReduceStep
			{
				FName UClassName;

			public:
				FReduceInterfaceRegistryTransactionsBySupportedClassName(FName InUClassName)
					: UClassName(InUClassName)
				{
				}

				void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry* const>& InEntries, FReduceOutputView& OutResult) const override
				{
					for (FFrontendQueryEntry* Entry : InEntries)
					{
						if (ensure(Entry->Value.IsType<FInterfaceRegistryTransaction>()))
						{
							FInterfaceRegistryKey RegistryKey = Entry->Value.Get<FInterfaceRegistryTransaction>().GetInterfaceRegistryKey();
							const IInterfaceRegistryEntry* RegEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);
							if (ensure(RegEntry))
							{
								if (RegEntry->UClassIsSupported(UClassName))
								{
									OutResult.Add(*Entry);
								}
							}
						}
					}
				}
			};

			class FReduceInterfaceRegistryTransactionsByDefault : public IFrontendQueryReduceStep
			{
			public:
				void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry* const>& InEntries, FReduceOutputView& OutResult) const override
				{
					for (FFrontendQueryEntry* Entry : InEntries)
					{
						if (ensure(Entry->Value.IsType<FInterfaceRegistryTransaction>()))
						{
							FInterfaceRegistryKey RegistryKey = Entry->Value.Get<FInterfaceRegistryTransaction>().GetInterfaceRegistryKey();
							const IInterfaceRegistryEntry* RegEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);
							if (ensure(RegEntry))
							{
								if (RegEntry->IsDefault())
								{
									OutResult.Add(*Entry);
								}
							}
						}
					}
				}
			};

			class FTransformInterfaceRegistryTransactionToInterface : public IFrontendQueryTransformStep
			{
			public:
				virtual void Transform(FFrontendQueryEntry::FValue& InValue) const override
				{
					FMetasoundFrontendInterface Interface;
					if (ensure(InValue.IsType<FInterfaceRegistryTransaction>()))
					{
						FInterfaceRegistryKey RegistryKey = InValue.Get<FInterfaceRegistryTransaction>().GetInterfaceRegistryKey();
						IInterfaceRegistry::Get().FindInterface(RegistryKey, Interface);
					}
					InValue.Set<FMetasoundFrontendInterface>(MoveTemp(Interface));
				}
			};

			class FMapInterfaceToInterfaceNameAndMajorVersion : public IFrontendQueryMapStep
			{
			public:
				FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FString Key;
					if (ensure(InEntry.Value.IsType<FMetasoundFrontendInterface>()))
					{
						const FMetasoundFrontendInterface& Interface = InEntry.Value.Get<FMetasoundFrontendInterface>();
						Key = FString::Format(TEXT("{0}_{1}"), { Interface.Version.Name.ToString(), Interface.Version.Number.Major });
					}
					return FFrontendQueryEntry::FKey{Key};
				}
			};

			class FReduceInterfacesToHighestVersion : public IFrontendQueryReduceStep
			{
			public:
				void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry* const>& InEntries, FReduceOutputView& OutResult) const override
				{
					FFrontendQueryEntry* HighestVersionEntry = nullptr;
					FMetasoundFrontendVersionNumber HighestVersion = FMetasoundFrontendVersionNumber::GetInvalid();

					for (FFrontendQueryEntry* Entry : InEntries)
					{
						if (ensure(Entry->Value.IsType<FMetasoundFrontendInterface>()))
						{
							const FMetasoundFrontendVersionNumber& VersionNumber = Entry->Value.Get<FMetasoundFrontendInterface>().Version.Number;

							if (VersionNumber > HighestVersion)
							{
								HighestVersionEntry = Entry;
								HighestVersion = VersionNumber;
							}
						}
					}

					if (HighestVersionEntry)
					{
						OutResult.Add(*HighestVersionEntry);
					}
				}
			};

			class FFilterInterfaceRegistryTransactionsByName : public IFrontendQueryFilterStep
			{
			public:
				FFilterInterfaceRegistryTransactionsByName(FName InName)
				: Name(InName)
				{
				}

				bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (ensure(InEntry.Value.IsType<FInterfaceRegistryTransaction>()))
					{
						return Name == InEntry.Value.Get<FInterfaceRegistryTransaction>().GetInterfaceVersion().Name;
					}
					return false;
				}

			private:
				FName Name;
			};


			class FFilterNodeRegistrationEventsByClassName : public IFrontendQueryFilterStep
			{
			public:
				FFilterNodeRegistrationEventsByClassName(const FMetasoundFrontendClassName& InName)
				: Name(InName)
				{
				}

				bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (ensure(InEntry.Value.IsType<FNodeRegistryTransaction>()))
					{
						return Name == InEntry.Value.Get<FNodeRegistryTransaction>().GetNodeClassInfo().ClassName;
					}
					return false;
				}
			private:
				FMetasoundFrontendClassName Name;
			};

			class FFilterInterfaceRegistryTransactionsByNameAndMajorVersion : public IFrontendQueryFilterStep
			{
			public:
				FFilterInterfaceRegistryTransactionsByNameAndMajorVersion(const FName& InName, int32 InMajorVersion)
				: Name(InName)
				, MajorVersion(InMajorVersion)
				{
				}

				bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (ensure(InEntry.Value.IsType<FInterfaceRegistryTransaction>()))
					{
						const FMetasoundFrontendVersion& Version = InEntry.Value.Get<FInterfaceRegistryTransaction>().GetInterfaceVersion();

						return (Name == Version.Name) && (MajorVersion == Version.Number.Major);
					}
					return false;
				}

			private:
				FName Name;
				int32 MajorVersion;
			};

			class FMapInterfaceToInterfaceName : public IFrontendQueryMapStep
			{
			public:
				FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FString Key;
					if (ensure(InEntry.Value.IsType<FMetasoundFrontendInterface>()))
					{
						Key = InEntry.Value.Get<FMetasoundFrontendInterface>().Version.Name.ToString();
					}
					return FFrontendQueryEntry::FKey{Key};
				}
			};

			class FTransformInterfaceRegistryTransactionToInterfaceVersion : public IFrontendQueryTransformStep
			{
			public:
				virtual void Transform(FFrontendQueryEntry::FValue& InValue) const override
				{
					FMetasoundFrontendVersion Version;
					if (ensure(InValue.IsType<FInterfaceRegistryTransaction>()))
					{
						Version = InValue.Get<FInterfaceRegistryTransaction>().GetInterfaceVersion();
					}
					InValue.Set<FMetasoundFrontendVersion>(Version);
				}
			};
		}

		class FSearchEngine : public ISearchEngine
		{
				FSearchEngine(const FSearchEngine&) = delete;
				FSearchEngine(FSearchEngine&&) = delete;
				FSearchEngine& operator=(const FSearchEngine&) = delete;
				FSearchEngine& operator=(FSearchEngine&&) = delete;

			public:
				FSearchEngine() = default;
				virtual ~FSearchEngine() = default;

				virtual TArray<FMetasoundFrontendClass> FindAllClasses(bool bInIncludeDeprecated) override;
				virtual TArray<FMetasoundFrontendClass> FindClassesWithName(const FNodeClassName& InName, bool bInSortByVersion) override;
				virtual bool FindClassWithHighestVersion(const FNodeClassName& InName, FMetasoundFrontendClass& OutClass) override;
				virtual bool FindClassWithMajorVersion(const FNodeClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass) override;

				virtual TArray<FMetasoundFrontendInterface> FindAllInterfaces(bool bInIncludeDeprecated) override;
				virtual TArray<FMetasoundFrontendInterface> FindUClassDefaultInterfaces(FName InUClassName) override;
				virtual TArray<FMetasoundFrontendVersion> FindAllRegisteredInterfacesWithName(FName InInterfaceName) override;
				virtual bool FindInterfaceWithHighestVersion(FName InInterfaceName, FMetasoundFrontendInterface& OutInterface) override;
				virtual bool FindInterfaceWithMajorVersion(FName InInterfaceName, int32 InMajorVersion, FMetasoundFrontendInterface& OutInterface) override;

			private:
				FFrontendQuery* FindQuery(const FString& InQueryName);
				FFrontendQuery* AddQuery(const FString& InQueryName, TUniquePtr<FFrontendQuery>&& InQuery);

				// Store of queries indexed by unique strings.
				TMap<FString, TUniquePtr<FFrontendQuery>> QueryCache;
		};

		ISearchEngine& ISearchEngine::Get()
		{
			static FSearchEngine SearchEngine;
			return SearchEngine;
		}

		TArray<FMetasoundFrontendClass> FSearchEngine::FindAllClasses(bool bInIncludeDeprecated)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindAllClasses);

			FString QueryName = FString(TEXT("FindAllClasses"));
			if (bInIncludeDeprecated)
			{
				QueryName += TEXT("IncludingDeprecated");
			}

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				// TODO: this query will be slow to update when new nodes are registered. 
				// Consider reworking call sites, or creating a reduce function
				// which performs the majority of the query logic.
				NewQuery->AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>();

				if (!bInIncludeDeprecated)
				{
					NewQuery->AddStep<FMapToFullClassName>()
						.AddStep<FReduceClassesToHighestVersion>();
				}

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendClass> Result;

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();
				for (const FFrontendQueryEntry* Entry : Selection.GetSelection())
				{
					check(Entry->Value.IsType<FMetasoundFrontendClass>());
					Result.Add(Entry->Value.Get<FMetasoundFrontendClass>());
				}
			}

			return Result;
		}

		TArray<FMetasoundFrontendClass> FSearchEngine::FindClassesWithName(const FNodeClassName& InName, bool bInSortByVersion)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassesWithName);
			using namespace SearchEngineQuerySteps;

			FString QueryName = FString(TEXT("FindClassesWithName"));
			if (bInSortByVersion)
			{
				QueryName += TEXT("SortedByVersion");
			}
			QueryName += InName.GetFullName().ToString();
			FFrontendQuery* Query = FindQuery(QueryName);

			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();
				NewQuery->AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FFilterNodeRegistrationEventsByClassName>(InName)
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FFilterClassesByClassName>(InName);

				if (bInSortByVersion)
				{
					NewQuery->AddStep<FSortClassesByVersion>();
				}

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendClass> Result;

			if (ensure(nullptr != Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();
				for (const FFrontendQueryEntry* Entry : Selection.GetSelection())
				{
					check(Entry->Value.IsType<FMetasoundFrontendClass>());
					Result.Add(Entry->Value.Get<FMetasoundFrontendClass>());
				}
			}

			return Result;
		}

		bool FSearchEngine::FindClassWithHighestVersion(const FNodeClassName& InName, FMetasoundFrontendClass& OutClass)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassWithHighestVersion);
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString(TEXT("FindHighestVersion")) + InName.GetFullName().ToString();
			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				// TODO: Create index of class names and major version to avoid duplicating
				// this query for each class name.
				NewQuery->AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FFilterNodeRegistrationEventsByClassName>(InName)
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FFilterClassesByClassName>(InName)
					.AddStep<FMapToFullClassName>()
					.AddStep<FReduceClassesToHighestVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();
				if (Entries.IsEmpty())
				{
					return false;
				}

				if (ensure(Entries.Num() == 1))
				{
					OutClass = Entries[0]->Value.Get<FMetasoundFrontendClass>();
					return true;
				}
			}

			return false;
		}

		bool FSearchEngine::FindClassWithMajorVersion(const FNodeClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassWithMajorVersion);
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString(TEXT("FindClassWithMajorVersion")) + InName.GetFullName().ToString() + FString::FromInt(InMajorVersion);
			FFrontendQuery* Query = FindQuery(QueryName);

			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				// TODO: Create index of class names to avoid duplicating
				// this query for each class name.
				NewQuery->AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FFilterNodeRegistrationEventsByClassName>(InName)
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FFilterClassesByClassName>(InName)
					.AddStep<FMapToFullClassName>()
					.AddStep<FReduceClassesToMajorVersion>(InMajorVersion);

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();
				if (Entries.IsEmpty())
				{
					return false;
				}

				if (ensure(Entries.Num() == 1))
				{
					OutClass = Entries[0]->Value.Get<FMetasoundFrontendClass>();
					return true;
				}
			}

			return false;
		}

		TArray<FMetasoundFrontendInterface> FSearchEngine::FindAllInterfaces(bool bInIncludeDeprecated)
		{
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString::Format(TEXT("FindAllInterfaces_IncludeDeprecated:{0}"), { bInIncludeDeprecated });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>();

				if (!bInIncludeDeprecated)
				{
					NewQuery->AddStep<FMapInterfaceToInterfaceNameAndMajorVersion>()
					.AddStep<FReduceInterfacesToHighestVersion>();
				}

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendInterface> Result;

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();

				for (const FFrontendQueryEntry* Entry : Selection.GetSelection())
				{
					if (ensure(Entry->Value.IsType<FMetasoundFrontendInterface>()))
					{
						Result.Add(Entry->Value.Get<FMetasoundFrontendInterface>());
					}
				}
			}

			return Result;
		}

		TArray<FMetasoundFrontendInterface> FSearchEngine::FindUClassDefaultInterfaces(FName InUClassName)
		{
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString::Format(TEXT("FindUClassDefaultInterfaces_Class:{0}"), { InUClassName.ToString() });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FReduceInterfaceRegistryTransactionsByDefault>()
					.AddStep<FReduceInterfaceRegistryTransactionsBySupportedClassName>(InUClassName)
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapInterfaceToInterfaceNameAndMajorVersion>()
					.AddStep<FReduceInterfacesToHighestVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendInterface> Result;

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();

				for (const FFrontendQueryEntry* Entry : Selection.GetSelection())
				{
					if (ensure(Entry->Value.IsType<FMetasoundFrontendInterface>()))
					{
						Result.Add(Entry->Value.Get<FMetasoundFrontendInterface>());
					}
				}
			}

			return Result;
		}

		TArray<FMetasoundFrontendVersion> FSearchEngine::FindAllRegisteredInterfacesWithName(FName InInterfaceName)
		{
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString::Format(TEXT("FindAllRegisteredInterfacesWithName_Name:{0}"), { InInterfaceName.ToString() });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FFilterInterfaceRegistryTransactionsByName>(InInterfaceName)
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterfaceVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendVersion> Result;

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();

				for (const FFrontendQueryEntry* Entry : Selection.GetSelection())
				{
					if (ensure(Entry->Value.IsType<FMetasoundFrontendVersion>()))
					{
						Result.Add(Entry->Value.Get<FMetasoundFrontendVersion>());
					}
				}
			}

			return Result;
		}

		bool FSearchEngine::FindInterfaceWithHighestVersion(FName InInterfaceName, FMetasoundFrontendInterface& OutInterface)
		{
			using namespace SearchEngineQuerySteps;
			const FString QueryName = FString::Format(TEXT("FindInterfaceWithHighestVersion_Name:{0}"), { InInterfaceName.ToString() });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FFilterInterfaceRegistryTransactionsByName>(InInterfaceName)
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapInterfaceToInterfaceName>()
					.AddStep<FReduceInterfacesToHighestVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();
				if (Entries.IsEmpty())
				{
					return false;
				}

				if (ensure(Entries.Num() == 1))
				{
					OutInterface = Entries[0]->Value.Get<FMetasoundFrontendInterface>();
					return true;
				}
			}

			return false;
		}

		bool FSearchEngine::FindInterfaceWithMajorVersion(FName InInterfaceName, int32 InMajorVersion, FMetasoundFrontendInterface& OutInterface)
		{
			using namespace SearchEngineQuerySteps;
			const FString QueryName = FString::Format(TEXT("FindInterfaceWithMajorVersion_Name:{0}_MajorVersion:{1}"), { InInterfaceName.ToString(), InMajorVersion });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FFilterInterfaceRegistryTransactionsByNameAndMajorVersion>(InInterfaceName, InMajorVersion)
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapInterfaceToInterfaceName>()
					.AddStep<FReduceInterfacesToHighestVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();
				if (Entries.IsEmpty())
				{
					return false;
				}

				if (ensure(Entries.Num() == 1))
				{
					OutInterface = Entries[0]->Value.Get<FMetasoundFrontendInterface>();
					return true;
				}
			}

			return false;
		}

		FFrontendQuery* FSearchEngine::FindQuery(const FString& InQueryName)
		{
			if (ClearMetaSoundFrontendSearchEngineCacheCVar)
			{
				ClearMetaSoundFrontendSearchEngineCacheCVar = 0;
				QueryCache.Reset();
			}

			if (TUniquePtr<FFrontendQuery>* Query = QueryCache.Find(InQueryName))
			{
				return Query->Get();
			}

			return nullptr;
		}

		FFrontendQuery* FSearchEngine::AddQuery(const FString& InQueryName, TUniquePtr<FFrontendQuery>&& InQuery)
		{
			return QueryCache.Add(InQueryName, MoveTemp(InQuery)).Get();
		}
	}
}
