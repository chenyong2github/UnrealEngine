// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "Dataflow/DataflowRenderingComponent.h"

#include "DataflowRenderingActor.generated.h"


UCLASS()
class DATAFLOWENGINEPLUGIN_API ADataflowRenderingActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:

	/* DataflowComponent */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Destruction, meta = (ExposeFunctionCategories = "Components|Dataflow", AllowPrivateAccess = "true"))
	TObjectPtr<UDataflowRenderingComponent> DataflowRenderingComponent;
	UDataflowRenderingComponent* GetDataflowRenderingComponent() const { return DataflowRenderingComponent; }


#if WITH_EDITOR
	virtual bool GetReferencedContentObjects(TArray<UObject*>& Objects) const override;
#endif
};
