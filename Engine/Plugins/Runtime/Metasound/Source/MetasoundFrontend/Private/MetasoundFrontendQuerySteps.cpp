// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendQuerySteps.h"

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistryTransaction.h"

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

	FGenerateNewlyAvailableNodeClasses::FGenerateNewlyAvailableNodeClasses()
	: CurrentTransactionID(Frontend::GetOriginRegistryTransactionID())
	{
	}

	void FGenerateNewlyAvailableNodeClasses::Generate(TArray<FFrontendQueryEntry>& OutEntries) const
	{
		const TArray<Frontend::FNodeClassInfo> ClassInfos = Frontend::GetNodeClassesRegisteredSince(CurrentTransactionID, &CurrentTransactionID);

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

	FFrontendQueryEntry::FKey FMapClassNameToMajorVersion::Map(const FFrontendQueryEntry& InEntry) const
	{
		const FMetasoundFrontendClass& FrontendClass = InEntry.Value.Get<FMetasoundFrontendClass>();
		const uint32 HashKey = GetTypeHash(FrontendClass.Metadata.ClassName.GetFullName());
		return static_cast<FFrontendQueryEntry::FKey>(HashKey);
	}

	void FReduceClassesToHighestVersion::Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry*>& InEntries, FReduceOutputView& OutResult) const
	{
		FFrontendQueryEntry* HighestVersionEntry = nullptr;
		int32 HighestMajorVersion = -1;

		for (FFrontendQueryEntry* Entry : InEntries)
		{
			const int32 EntryMajorVersion = Entry->Value.Get<FMetasoundFrontendClass>().Metadata.Version.Major;

			if (!HighestVersionEntry || HighestMajorVersion < EntryMajorVersion)
			{
				HighestVersionEntry = Entry;
				HighestMajorVersion = EntryMajorVersion;
			}
		}

		if (HighestVersionEntry)
		{
			OutResult.Add(*HighestVersionEntry);
		}
	}

	FReduceClassesToMajorVersion::FReduceClassesToMajorVersion(int32 InMajorVersion)
		: MajorVersion(InMajorVersion)
	{
	}

	void FReduceClassesToMajorVersion::Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry*>& InEntries, FReduceOutputView& OutResult) const
	{
		for (FFrontendQueryEntry* Entry : InEntries)
		{
			const int32 EntryMajorVersion = Entry->Value.Get<FMetasoundFrontendClass>().Metadata.Version.Major;
			if (MajorVersion == EntryMajorVersion)
			{
				OutResult.Add(*Entry);
				return;
			}
		}
	}

	bool FSortClassesByVersion::Sort(const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS) const
	{
		const FMetasoundFrontendVersionNumber& VersionLHS = InEntryLHS.Value.Get<FMetasoundFrontendClass>().Metadata.Version;
		const FMetasoundFrontendVersionNumber& VersionRHS = InEntryRHS.Value.Get<FMetasoundFrontendClass>().Metadata.Version;
		return VersionLHS > VersionRHS;
	}
}
