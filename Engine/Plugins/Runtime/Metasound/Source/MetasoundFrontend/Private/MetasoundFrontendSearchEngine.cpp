// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendSearchEngine.h"

#include "Algo/Transform.h"
#include "HAL/IConsoleManager.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"

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
			FString QueryName = FString(TEXT("FindAllClasses"));
			if (bInIncludeDeprecated)
			{
				QueryName += TEXT("IncludingDeprecated");
			}

			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FGenerateNewlyAvailableNodeClasses>();

				if (!bInIncludeDeprecated)
				{
					NewQuery->AddStep<FMapClassNameToMajorVersion>()
						.AddStep<FReduceClassesToHighestVersion>();
				}

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendClass> Result;

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->ExecuteQueryAndAppend();
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
				NewQuery->AddStep<FGenerateNewlyAvailableNodeClasses>()
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
				FFrontendQuerySelectionView Selection = Query->ExecuteQueryAndAppend();
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
			const FString QueryName = FString(TEXT("FindHighestVersion")) + InName.GetFullName().ToString();
			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FGenerateNewlyAvailableNodeClasses>()
					.AddStep<FFilterClassesByClassName>(InName)
					.AddStep<FMapClassNameToMajorVersion>()
					.AddStep<FReduceClassesToHighestVersion>();

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->ExecuteQueryAndAppend();
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
			const FString QueryName = FString(TEXT("FindClassWithMajorVersion")) + InName.GetFullName().ToString() + FString::FromInt(InMajorVersion);
			FFrontendQuery* Query = FindQuery(QueryName);
			if (!Query)
			{
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				NewQuery->AddStep<FGenerateNewlyAvailableNodeClasses>()
					.AddStep<FFilterClassesByClassName>(InName)
					.AddStep<FMapClassNameToMajorVersion>()
					.AddStep<FReduceClassesToMajorVersion>(InMajorVersion);

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			if (ensure(Query))
			{
				FFrontendQuerySelectionView Selection = Query->ExecuteQueryAndAppend();
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
