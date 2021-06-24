// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontendDocument.h"

namespace Metasound
{
	namespace Frontend
	{

		METASOUNDFRONTEND_API bool IsSubsetOfArchetype(const FMetasoundFrontendArchetype& InSubsetArchetype, const FMetasoundFrontendArchetype& InSupersetArchetype);

		METASOUNDFRONTEND_API bool IsSubsetOfClass(const FMetasoundFrontendArchetype& InSubsetArchetype, const FMetasoundFrontendClass& InSupersetClass);

		METASOUNDFRONTEND_API bool IsEquivalentArchetype(const FMetasoundFrontendArchetype& InInputArchetype, const FMetasoundFrontendArchetype& InTargetArchetype);

		METASOUNDFRONTEND_API bool IsEqualArchetype(const FMetasoundFrontendArchetype& InInputArchetype, const FMetasoundFrontendArchetype& InTargetArchetype);

		METASOUNDFRONTEND_API int32 InputOutputDifferenceCount(const FMetasoundFrontendClass& InClass, const FMetasoundFrontendArchetype& InArchetype);

		METASOUNDFRONTEND_API int32 InputOutputDifferenceCount(const FMetasoundFrontendArchetype& InArchetypeA, const FMetasoundFrontendArchetype& InArchetypeB);

		METASOUNDFRONTEND_API void GatherRequiredEnvironmentVariables(const FMetasoundFrontendClass& InClass, TArray<FMetasoundFrontendEnvironmentVariable>& OutEnvironmentVariables);

		METASOUNDFRONTEND_API const FMetasoundFrontendArchetype* FindMostSimilarArchetypeSupportingEnvironment(const FMetasoundFrontendGraphClass& InRootGraph, const TArray<FMetasoundFrontendClass>& InDependencies, const TArray<FMetasoundFrontendGraphClass>& InSubgraphs, const TArray<FMetasoundFrontendArchetype>& InCandidateArchetypes);

		METASOUNDFRONTEND_API const FMetasoundFrontendArchetype* FindMostSimilarArchetypeSupportingEnvironment(const FMetasoundFrontendDocument& InDocument, const TArray<FMetasoundFrontendArchetype>& InCandidateArchetypes);
	}
}

