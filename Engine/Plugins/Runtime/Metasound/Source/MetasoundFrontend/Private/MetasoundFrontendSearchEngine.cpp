// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendSearchEngine.h"

#include "Algo/Transform.h"
#include "HAL/IConsoleManager.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendRegistryTransaction.h"
#include "MetasoundTrace.h"

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
			class FArchetypeRegistryTransactionSource : public IFrontendQuerySource
			{
			public:
				FArchetypeRegistryTransactionSource()
				: CurrentTransactionID(GetOriginRegistryTransactionID())
				{
				}

				void Stream(TArray<FFrontendQueryEntry>& OutEntries) override
				{
					auto AddEntry = [&OutEntries](const FArchetypeRegistryTransaction& InTransaction)
					{
						OutEntries.Emplace(FFrontendQueryEntry::FValue(TInPlaceType<FArchetypeRegistryTransaction>(), InTransaction));
					};
					
					IArchetypeRegistry::Get().ForEachRegistryTransactionSince(CurrentTransactionID, &CurrentTransactionID, AddEntry);
				}

				void Reset() override
				{
					CurrentTransactionID = GetOriginRegistryTransactionID();
				}

			private:
				FRegistryTransactionID CurrentTransactionID;
			};

			class FMapArchetypeRegistryTransactionsToArchetypeRegistryKeys : public IFrontendQueryMapStep
			{
			public:
				FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FArchetypeRegistryKey RegistryKey;
					if (ensure(InEntry.Value.IsType<FArchetypeRegistryTransaction>()))
					{
						RegistryKey = InEntry.Value.Get<FArchetypeRegistryTransaction>().GetArchetypeRegistryKey();
					}
					return FFrontendQueryEntry::FKey{RegistryKey};
				}
			};

			class FReduceArchetypeRegistryTransactionsToCurrentStatus : public IFrontendQueryReduceStep
			{
			public:
				void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const override
				{
					int32 State = 0;
					FFrontendQueryEntry* FinalEntry = nullptr;

					for (FFrontendQueryEntry* Entry : InEntries)
					{
						if (ensure(Entry->Value.IsType<FArchetypeRegistryTransaction>()))
						{
							const FArchetypeRegistryTransaction& Transaction = Entry->Value.Get<FArchetypeRegistryTransaction>();
							
							switch (Transaction.GetTransactionType())
							{
								case FArchetypeRegistryTransaction::ETransactionType::ArchetypeRegistration:
									State++;
									FinalEntry = Entry;
									break;

								case FArchetypeRegistryTransaction::ETransactionType::ArchetypeUnregistration:
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

			class FTransformArchetypeRegistryTransactionToArchetype : public IFrontendQueryTransformStep
			{
			public:
				virtual void Transform(FFrontendQueryEntry::FValue& InValue) const override
				{
					FMetasoundFrontendArchetype Archetype;
					if (ensure(InValue.IsType<FArchetypeRegistryTransaction>()))
					{
						FArchetypeRegistryKey RegistryKey = InValue.Get<FArchetypeRegistryTransaction>().GetArchetypeRegistryKey();
						IArchetypeRegistry::Get().FindArchetype(RegistryKey, Archetype);
					}
					InValue.Set<FMetasoundFrontendArchetype>(MoveTemp(Archetype));
				}
			};

			class FMapArchetypeToArchetypeNameAndMajorVersion : public IFrontendQueryMapStep
			{
			public:
				FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FString Key;
					if (ensure(InEntry.Value.IsType<FMetasoundFrontendArchetype>()))
					{
						const FMetasoundFrontendArchetype& Arch = InEntry.Value.Get<FMetasoundFrontendArchetype>();
						Key = FString::Format(TEXT("{0}_{1}"), { Arch.Version.Name.ToString(), Arch.Version.Number.Major });
					}
					return FFrontendQueryEntry::FKey{Key};
				}
			};

			class FReduceArchetypesToHighestVersion : public IFrontendQueryReduceStep
			{
			public:
				void Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const override
				{
					FFrontendQueryEntry* HighestVersionEntry = nullptr;
					FMetasoundFrontendVersionNumber HighestVersion = FMetasoundFrontendVersionNumber::GetInvalid();

					for (FFrontendQueryEntry* Entry : InEntries)
					{
						if (ensure(Entry->Value.IsType<FMetasoundFrontendArchetype>()))
						{
							const FMetasoundFrontendVersionNumber& VersionNumber = Entry->Value.Get<FMetasoundFrontendArchetype>().Version.Number;

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

			class FFilterArchetypeRegistryTransactionsByName : public IFrontendQueryFilterStep
			{
			public:
				FFilterArchetypeRegistryTransactionsByName(const FName& InName)
				: Name(InName)
				{
				}

				bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (ensure(InEntry.Value.IsType<FArchetypeRegistryTransaction>()))
					{
						return Name == InEntry.Value.Get<FArchetypeRegistryTransaction>().GetArchetypeVersion().Name;
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

			class FFilterArchetypeRegistryTransactionsByNameAndMajorVersion : public IFrontendQueryFilterStep
			{
			public:
				FFilterArchetypeRegistryTransactionsByNameAndMajorVersion(const FName& InName, int32 InMajorVersion)
				: Name(InName)
				, MajorVersion(InMajorVersion)
				{
				}

				bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (ensure(InEntry.Value.IsType<FArchetypeRegistryTransaction>()))
					{
						const FMetasoundFrontendVersion& Version = InEntry.Value.Get<FArchetypeRegistryTransaction>().GetArchetypeVersion();

						return (Name == Version.Name) && (MajorVersion == Version.Number.Major);
					}
					return false;
				}

			private:
				FName Name;
				int32 MajorVersion;
			};

			class FMapArchetypeToArchetypeName : public IFrontendQueryMapStep
			{
			public:
				FFrontendQueryEntry::FKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FString Key;
					if (ensure(InEntry.Value.IsType<FMetasoundFrontendArchetype>()))
					{
						Key = InEntry.Value.Get<FMetasoundFrontendArchetype>().Version.Name.ToString();
					}
					return FFrontendQueryEntry::FKey{Key};
				}
			};

			class FTransformArchetypeRegistryTransactionToArchetypeVersion : public IFrontendQueryTransformStep
			{
			public:
				virtual void Transform(FFrontendQueryEntry::FValue& InValue) const override
				{
					FMetasoundFrontendVersion Version;
					if (ensure(InValue.IsType<FArchetypeRegistryTransaction>()))
					{
						Version = InValue.Get<FArchetypeRegistryTransaction>().GetArchetypeVersion();
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

				TArray<FMetasoundFrontendClass> FindAllClasses(bool bInIncludeDeprecated) override;
				TArray<FMetasoundFrontendClass> FindClassesWithName(const FNodeClassName& InName, bool bInSortByVersion) override;

				bool FindClassWithHighestVersion(const FNodeClassName& InName, FMetasoundFrontendClass& OutClass) override;
				bool FindClassWithMajorVersion(const FNodeClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass) override;

				virtual TArray<FMetasoundFrontendArchetype> FindAllArchetypes(bool bInIncludeDeprecated) override;

				virtual TArray<FMetasoundFrontendVersion> FindAllRegisteredArchetypesWithName(const FName& InArchetypeName) override;

				virtual bool FindArchetypeWithHighestVersion(const FName& InArchetypeName, FMetasoundFrontendArchetype& OutArchetype) override;

				virtual bool FindArchetypeWithMajorVersion(const FName& InArchetypeName, int32 InMajorVersion, FMetasoundFrontendArchetype& OutArchetype) override;

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

		TArray<FMetasoundFrontendArchetype> FSearchEngine::FindAllArchetypes(bool bInIncludeDeprecated)
		{
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString::Format(TEXT("FindAllArchetypes_IncludeDeprecated:{0}"), { bInIncludeDeprecated });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FArchetypeRegistryTransactionSource>()
					.AddStep<FMapArchetypeRegistryTransactionsToArchetypeRegistryKeys>()
					.AddStep<FReduceArchetypeRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformArchetypeRegistryTransactionToArchetype>();

				if (!bInIncludeDeprecated)
				{
					NewQuery->AddStep<FMapArchetypeToArchetypeNameAndMajorVersion>()
					.AddStep<FReduceArchetypesToHighestVersion>();
				}

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendArchetype> Result;

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->Execute();
				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();

				for (const FFrontendQueryEntry* Entry : Selection.GetSelection())
				{
					if (ensure(Entry->Value.IsType<FMetasoundFrontendArchetype>()))
					{
						Result.Add(Entry->Value.Get<FMetasoundFrontendArchetype>());
					}
				}
			}

			return Result;
		}

		TArray<FMetasoundFrontendVersion> FSearchEngine::FindAllRegisteredArchetypesWithName(const FName& InArchetypeName)
		{
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString::Format(TEXT("FindAllRegisteredArchetypesWithName_Name:{0}"), { InArchetypeName.ToString() });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FArchetypeRegistryTransactionSource>()
					.AddStep<FFilterArchetypeRegistryTransactionsByName>(InArchetypeName)
					.AddStep<FMapArchetypeRegistryTransactionsToArchetypeRegistryKeys>()
					.AddStep<FReduceArchetypeRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformArchetypeRegistryTransactionToArchetypeVersion>();

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

		bool FSearchEngine::FindArchetypeWithHighestVersion(const FName& InArchetypeName, FMetasoundFrontendArchetype& OutArchetype)
		{
			using namespace SearchEngineQuerySteps;
			const FString QueryName = FString::Format(TEXT("FindArchetypeWithHighestVersion_Name:{0}"), { InArchetypeName.ToString() });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FArchetypeRegistryTransactionSource>()
					.AddStep<FFilterArchetypeRegistryTransactionsByName>(InArchetypeName)
					.AddStep<FMapArchetypeRegistryTransactionsToArchetypeRegistryKeys>()
					.AddStep<FReduceArchetypeRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformArchetypeRegistryTransactionToArchetype>()
					.AddStep<FMapArchetypeToArchetypeName>()
					.AddStep<FReduceArchetypesToHighestVersion>();

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
					OutArchetype = Entries[0]->Value.Get<FMetasoundFrontendArchetype>();
					return true;
				}
			}

			return false;
		}

		bool FSearchEngine::FindArchetypeWithMajorVersion(const FName& InArchetypeName, int32 InMajorVersion, FMetasoundFrontendArchetype& OutArchetype)
		{
			using namespace SearchEngineQuerySteps;
			const FString QueryName = FString::Format(TEXT("FindArchetypeWithMajorVersion_Name:{0}_MajorVersion:{1}"), { InArchetypeName.ToString(), InMajorVersion });

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FArchetypeRegistryTransactionSource>()
					.AddStep<FFilterArchetypeRegistryTransactionsByNameAndMajorVersion>(InArchetypeName, InMajorVersion)
					.AddStep<FMapArchetypeRegistryTransactionsToArchetypeRegistryKeys>()
					.AddStep<FReduceArchetypeRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformArchetypeRegistryTransactionToArchetype>()
					.AddStep<FMapArchetypeToArchetypeName>()
					.AddStep<FReduceArchetypesToHighestVersion>();

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
					OutArchetype = Entries[0]->Value.Get<FMetasoundFrontendArchetype>();
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
