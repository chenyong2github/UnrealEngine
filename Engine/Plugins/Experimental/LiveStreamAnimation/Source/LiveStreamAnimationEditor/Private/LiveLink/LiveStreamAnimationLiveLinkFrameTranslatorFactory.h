// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "LiveStreamAnimationLiveLinkFrameTranslatorFactory.generated.h"

UCLASS()
class LIVESTREAMANIMATIONEDITOR_API ULiveStreamAnimationLiveLinkFrameTranslatorFactory : public UFactory
{
	GENERATED_BODY()

public:

	ULiveStreamAnimationLiveLinkFrameTranslatorFactory();

	//~ Begin UFactory Interface
	virtual uint32 GetMenuCategories() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
	//~ End UFactory Interface
};