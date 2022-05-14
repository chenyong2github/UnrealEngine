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
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const;
#if WITH_EDITOR && !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
#endif // WITH_EDITOR && !UE_BUILD_SHIPPING
	// End of UEdGraphNode interface

	bool IsBound() { return EgGraph && EgNodeGuid.IsValid(); }

	TSharedPtr<Eg::FGraph> GetEgGraph() { return EgGraph;}
	void SetEgGraph(TSharedPtr<Eg::FGraph> InEgGraph) { EgGraph = InEgGraph; }

	FGuid GetEgNodeGuid() const { return EgNodeGuid; }
	void SetEgNodeGuid(FGuid InGuid) { EgNodeGuid = InGuid; }

	// UObject interface
	void Serialize(FArchive& Ar);
	// End UObject interface

};

