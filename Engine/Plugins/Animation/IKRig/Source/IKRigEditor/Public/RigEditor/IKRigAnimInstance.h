// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimPreviewInstance.h"

#include "IKRigAnimInstance.generated.h"

class UIKRigDefinition;

UCLASS(transient, NotBlueprintable)
class UIKRigAnimInstance : public UAnimPreviewInstance
{
	GENERATED_UCLASS_BODY()

public:

	void SetIKRigAsset(UIKRigDefinition* IKRigAsset);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
