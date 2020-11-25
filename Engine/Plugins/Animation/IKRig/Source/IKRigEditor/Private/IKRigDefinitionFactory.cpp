// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigDefinitionFactory.h"
#include "IKRigDefinition.h"

#define LOCTEXT_NAMESPACE "IKRigDefinitionFactory"


/* UFactory interface
 *****************************************************************************/

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
	return LOCTEXT("IKRigDefinition_DisplayName", "IK Rig Definition");
}

uint32 UIKRigDefinitionFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

FText UIKRigDefinitionFactory::GetToolTip() const
{
	return LOCTEXT("IKRigDefinition_Tooltip", "Create IK Rig simply to animate or to use in runtime.");
}

FString UIKRigDefinitionFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("NewIKRigDefinition"));
}
#undef LOCTEXT_NAMESPACE
