// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGGraphSetupBP.h"

#include "PCGGraph.generated.h"

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPCGGraphChanged, UPCGGraph* /*Graph*/, bool /*bIsStructural*/);
#endif // WITH_EDITOR

UCLASS(BlueprintType, ClassGroup = (Procedural), hidecategories=(Object))
class PCG_API UPCGGraph : public UObject
{
	GENERATED_BODY()

public:
	UPCGGraph(const FObjectInitializer& ObjectInitializer);
	/** ~Begin UObject interface */
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	/** ~End UObject interface */

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Template)
	TSubclassOf<UPCGGraphSetupBP> GraphTemplate;

#if WITH_EDITOR
	/** Resets & initializes the graph from the Graph Template parameter. */
	UFUNCTION(BlueprintCallable, Category = Template)
	void InitializeFromTemplate();
#endif

	/** Creates a default node based on the settings class wanted. Returns the newly created node. */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* AddNodeOfType(TSubclassOf<class UPCGSettings> InSettingsClass);

	/** Creates a node and assigns it in the input settings. Returns the created node. */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* AddNode(UPCGSettings* InSettings);

	/** Adds a directed edge in the graph. Returns the "To" node for easy chaining */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* AddEdge(UPCGNode* From, UPCGNode* To);

	/** Returns the graph input node */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* GetInputNode() const { return InputNode; }

	/** Returns the graph output node */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* GetOutputNode() const { return OutputNode; }

	bool Contains(UPCGNode* Node) const;
	const TArray<UPCGNode*>& GetNodes() const { return Nodes; }
#if WITH_EDITOR
	FPCGTagToSettingsMap GetTrackedTagsToSettings() const;
#endif

#if WITH_EDITOR
	FOnPCGGraphChanged OnGraphChangedDelegate;
#endif // WITH_EDITOR

protected:
	void OnNodeAdded(UPCGNode* InNode);
	void OnNodeRemoved(UPCGNode* InNode);

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Graph)
	TArray<TObjectPtr<UPCGNode>> Nodes;

	// Add input/output nodes
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Graph)
	TObjectPtr<UPCGNode> InputNode;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Graph)
	TObjectPtr<UPCGNode> OutputNode;

#if WITH_EDITOR
private:
	void NotifyGraphChanged(bool bIsStructural);
	void OnSettingsChanged(UPCGNode* InNode);
	void OnStructuralSettingsChanged(UPCGNode* InNode);

	bool bEnableGraphChangeNotifications = true;
#endif // WITH_EDITOR
};
