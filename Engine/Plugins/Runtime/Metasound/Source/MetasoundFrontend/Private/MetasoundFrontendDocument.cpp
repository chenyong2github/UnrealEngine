// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendDocument.h"

#include "Algo/Transform.h"
#include "IAudioParameterInterfaceRegistry.h"
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
}

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

FMetasoundFrontendClassMetadata::FMetasoundFrontendClassMetadata(const Metasound::FNodeClassMetadata& InNodeClassMetadata)
: ClassName(InNodeClassMetadata.ClassName)
, Version{ InNodeClassMetadata.MajorVersion, InNodeClassMetadata.MinorVersion }
, Type(EMetasoundFrontendClassType::External)
, DisplayName(InNodeClassMetadata.DisplayName)
, Description(InNodeClassMetadata.Description)
, PromptIfMissing(InNodeClassMetadata.PromptIfMissing)
, Author(InNodeClassMetadata.Author)
, Keywords(InNodeClassMetadata.Keywords)
, CategoryHierarchy(InNodeClassMetadata.CategoryHierarchy)
, bIsDeprecated(InNodeClassMetadata.bDeprecated)
{
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
