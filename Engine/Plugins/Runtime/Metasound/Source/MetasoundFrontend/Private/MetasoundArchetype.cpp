// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetasoundArchetype.h"

#include "Algo/ForEach.h"
#include "MetasoundFrontendDocument.h"

namespace Metasound
{
	namespace Frontend
	{
		namespace MetasoundArchetypeIntrinsics
		{
			bool IsLessThanVertex(const FMetasoundFrontendVertex& VertexA, const FMetasoundFrontendVertex& VertexB)
			{
				// For archetypes we can ignore metadata when comparing
				if (VertexA.Name == VertexB.Name)
				{
					return VertexA.TypeName.FastLess(VertexB.TypeName);
				}
				return VertexA.Name < VertexB.Name;
			}

			bool IsLessThanEnvironmentVariable(const FMetasoundFrontendEnvironmentVariable& EnvironmentA, const FMetasoundFrontendEnvironmentVariable& EnvironmentB)
			{
				// For archetypes we can ignore metadata when comparing
				if (EnvironmentA.Name == EnvironmentB.Name)
				{
					return EnvironmentA.TypeName.FastLess(EnvironmentB.TypeName);
				}
				return EnvironmentA.Name < EnvironmentB.Name;
			}

			bool IsEquivalentEnvironmentVariable(const FMetasoundFrontendEnvironmentVariable& EnvironmentA, const FMetasoundFrontendEnvironmentVariable& EnvironmentB)
			{
				// For archetypes we can ignore display name and tooltip
				return (EnvironmentA.Name == EnvironmentB.Name) && (EnvironmentA.TypeName == EnvironmentB.TypeName);
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

			template<typename FrontendTypeA, typename FrontendTypeB, typename ComparisonType>
			void SetDifference(const TArray<FrontendTypeA>& InSetA, const TArray<FrontendTypeB>& InSetB, TArray<const FrontendTypeA*>& OutUniqueA, TArray<const FrontendTypeB*>& OutUniqueB, ComparisonType Compare)
			{
				TArray<const FrontendTypeA*> SortedA = MakePointerToArrayElements(InSetA);
				SortedA.Sort(Compare);

				TArray<const FrontendTypeB*> SortedB = MakePointerToArrayElements(InSetB);
				SortedB.Sort(Compare);

				int32 IndexA = 0;
				int32 IndexB = 0;

				while ((IndexA < SortedA.Num()) && (IndexB < SortedB.Num()))
				{
					if (Compare(*SortedA[IndexA], *SortedB[IndexB]))
					{
						OutUniqueA.Add(SortedA[IndexA]);
						IndexA++;
					}
					else if (Compare(*SortedB[IndexB], *SortedA[IndexA]))
					{
						OutUniqueB.Add(SortedB[IndexB]);
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

			template<typename FrontendTypeA, typename FrontendTypeB, typename ComparisonType>
			bool IsSetEquivalent(const TArray<FrontendTypeA>& InSetA, const TArray<FrontendTypeB>& InSetB, ComparisonType Compare)
			{
				TArray<const FrontendTypeA*> UniqueA;
				TArray<const FrontendTypeB*> UniqueB;

				SetDifference(InSetA, InSetB, UniqueA, UniqueB, Compare);

				return (UniqueA.Num() == 0) && (UniqueB.Num() == 0);
			}
			
			template<typename FrontendTypeA, typename FrontendTypeB, typename ComparisonType>
			bool IsSetIncluded(const TArray<FrontendTypeA>& InSubset, const TArray<FrontendTypeB>& InSuperset, ComparisonType Compare)
			{
				TArray<const FrontendTypeA*> UniqueSubset;
				TArray<const FrontendTypeB*> UniqueSuperset;

				SetDifference(InSubset, InSuperset, UniqueSubset, UniqueSuperset, Compare);

				return UniqueSubset.Num() == 0;
			}

			template<typename FrontendTypeA, typename FrontendTypeB, typename ComparisonType>
			int32 SetDifferenceCount(const TArray<FrontendTypeA>& InSetA, const TArray<FrontendTypeB>& InSetB, ComparisonType Compare)
			{
				TArray<const FrontendTypeA*> UniqueA;
				TArray<const FrontendTypeB*> UniqueB;

				SetDifference(InSetA, InSetB, UniqueA, UniqueB, Compare);

				return UniqueA.Num() + UniqueB.Num();
			}
		}

		bool IsSubsetOfArchetype(const FMetasoundFrontendArchetype& InSubsetArchetype, const FMetasoundFrontendArchetype& InSupersetArchetype)
		{
			using namespace MetasoundArchetypeIntrinsics;

			const bool bIsInputSetIncluded = IsSetIncluded(InSubsetArchetype.Interface.Inputs, InSupersetArchetype.Interface.Inputs, &IsLessThanVertex);

			const bool bIsOutputSetIncluded = IsSetIncluded(InSubsetArchetype.Interface.Outputs, InSupersetArchetype.Interface.Outputs, &IsLessThanVertex);

			const bool bIsEnvironmentVariableSetIncluded = IsSetIncluded(InSubsetArchetype.Interface.Environment, InSupersetArchetype.Interface.Environment, &IsLessThanEnvironmentVariable);

			return bIsInputSetIncluded && bIsOutputSetIncluded && bIsEnvironmentVariableSetIncluded;
		}

		bool IsEquivalentArchetype(const FMetasoundFrontendArchetype& InInputArchetype, const FMetasoundFrontendArchetype& InTargetArchetype)
		{
			using namespace MetasoundArchetypeIntrinsics;

			const bool bIsInputSetEquivalent = IsSetEquivalent(InTargetArchetype.Interface.Inputs, InInputArchetype.Interface.Inputs, &IsLessThanVertex);

			const bool bIsOutputSetEquivalent = IsSetEquivalent(InTargetArchetype.Interface.Outputs, InInputArchetype.Interface.Outputs, &IsLessThanVertex);

			const bool bIsEnvironmentVariableSetEquivalent = IsSetEquivalent(InTargetArchetype.Interface.Environment, InInputArchetype.Interface.Environment, &IsLessThanEnvironmentVariable);

			return bIsInputSetEquivalent && bIsOutputSetEquivalent && bIsEnvironmentVariableSetEquivalent;
		}

		bool IsEqualArchetype(const FMetasoundFrontendArchetype& InInputArchetype, const FMetasoundFrontendArchetype& InTargetArchetype)
		{
			bool bIsMetadataEqual = InInputArchetype.Name == InTargetArchetype.Name;
			bIsMetadataEqual &= InInputArchetype.Version.Major == InTargetArchetype.Version.Major;
			bIsMetadataEqual &= InInputArchetype.Version.Minor== InTargetArchetype.Version.Minor;

			if (bIsMetadataEqual)
			{
				// If metadata is equal, then inputs/outputs/environment variables should also be equal. 
				// Consider updating archetype version info if this ensure is hit.
				return ensure(IsEquivalentArchetype(InInputArchetype, InTargetArchetype));
			}

			return false;
		}

		int32 InputOutputDifferenceCount(const FMetasoundFrontendDocument& InDocument, const FMetasoundFrontendArchetype& InArchetype)
		{
			using namespace MetasoundArchetypeIntrinsics;

			int32 DiffCount = SetDifferenceCount(InDocument.RootGraph.Interface.Inputs, InArchetype.Interface.Inputs, &IsLessThanVertex);

			DiffCount += SetDifferenceCount(InDocument.RootGraph.Interface.Outputs, InArchetype.Interface.Outputs, &IsLessThanVertex);

			return DiffCount;
		}

		int32 InputOutputDifferenceCount(const FMetasoundFrontendArchetype& InArchetypeA, const FMetasoundFrontendArchetype& InArchetypeB)
		{
			using namespace MetasoundArchetypeIntrinsics;

			int32 DiffCount = SetDifferenceCount(InArchetypeA.Interface.Inputs, InArchetypeB.Interface.Inputs, &IsLessThanVertex);

			DiffCount += SetDifferenceCount(InArchetypeA.Interface.Outputs, InArchetypeB.Interface.Outputs, &IsLessThanVertex);

			return DiffCount;
		}

		void GatherRequiredEnvironmentVariables(const FMetasoundFrontendDocument& InDocument, TArray<FMetasoundFrontendEnvironmentVariable>& OutEnvironmentVariables)
		{
			auto GatherRequiredEnvironmentVariablesFromClass = [&](const FMetasoundFrontendClass& InClass)
			{
				using namespace MetasoundArchetypeIntrinsics;

				for (const FMetasoundFrontendClassEnvironmentVariable& EnvVar : InClass.Interface.Environment)
				{
					if (EnvVar.bIsRequired)
					{
						auto IsEquivalentEnvVar = [&](const FMetasoundFrontendEnvironmentVariable& OtherEnvVar) 
						{ 
							return IsEquivalentEnvironmentVariable(EnvVar, OtherEnvVar); 
						};

						// Basically same as TArray::AddUnique except uses the `IsEquivalentEnvironmentVariable` instead of `operator==` to test for uniqueness.
						if (nullptr == OutEnvironmentVariables.FindByPredicate(IsEquivalentEnvVar))
						{
							OutEnvironmentVariables.Add(EnvVar);
						}
					}
				}
			};

			GatherRequiredEnvironmentVariablesFromClass(InDocument.RootGraph);
			Algo::ForEach(InDocument.Dependencies, GatherRequiredEnvironmentVariablesFromClass);
			Algo::ForEach(InDocument.Subgraphs, GatherRequiredEnvironmentVariablesFromClass);
		}

		const FMetasoundFrontendArchetype* FindMostSimilarArchetypeSupportingEnvironment(const FMetasoundFrontendDocument& InDocument, const TArray<FMetasoundFrontendArchetype>& InCandidateArchetypes)
		{
			using namespace MetasoundArchetypeIntrinsics;

			const FMetasoundFrontendArchetype* Result = nullptr;

			TArray<const FMetasoundFrontendArchetype*> CandidateArchs = MakePointerToArrayElements(InCandidateArchetypes);

			TArray<FMetasoundFrontendEnvironmentVariable> AllDocumentEnvironmentVariables;
			GatherRequiredEnvironmentVariables(InDocument, AllDocumentEnvironmentVariables);

			// Remove all archetypes which do not provide all environment variables. 
			auto DoesNotProvideAllEnvironmentVariables = [&](const FMetasoundFrontendArchetype* CandidateArch)
			{
				return !IsSetIncluded(AllDocumentEnvironmentVariables, CandidateArch->Interface.Environment, &IsLessThanEnvironmentVariable);
			};

			CandidateArchs.RemoveAll(DoesNotProvideAllEnvironmentVariables);

			// Return the archetype with the least amount of differences.
			auto DifferencesFromDocument = [&](const FMetasoundFrontendArchetype* CandidateArch) -> int32
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
