// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigEditor/IKRigDefinitionFactory.h"
#include "IKRigDefinition.h"
#include "AssetTypeCategories.h"

#define LOCTEXT_NAMESPACE "IKRigDefinitionFactory"


UIKRigDefinitionFactory::UIKRigDefinitionFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UIKRigDefinition::StaticClass();
}

UObject* UIKRigDefinitionFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UIKRigDefinition>(InParent, Name, Flags | RF_Transactional);;
}

bool UIKRigDefinitionFactory::ShouldShowInNewMenu() const
{
	return true;
}

bool UIKRigDefinitionFactory::ConfigureProperties()
{
	return true;
}

FText UIKRigDefinitionFactory::GetDisplayName() const
{
	return LOCTEXT("IKRigDefinition_DisplayName", "IK Rig");
}

uint32 UIKRigDefinitionFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

FText UIKRigDefinitionFactory::GetToolTip() const
{
	return LOCTEXT("IKRigDefinition_Tooltip", "Defines a set of IK Solvers and Effectors to pose a skeleton with Goals.");
}

FString UIKRigDefinitionFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("NewIKRig"));
}
#undef LOCTEXT_NAMESPACE
