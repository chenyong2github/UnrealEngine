// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SharedPointer.h"
#include "GameFramework/LightWeightInstanceManager.h"

#include "Actor.h"

#include "LightWeightInstanceStaticMeshManager.generated.h"

DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FActorInstanceHandle, FOnActorReady, FActorInstanceHandle, InHandle);





UCLASS(BlueprintType, Blueprintable)
class ENGINE_API ALightWeightInstanceStaticMeshManager : public ALightWeightInstanceManager
{
	GENERATED_UCLASS_BODY()

	virtual void SetRepresentedClass(UClass* ActorClass) override;

	// Sets the static mesh to use based on the info contained in InActor
	virtual void SetStaticMeshFromActor(AActor* InActor);

	// Clears the static mesh used for rendering instances
	void ClearStaticMesh();

	virtual int32 ConvertCollisionIndexToLightWeightIndex(int32 InIndex) const override;

protected:
	virtual void AddNewInstanceAt(FLWIData* InitData, int32 Index) override;

	virtual void RemoveInstance(int32 Index) override;

	void RemoveInstanceFromRendering(int32 DataIndex);

	// sets the parameters on the instanced static mesh component
	virtual void SetInstancedStaticMeshParams();

	// Called when we set the static mesh
	void OnStaticMeshSet();

	virtual void OnRep_Transforms() override;

	virtual void PostActorSpawn(const FActorInstanceHandle& Handle) override;

public:

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	UPROPERTY(ReplicatedUsing = OnRep_StaticMesh)
	TSoftObjectPtr<UStaticMesh> StaticMesh;
	UFUNCTION()
	void OnRep_StaticMesh();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Debug, AdvancedDisplay, meta = (BlueprintProtected = "true", AllowPrivateAccess = "true"))
	TObjectPtr<class UHierarchicalInstancedStaticMeshComponent> InstancedStaticMeshComponent;

	//
	// Bookkeeping info
	//

	// keep track of the relationship between our data and the rendering data stored in the instanced static mesh component
	UPROPERTY(Replicated)
	TArray<int32> RenderingIndicesToDataIndices;
	UPROPERTY(Replicated)
	TArray<int32> DataIndicesToRenderingIndices;
};
