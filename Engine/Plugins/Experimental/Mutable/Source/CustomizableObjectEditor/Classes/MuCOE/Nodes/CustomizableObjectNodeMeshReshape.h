// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/Nodes/CustomizableObjectNode.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshReshapeCommon.h"

#include "CustomizableObjectNodeMeshReshape.generated.h"

UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeMeshReshape : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UCustomizableObjectNodeMeshReshape();

	// Begin EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;

	// UCustomizableObjectNode interface
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;
	FString GetRefreshMessage() const override;

	inline UEdGraphPin* BaseMeshPin() const
	{
		return FindPin(TEXT("Base Mesh"), EGPD_Input);
	}

	inline UEdGraphPin* BaseShapePin() const
	{
		return FindPin(TEXT("Base Shape"), EGPD_Input);
	}

	inline UEdGraphPin* TargetShapePin() const
	{
		return FindPin(TEXT("Target Shape"), EGPD_Input);
	}

	/** Enable the deformation of the skeleton of the base mesh. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bReshapeSkeleton = false;	

	/** Enable the deformation of physics volumes of the base mesh */
    UPROPERTY(EditAnywhere, Category = CustomizableObject)
    bool bReshapePhysicsVolumes = false;
	
	/** Enable rigid parts base mesh. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	bool bEnableRigidParts = false;

	/** Enables the deformation of all bones of the skeleton */
	UPROPERTY(EditAnywhere, Category = ReshapeBones, meta=(EditCondition="bReshapeSkeleton"))
	bool bDeformAllBones = false;

	/** Array with bones that will be deformed */
	UPROPERTY(EditAnywhere, Category = ReshapeBones, meta=(EditCondition="bReshapeSkeleton && !bDeformAllBones"))
	TArray<FMeshReshapeBoneReference> BonesToDeform;

	/** Enables the deformation of all physic bodies*/
	UPROPERTY(EditAnywhere, Category = ReshapePhysics, meta = (EditCondition = "bReshapePhysicsVolumes"))
	bool bDeformAllPhysicsBodies = false;

	/** Array with bones with physics bodies that will be deformed */
	UPROPERTY(EditAnywhere, Category = ReshapePhysics, meta=(EditCondition="bReshapePhysicsVolumes && !bDeformAllPhysicsBodies"))
	TArray<FMeshReshapeBoneReference> PhysicsBodiesToDeform;
};

