// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UDynamicMesh.h"
#include "Components/DynamicMeshComponent.h"

#include "DynamicMeshActor.generated.h"


/**
 * ADynamicMeshActor is an Actor that has a USimpleDynamicMeshComponent as it's RootObject.
 * Used for experimental procedural geometry generation Blueprints. Currently no physics support.
 */
UCLASS(ConversionRoot, ComponentWrapperClass, ClassGroup=DynamicMesh, meta = (ChildCanTick))
class GEOMETRYFRAMEWORK_API ADynamicMeshActor : public AActor
{
	GENERATED_UCLASS_BODY()
private:
	UPROPERTY(Category = DynamicMeshActor, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Mesh,Rendering,Physics,Components|StaticMesh", AllowPrivateAccess = "true"))
	TObjectPtr<class UDynamicMeshComponent> DynamicMeshComponent;

public:
	UFUNCTION(BlueprintCallable, Category = DynamicMeshActor)
		UDynamicMeshComponent* GetDynamicMeshComponent() const { return DynamicMeshComponent; }
};


