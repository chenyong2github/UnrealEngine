// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontendDataLayout.h"

namespace Metasound
{
	namespace Frontend
	{
		METASOUNDFRONTEND_API bool IsSubsetOfArchetype(const FMetasoundArchetype& InSubsetArchetype, const FMetasoundArchetype& InSupersetArchetype);

		METASOUNDFRONTEND_API bool IsEquivalentArchetype(const FMetasoundArchetype& InInputArchetype, const FMetasoundArchetype& InTargetArchetype);

		METASOUNDFRONTEND_API bool IsEqualArchetype(const FMetasoundArchetype& InInputArchetype, const FMetasoundArchetype& InTargetArchetype);

		METASOUNDFRONTEND_API int32 InputOutputDifferenceCount(const FMetasoundArchetype& InArchetypeA, const FMetasoundArchetype& InArchetypeB);

		METASOUNDFRONTEND_API void GatherRequiredEnvironmentVariables(const FMetasoundDocument& InDocument, TArray<FMetasoundEnvironmentVariableDescription>& OutEnvironmentVariables);

		METASOUNDFRONTEND_API const FMetasoundArchetype* FindMostSimilarArchetypeSupportingEnvironment(const FMetasoundDocument& InDocument, const TArray<FMetasoundArchetype>& InCandidateArchetypes);
	}
}

