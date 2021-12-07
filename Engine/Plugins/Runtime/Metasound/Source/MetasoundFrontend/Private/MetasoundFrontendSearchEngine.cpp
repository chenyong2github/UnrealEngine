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
			class FMapClassToFullClassNameAndMajorVersion : public IFrontendQueryMapStep
			{
			public:
				static FFrontendQueryKey GetKey(const FMetasoundFrontendClassName& InClassName, int32 InMajorVersion)
				{
					return FFrontendQueryKey(FString::Format(TEXT("{0}_v{1}"), {InClassName.ToString(), InMajorVersion}));
				}

				virtual FFrontendQueryKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					if (ensure(InEntry.Value.IsType<FMetasoundFrontendClass>()))
					{
						const FMetasoundFrontendClassMetadata& Metadata = InEntry.Value.Get<FMetasoundFrontendClass>().Metadata;
						return FMapClassToFullClassNameAndMajorVersion::GetKey(Metadata.GetClassName(), Metadata.GetVersion().Major);
					}

					return FFrontendQueryKey();
				}
			};


			class FInterfaceRegistryTransactionSource : public IFrontendQueryStreamStep
			{
			public:
				FInterfaceRegistryTransactionSource()
				: CurrentTransactionID(GetOriginRegistryTransactionID())
				{
				}

				virtual void Stream(TArray<FFrontendQueryValue>& OutEntries) override
				{
					auto AddValue = [&OutEntries](const FInterfaceRegistryTransaction& InTransaction)
					{
						OutEntries.Emplace(TInPlaceType<FInterfaceRegistryTransaction>(), InTransaction);
					};
					
					IInterfaceRegistry::Get().ForEachRegistryTransactionSince(CurrentTransactionID, &CurrentTransactionID, AddValue);
				}

			private:
				FRegistryTransactionID CurrentTransactionID;
			};

			class FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys : public IFrontendQueryMapStep
			{
			public:
				virtual FFrontendQueryKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FInterfaceRegistryKey RegistryKey;
					if (ensure(InEntry.Value.IsType<FInterfaceRegistryTransaction>()))
					{
						RegistryKey = InEntry.Value.Get<FInterfaceRegistryTransaction>().GetInterfaceRegistryKey();
					}
					return FFrontendQueryKey{RegistryKey};
				}
			};

			class FMapInterfaceRegistryTransactionToInterfaceName : public IFrontendQueryMapStep
			{
			public:
				virtual FFrontendQueryKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FInterfaceRegistryKey RegistryKey;
					if (ensure(InEntry.Value.IsType<FInterfaceRegistryTransaction>()))
					{
						return FFrontendQueryKey{InEntry.Value.Get<FInterfaceRegistryTransaction>().GetInterfaceVersion().Name};
					}
					return FFrontendQueryKey();
				}
			};

			class FReduceInterfaceRegistryTransactionsToCurrentStatus : public IFrontendQueryReduceStep
			{
			public:
				virtual void Reduce(const FFrontendQueryKey& InKey, FFrontendQueryPartition& InOutEntries) const override
				{
					int32 State = 0;
					FFrontendQueryEntry* FinalEntry = nullptr;

					for (FFrontendQueryEntry& Entry : InOutEntries)
					{
						if (ensure(Entry.Value.IsType<FInterfaceRegistryTransaction>()))
						{
							const FInterfaceRegistryTransaction& Transaction = Entry.Value.Get<FInterfaceRegistryTransaction>();
							
							switch (Transaction.GetTransactionType())
							{
								case FInterfaceRegistryTransaction::ETransactionType::InterfaceRegistration:
									State++;
									FinalEntry = &Entry;
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
						FFrontendQueryEntry Entry = *FinalEntry;
						InOutEntries.Reset();
						InOutEntries.Add(Entry);
					}
					else
					{
						InOutEntries.Reset();
					}
				}
			};

			class FFilterInterfaceRegistryTransactionsBySupportedClassName : public IFrontendQueryFilterStep
			{
				FName UClassName;

			public:
				FFilterInterfaceRegistryTransactionsBySupportedClassName(FName InUClassName)
					: UClassName(InUClassName)
				{
				}

				virtual bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (ensure(InEntry.Value.IsType<FInterfaceRegistryTransaction>()))
					{
						FInterfaceRegistryKey RegistryKey = InEntry.Value.Get<FInterfaceRegistryTransaction>().GetInterfaceRegistryKey();
						const IInterfaceRegistryEntry* RegEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);
						if (ensure(RegEntry))
						{
							return RegEntry->UClassIsSupported(UClassName);
						}
					}
					return false;
				}
			};

			class FFilterInterfaceRegistryTransactionsByDefault : public IFrontendQueryFilterStep
			{
			public:
				virtual bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (ensure(InEntry.Value.IsType<FInterfaceRegistryTransaction>()))
					{
						FInterfaceRegistryKey RegistryKey = InEntry.Value.Get<FInterfaceRegistryTransaction>().GetInterfaceRegistryKey();
						const IInterfaceRegistryEntry* RegEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);
						if (ensure(RegEntry))
						{
							return RegEntry->IsDefault();
						}
					}
					return false;
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
				static FFrontendQueryKey GetKey(const FString& InName, int32 InMajorVersion)
				{
					return FFrontendQueryKey{FString::Format(TEXT("{0}_{1}"), { InName, InMajorVersion })};
				}

				virtual FFrontendQueryKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FString Key;
					if (ensure(InEntry.Value.IsType<FMetasoundFrontendInterface>()))
					{
						const FMetasoundFrontendInterface& Interface = InEntry.Value.Get<FMetasoundFrontendInterface>();

						return FMapInterfaceToInterfaceNameAndMajorVersion::GetKey(Interface.Version.Name.ToString(), Interface.Version.Number.Major);
					}
					return FFrontendQueryKey{Key};
				}
			};

			class FReduceInterfacesToHighestVersion : public IFrontendQueryReduceStep
			{
			public:
				virtual void Reduce(const FFrontendQueryKey& InKey, FFrontendQueryPartition& InOutEntries) const override
				{
					FFrontendQueryEntry* HighestVersionEntry = nullptr;
					FMetasoundFrontendVersionNumber HighestVersion = FMetasoundFrontendVersionNumber::GetInvalid();

					for (FFrontendQueryEntry& Entry : InOutEntries)
					{
						if (ensure(Entry.Value.IsType<FMetasoundFrontendInterface>()))
						{
							const FMetasoundFrontendVersionNumber& VersionNumber = Entry.Value.Get<FMetasoundFrontendInterface>().Version.Number;
							if (VersionNumber > HighestVersion)
							{
								HighestVersionEntry = &Entry;
								HighestVersion = VersionNumber;
							}
						}
					}

					if (HighestVersionEntry)
					{
						FFrontendQueryEntry Entry = *HighestVersionEntry;
						InOutEntries.Reset();
						InOutEntries.Add(Entry);
					}
					else
					{
						InOutEntries.Reset();
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

				virtual bool Filter(const FFrontendQueryEntry& InEntry) const override
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

				virtual bool Filter(const FFrontendQueryEntry& InEntry) const override
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

				virtual bool Filter(const FFrontendQueryEntry& InEntry) const override
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
				virtual FFrontendQueryKey Map(const FFrontendQueryEntry& InEntry) const override
				{
					FName Key;
					if (ensure(InEntry.Value.IsType<FMetasoundFrontendInterface>()))
					{
						Key = InEntry.Value.Get<FMetasoundFrontendInterface>().Version.Name;
					}
					return FFrontendQueryKey{Key};
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
				virtual TArray<FMetasoundFrontendClass> FindClassesWithName(const FMetasoundFrontendClassName& InName, bool bInSortByVersion) override;
				virtual bool FindClassWithHighestVersion(const FMetasoundFrontendClassName& InName, FMetasoundFrontendClass& OutClass) override;
				virtual bool FindClassWithMajorVersion(const FMetasoundFrontendClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass) override;

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
			using namespace SearchEngineQuerySteps;

			FString QueryName = FString(TEXT("FindAllClasses"));
			if (bInIncludeDeprecated)
			{
				QueryName += TEXT("IncludingDeprecated");
			}

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>();

				if (!bInIncludeDeprecated)
				{
					NewQuery->AddStep<FMapToFullClassName>()
						.AddStep<FReduceClassesToHighestVersion>();
				}

				NewQuery->AddStep<FMapToNull>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendClass> Result;

			if (ensure(Query))
			{
				// TODO: cache result and only rebuild if query actually updates. 
				const FFrontendQuerySelection& Selection = Query->Update();
				if (const FFrontendQueryPartition* Entries = Selection.Find(FFrontendQueryKey()))
				{
					for (const FFrontendQueryEntry& Entry : *Entries)
					{
						check(Entry.Value.IsType<FMetasoundFrontendClass>());
						Result.Add(Entry.Value.Get<FMetasoundFrontendClass>());
					}
				}
			}

			return Result;
		}

		TArray<FMetasoundFrontendClass> FSearchEngine::FindClassesWithName(const FMetasoundFrontendClassName& InName, bool bInSortByVersion)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassesWithName);
			using namespace SearchEngineQuerySteps;

			FString QueryName = FString(TEXT("FindClassesWithName"));
			if (bInSortByVersion)
			{
				QueryName += TEXT("_SortedByVersion");
			}
			FFrontendQuery* Query = FindQuery(QueryName);

			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();
				NewQuery->AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FMapClassesToClassName>();

				if (bInSortByVersion)
				{
					NewQuery->AddStep<FSortClassesByVersion>();
				}

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendClass> Result;

			if (ensure(nullptr != Query))
			{
				const FFrontendQuerySelection& Selection = Query->Update();
				const FFrontendQueryKey Key(InName.GetFullName());
				if (const FFrontendQueryPartition* Entries = Selection.Find(Key))
				{
					for (const FFrontendQueryEntry& Entry : *Entries)
					{
						check(Entry.Value.IsType<FMetasoundFrontendClass>());
						Result.Add(Entry.Value.Get<FMetasoundFrontendClass>());
					}
				}
			}

			return Result;
		}

		bool FSearchEngine::FindClassWithHighestVersion(const FMetasoundFrontendClassName& InName, FMetasoundFrontendClass& OutClass)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassWithHighestVersion);
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString(TEXT("FindHighestVersion"));
			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FMapToFullClassName>()
					.AddStep<FReduceClassesToHighestVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				const FFrontendQuerySelection& Selection = Query->Update();
				const FFrontendQueryKey Key(InName.GetFullName());

				if (const FFrontendQueryPartition* Entries = Selection.Find(Key))
				{
					if (ensure(Entries->Num() == 1))
					{
						OutClass = Entries->CreateConstIterator()->Value.Get<FMetasoundFrontendClass>();
						return true;
					}
				}
			}

			return false;
		}

		bool FSearchEngine::FindClassWithMajorVersion(const FMetasoundFrontendClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassWithMajorVersion);
			using namespace SearchEngineQuerySteps;

			const FString QueryName = FString(TEXT("FindClassWithMajorVersion"));
			FFrontendQuery* Query = FindQuery(QueryName);

			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FMapToFullClassName>()
					.AddStep<FMapClassToFullClassNameAndMajorVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}


			if (ensure(Query))
			{
				const FFrontendQueryKey Key = FMapClassToFullClassNameAndMajorVersion::GetKey(InName, InMajorVersion);
				const FFrontendQuerySelection& Selection = Query->Update();

				if (const FFrontendQueryPartition* Entries = Selection.Find(Key))
				{
					if (ensure(Entries->Num() == 1))
					{
						OutClass = Entries->CreateConstIterator()->Value.Get<FMetasoundFrontendClass>();
						return true;
					}
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
				NewQuery->AddStep<FMapToNull>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendInterface> Result;

			if (ensure(Query))
			{
				const FFrontendQuerySelection& Selection = Query->Update();
				if (const FFrontendQueryPartition* Entries = Selection.Find(FFrontendQueryKey()))
				{
					for (const FFrontendQueryEntry& Entry : *Entries)
					{
						if (ensure(Entry.Value.IsType<FMetasoundFrontendInterface>()))
						{
							Result.Add(Entry.Value.Get<FMetasoundFrontendInterface>());
						}
					}
				}
			}

			return Result;
		}

		TArray<FMetasoundFrontendInterface> FSearchEngine::FindUClassDefaultInterfaces(FName InUClassName)
		{
			constexpr bool bIncludeDeprecated = false;
			TArray<FMetasoundFrontendInterface> AllInterfaces = FindAllInterfaces(bIncludeDeprecated);

			auto DoesNotSupportUClass = [&InUClassName](const FMetasoundFrontendInterface& InInterface)
			{
				FInterfaceRegistryKey RegistryKey = GetInterfaceRegistryKey(InInterface);
				const IInterfaceRegistryEntry* RegEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);
				if (ensure(RegEntry))
				{
					return RegEntry->UClassIsSupported(InUClassName);
				}

				return true;
			};

			AllInterfaces.RemoveAllSwap(DoesNotSupportUClass);

			return AllInterfaces;
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
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FMapInterfaceRegistryTransactionToInterfaceName>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterfaceVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendVersion> Result;

			if (ensure(Query))
			{
				const FFrontendQueryKey Key{InInterfaceName};
				const FFrontendQuerySelection& Selection = Query->Update();
				if (const FFrontendQueryPartition* Entries = Selection.Find(Key))
				{
					for (const FFrontendQueryEntry& Entry : *Entries)
					{
						if (ensure(Entry.Value.IsType<FMetasoundFrontendVersion>()))
						{
							Result.Add(Entry.Value.Get<FMetasoundFrontendVersion>());
						}
					}
				}
			}

			return Result;
		}

		bool FSearchEngine::FindInterfaceWithHighestVersion(FName InInterfaceName, FMetasoundFrontendInterface& OutInterface)
		{
			using namespace SearchEngineQuerySteps;
			const FString QueryName = TEXT("FindInterfaceWithHighestVersion");

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapInterfaceToInterfaceName>()
					.AddStep<FReduceInterfacesToHighestVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				const FFrontendQueryKey Key{InInterfaceName};
				const FFrontendQuerySelection& Selection = Query->Update();
				if (const FFrontendQueryPartition* Entries = Selection.Find(Key))
				{
					if (ensure(Entries->Num() == 1))
					{
						OutInterface = Entries->CreateConstIterator()->Value.Get<FMetasoundFrontendInterface>();
						return true;
					}
				}
			}

			return false;
		}

		bool FSearchEngine::FindInterfaceWithMajorVersion(FName InInterfaceName, int32 InMajorVersion, FMetasoundFrontendInterface& OutInterface)
		{
			using namespace SearchEngineQuerySteps;
			const FString QueryName = TEXT("FindInterfaceWithMajorVersion");

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapInterfaceToInterfaceNameAndMajorVersion>()
					.AddStep<FReduceInterfacesToHighestVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				const FFrontendQueryKey Key = FMapInterfaceToInterfaceNameAndMajorVersion::GetKey(InInterfaceName.ToString(), InMajorVersion);
				const FFrontendQuerySelection& Selection = Query->Update();
				if (const FFrontendQueryPartition* Entries = Selection.Find(Key))
				{
					if (ensure(Entries->Num() == 1))
					{
						OutInterface = Entries->CreateConstIterator()->Value.Get<FMetasoundFrontendInterface>();
						return true;
					}
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
