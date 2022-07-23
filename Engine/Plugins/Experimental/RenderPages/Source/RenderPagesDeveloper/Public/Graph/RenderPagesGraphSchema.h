// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "RenderPagesGraphSchema.generated.h"


/**
 * A UEdGraphSchema child class for the RenderPages modules.
 *
 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
 */
UCLASS(Deprecated)
class RENDERPAGESDEVELOPER_API UDEPRECATED_RenderPagesGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()
};
