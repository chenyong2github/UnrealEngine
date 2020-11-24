// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFactory.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Metasound.h"
#include "MetasoundSource.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphBuilder.h"

UMetasoundFactory::UMetasoundFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMetasound::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UMetasoundFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext)
{
	UMetasound* NewMetasound = NewObject<UMetasound>(InParent, Name, Flags);

	FMetasoundClassMetadata Metadata;

	Metadata.NodeName = NewMetasound->GetName();
	Metadata.MajorVersion = 1;
	Metadata.MinorVersion = 0;
	Metadata.NodeType = EMetasoundClassType::MetasoundGraph;
	Metadata.AuthorName = FText::FromString(UKismetSystemLibrary::GetPlatformUserName());

	NewMetasound->SetMetadata(Metadata);

	NewMetasound->ConformDocumentToArchetype();

	return NewMetasound;
}

UMetasoundSourceFactory::UMetasoundSourceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMetasoundSource::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UMetasoundSourceFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext)
{
	UMetasoundSource* MetasoundSource = NewObject<UMetasoundSource>(InParent, Name, Flags);

	FMetasoundClassMetadata Metadata;

	Metadata.NodeName = MetasoundSource->GetName();
	Metadata.MajorVersion = 1;
	Metadata.MinorVersion = 0;
	Metadata.NodeType = EMetasoundClassType::MetasoundGraph;
	Metadata.AuthorName = FText::FromString(UKismetSystemLibrary::GetPlatformUserName());

	MetasoundSource->SetMetadata(Metadata);

	MetasoundSource->ConformDocumentToArchetype();

	return MetasoundSource;
}

