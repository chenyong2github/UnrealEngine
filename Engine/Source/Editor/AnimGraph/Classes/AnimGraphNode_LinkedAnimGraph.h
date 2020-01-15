// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "AnimGraphNode_LinkedAnimGraphBase.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"

#include "AnimGraphNode_LinkedAnimGraph.generated.h"

UCLASS(MinimalAPI)
class UAnimGraphNode_LinkedAnimGraph : public UAnimGraphNode_LinkedAnimGraphBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_LinkedAnimGraph Node;

	// Begin UAnimGraphNode_CustomProperty
	virtual FAnimNode_CustomProperty* GetCustomPropertyNode() override { return &Node;  }
	virtual const FAnimNode_CustomProperty* GetCustomPropertyNode() const override { return &Node; }

	// Begin UAnimGraphNode_LinkedAnimGraphBase
	virtual FAnimNode_LinkedAnimGraph* GetLinkedAnimGraphNode() override { return &Node; }
	virtual const FAnimNode_LinkedAnimGraph* GetLinkedAnimGraphNode() const override { return &Node; }
};

UE_DEPRECATED(4.24, "UAnimGraphNode_SubInstance has been renamed to UAnimGraphNode_LinkedAnimGraph")
typedef UAnimGraphNode_LinkedAnimGraph UAnimGraphNode_SubInstance;