// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeFactory.h"
#include "StateTree.h"
#include "StateTreeEditorModule.h"
#include "AIModule.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

/////////////////////////////////////////////////////
// UStateTreeFactory

UStateTreeFactory::UStateTreeFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UStateTree::StaticClass();
}

uint32 UStateTreeFactory::GetMenuCategories() const
{
	IAIModule& AIModule = FModuleManager::GetModuleChecked<IAIModule>("AIModule").Get();
	return AIModule.GetAIAssetCategoryBit();
}

UObject* UStateTreeFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UStateTree* NewStateTree = NewObject<UStateTree>(InParent, Class, Name, Flags);
	return NewStateTree;
}

#undef LOCTEXT_NAMESPACE
