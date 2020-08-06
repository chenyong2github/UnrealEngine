// Copyright Epic Games, Inc. All Rights Reserverd.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "LSALiveLinkFrameTranslatorFactory.generated.h"

UCLASS()
class LSALIVELINKEDITOR_API ULSALiveLinkFrameTranslatorFactory : public UFactory
{
	GENERATED_BODY()

public:

	ULSALiveLinkFrameTranslatorFactory();

	//~ Begin UFactory Interface
	virtual uint32 GetMenuCategories() const override;
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
	//~ End UFactory Interface
};