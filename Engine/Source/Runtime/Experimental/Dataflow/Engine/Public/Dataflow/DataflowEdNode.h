// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Dataflow/DataflowCore.h"

#include "DataflowEdNode.generated.h"



UCLASS()
class DATAFLOWENGINE_API UDataflowEdNode : public UEdGraphNode
{
	GENERATED_BODY()

	FGuid DataflowNodeGuid;
	TSharedPtr<Dataflow::FGraph> DataflowGraph;
public:

	// UEdGraphNode interface
	virtual void AllocateDefaultPins();
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const;
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
	// End of UEdGraphNode interface

	bool IsBound() { return DataflowGraph && DataflowNodeGuid.IsValid(); }

	TSharedPtr<Dataflow::FGraph> GetDataflowGraph() { return DataflowGraph;}
	void SetDataflowGraph(TSharedPtr<Dataflow::FGraph> InDataflowGraph) { DataflowGraph = InDataflowGraph; }

	FGuid GetDataflowNodeGuid() const { return DataflowNodeGuid; }
	void SetDataflowNodeGuid(FGuid InGuid) { DataflowNodeGuid = InGuid; }

	// UObject interface
	void Serialize(FArchive& Ar);
	// End UObject interface

};

