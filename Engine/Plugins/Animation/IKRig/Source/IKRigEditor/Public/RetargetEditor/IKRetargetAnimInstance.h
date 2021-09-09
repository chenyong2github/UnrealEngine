// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimPreviewInstance.h"
#include "AnimNodes/AnimNode_IKRetargeter.h"

#include "IKRetargetAnimInstance.generated.h"

class UIKRetargeter;

UCLASS(transient, NotBlueprintable)
class UIKRetargetAnimInstance : public UAnimPreviewInstance
{
	GENERATED_UCLASS_BODY()

public:

	void SetRetargetAssetAndSourceComponent(
		UIKRetargeter* InIKRetargetAsset,
		TWeakObjectPtr<USkeletalMeshComponent> InSourceMeshComponent);

	UIKRetargeter* GetCurrentlyUsedRetargeter() const;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	UPROPERTY(Transient)
	FAnimNode_IKRetargeter IKRetargeterNode;
};
