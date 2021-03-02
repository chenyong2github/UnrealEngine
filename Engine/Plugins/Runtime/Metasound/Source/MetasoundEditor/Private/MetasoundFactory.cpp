// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFactory.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Metasound.h"
#include "MetasoundSource.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphBuilder.h"

// TODO: Re-enable and potentially rename once composition is supported
// UMetasoundFactory::UMetasoundFactory(const FObjectInitializer& ObjectInitializer)
// 	: Super(ObjectInitializer)
// {
// 	SupportedClass = UMetasound::StaticClass();
// 
// 	bCreateNew = true;
// 	bEditorImport = false;
// 	bEditAfterNew = true;
// }
// 
// UObject* UMetasoundFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext)
// {
// 	UMetasound* NewMetasound = NewObject<UMetasound>(InParent, Name, Flags);
// 
// 	FMetasoundFrontendClassMetadata Metadata;
// 
// 	Metadata.ClassName = FMetasoundFrontendClassName{TEXT(""), *NewMetasound->GetName(), TEXT("")};
// 	Metadata.Version.Major = 1;
// 	Metadata.Version.Minor = 0;
// 	Metadata.Type = EMetasoundFrontendClassType::Graph;
// 	Metadata.Author = FText::FromString(UKismetSystemLibrary::GetPlatformUserName());
// 
// 	NewMetasound->SetMetadata(Metadata);
// 
// 	NewMetasound->ConformDocumentToMetasoundArchetype();
// 
// 	return NewMetasound;
// }

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

	FMetasoundFrontendClassMetadata Metadata;

	Metadata.ClassName = FMetasoundFrontendClassName{TEXT(""), *MetasoundSource->GetName(), TEXT("")};
	Metadata.Version.Major = 1;
	Metadata.Version.Minor = 0;
	Metadata.Type = EMetasoundFrontendClassType::Graph;
	Metadata.Author = FText::FromString(UKismetSystemLibrary::GetPlatformUserName());

	MetasoundSource->SetMetadata(Metadata);

	MetasoundSource->ConformDocumentToMetasoundArchetype();

	return MetasoundSource;
}

