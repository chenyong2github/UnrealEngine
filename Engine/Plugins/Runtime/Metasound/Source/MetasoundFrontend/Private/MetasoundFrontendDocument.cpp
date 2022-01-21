// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendDocument.h"

#include "Algo/ForEach.h"
#include "Algo/Transform.h"
#include "IAudioParameterInterfaceRegistry.h"
#include "Logging/LogMacros.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundLog.h"

namespace Metasound
{
	const FGuid FrontendInvalidID = FGuid();

	namespace Frontend
	{
		namespace DisplayStyle
		{
			namespace NodeLayout
			{
				const FVector2D DefaultOffsetX { 300.0f, 0.0f };
				const FVector2D DefaultOffsetY { 0.0f, 80.0f };
			} // namespace NodeLayout
		} // namespace DisplayStyle
	} // namespace Frontend

	namespace DocumentPrivate
	{
		/*
		 * Sets an array to a given array and updates the change ID if the array changed.
		 * @returns true if value changed, false if not.
		 */
		template <typename TElementType>
		bool SetWithChangeID(const TElementType& InNewValue, TElementType& OutValue, FGuid& OutChangeID)
		{
			if (OutValue != InNewValue)
			{
				OutValue = InNewValue;
				OutChangeID = FGuid::NewGuid();
				return true;
			}

			return false;
		}

		/* Array Text specialization as FText does not implement == nor does it support IsBytewiseComparable */
		template <>
		bool SetWithChangeID<TArray<FText>>(const TArray<FText>& InNewArray, TArray<FText>& OutArray, FGuid& OutChangeID)
		{
			bool bIsEqual = OutArray.Num() == InNewArray.Num();
			if (bIsEqual)
			{
				for (int32 i = 0; i < InNewArray.Num(); ++i)
				{
					bIsEqual &= InNewArray[i].IdenticalTo(OutArray[i]);
				}
			}

			if (!bIsEqual)
			{
				OutArray = InNewArray;
				OutChangeID = FGuid::NewGuid();
			}

			return !bIsEqual;
		}

		/* Text specialization as FText does not implement == nor does it support IsBytewiseComparable */
		template <>
		bool SetWithChangeID<FText>(const FText& InNewText, FText& OutText, FGuid& OutChangeID)
		{
			if (!InNewText.IdenticalTo(OutText))
			{
				OutText = InNewText;
				OutChangeID = FGuid::NewGuid();
				return true;
			}

			return false;
		}
	}
} // namespace Metasound

FMetasoundFrontendNodeInterface::FMetasoundFrontendNodeInterface(const FMetasoundFrontendClassInterface& InClassInterface)
{
	for (const FMetasoundFrontendClassInput& Input : InClassInterface.Inputs)
	{
		Inputs.Add(Input);
	}

	for (const FMetasoundFrontendClassOutput& Output : InClassInterface.Outputs)
	{
		Outputs.Add(Output);
	}

	for (const FMetasoundFrontendClassEnvironmentVariable& EnvVar : InClassInterface.Environment)
	{
		FMetasoundFrontendVertex EnvVertex;
		EnvVertex.Name = EnvVar.Name;
		EnvVertex.TypeName = EnvVar.TypeName;

		Environment.Add(MoveTemp(EnvVertex));
	}
}

FMetasoundFrontendNode::FMetasoundFrontendNode(const FMetasoundFrontendClass& InClass)
: ClassID(InClass.ID)
, Name(InClass.Metadata.GetClassName().Name.ToString())
, Interface(InClass.Interface)
{

}

FString FMetasoundFrontendVersion::ToString() const
{
	return FString::Format(TEXT("{0} {1}"), { Name.ToString(), Number.ToString() });
}

bool FMetasoundFrontendVersion::IsValid() const
{
	return Number != GetInvalid().Number && Name != GetInvalid().Name;
}

const FMetasoundFrontendVersion& FMetasoundFrontendVersion::GetInvalid()
{
	static const FMetasoundFrontendVersion InvalidVersion { FName(), FMetasoundFrontendVersionNumber::GetInvalid() };
	return InvalidVersion;
}

bool FMetasoundFrontendVertex::IsFunctionalEquivalent(const FMetasoundFrontendVertex& InLHS, const FMetasoundFrontendVertex& InRHS)
{
	return (InLHS.Name == InRHS.Name) && (InLHS.TypeName == InRHS.TypeName);
}

void FMetasoundFrontendClassVertex::SplitName(FName& OutNamespace, FName& OutParameterName) const
{
	Audio::FParameterPath::SplitName(Name, OutNamespace, OutParameterName);
}

bool FMetasoundFrontendClassVertex::IsFunctionalEquivalent(const FMetasoundFrontendClassVertex& InLHS, const FMetasoundFrontendClassVertex& InRHS)
{
	return FMetasoundFrontendVertex::IsFunctionalEquivalent(InLHS, InRHS);
}

FMetasoundFrontendClassName::FMetasoundFrontendClassName(const FName& InNamespace, const FName& InName, const FName& InVariant)
: Namespace(InNamespace)
, Name(InName)
, Variant(InVariant)
{
}

FMetasoundFrontendClassName::FMetasoundFrontendClassName(const Metasound::FNodeClassName& InName)
: FMetasoundFrontendClassName(InName.GetNamespace(), InName.GetName(), InName.GetVariant())
{
}

FName FMetasoundFrontendClassName::GetScopedName() const
{
	return Metasound::FNodeClassName::FormatScopedName(Namespace, Name);
}

FName FMetasoundFrontendClassName::GetFullName() const
{
	return Metasound::FNodeClassName::FormatFullName(Namespace, Name, Variant);
}

FString FMetasoundFrontendClassName::ToString() const
{
	return GetFullName().ToString();
}

bool operator==(const FMetasoundFrontendClassName& InLHS, const FMetasoundFrontendClassName& InRHS)
{
	return (InLHS.Namespace == InRHS.Namespace) && (InLHS.Name == InRHS.Name) && (InLHS.Variant == InRHS.Variant);
}

bool operator!=(const FMetasoundFrontendClassName& InLHS, const FMetasoundFrontendClassName& InRHS)
{
	return !(InLHS == InRHS);
}

void FMetasoundFrontendClassMetadata::SetAuthor(const FText& InAuthor)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(InAuthor, Author, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetAutoUpdateManagesInterface(bool bInAutoUpdateManagesInterface)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(bInAutoUpdateManagesInterface, bAutoUpdateManagesInterface, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetCategoryHierarchy(const TArray<FText>& InCategoryHierarchy)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(InCategoryHierarchy, CategoryHierarchy, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetKeywords(const TArray<FText>& InKeywords)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(InKeywords, Keywords, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetClassName(const FMetasoundFrontendClassName& InClassName)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(InClassName, ClassName, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetDescription(const FText& InDescription)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(InDescription, Description, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetDisplayName(const FText& InDisplayName)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(InDisplayName, DisplayName, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetIsDeprecated(bool bInIsDeprecated)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(bInIsDeprecated, bIsDeprecated, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetPromptIfMissing(const FText& InPromptIfMissing)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(InPromptIfMissing, PromptIfMissing, ChangeID);
}

void FMetasoundFrontendClassMetadata::SetVersion(const FMetasoundFrontendVersionNumber& InVersion)
{
	using namespace Metasound::DocumentPrivate;
	SetWithChangeID(InVersion, Version, ChangeID);
}

void FMetasoundFrontendClass::CacheRegistryData()
{
	using namespace Metasound::Frontend;

	const FNodeRegistryKey Key = NodeRegistryKey::CreateKey(Metadata);
	FMetasoundFrontendClass Class;

	FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
	if (ensure(Registry))
	{
		if (Registry->FindFrontendClassFromRegistered(Key, Class))
		{
			Metadata = Class.Metadata;

			using FNameTypeKey = TPair<FName, FName>;
			TMap<FNameTypeKey, const FMetasoundFrontendVertexMetadata*> InterfaceMembers;

			auto MakePairFromVertex = [](const FMetasoundFrontendClassVertex& InVertex)
			{
				const FNameTypeKey Key(InVertex.Name, InVertex.TypeName);
				return TPair<FNameTypeKey, const FMetasoundFrontendVertexMetadata*> { Key, &InVertex.Metadata };
			};

			auto CacheRegistryVertexMetadata = [&](FMetasoundFrontendClassVertex& OutVertex)
			{
				const FNameTypeKey Key(OutVertex.Name, OutVertex.TypeName);
				if (const FMetasoundFrontendVertexMetadata* RegVertex = InterfaceMembers.FindRef(Key))
				{
					OutVertex.Metadata = *RegVertex;
				}
			};

			Algo::Transform(Class.Interface.Inputs, InterfaceMembers, [&](const FMetasoundFrontendClassInput& Input) { return MakePairFromVertex(Input); });
			Algo::ForEach(Interface.Inputs, [&](FMetasoundFrontendClassInput& Input) { CacheRegistryVertexMetadata(Input); });

			InterfaceMembers.Reset();

			Algo::Transform(Class.Interface.Outputs, InterfaceMembers, [&](const FMetasoundFrontendClassOutput& Output) { return MakePairFromVertex(Output); });
			Algo::ForEach(Interface.Outputs, [&](FMetasoundFrontendClassOutput& Output) { CacheRegistryVertexMetadata(Output); });
		}
		else
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to load document dependency class metadata: Missing dependency with key '%s'"), *Key);
		}

		Interface.InputStyle = Class.Interface.InputStyle;
		Interface.OutputStyle = Class.Interface.OutputStyle;
		Style = Class.Style;
	}
}

void FMetasoundFrontendClass::ClearRegistryData()
{
	Metadata.DisplayName = { };
	Metadata.Description = { };
	Metadata.PromptIfMissing = { };
	Metadata.Author = { };
	Metadata.Keywords.Reset();
	Metadata.CategoryHierarchy.Reset();

	Algo::ForEach(Interface.Inputs, [](FMetasoundFrontendClassInput& Input) { Input.Metadata = { }; });
	Algo::ForEach(Interface.Outputs, [](FMetasoundFrontendClassOutput& Output) { Output.Metadata = { }; });

	Interface.InputStyle = { };
	Interface.OutputStyle = { };
	Style = { };
}

FMetasoundFrontendClassMetadata FMetasoundFrontendClassMetadata::GenerateClassDescription(const Metasound::FNodeClassMetadata& InNodeClassMetadata, EMetasoundFrontendClassType InType)
{
	FMetasoundFrontendClassMetadata NewMetadata;
	NewMetadata.Type = InType;

	// TODO: This flag is only used by the graph class' metadata.
	// Should probably be moved elsewhere (AssetBase?) as to not
	// get confused with behavior encapsulated on registry class
	// descriptions/individual node class dependencies.
	NewMetadata.bAutoUpdateManagesInterface = false;

	NewMetadata.ClassName = InNodeClassMetadata.ClassName;
	NewMetadata.Version = { InNodeClassMetadata.MajorVersion, InNodeClassMetadata.MinorVersion };
	NewMetadata.DisplayName = InNodeClassMetadata.DisplayName;
	NewMetadata.Description = InNodeClassMetadata.Description;
	NewMetadata.PromptIfMissing = InNodeClassMetadata.PromptIfMissing;
	NewMetadata.Author = InNodeClassMetadata.Author;
	NewMetadata.Keywords = InNodeClassMetadata.Keywords;
	NewMetadata.CategoryHierarchy = InNodeClassMetadata.CategoryHierarchy;
	NewMetadata.bIsDeprecated = InNodeClassMetadata.bDeprecated;

	return NewMetadata;
}

FMetasoundFrontendClassInput::FMetasoundFrontendClassInput(const FMetasoundFrontendClassVertex& InOther)
:	FMetasoundFrontendClassVertex(InOther)
{
	using namespace Metasound::Frontend;

	EMetasoundFrontendLiteralType DefaultType = GetMetasoundFrontendLiteralType(IDataTypeRegistry::Get().GetDesiredLiteralType(InOther.TypeName));

	DefaultLiteral.SetType(DefaultType);
}

FMetasoundFrontendClassVariable::FMetasoundFrontendClassVariable(const FMetasoundFrontendClassVertex& InOther)
	: FMetasoundFrontendClassVertex(InOther)
{
	using namespace Metasound::Frontend;

	EMetasoundFrontendLiteralType DefaultType = GetMetasoundFrontendLiteralType(IDataTypeRegistry::Get().GetDesiredLiteralType(InOther.TypeName));

	DefaultLiteral.SetType(DefaultType);
}

FMetasoundFrontendGraphClass::FMetasoundFrontendGraphClass()
{
	Metadata.SetType(EMetasoundFrontendClassType::Graph);
}

FMetasoundFrontendDocument::FMetasoundFrontendDocument()
{
	RootGraph.ID = FGuid::NewGuid();
	RootGraph.Metadata.SetType(EMetasoundFrontendClassType::Graph);
	ArchetypeVersion = FMetasoundFrontendVersion::GetInvalid();
}
