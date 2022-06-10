// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "RenderPagesGraphNode.generated.h"


/**
 * A UEdGraphNode child class for the RenderPages modules.
 *
 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
 */
UCLASS()
class RENDERPAGESDEVELOPER_API URenderPagesGraphNode : public UEdGraphNode
{
	GENERATED_BODY()
};
