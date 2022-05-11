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
#if WITH_EDITOR
	friend class FPCGEditor;
#endif // WITH_EDITOR

	GENERATED_BODY()

public:
	UPCGGraph(const FObjectInitializer& ObjectInitializer);
	/** ~Begin UObject interface */
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	/** ~End UObject interface */

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = AssetInfo, AssetRegistrySearchable)
	bool bExposeToLibrary = false;

	UPROPERTY(EditAnywhere, Category = AssetInfo, AssetRegistrySearchable)
	FText Category;

	UPROPERTY(EditAnywhere, Category = AssetInfo, AssetRegistrySearchable)
	FText Description;
#endif

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Template)
	TSubclassOf<UPCGGraphSetupBP> GraphTemplate;

#if WITH_EDITOR
	/** Resets & initializes the graph from the Graph Template parameter. */
	UFUNCTION(BlueprintCallable, Category = Template)
	void InitializeFromTemplate();
#endif

	/** Creates a default node based on the settings class wanted. Returns the newly created node. */
	UFUNCTION(BlueprintCallable, Category = Graph, meta=(DeterminesOutputType = "InSettingsClass", DynamicOutputParam = "DefaultNodeSettings"))
	UPCGNode* AddNodeOfType(TSubclassOf<class UPCGSettings> InSettingsClass, UPCGSettings*& DefaultNodeSettings);

	/** Creates a node and assigns it in the input settings. Returns the created node. */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* AddNode(UPCGSettings* InSettings);

	/** Adds a directed edge in the graph. Returns the "To" node for easy chaining */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* AddEdge(UPCGNode* From, UPCGNode* To);

	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* AddLabeledEdge(UPCGNode* From, const FName& InboundLabel, UPCGNode* To, const FName& OutboundLabel);

	/** Returns the graph input node */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* GetInputNode() const { return InputNode; }

	/** Returns the graph output node */
	UFUNCTION(BlueprintCallable, Category = Graph)
	UPCGNode* GetOutputNode() const { return OutputNode; }

	bool Contains(UPCGNode* Node) const;
	const TArray<UPCGNode*>& GetNodes() const { return Nodes; }
	void AddNode(UPCGNode* InNode);
	void RemoveNode(UPCGNode* InNode);
	bool RemoveEdge(UPCGNode* From, const FName& FromLabel, UPCGNode* To, const FName& ToLabel);
	bool RemoveAllInboundEdges(UPCGNode* InNode);
	bool RemoveAllOutboundEdges(UPCGNode* InNode);
	bool RemoveInboundEdges(UPCGNode* InNode, const FName& InboundLabel);
	bool RemoveOutboundEdges(UPCGNode* InNode, const FName& OutboundLabel);

#if WITH_EDITOR
	void DisableNotificationsForEditor();
	void EnableNotificationsForEditor();
	void PreNodeUndo(UPCGNode* InPCGNode);
	void PostNodeUndo(UPCGNode* InPCGNode);
#endif

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
	void OnNodeChanged(UPCGNode* InNode, EPCGChangeType ChangeType);

	int32 GraphChangeNotificationsDisableCounter = 0;
	bool bDelayedChangeNotification = false;
	bool bDelayedChangeNotificationStructural = false;
#endif // WITH_EDITOR
};
