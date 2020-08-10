// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LSALiveLinkFrameTranslatorFactory.h"
#include "LSALiveLinkFrameTranslator.h"
#include "AssetTypeCategories.h"

ULSALiveLinkFrameTranslatorFactory::ULSALiveLinkFrameTranslatorFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = ULSALiveLinkFrameTranslator::StaticClass();
}

//~ Begin UFactory Interface
uint32 ULSALiveLinkFrameTranslatorFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

UObject* ULSALiveLinkFrameTranslatorFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ULSALiveLinkFrameTranslator* Translator = NewObject<ULSALiveLinkFrameTranslator>(InParent, InClass, InName, Flags);
	return Translator;
}

bool ULSALiveLinkFrameTranslatorFactory::ShouldShowInNewMenu() const
{
	return true;
}
//~ End UFactory Interface