// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCBehaviourNodeFactory.h"

#include "AssetToolsModule.h"
#include "Behaviour/RCBehaviourBlueprintNode.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "RCBehaviourNodeFactory"

URCBehaviourNodeFactory::URCBehaviourNodeFactory()
{
	SupportedClass = UBlueprint::StaticClass();
	ParentClass = URCBehaviourBlueprintNode::StaticClass();

	bCreateNew = true;
	bEditAfterNew = true;
}

FText URCBehaviourNodeFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Remote Control Behaviour Node");
}

UObject* URCBehaviourNodeFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UBlueprint* NodeBlueprint = nullptr;
	if (ensure(SupportedClass == Class))
	{
		ensure(0 != (RF_Public & Flags));
		NodeBlueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), NAME_None);
	}
	return NodeBlueprint;
}

uint32 URCBehaviourNodeFactory::GetMenuCategories() const
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	return AssetTools.RegisterAdvancedAssetCategory("Remote Control", LOCTEXT("AssetCategoryName", "Remote Control Behaviour Node"));
}

#undef LOCTEXT_NAMESPACE