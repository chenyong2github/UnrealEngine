// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LiveLink/LiveStreamAnimationLiveLinkFrameTranslatorFactory.h"
#include "LiveLink/LiveStreamAnimationLiveLinkFrameTranslator.h"
#include "AssetTypeCategories.h"

ULiveStreamAnimationLiveLinkFrameTranslatorFactory::ULiveStreamAnimationLiveLinkFrameTranslatorFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = ULiveStreamAnimationLiveLinkFrameTranslator::StaticClass();
}

//~ Begin UFactory Interface
uint32 ULiveStreamAnimationLiveLinkFrameTranslatorFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Animation;
}

UObject* ULiveStreamAnimationLiveLinkFrameTranslatorFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ULiveStreamAnimationLiveLinkFrameTranslator* Translator = NewObject<ULiveStreamAnimationLiveLinkFrameTranslator>(InParent, InClass, InName, Flags);
	return Translator;
}

bool ULiveStreamAnimationLiveLinkFrameTranslatorFactory::ShouldShowInNewMenu() const
{
	return true;
}
//~ End UFactory Interface