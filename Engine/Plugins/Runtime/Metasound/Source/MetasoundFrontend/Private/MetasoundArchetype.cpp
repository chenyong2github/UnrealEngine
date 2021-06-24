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

				if (IndexA < SortedA.Num())
				{
					OutUniqueA.Append(&SortedA[IndexA], SortedA.Num() - IndexA);
				}

				if (IndexB < SortedB.Num())
				{
					OutUniqueB.Append(&SortedB[IndexB], SortedB.Num() - IndexB);
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

		bool IsSubsetOfClass(const FMetasoundFrontendArchetype& InSubsetArchetype, const FMetasoundFrontendClass& InSupersetClass)
		{
			// TODO: Environment variables are ignored as they are poorly supported and classes describe which environment variables are required,
			// not which are supported 
			using namespace MetasoundArchetypeIntrinsics;

			const bool bIsInputSetIncluded = IsSetIncluded(InSubsetArchetype.Interface.Inputs, InSupersetClass.Interface.Inputs, &IsLessThanVertex);

			const bool bIsOutputSetIncluded = IsSetIncluded(InSubsetArchetype.Interface.Outputs, InSupersetClass.Interface.Outputs, &IsLessThanVertex);

			return bIsInputSetIncluded && bIsOutputSetIncluded;
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
			bool bIsMetadataEqual = InInputArchetype.Version.Name == InTargetArchetype.Version.Name;
			bIsMetadataEqual &= InInputArchetype.Version.Number.Major == InTargetArchetype.Version.Number.Major;
			bIsMetadataEqual &= InInputArchetype.Version.Number.Minor == InTargetArchetype.Version.Number.Minor;

			if (bIsMetadataEqual)
			{
				// If metadata is equal, then inputs/outputs/environment variables should also be equal. 
				// Consider updating archetype version info if this ensure is hit.
				return ensure(IsEquivalentArchetype(InInputArchetype, InTargetArchetype));
			}

			return false;
		}

		int32 InputOutputDifferenceCount(const FMetasoundFrontendClass& InClass, const FMetasoundFrontendArchetype& InArchetype)
		{
			using namespace MetasoundArchetypeIntrinsics;

			int32 DiffCount = SetDifferenceCount(InClass.Interface.Inputs, InArchetype.Interface.Inputs, &IsLessThanVertex);

			DiffCount += SetDifferenceCount(InClass.Interface.Outputs, InArchetype.Interface.Outputs, &IsLessThanVertex);

			return DiffCount;
		}

		int32 InputOutputDifferenceCount(const FMetasoundFrontendArchetype& InArchetypeA, const FMetasoundFrontendArchetype& InArchetypeB)
		{
			using namespace MetasoundArchetypeIntrinsics;

			int32 DiffCount = SetDifferenceCount(InArchetypeA.Interface.Inputs, InArchetypeB.Interface.Inputs, &IsLessThanVertex);

			DiffCount += SetDifferenceCount(InArchetypeA.Interface.Outputs, InArchetypeB.Interface.Outputs, &IsLessThanVertex);

			return DiffCount;
		}

		void GatherRequiredEnvironmentVariables(const FMetasoundFrontendGraphClass& InRootGraph, const TArray<FMetasoundFrontendClass>& InDependencies, const TArray<FMetasoundFrontendGraphClass>& InSubgraphs, TArray<FMetasoundFrontendEnvironmentVariable>& OutEnvironmentVariables)
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

			GatherRequiredEnvironmentVariablesFromClass(InRootGraph);
			Algo::ForEach(InDependencies, GatherRequiredEnvironmentVariablesFromClass);
			Algo::ForEach(InSubgraphs, GatherRequiredEnvironmentVariablesFromClass);
		}

		
		const FMetasoundFrontendArchetype* FindMostSimilarArchetypeSupportingEnvironment(const FMetasoundFrontendGraphClass& InRootGraph, const TArray<FMetasoundFrontendClass>& InDependencies, const TArray<FMetasoundFrontendGraphClass>& InSubgraphs, const TArray<FMetasoundFrontendArchetype>& InCandidateArchetypes)
		{
			using namespace MetasoundArchetypeIntrinsics;

			const FMetasoundFrontendArchetype* Result = nullptr;

			TArray<const FMetasoundFrontendArchetype*> CandidateArchs = MakePointerToArrayElements(InCandidateArchetypes);

			TArray<FMetasoundFrontendEnvironmentVariable> AllEnvironmentVariables;
			GatherRequiredEnvironmentVariables(InRootGraph, InDependencies, InSubgraphs, AllEnvironmentVariables);

			// Remove all archetypes which do not provide all environment variables. 
			auto DoesNotProvideAllEnvironmentVariables = [&](const FMetasoundFrontendArchetype* CandidateArch)
			{
				return !IsSetIncluded(AllEnvironmentVariables, CandidateArch->Interface.Environment, &IsLessThanEnvironmentVariable);
			};

			CandidateArchs.RemoveAll(DoesNotProvideAllEnvironmentVariables);

			// Return the archetype with the least amount of differences.
			auto DifferencesFromRootGraph = [&](const FMetasoundFrontendArchetype* CandidateArch) -> int32
			{ 
				return InputOutputDifferenceCount(InRootGraph, *CandidateArch); 
			};

			Algo::SortBy(CandidateArchs, DifferencesFromRootGraph);

			if (0 == CandidateArchs.Num())
			{
				return nullptr;
			}

			return CandidateArchs[0];
		}
		
		const FMetasoundFrontendArchetype* FindMostSimilarArchetypeSupportingEnvironment(const FMetasoundFrontendDocument& InDocument, const TArray<FMetasoundFrontendArchetype>& InCandidateArchetypes)
		{
			return FindMostSimilarArchetypeSupportingEnvironment(InDocument.RootGraph, InDocument.Dependencies, InDocument.Subgraphs, InCandidateArchetypes);
		}
	}
}
