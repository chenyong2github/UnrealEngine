// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimPreviewInstance.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"

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

	const UIKRetargetProcessor* GetRetargetProcessor() const;

	void SetProcessorNeedsInitialized();

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	UPROPERTY(Transient)
	FAnimNode_RetargetPoseFromMesh IKRetargeterNode;
};
