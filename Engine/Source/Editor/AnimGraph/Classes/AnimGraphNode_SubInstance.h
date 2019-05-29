// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "AnimGraphNode_SubInstanceBase.h"
#include "Animation/AnimNode_SubInstance.h"

#include "AnimGraphNode_SubInstance.generated.h"

UCLASS(MinimalAPI)
class UAnimGraphNode_SubInstance : public UAnimGraphNode_SubInstanceBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_SubInstance Node;

	// Begin UAnimGraphNode_CustomProperty
	virtual FAnimNode_CustomProperty* GetCustomPropertyNode() override { return &Node;  }
	virtual const FAnimNode_CustomProperty* GetCustomPropertyNode() const override { return &Node; }

	// Begin UAnimGraphNode_SubInstanceBase
	virtual FAnimNode_SubInstance* GetSubInstanceNode() override { return &Node;  }
	virtual const FAnimNode_SubInstance* GetSubInstanceNode() const override { return &Node; }
};
