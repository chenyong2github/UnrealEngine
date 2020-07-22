// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFactory.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Metasound.h"
#include "MetasoundEditorGraphNode.h"


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
	UMetasound* Metasound = NewObject<UMetasound>(InParent, Name, Flags);

	FMetasoundClassMetadata Metadata;
	Metadata.NodeName = Metasound->GetName();
	Metadata.NodeType = EMetasoundClassType::MetasoundGraph;
	Metadata.AuthorName = FText::FromString(UKismetSystemLibrary::GetPlatformUserName());
	Metasound->SetMetadata(Metadata);

	return Metasound;
}
