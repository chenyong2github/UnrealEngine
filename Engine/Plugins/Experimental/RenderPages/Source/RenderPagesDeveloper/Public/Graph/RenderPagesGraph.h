// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "RenderPagesGraph.generated.h"


class URenderPagesBlueprint;


/**
 * A UEdGraph child class for the RenderPages modules.
 *
 * Required in order for a RenderPageCollection to be able to have a blueprint graph.
 */
UCLASS(Deprecated)
class RENDERPAGESDEVELOPER_API UDEPRECATED_RenderPagesGraph : public UEdGraph
{
	GENERATED_BODY()

public:
	/** Set up this graph. */
	void Initialize(URenderPagesBlueprint* InBlueprint);

	/** Sets the required values for this graph. */
	virtual void PostLoad() override;

	/** Returns the blueprint of this graph. */
	URenderPagesBlueprint* GetBlueprint() const;
};
