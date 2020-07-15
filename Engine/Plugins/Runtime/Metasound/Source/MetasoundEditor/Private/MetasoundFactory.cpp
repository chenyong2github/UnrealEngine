// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFactory.h"

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
	UMetasound* MetaSound = NewObject<UMetasound>(InParent, Name, Flags);
	return MetaSound;
}
