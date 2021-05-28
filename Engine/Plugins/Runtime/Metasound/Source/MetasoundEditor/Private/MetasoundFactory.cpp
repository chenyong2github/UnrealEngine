// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFactory.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Metasound.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundSource.h"


// TODO: Re-enable and potentially rename once composition is supported
// UMetasoundFactory::UMetasoundFactory(const FObjectInitializer& ObjectInitializer)
// 	: Super(ObjectInitializer)
// {
// 	SupportedClass = UMetaSound::StaticClass();
// 
// 	bCreateNew = true;
// 	bEditorImport = false;
// 	bEditAfterNew = true;
// }
// 
// UObject* UMetasoundFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext)
// {
// 	UMetaSound* Metasound = NewObject<UMetaSound>(InParent, Name, Flags);
//	FGraphBuilder::InitMetasound(Metasound);
// }

UMetasoundSourceFactory::UMetasoundSourceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMetaSoundSource::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UMetasoundSourceFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext)
{
	using namespace Metasound::Editor;

	UMetaSoundSource* MetaSoundSource = NewObject<UMetaSoundSource>(InParent, Name, Flags);
	check(MetaSoundSource);
	FGraphBuilder::InitMetaSound(*MetaSoundSource, UKismetSystemLibrary::GetPlatformUserName());
	return MetaSoundSource;
}
