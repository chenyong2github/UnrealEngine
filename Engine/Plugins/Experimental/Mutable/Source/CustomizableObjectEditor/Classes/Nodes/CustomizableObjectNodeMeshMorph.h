// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/CustomizableObjectNode.h"
#include "Nodes/CustomizableObjectNodeMeshReshapeCommon.h"

#include "CustomizableObjectNodeMeshMorph.generated.h"

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMeshMorph : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UCustomizableObjectNodeMeshMorph();

	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	bool IsNodeOutDatedAndNeedsRefresh() override;
	FString GetRefreshMessage() const override;
	void PinConnectionListChanged(UEdGraphPin* Pin) override;


	// Own interface
	class UCustomizableObjectNodeSkeletalMesh* GetSourceSkeletalMesh() const;

	UEdGraphPin* MeshPin() const
	{
		return FindPin(TEXT("Mesh"), EGPD_Input);
	}

	UEdGraphPin* FactorPin() const
	{
		return FindPin(TEXT("Factor"));
	}

	UPROPERTY(Category=CustomizableObject, EditAnywhere)
	FString MorphTargetName;

	/** Enable the deformation of the skeleton of the base mesh. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bReshapeSkeleton = false;

	/** Enable the deformation of physics volumes of the base mesh */
    UPROPERTY(EditAnywhere, Category = CustomizableObject)
    bool bReshapePhysicsVolumes = false;
	
	/** Enables the deformation of all bones of the skeleton */
	UPROPERTY(EditAnywhere, Category = ReshapeBones, meta=(EditCondition="bReshapeSkeleton"))
	bool bDeformAllBones = false;

	/** Array with bones that will be deformed */
	UPROPERTY(EditAnywhere, Category = ReshapeBones, meta=(EditCondition="bReshapeSkeleton && !bDeformAllBones"))
	TArray<FMeshReshapeBoneReference> BonesToDeform;

	UPROPERTY(EditAnywhere, Category = ReshapePhysics, meta = (EditCondition="bReshapePhysicsVolumes"))
	bool bDeformAllPhysicsBodies = false;

	UPROPERTY(EditAnywhere, Category = ReshapePhysics, meta=(EditCondition="bReshapePhysicsVolumes && !bDeformAllPhysicsBodies"))
	TArray<FMeshReshapeBoneReference> PhysicsBodiesToDeform;

};

