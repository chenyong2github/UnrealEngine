// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendSearchEngine.h"

#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"

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

				TArray<FMetasoundFrontendClass> FindClassesWithClassName(const FNodeClassName& InName) override;

			private:

				FFrontendQuery* FindQuery(const FString& InQueryName);
				FFrontendQuery* AddQuery(const FString& InQueryName, TUniquePtr<FFrontendQuery>&& InQuery);

				// Store queries. Index them by unique strings. 
				TMap<FString, TUniquePtr<FFrontendQuery>> QueryCache;
		};

		ISearchEngine& ISearchEngine::Get()
		{
			static FSearchEngine SearchEngine;
			return SearchEngine;
		}

		TArray<FMetasoundFrontendClass> FSearchEngine::FindClassesWithClassName(const FNodeClassName& InName) 
		{
			// Unique string for query with given parameter name. 
			FString QueryName = FString(TEXT("FindClassesWithClassName")) + InName.GetFullName().ToString();

			FFrontendQuery* Query = FindQuery(QueryName);

			if (nullptr == Query)
			{
				// If the query doesn't exist, it needs to be created. 
				TUniquePtr<FFrontendQuery> NewQuery = MakeUnique<FFrontendQuery>();

				// Note: Initial query step is FGenerateNewlyAvailableNodeClasses, which does not run query against already examined node classes.
				NewQuery->AddStep<FGenerateNewlyAvailableNodeClasses>()
					.AddStep<FFilterClassesByClassName>(InName);

				Query = AddQuery(QueryName, MoveTemp(NewQuery));
			}

			TArray<FMetasoundFrontendClass> Result;

			if (ensure(nullptr != Query))
			{
				// Execute the query again and append the results. (Note: Initial query 
				// step is FGenerateNewlyAvailableNodeClasses, which does not run query 
				// against already examined node classes.)
				FFrontendQuerySelectionView Selection = Query->ExecuteQueryAndAppend();

				TArrayView<const FFrontendQueryEntry* const> Entries = Selection.GetSelection();

				// Copy result to array
				for (const FFrontendQueryEntry* Entry : Selection.GetSelection())
				{
					check(Entry->Value.IsType<FMetasoundFrontendClass>());
					Result.Add(Entry->Value.Get<FMetasoundFrontendClass>());
				}
			}

			return Result;
		}

		FFrontendQuery* FSearchEngine::FindQuery(const FString& InQueryName)
		{
			if (TUniquePtr<FFrontendQuery>* QueryUPtr = QueryCache.Find(InQueryName))
			{
				return QueryUPtr->Get();
			}

			return nullptr;
		}

		FFrontendQuery* FSearchEngine::AddQuery(const FString& InQueryName, TUniquePtr<FFrontendQuery>&& InQuery)
		{
			FFrontendQuery* QueryPtr = InQuery.Get();

			QueryCache.Add(InQueryName, MoveTemp(InQuery));

			return QueryPtr;
		}
	}
}
