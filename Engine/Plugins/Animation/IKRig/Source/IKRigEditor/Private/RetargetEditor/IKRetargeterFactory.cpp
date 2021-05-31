// Copyright Epic Games, Inc. All Rights Reserved.

#include "RetargetEditor/IKRetargeterFactory.h"
#include "IKRig/Public/Retargeter/IKRetargeter.h"
#include "AssetTypeCategories.h"

#define LOCTEXT_NAMESPACE "IKRetargeterFactory"


UIKRetargeterFactory::UIKRetargeterFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UIKRetargeter::StaticClass();
}

UObject* UIKRetargeterFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UIKRetargeter>(InParent, Name, Flags | RF_Transactional);;
}

bool UIKRetargeterFactory::ShouldShowInNewMenu() const
{
	return true;
}

bool UIKRetargeterFactory::ConfigureProperties()
{
	return true;
}

FText UIKRetargeterFactory::GetDisplayName() const
{
	return LOCTEXT("IKRetargeter_DisplayName", "IK Retargeter");
}

uint32 UIKRetargeterFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

FText UIKRetargeterFactory::GetToolTip() const
{
	return LOCTEXT("IKRetargeter_Tooltip", "Defines a pair of Source/Target Retarget Rigs and the mapping between them.");
}

FString UIKRetargeterFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("NewIKRetargeter"));
}
#undef LOCTEXT_NAMESPACE
