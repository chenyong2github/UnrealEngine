// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "RCBehaviourNodeFactory.generated.h"

/** Factory for Remote Control Logic's custom blueprint behavior feature
* The associated Blueprint class is URCBehaviourBlueprintNode
*/
UCLASS(hidecategories = Object)
class URCBehaviourNodeFactory : public UFactory
{
	GENERATED_BODY()
public:
	URCBehaviourNodeFactory();
	
	/** The parent class of the created blueprint */
	UPROPERTY(VisibleAnywhere, Category="RCBehaviourNodeFactory", meta=(AllowAbstract = "", BlueprintBaseOnly = ""))
	TSubclassOf<UObject> ParentClass;
	
	//~ Begin UFactory Interface
	virtual FText GetDisplayName() const override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	//~ Begin UFactory Interface
	
};