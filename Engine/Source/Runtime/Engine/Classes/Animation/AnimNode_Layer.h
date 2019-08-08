// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimNode_SubInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimLayerInterface.h"

#include "AnimNode_Layer.generated.h"

struct FAnimInstanceProxy;
class UUserDefinedStruct;
struct FAnimBlueprintFunction;
class IAnimClassInterface;

USTRUCT(BlueprintInternalUseOnly)
struct ENGINE_API FAnimNode_Layer : public FAnimNode_SubInstance
{
	GENERATED_BODY()

public:
	/** 
	 * Optional interface. If this is set then this node will only accept (both statically and dynamically) anim instances that implement this interface.
	 * If not set, then this is considered a 'self' layer. This value is set when Layer is changed in the details panel.
	 */
	UPROPERTY()
	TSubclassOf<UAnimLayerInterface> Interface;

	/** The layer in the interface to use */
	UPROPERTY(EditAnywhere, Category = Settings)
	FName Layer;

	/** Set the layer's 'overlay' externally managed sub instance. */
	void SetLayerOverlaySubInstance(const UAnimInstance* InOwningAnimInstance, UAnimInstance* InNewSubInstance);

	/** FAnimNode_Base interface */
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;

	/** FAnimNode_SubInstance interface */
	virtual FName GetDynamicLinkFunctionName() const override;
	virtual UAnimInstance* GetDynamicLinkTarget(UAnimInstance* InOwningAnimInstance) const override;
	virtual UClass* GetTargetClass() const override 
	{
		return *Interface;
	}

protected:
	void InitializeSelfLayer(const UAnimInstance* SelfAnimInstance);
};