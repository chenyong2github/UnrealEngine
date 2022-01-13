// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendSearchEngine.h"

#include "Algo/MaxElement.h"
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
					// Get most recent transaction
					const FFrontendQueryEntry* FinalEntry = Algo::MaxElementBy(InOutEntries, GetTransactionTimestamp);

					// Check if most recent transaction is valid and not an unregister transaction.
					if (IsValidTransactionOfType(FInterfaceRegistryTransaction::ETransactionType::InterfaceRegistration, FinalEntry))
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
			private:
				static FInterfaceRegistryTransaction::FTimeType GetTransactionTimestamp(const FFrontendQueryEntry& InEntry)
				{
					if (ensure(InEntry.Value.IsType<FInterfaceRegistryTransaction>()))
					{
						return InEntry.Value.Get<FInterfaceRegistryTransaction>().GetTimestamp();
					}
					return 0;
				}

				static bool IsValidTransactionOfType(FInterfaceRegistryTransaction::ETransactionType InType, const FFrontendQueryEntry* InEntry)
				{
					if (nullptr != InEntry)
					{
						if (InEntry->Value.IsType<FInterfaceRegistryTransaction>())
						{
							return InEntry->Value.Get<FInterfaceRegistryTransaction>().GetTransactionType() == InType;
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

			class FRemoveDeprecatedClasses : public IFrontendQueryFilterStep
			{
			public:
				virtual bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (InEntry.Value.IsType<FMetasoundFrontendClass>())
					{
						return !InEntry.Value.Get<FMetasoundFrontendClass>().Metadata.GetIsDeprecated();
					}
					return false;
				}
			};

			class FRemoveInterfacesWhichAreNotDefault : public IFrontendQueryFilterStep
			{
				virtual bool Filter(const FFrontendQueryEntry& InEntry) const override
				{
					if (InEntry.Value.IsType<FMetasoundFrontendInterface>())
					{
						FInterfaceRegistryKey RegistryKey = GetInterfaceRegistryKey(InEntry.Value.Get<FMetasoundFrontendInterface>());
						const IInterfaceRegistryEntry* RegEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);

						if (nullptr != RegEntry)
						{
							return RegEntry->IsDefault();
						}
					}
					return false;
				}
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

		// Base interface for a search engine query. This allows the query to be
		// primed to improve execution performance in critical call-stacks.
		struct ISearchEngineQuery
		{
			// Prime the query to improve execution performance during subsequent 
			// queries. 
			virtual void Prime() = 0;
			virtual ~ISearchEngineQuery() = default;
		};

		// TSearchEngineQuery provides a base for all queries used in the search
		// engine. It requires a `QueryPolicy` as a template parameter. 
		//
		// A `QueryPolicy` is a struct containing a static function to create the 
		// query, a static function to build a result from the query selection, 
		// and query result type.
		//
		// Example:
		//
		// struct FQueryPolicyExample
		// {
		//		using ResultType = FMetasoundFrontendClass;
		//
		//		static FFrontendQuery CreateQuery()
		//		{
		//			using namespace SearchEngineQuerySteps;
		//			FFrontendQuery Query;
		//			Query.AddStep<FNodeClassRegistrationEvents>()
		//				.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
		//				.AddStep<FReduceRegistrationEventsToCurrentStatus>()
		//				.AddStep<FTransformRegistrationEventsToClasses>();
		//			return Query;
		//		}
		//
		//		static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
		//		{
		//			FMetasoundFrontendClass MetaSoundClass;
		//			if (ensureMsgf(InPartition.Num() > 0, TEXT("Cannot retrieve class from empty partition")))
		//			{
		//				MetaSoundClass = InPartition.CreateConstIterator()->Value.Get<FMetasoundFrontendClass>();
		//			}
		//			return MetaSoundClass;
		//		}
		// };
		template<typename QueryPolicy>
		struct TSearchEngineQuery : public ISearchEngineQuery
		{
			using ResultType = typename QueryPolicy::ResultType;

			TSearchEngineQuery()
			: Query(QueryPolicy::CreateQuery())
			{
			}

			virtual void Prime() override
			{
				FScopeLock Lock(&QueryCriticalSection);
				Update();
			}

			// Updates the query to the most recent value and returns the
			// result.
			ResultType UpdateAndFindResult(const FFrontendQueryKey& InKey)
			{
				FScopeLock Lock(&QueryCriticalSection);

				Update();
				if (const ResultType* Result = ResultCache.Find(InKey))
				{
					return *Result;
				}
				return ResultType();
			}

			// Updates the query to the most recent value and assigns the value 
			// to OutResult. Returns true on success, false on failure. 
			bool UpdateAndFindResult(const FFrontendQueryKey& InKey, ResultType& OutResult)
			{
				FScopeLock Lock(&QueryCriticalSection);

				Update();
				if (const ResultType* Result = ResultCache.Find(InKey))
				{
					OutResult = *Result;
					return true;
				}
				return false;
			}

		private:
			void Update()
			{
				METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSound::SearchEngineQuery::Update);

				TSet<FFrontendQueryKey> UpdatedKeys;
				const FFrontendQuerySelection& Selection = Query.Update(UpdatedKeys);

				if (UpdatedKeys.Num() > 0)
				{
					METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(MetaSound::SearchEngineQuery::BuildResult);

					for (const FFrontendQueryKey& Key : UpdatedKeys)
					{
						const FFrontendQueryPartition* Partition = Query.GetSelection().Find(Key);
						if (nullptr != Partition)
						{
							ResultCache.Add(Key, QueryPolicy::BuildResult(*Partition));
						}
						else
						{
							ResultCache.Remove(Key);
						}
					}
				}
			}

			FCriticalSection QueryCriticalSection;
			FFrontendQuery Query;
			TMap<FFrontendQueryKey, ResultType> ResultCache;
		};

		TArray<FMetasoundFrontendClass> BuildArrayOfClassesFromPartition(const FFrontendQueryPartition& InPartition)
		{
			TArray<FMetasoundFrontendClass> Result;
			for (const FFrontendQueryEntry& Entry : InPartition)
			{
				check(Entry.Value.IsType<FMetasoundFrontendClass>());
				Result.Add(Entry.Value.Get<FMetasoundFrontendClass>());
			}
			return Result;
		}

		FMetasoundFrontendClass BuildSingleClassFromPartition(const FFrontendQueryPartition& InPartition)
		{
			FMetasoundFrontendClass MetaSoundClass;
			if (ensureMsgf(InPartition.Num() > 0, TEXT("Cannot retrieve class from empty partition")))
			{
				MetaSoundClass = InPartition.CreateConstIterator()->Value.Get<FMetasoundFrontendClass>();
			}
			return MetaSoundClass;
		}

		TArray<FMetasoundFrontendInterface> BuildArrayOfInterfacesFromPartition(const FFrontendQueryPartition& InPartition)
		{
			TArray<FMetasoundFrontendInterface> Result;
			for (const FFrontendQueryEntry& Entry : InPartition)
			{
				check(Entry.Value.IsType<FMetasoundFrontendInterface>());
				Result.Add(Entry.Value.Get<FMetasoundFrontendInterface>());
			}
			return Result;
		}

		FMetasoundFrontendInterface BuildSingleInterfaceFromPartition(const FFrontendQueryPartition& InPartition)
		{
			FMetasoundFrontendInterface MetaSoundInterface;
			if (ensureMsgf(InPartition.Num() > 0, TEXT("Cannot retrieve class from empty partition")))
			{
				MetaSoundInterface = InPartition.CreateConstIterator()->Value.Get<FMetasoundFrontendInterface>();
			}
			return MetaSoundInterface;
		}

		TArray<FMetasoundFrontendVersion> BuildArrayOfVersionsFromPartition(const FFrontendQueryPartition& InPartition)
		{
			TArray<FMetasoundFrontendVersion> Result;
			for (const FFrontendQueryEntry& Entry : InPartition)
			{
				check(Entry.Value.IsType<FMetasoundFrontendVersion>());
				Result.Add(Entry.Value.Get<FMetasoundFrontendVersion>());
			}
			return Result;
		}

		// Policy for finding all registered metasound classes. 
		struct FFindAllClassesQueryPolicy
		{
			using ResultType = TArray<FMetasoundFrontendClass>;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FRemoveDeprecatedClasses>()
					.AddStep<FMapToNull>();
				return Query;
			}

			static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildArrayOfClassesFromPartition(InPartition);
			}
		};

		// Policy for finding all registered metasound classes, including deprecated classes. 
		struct FFindAllClassesIncludingAllVersionsQueryPolicy
		{
			using ResultType = TArray<FMetasoundFrontendClass>;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FMapToNull>();
				return Query;
			}

			static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildArrayOfClassesFromPartition(InPartition);
			}
		};

		// Policy for finding all registered metasound classes sorted by version 
		// and indexed by name.
		struct FFindClassesWithNameSortedQueryPolicy
		{
			using ResultType = TArray<FMetasoundFrontendClass>;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FMapClassesToClassName>()
					.AddStep<FSortClassesByVersion>();
				return Query;
			}

			static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildArrayOfClassesFromPartition(InPartition);
			}
		};

		// Policy for finding all registered metasound classes indexed by name.
		struct FFindClassesWithNameUnsortedQueryPolicy
		{
			using ResultType = TArray<FMetasoundFrontendClass>;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FMapClassesToClassName>();
				return Query;
			}

			static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildArrayOfClassesFromPartition(InPartition);
			}
		};

		// Policy for finding highest versioned metasound class by name.
		struct FFindClassWithHighestVersionQueryPolicy
		{
			using ResultType = FMetasoundFrontendClass;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FRemoveDeprecatedClasses>()
					.AddStep<FMapToFullClassName>()
					.AddStep<FReduceClassesToHighestVersion>();
				return Query;
			}

			static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildSingleClassFromPartition(InPartition);
			}
		};

		// Policy for finding highest versioned metasound class by name and major version.
		struct FFindClassWithMajorVersionQueryPolicy
		{
			using ResultType = FMetasoundFrontendClass;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FNodeClassRegistrationEvents>()
					.AddStep<FMapRegistrationEventsToNodeRegistryKeys>()
					.AddStep<FReduceRegistrationEventsToCurrentStatus>()
					.AddStep<FTransformRegistrationEventsToClasses>()
					.AddStep<FRemoveDeprecatedClasses>()
					.AddStep<FMapClassToFullClassNameAndMajorVersion>()
					.AddStep<FReduceClassesToHighestVersion>();
				return Query;
			}

			static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildSingleClassFromPartition(InPartition);
			}
		};

		// Policy for finding all registered interfaces. 
		struct FFindAllInterfacesQueryPolicy
		{
			using ResultType = TArray<FMetasoundFrontendInterface>;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapInterfaceToInterfaceNameAndMajorVersion>()
					.AddStep<FReduceInterfacesToHighestVersion>()
					.AddStep<FMapToNull>();
				return Query;
			}

			static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildArrayOfInterfacesFromPartition(InPartition);
			}
		};

		// Policy for finding all registered default interfaces. 
		struct FFindAllDefaultInterfacesQueryPolicy
		{
			using ResultType = TArray<FMetasoundFrontendInterface>;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapInterfaceToInterfaceNameAndMajorVersion>()
					.AddStep<FReduceInterfacesToHighestVersion>()
					.AddStep<FRemoveInterfacesWhichAreNotDefault>()
					.AddStep<FMapToNull>();
				return Query;
			}

			static ResultType BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildArrayOfInterfacesFromPartition(InPartition);
			}
		};

		// Policy for finding all registered interfaces (including deprecated). 
		struct FFindAllInterfacesIncludingAllVersionsQueryPolicy
		{
			using ResultType = TArray<FMetasoundFrontendInterface>;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapToNull>();
				return Query;
			}

			static TArray<FMetasoundFrontendInterface> BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildArrayOfInterfacesFromPartition(InPartition);
			}
		};

		// Policy for finding all registered interface versions (name & version number)
		// by name.
		struct FFindAllRegisteredInterfacesWithNameQueryPolicy
		{
			using ResultType = TArray<FMetasoundFrontendVersion>;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FMapInterfaceRegistryTransactionToInterfaceName>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterfaceVersion>();
				return Query;
			}

			static TArray<FMetasoundFrontendVersion> BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildArrayOfVersionsFromPartition(InPartition);
			}
		};

		// Policy for finding the highest version registered interfaces by name.
		struct FFindInterfaceWithHighestVersionQueryPolicy
		{
			using ResultType = FMetasoundFrontendInterface;

			static FFrontendQuery CreateQuery()
			{
				using namespace SearchEngineQuerySteps;
				FFrontendQuery Query;
				Query.AddStep<FInterfaceRegistryTransactionSource>()
					.AddStep<FMapInterfaceRegistryTransactionsToInterfaceRegistryKeys>()
					.AddStep<FReduceInterfaceRegistryTransactionsToCurrentStatus>()
					.AddStep<FTransformInterfaceRegistryTransactionToInterface>()
					.AddStep<FMapInterfaceToInterfaceName>()
					.AddStep<FReduceInterfacesToHighestVersion>();
				return Query;
			}

			static FMetasoundFrontendInterface BuildResult(const FFrontendQueryPartition& InPartition)
			{
				return BuildSingleInterfaceFromPartition(InPartition);
			}
		};

		class FSearchEngine : public ISearchEngine
		{
			FSearchEngine(const FSearchEngine&) = delete;
			FSearchEngine(FSearchEngine&&) = delete;
			FSearchEngine& operator=(const FSearchEngine&) = delete;
			FSearchEngine& operator=(FSearchEngine&&) = delete;

		public:
			FSearchEngine() = default;
			virtual ~FSearchEngine() = default;

			virtual void Prime() override;
			virtual TArray<FMetasoundFrontendClass> FindAllClasses(bool bInIncludeAllVersions) override;
			virtual TArray<FMetasoundFrontendClass> FindClassesWithName(const FMetasoundFrontendClassName& InName, bool bInSortByVersion) override;
			virtual bool FindClassWithHighestVersion(const FMetasoundFrontendClassName& InName, FMetasoundFrontendClass& OutClass) override;
			virtual bool FindClassWithMajorVersion(const FMetasoundFrontendClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass) override;

			virtual TArray<FMetasoundFrontendInterface> FindAllInterfaces(bool bInIncludeAllVersions) override;
			virtual TArray<FMetasoundFrontendInterface> FindUClassDefaultInterfaces(FName InUClassName) override;
			virtual TArray<FMetasoundFrontendVersion> FindAllRegisteredInterfacesWithName(FName InInterfaceName) override;
			virtual bool FindInterfaceWithHighestVersion(FName InInterfaceName, FMetasoundFrontendInterface& OutInterface) override;

		private:

			TSearchEngineQuery<FFindAllClassesQueryPolicy> FindAllClassesQuery;
			TSearchEngineQuery<FFindAllClassesIncludingAllVersionsQueryPolicy> FindAllClassesIncludingAllVersionsQuery;
			TSearchEngineQuery<FFindClassesWithNameUnsortedQueryPolicy> FindClassesWithNameUnsortedQuery;
			TSearchEngineQuery<FFindClassesWithNameSortedQueryPolicy> FindClassesWithNameSortedQuery;
			TSearchEngineQuery<FFindClassWithHighestVersionQueryPolicy> FindClassWithHighestVersionQuery;
			TSearchEngineQuery<FFindClassWithMajorVersionQueryPolicy> FindClassWithMajorVersionQuery;
			TSearchEngineQuery<FFindAllInterfacesQueryPolicy> FindAllInterfacesQuery;
			TSearchEngineQuery<FFindAllDefaultInterfacesQueryPolicy> FFindAllDefaultInterfacesQuery;
			TSearchEngineQuery<FFindAllInterfacesIncludingAllVersionsQueryPolicy> FindAllInterfacesIncludingAllVersionsQuery;
			TSearchEngineQuery<FFindAllRegisteredInterfacesWithNameQueryPolicy> FindAllRegisteredInterfacesWithNameQuery;
			TSearchEngineQuery<FFindInterfaceWithHighestVersionQueryPolicy> FindInterfaceWithHighestVersionQuery;
		};

		ISearchEngine& ISearchEngine::Get()
		{
			static FSearchEngine SearchEngine;
			return SearchEngine;
		}

		void FSearchEngine::Prime()
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::Prime);

			FindAllClassesQuery.Prime();
			FindAllClassesIncludingAllVersionsQuery.Prime();
			FindClassesWithNameUnsortedQuery.Prime();
			FindClassesWithNameSortedQuery.Prime();
			FindClassWithHighestVersionQuery.Prime();
			FindClassWithMajorVersionQuery.Prime();
			FindAllInterfacesIncludingAllVersionsQuery.Prime();
			FindAllInterfacesQuery.Prime();
			FindAllRegisteredInterfacesWithNameQuery.Prime();
			FindInterfaceWithHighestVersionQuery.Prime();
		}

		TArray<FMetasoundFrontendClass> FSearchEngine::FindAllClasses(bool bInIncludeAllVersions)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindAllClasses);

			FFrontendQueryKey NullKey;
			if (bInIncludeAllVersions)
			{
				return FindAllClassesIncludingAllVersionsQuery.UpdateAndFindResult(NullKey);
			}
			else
			{
				return FindAllClassesQuery.UpdateAndFindResult(NullKey);
			}
		}

		TArray<FMetasoundFrontendClass> FSearchEngine::FindClassesWithName(const FMetasoundFrontendClassName& InName, bool bInSortByVersion)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassesWithName);

			const FFrontendQueryKey Key(InName.GetFullName());
			if (bInSortByVersion)
			{
				return FindClassesWithNameSortedQuery.UpdateAndFindResult(Key);
			}
			else 
			{
				return FindClassesWithNameUnsortedQuery.UpdateAndFindResult(Key);
			}
		}

		bool FSearchEngine::FindClassWithHighestVersion(const FMetasoundFrontendClassName& InName, FMetasoundFrontendClass& OutClass)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassWithHighestVersion);

			const FFrontendQueryKey Key{InName.GetFullName()};
			return FindClassWithHighestVersionQuery.UpdateAndFindResult(Key, OutClass);
		}

		bool FSearchEngine::FindClassWithMajorVersion(const FMetasoundFrontendClassName& InName, int32 InMajorVersion, FMetasoundFrontendClass& OutClass)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindClassWithMajorVersion);
			using namespace SearchEngineQuerySteps;

			const FFrontendQueryKey Key = FMapClassToFullClassNameAndMajorVersion::GetKey(InName, InMajorVersion);
			return FindClassWithMajorVersionQuery.UpdateAndFindResult(Key, OutClass);
		}

		TArray<FMetasoundFrontendInterface> FSearchEngine::FindAllInterfaces(bool bInIncludeAllVersions)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindAllInterfaces);

			const FFrontendQueryKey NullKey;
			if (bInIncludeAllVersions)
			{
				return FindAllInterfacesIncludingAllVersionsQuery.UpdateAndFindResult(NullKey);
			}
			else
			{
				return FindAllInterfacesQuery.UpdateAndFindResult(NullKey);
			}
		}

		TArray<FMetasoundFrontendInterface> FSearchEngine::FindUClassDefaultInterfaces(FName InUClassName)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindUClassDefaultInterfaces);

			const FFrontendQueryKey NullKey;
			TArray<FMetasoundFrontendInterface> DefaultInterfaces = FFindAllDefaultInterfacesQuery.UpdateAndFindResult(NullKey);

			// All default interfaces are filtered here because a UClass name cannot effectively 
			// be used as a "Key" for this query.
			//
			// We cannot know all the UClasses associated with each default interface given only a UClassName
			// because we need to inspect UClass inheritance to handle derived UClasses
			// which should be compatible with their parent's default interfaces. 
			//
			// This module does not have access to the UClass inheritance structure.
			// Instead, this information is accessed in the IInterfaceRegistryEntry
			// concrete implementation. 
			auto DoesNotSupportUClass = [&InUClassName](const FMetasoundFrontendInterface& InInterface)
			{
				FInterfaceRegistryKey RegistryKey = GetInterfaceRegistryKey(InInterface);
				const IInterfaceRegistryEntry* RegEntry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);
				if (ensure(RegEntry))
				{
					return !RegEntry->UClassIsSupported(InUClassName);
				}

				return true;
			};

			DefaultInterfaces.RemoveAllSwap(DoesNotSupportUClass);

			return DefaultInterfaces;
		}

		TArray<FMetasoundFrontendVersion> FSearchEngine::FindAllRegisteredInterfacesWithName(FName InInterfaceName)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindAllRegisteredInterfacesWithName);

			const FFrontendQueryKey Key{InInterfaceName};
			return FindAllRegisteredInterfacesWithNameQuery.UpdateAndFindResult(Key);
		}

		bool FSearchEngine::FindInterfaceWithHighestVersion(FName InInterfaceName, FMetasoundFrontendInterface& OutInterface)
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(metasound::FSearchEngine::FindInterfaceWithHighestVersion);

			const FFrontendQueryKey Key{InInterfaceName};
			return FindInterfaceWithHighestVersionQuery.UpdateAndFindResult(Key, OutInterface);
		}
	}
}
