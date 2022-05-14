// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EvalGraph/EvalGraph.h"

#include "EvalGraphEdNode.generated.h"



UCLASS()
class EVALGRAPHENGINE_API UEvalGraphEdNode : public UEdGraphNode
{
	GENERATED_BODY()

	FGuid EgNodeGuid;
	TSharedPtr<Eg::FGraph> EgGraph;

public:

	// UEdGraphNode interface
	virtual void AllocateDefaultPins();
	// End of UEdGraphNode interface

	void SetEgGraph(TSharedPtr<Eg::FGraph> InEgGraph) { EgGraph = InEgGraph; }
	void SetEgNode(FGuid InGuid) {EgNodeGuid = InGuid;}

	void Serialize(FArchive& Ar);

};

