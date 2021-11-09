// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "MassEntityTraitBase.h"
#include "InstancedStruct.h"
#include "MassAssortedFragmentsTrait.generated.h"

/**
* Mass Agent Feature which appends a list of specified fragments.  
*/
UCLASS(meta=(DisplayName="Assorted Fragments"))
class MASSSPAWNER_API UMassAssortedFragmentsTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()

protected:

	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const override;

	UPROPERTY(Category="Fragments", EditAnywhere, meta = (BaseStruct = "MassFragment", ExcludeBaseStruct))
	TArray<FInstancedStruct> Fragments;
};
