// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LiveLink/LSALiveLinkFrameTranslatorFactory.h"
#include "LiveLink/LiveStreamAnimationLiveLinkFrameTranslator.h"
#include "AssetTypeCategories.h"

ULSALiveLinkFrameTranslatorFactory::ULSALiveLinkFrameTranslatorFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = ULiveStreamAnimationLiveLinkFrameTranslator::StaticClass();
}

//~ Begin UFactory Interface
uint32 ULSALiveLinkFrameTranslatorFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

UObject* ULSALiveLinkFrameTranslatorFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ULiveStreamAnimationLiveLinkFrameTranslator* Translator = NewObject<ULiveStreamAnimationLiveLinkFrameTranslator>(InParent, InClass, InName, Flags);
	return Translator;
}

bool ULSALiveLinkFrameTranslatorFactory::ShouldShowInNewMenu() const
{
	return true;
}
//~ End UFactory Interface