// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Dataflow/Dataflow.h"

#include "DataflowObject.generated.h"

/**
* UDataflow (UObject)
*
* UObject wrapper for the Dataflow::FGraph
*
*/
UCLASS(BlueprintType, customconstructor)
class DATAFLOWENGINE_API UDataflow : public UEdGraph
{
	GENERATED_UCLASS_BODY()

	TSharedPtr<Dataflow::FGraph, ESPMode::ThreadSafe> Dataflow;

public:
	UDataflow(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** UObject Interface */
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
	/** End UObject Interface */

	void Serialize(FArchive& Ar);

	/** Accessors for internal geometry collection */
	void SetDataflow(TSharedPtr<Dataflow::FGraph, ESPMode::ThreadSafe> DataflowIn) { Dataflow = DataflowIn; }
	TSharedPtr<Dataflow::FGraph, ESPMode::ThreadSafe>       GetDataflow() { return Dataflow; }
	const TSharedPtr<Dataflow::FGraph, ESPMode::ThreadSafe> GetDataflow() const { return Dataflow; }


};

