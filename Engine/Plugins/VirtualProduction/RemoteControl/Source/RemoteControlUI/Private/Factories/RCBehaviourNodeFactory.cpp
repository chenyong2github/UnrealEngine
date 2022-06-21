// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCBehaviourNodeFactory.h"

#include "Behaviour/RCBehaviourBlueprintNode.h"
#include "IRemoteControlUIModule.h"
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
	return LOCTEXT("DisplayName", "Remote Control Behavior");
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
	return IRemoteControlUIModule::Get().GetRemoteControlAssetCategory();
}

#undef LOCTEXT_NAMESPACE