// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendQuerySteps.h"

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"

namespace Metasound
{
	void FGenerateAllAvailableNodeClasses::Generate(TArray<FFrontendQueryEntry>& OutEntries) const
	{
		const TArray<Frontend::FNodeClassInfo> ClassInfos = Frontend::GetAllAvailableNodeClasses();

		for (const Frontend::FNodeClassInfo& ClassInfo : ClassInfos)
		{
			OutEntries.Emplace(TInPlaceType<FMetasoundFrontendClass>(), Frontend::GenerateClassDescription(ClassInfo));
		}
	}

	FFilterClassesByInputVertexDataType::FFilterClassesByInputVertexDataType(const FName& InTypeName)
	:	InputVertexTypeName(InTypeName)
	{
	}

	bool FFilterClassesByInputVertexDataType::Filter(const FFrontendQueryEntry& InEntry) const
	{
		check(InEntry.Value.IsType<FMetasoundFrontendClass>());

		return InEntry.Value.Get<FMetasoundFrontendClass>().Interface.Inputs.ContainsByPredicate(
			[this](const FMetasoundFrontendClassInput& InDesc)
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
		return InEntry.Value.Get<FMetasoundFrontendClass>().Interface.Outputs.ContainsByPredicate(
			[this](const FMetasoundFrontendClassOutput& InDesc)
			{
				return InDesc.TypeName == OutputVertexTypeName;
			}
		);
	}

	FFilterClassesByClassName::FFilterClassesByClassName(const FMetasoundFrontendClassName& InClassName)
	: ClassName(InClassName)
	{
	}

	bool FFilterClassesByClassName::Filter(const FFrontendQueryEntry& InEntry) const 
	{
		return InEntry.Value.Get<FMetasoundFrontendClass>().Metadata.ClassName == ClassName;
	}

	FFilterClassesByClassID::FFilterClassesByClassID(const FGuid InClassID)
		: ClassID(InClassID)
	{
	}

	bool FFilterClassesByClassID::Filter(const FFrontendQueryEntry& InEntry) const
	{
		return InEntry.Value.Get<FMetasoundFrontendClass>().ID == ClassID;
	}
}

