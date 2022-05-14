// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EvalGraph/EvalGraph.h"

#include "EvalGraphObject.generated.h"

/**
* UEvalGraph (UObject)
*
* UObject wrapper for the Eg::FGraph
*
*/
UCLASS(BlueprintType, customconstructor)
class EVALGRAPHENGINE_API UEvalGraph : public UEdGraph
{
	GENERATED_UCLASS_BODY()

	TSharedPtr<Eg::FGraph, ESPMode::ThreadSafe> EvalGraph;

public:
	UEvalGraph(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** UObject Interface */
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
	/** End UObject Interface */

	void Serialize(FArchive& Ar);

	/** Accessors for internal geometry collection */
	void SetEvalGraph(TSharedPtr<Eg::FGraph, ESPMode::ThreadSafe> EvalGraphIn) { EvalGraph = EvalGraphIn; }
	TSharedPtr<Eg::FGraph, ESPMode::ThreadSafe>       GetEvalGraph() { return EvalGraph; }
	const TSharedPtr<Eg::FGraph, ESPMode::ThreadSafe> GetEvalGraph() const { return EvalGraph; }


};

