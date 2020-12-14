// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundArchetype.h"

namespace Metasound
{
	namespace Frontend
	{
		namespace MetasoundArchetypeIntrinsics
		{

			bool IsLessThanInputDescription(const FMetasoundInputDescription& InputA, const FMetasoundInputDescription& InputB)
			{
				// For archetypes we can ignore display name, tooltip and literalvalue.
				if (InputA.Name == InputB.Name)
				{
					return InputA.TypeName.FastLess(InputB.TypeName);
				}
				return InputA.Name < InputB.Name;
			}

			bool IsLessThanOutputDescription(const FMetasoundOutputDescription& OutputA, const FMetasoundOutputDescription& OutputB)
			{
				// For archetypes we can ignore display name and tooltip
				if (OutputA.Name == OutputB.Name)
				{
					return OutputA.TypeName.FastLess(OutputB.TypeName);
				}
				return OutputA.Name < OutputB.Name;
			}

			bool IsLessThanEnvironmentDescription(const FMetasoundEnvironmentVariableDescription& EnvironmentA, const FMetasoundEnvironmentVariableDescription& EnvironmentB)
			{
				// For archetypes we can ignore display name and tooltip
				return (EnvironmentA.Name < EnvironmentB.Name);
			}

			bool IsEquivalentEnvironmentDescription(const FMetasoundEnvironmentVariableDescription& EnvironmentA, const FMetasoundEnvironmentVariableDescription& EnvironmentB)
			{
				// For archetypes we can ignore display name and tooltip
				return (EnvironmentA.Name == EnvironmentB.Name);
			}



			template<typename Type>
			TArray<const Type*> MakePointerToArrayElements(const TArray<Type>& InArray)
			{
				TArray<const Type*> PointerArray;
				for (const Type& Element : InArray)
				{
					PointerArray.Add(&Element);
				}
				return PointerArray;
			}

			template<typename DescriptionType, typename ComparisonType>
			void DescriptionSetDifference(const TArray<DescriptionType>& InDescriptionsA, const TArray<DescriptionType>& InDescriptionsB, TArray<const DescriptionType*>& OutUniqueDescriptionsA, TArray<const DescriptionType*>& OutUniqueDescriptionsB, ComparisonType Compare)
			{
				TArray<const DescriptionType*> SortedA = MakePointerToArrayElements(InDescriptionsA);
				SortedA.Sort(Compare);

				TArray<const DescriptionType*> SortedB = MakePointerToArrayElements(InDescriptionsB);
				SortedB.Sort(Compare);

				int32 IndexA = 0;
				int32 IndexB = 0;

				while ((IndexA < SortedA.Num()) && (IndexB < SortedB.Num()))
				{
					if (Compare(*SortedA[IndexA], *SortedB[IndexB]))
					{
						OutUniqueDescriptionsA.Add(SortedA[IndexA]);
						IndexA++;
					}
					else if (Compare(*SortedB[IndexB], *SortedA[IndexA]))
					{
						OutUniqueDescriptionsB.Add(SortedB[IndexB]);
						IndexB++;
					}
					else
					{
						// Equal. Increment both indices.
						IndexA++;
						IndexB++;
					}
				}
			}

			template<typename DescriptionType, typename ComparisonType>
			bool IsDescriptionSetEquivalent(const TArray<DescriptionType>& InDescriptionsA, const TArray<DescriptionType>& InDescriptionsB, ComparisonType Compare)
			{
				TArray<const DescriptionType*> UniqueA;
				TArray<const DescriptionType*> UniqueB;

				DescriptionSetDifference(InDescriptionsA, InDescriptionsB, UniqueA, UniqueB, Compare);

				return (UniqueA.Num() == 0) && (UniqueB.Num() == 0);
			}
			
			template<typename DescriptionType, typename ComparisonType>
			bool IsDescriptionSetIncluded(const TArray<DescriptionType>& InSubset, const TArray<DescriptionType>& InSuperset, ComparisonType Compare)
			{
				TArray<const DescriptionType*> UniqueSubset;
				TArray<const DescriptionType*> UniqueSuperset;

				DescriptionSetDifference(InSubset, InSuperset, UniqueSubset, UniqueSuperset, Compare);

				return UniqueSubset.Num() == 0;
			}

			template<typename DescriptionType, typename ComparisonType>
			int32 DescriptionSetDifferenceCount(const TArray<DescriptionType>& InDescriptionsA, const TArray<DescriptionType>& InDescriptionsB, ComparisonType Compare)
			{
				TArray<const DescriptionType*> UniqueA;
				TArray<const DescriptionType*> UniqueB;

				DescriptionSetDifference(InDescriptionsA, InDescriptionsB, UniqueA, UniqueB, Compare);

				return UniqueA.Num() + UniqueB.Num();
			}
		}

		bool IsSubsetOfArchetype(const FMetasoundArchetype& InSubsetArchetype, const FMetasoundArchetype& InSupersetArchetype)
		{
			using namespace MetasoundArchetypeIntrinsics;

			const bool bIsInputSetIncluded = IsDescriptionSetIncluded(InSubsetArchetype.RequiredInputs, InSupersetArchetype.RequiredInputs, &IsLessThanInputDescription);

			const bool bIsOutputSetIncluded = IsDescriptionSetIncluded(InSubsetArchetype.RequiredOutputs, InSupersetArchetype.RequiredOutputs, &IsLessThanOutputDescription);

			const bool bIsEnvironmentVariableSetIncluded = IsDescriptionSetIncluded(InSubsetArchetype.EnvironmentVariables, InSupersetArchetype.EnvironmentVariables, &IsLessThanEnvironmentDescription);

			return bIsInputSetIncluded && bIsOutputSetIncluded && bIsEnvironmentVariableSetIncluded;
		}

		bool IsEquivalentArchetype(const FMetasoundArchetype& InInputArchetype, const FMetasoundArchetype& InTargetArchetype)
		{
			using namespace MetasoundArchetypeIntrinsics;

			const bool bIsInputSetEquivalent = IsDescriptionSetEquivalent(InTargetArchetype.RequiredInputs, InInputArchetype.RequiredInputs, &IsLessThanInputDescription);

			const bool bIsOutputSetEquivalent = IsDescriptionSetEquivalent(InTargetArchetype.RequiredOutputs, InInputArchetype.RequiredOutputs, &IsLessThanOutputDescription);

			const bool bIsEnvironmentVariableSetEquivalent = IsDescriptionSetEquivalent(InTargetArchetype.EnvironmentVariables, InInputArchetype.EnvironmentVariables, &IsLessThanEnvironmentDescription);

			return bIsInputSetEquivalent && bIsOutputSetEquivalent && bIsEnvironmentVariableSetEquivalent;
		}

		bool IsEqualArchetype(const FMetasoundArchetype& InInputArchetype, const FMetasoundArchetype& InTargetArchetype)
		{
			bool bIsMetadataEqual = InInputArchetype.ArchetypeName == InTargetArchetype.ArchetypeName;
			bIsMetadataEqual &= InInputArchetype.MajorVersion == InTargetArchetype.MajorVersion;
			bIsMetadataEqual &= InInputArchetype.MinorVersion == InTargetArchetype.MinorVersion;

			if (bIsMetadataEqual)
			{
				// If metadata is equal, then inputs/outputs/environment variables should also be equal. 
				// Consider updating archetype version info if this ensure is hit.
				return ensure(IsEquivalentArchetype(InInputArchetype, InTargetArchetype));
			}

			return false;
		}

		int32 InputOutputDifferenceCount(const FMetasoundDocument& InDocument, const FMetasoundArchetype& InArchetype)
		{
			using namespace MetasoundArchetypeIntrinsics;

			int32 DiffCount = DescriptionSetDifferenceCount(InDocument.RootClass.Inputs, InArchetype.RequiredInputs, &IsLessThanInputDescription);

			DiffCount += DescriptionSetDifferenceCount(InDocument.RootClass.Outputs, InArchetype.RequiredOutputs, &IsLessThanOutputDescription);

			return DiffCount;
		}

		int32 InputOutputDifferenceCount(const FMetasoundArchetype& InArchetypeA, const FMetasoundArchetype& InArchetypeB)
		{
			using namespace MetasoundArchetypeIntrinsics;

			int32 DiffCount = DescriptionSetDifferenceCount(InArchetypeA.RequiredInputs, InArchetypeB.RequiredInputs, &IsLessThanInputDescription);

			DiffCount += DescriptionSetDifferenceCount(InArchetypeA.RequiredOutputs, InArchetypeB.RequiredOutputs, &IsLessThanOutputDescription);

			return DiffCount;
		}

		void GatherRequiredEnvironmentVariables(const FMetasoundDocument& InDocument, TArray<FMetasoundEnvironmentVariableDescription>& OutEnvironmentVariables)
		{
			using namespace MetasoundArchetypeIntrinsics;

			OutEnvironmentVariables = InDocument.RootClass.EnvironmentVariables;

			for (const FMetasoundClassDescription& MetasoundClass : InDocument.Dependencies)
			{
				for (const FMetasoundEnvironmentVariableDescription& EnvVar : MetasoundClass.EnvironmentVariables)
				{
					auto IsEquivalentEnvVar = [&](const FMetasoundEnvironmentVariableDescription& OtherEnvVar) 
					{ 
						return IsEquivalentEnvironmentDescription(EnvVar, OtherEnvVar); 
					};

					// Basically same as TArray::AddUnique except uses the `IsEquivalentEnvironmentDescription` instead of `operator==` to test for uniqueness.
					if (nullptr == OutEnvironmentVariables.FindByPredicate(IsEquivalentEnvVar))
					{
						OutEnvironmentVariables.Add(EnvVar);
					}
				}
			}
		}

		const FMetasoundArchetype* FindMostSimilarArchetypeSupportingEnvironment(const FMetasoundDocument& InDocument, const TArray<FMetasoundArchetype>& InCandidateArchetypes)
		{
			using namespace MetasoundArchetypeIntrinsics;

			const FMetasoundArchetype* Result = nullptr;

			TArray<const FMetasoundArchetype*> CandidateArchs = MakePointerToArrayElements(InCandidateArchetypes);

			TArray<FMetasoundEnvironmentVariableDescription> AllDocumentEnvironmentVariables;
			GatherRequiredEnvironmentVariables(InDocument, AllDocumentEnvironmentVariables);

			// Remove all archetypes which do not provide all environment variables. 
			auto DoesNotProvideAllEnvironmentVariables = [&](const FMetasoundArchetype* CandidateArch)
			{
				return !IsDescriptionSetIncluded(AllDocumentEnvironmentVariables, CandidateArch->EnvironmentVariables, &IsLessThanEnvironmentDescription);
			};

			CandidateArchs.RemoveAll(DoesNotProvideAllEnvironmentVariables);

			// Return the archetype with the least amount of differences.
			auto DifferencesFromDocument = [&](const FMetasoundArchetype* CandidateArch) -> int32
			{ 
				return InputOutputDifferenceCount(InDocument, *CandidateArch); 
			};

			Algo::SortBy(CandidateArchs, DifferencesFromDocument);

			if (0 == CandidateArchs.Num())
			{
				return nullptr;
			}

			return CandidateArchs[0];
		}
	}
}
