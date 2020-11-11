// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendQuerySteps.h"

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"

namespace Metasound
{
	void FGenerateAllAvailableNodeClasses::Generate(TArray<FFrontendQueryEntry>& OutEntries) const
	{
		const TArray<Frontend::FNodeClassInfo> ClassInfos = Frontend::GetAllAvailableNodeClasses();

		for (const Frontend::FNodeClassInfo& ClassInfo : ClassInfos)
		{
			OutEntries.Emplace(TInPlaceType<FMetasoundClassDescription>(), Frontend::GenerateClassDescriptionForNode(ClassInfo));
		}
	}

	FFilterClassesByInputVertexDataType::FFilterClassesByInputVertexDataType(const FName& InTypeName)
	:	InputVertexTypeName(InTypeName)
	{
	}

	bool FFilterClassesByInputVertexDataType::Filter(const FFrontendQueryEntry& InEntry) const
	{
		check(InEntry.Value.IsType<FMetasoundClassDescription>());

		return InEntry.Value.Get<FMetasoundClassDescription>().Inputs.ContainsByPredicate(
			[this](const FMetasoundInputDescription& InDesc)
			{
				return InDesc.TypeName == InputVertexTypeName;
			}
		);
	}

	FFilterClassesByOutputVertexDataType::FFilterClassesByOutputVertexDataType(const FName& InTypeName)
	:	OutputVertexTypeName(InTypeName)
	{
	}

	bool FFilterClassesByOutputVertexDataType::Filter(const FFrontendQueryEntry& InEntry) const
	{
		return InEntry.Value.Get<FMetasoundClassDescription>().Outputs.ContainsByPredicate(
			[this](const FMetasoundOutputDescription& InDesc)
			{
				return InDesc.TypeName == OutputVertexTypeName;
			}
		);
	}

}

