// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendQuerySteps.h"

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistryTransaction.h"

namespace Metasound
{
	FNodeClassRegistrationEvents::FNodeClassRegistrationEvents()
	: CurrentTransactionID(Frontend::GetOriginRegistryTransactionID())
	{
	}

	void FNodeClassRegistrationEvents::Stream(TArray<FFrontendQueryEntry>& OutEntries)
	{
		using namespace Frontend;

		auto AddEntry = [&OutEntries](const FNodeRegistryTransaction& InTransaction)
		{
			OutEntries.Emplace(FFrontendQueryEntry::FValue(TInPlaceType<FNodeRegistryTransaction>(), InTransaction));
		};
		
		if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
		{
			Registry->ForEachNodeRegistryTransactionSince(CurrentTransactionID, &CurrentTransactionID, AddEntry);
		}
	}

	void FNodeClassRegistrationEvents::Reset()
	{
		CurrentTransactionID = Frontend::GetOriginRegistryTransactionID();
	}

	FFrontendQueryEntry::FKey FMapRegistrationEventsToNodeRegistryKeys::Map(const FFrontendQueryEntry& InEntry) const 
	{
		using namespace Frontend;

		FNodeRegistryKey RegistryKey;

		if (ensure(InEntry.Value.IsType<FNodeRegistryTransaction>()))
		{
			RegistryKey = InEntry.Value.Get<FNodeRegistryTransaction>().GetNodeRegistryKey();
		}

		return FFrontendQueryEntry::FKey(RegistryKey);
	}

	void FReduceRegistrationEventsToCurrentStatus::Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const
	{
		using namespace Frontend;

		int32 State = 0;
		FFrontendQueryEntry* FinalEntry = nullptr;

		for (FFrontendQueryEntry* Entry : InEntries)
		{
			if (ensure(Entry->Value.IsType<FNodeRegistryTransaction>()))
			{
				const FNodeRegistryTransaction& Transaction = Entry->Value.Get<FNodeRegistryTransaction>();
				
				switch (Transaction.GetTransactionType())
				{
					case FNodeRegistryTransaction::ETransactionType::NodeRegistration:
						State++;
						FinalEntry = Entry;
						break;

					case FNodeRegistryTransaction::ETransactionType::NodeUnregistration:
						State--;
						break;

					default:
						break;
				}
			}
		}

		if ((nullptr != FinalEntry) && (State > 0))
		{
			OutResult.Add(*FinalEntry);
		}
	}

	void FTransformRegistrationEventsToClasses::Transform(FFrontendQueryEntry::FValue& InValue) const
	{
		using namespace Frontend;

		FMetasoundFrontendClass FrontendClass;

		if (ensure(InValue.IsType<FNodeRegistryTransaction>()))
		{
			const FNodeRegistryTransaction& Transaction = InValue.Get<FNodeRegistryTransaction>();
			
			bool bSuccess = FMetasoundFrontendRegistryContainer::Get()->FindFrontendClassFromRegistered(Transaction.GetNodeRegistryKey(), FrontendClass);
			check(bSuccess);
		}

		InValue.Set<FMetasoundFrontendClass>(MoveTemp(FrontendClass));
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
		return InEntry.Value.Get<FMetasoundFrontendClass>().Metadata.GetClassName() == ClassName;
	}

	FFilterClassesByClassID::FFilterClassesByClassID(const FGuid InClassID)
		: ClassID(InClassID)
	{
	}

	bool FFilterClassesByClassID::Filter(const FFrontendQueryEntry& InEntry) const
	{
		return InEntry.Value.Get<FMetasoundFrontendClass>().ID == ClassID;
	}

	FFrontendQueryEntry::FKey FMapToFullClassName::Map(const FFrontendQueryEntry& InEntry) const
	{
		const FMetasoundFrontendClass& FrontendClass = InEntry.Value.Get<FMetasoundFrontendClass>();
		return FFrontendQueryEntry::FKey(FrontendClass.Metadata.GetClassName().GetFullName());
	}

	void FReduceClassesToHighestVersion::Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const
	{
		FFrontendQueryEntry* HighestVersionEntry = nullptr;
		int32 HighestMajorVersion = -1;

		for (FFrontendQueryEntry* Entry : InEntries)
		{
			const int32 EntryMajorVersion = Entry->Value.Get<FMetasoundFrontendClass>().Metadata.GetVersion().Major;

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

	void FReduceClassesToMajorVersion::Reduce(FFrontendQueryEntry::FKey InKey, TArrayView<FFrontendQueryEntry * const>& InEntries, FReduceOutputView& OutResult) const
	{
		for (FFrontendQueryEntry* Entry : InEntries)
		{
			const int32 EntryMajorVersion = Entry->Value.Get<FMetasoundFrontendClass>().Metadata.GetVersion().Major;
			if (MajorVersion == EntryMajorVersion)
			{
				OutResult.Add(*Entry);
				return;
			}
		}
	}

	bool FSortClassesByVersion::Sort(const FFrontendQueryEntry& InEntryLHS, const FFrontendQueryEntry& InEntryRHS) const
	{
		const FMetasoundFrontendVersionNumber& VersionLHS = InEntryLHS.Value.Get<FMetasoundFrontendClass>().Metadata.GetVersion();
		const FMetasoundFrontendVersionNumber& VersionRHS = InEntryRHS.Value.Get<FMetasoundFrontendClass>().Metadata.GetVersion();
		return VersionLHS > VersionRHS;
	}
}
