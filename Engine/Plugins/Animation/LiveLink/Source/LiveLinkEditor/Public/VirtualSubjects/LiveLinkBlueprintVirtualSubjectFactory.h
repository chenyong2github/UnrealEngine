// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "UObject/ObjectMacros.h"

#include "LiveLinkBlueprintVirtualSubjectFactory.generated.h"

class ULiveLinkRole;

UCLASS(hidecategories = Object)
class LIVELINKEDITOR_API ULiveLinkBlueprintVirtualSubjectFactory : public UFactory
{
	GENERATED_BODY()
	
public:
	ULiveLinkBlueprintVirtualSubjectFactory();
	
	// The parent class of the created blueprint
	UPROPERTY(EditAnywhere, Category="BlueprintVirtualSubjectFactory", meta=(AllowAbstract = "", BlueprintBaseOnly = ""))
	TSubclassOf<UObject> ParentClass;

	UPROPERTY(BlueprintReadWrite, Category = "Live Link Blueprint Virtual Subject Factory")
	TSubclassOf<ULiveLinkRole> Role;

	//~ Begin UFactory Interface
	virtual FText GetDisplayName() const override;
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
	virtual uint32 GetMenuCategories() const override;
	//~ Begin UFactory Interface
};
