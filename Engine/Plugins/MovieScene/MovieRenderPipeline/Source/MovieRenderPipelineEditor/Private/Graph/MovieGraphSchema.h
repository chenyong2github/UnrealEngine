// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreTypes.h"
#include "EdGraph/EdGraphSchema.h"
#include "MovieGraphSchema.generated.h"

class UMovieGraphNode;


UCLASS()
class UMovieGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()
	//~ Begin EdGraphSchema Interface
	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	//virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual bool TryCreateConnection(UEdGraphPin* InA, UEdGraphPin* InB) const override;
	//virtual const FPinConnectionResponse CanMergeNodes(const UEdGraphNode* A, const UEdGraphNode* B) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

	//virtual int32 GetNodeSelectionCount(const UEdGraph* Graph) const override;
	//virtual bool IsCacheVisualizationOutOfDate(int32 InVisualizationCacheID) const override;
	//virtual int32 GetCurrentVisualizationCacheID() const override;
	//virtual void ForceVisualizationCacheClear() const override;
	//virtual TSharedPtr<FEdGraphSchemaAction> GetCreateCommentAction() const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;
	//~ End EdGraphSchema Interface
	static void InitMoviePipelineNodeClasses();
	
	
private:
	static TArray<UClass*> MoviePipelineNodeClasses;
	
};


USTRUCT()
struct FMovieGraphSchemaAction_NewNativeElement : public FEdGraphSchemaAction
{
	GENERATED_BODY()
	
	// Inherit the base class's constructors
	using FEdGraphSchemaAction::FEdGraphSchemaAction;
	
	// Simple type info
	static FName StaticGetTypeId()
	{
		static FName Type("FMovieGraphSchemaAction_NewNativeElement");
		return Type;
	}
	
	UPROPERTY()
	TSubclassOf<UMovieGraphNode> NodeClass;

	//~ Begin FEdGraphSchemaAction Interface
	virtual FName GetTypeId() const override { return StaticGetTypeId(); }
	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
	//~ End FEdGraphSchemaAction Interface
};