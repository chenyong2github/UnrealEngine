// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "ClothEditorContextObject.generated.h"

class SDataflowGraphEditor;
class UDataflow;
class UEdGraphNode;

namespace UE::Chaos::ClothAsset
{
enum class EClothPatternVertexType : uint8;
}
struct FManagedArrayCollection;


UCLASS()
class CHAOSCLOTHASSETEDITORTOOLS_API UClothEditorContextObject : public UObject
{
	GENERATED_BODY()

public:

	void Init(TWeakPtr<SDataflowGraphEditor> DataflowGraphEditor, TObjectPtr<UDataflow> DataflowGraph, UE::Chaos::ClothAsset::EClothPatternVertexType InConstructionViewMode, TWeakPtr<FManagedArrayCollection> SelectedClothCollection);

	TWeakPtr<SDataflowGraphEditor> GetDataflowGraphEditor();
	const TWeakPtr<const SDataflowGraphEditor> GetDataflowGraphEditor() const;

	TObjectPtr<UDataflow> GetDataflowGraph();
	const TObjectPtr<const UDataflow> GetDataflowGraph() const;

	/** 
	 * Return the single selected node in the Dataflow Graph Editor, or nullptr if multiple or no nodes are selected 
	 */
	UEdGraphNode* GetSingleSelectedNode() const;

	/** 
	* Return the single selected node in the Dataflow Graph Editor only if it has an output of the specified type
	* If there is not a single node selected, or if it does not have the specified output, return null 
	*/
	UEdGraphNode* GetSingleSelectedNodeWithOutputType(const FName& SelectedNodeOutputTypeName) const;

	/** 
	 * Create a node with the specified type in the graph 
	 */
	UEdGraphNode* CreateNewNode(const FName& NewNodeTypeName);


	/** Create a node with the specified type, then connect it to the output of the specified UpstreamNode.
	* If the specified output of the upstream node is already connected to another node downstream, we first break
	* that connecttion, then insert the new node along the previous connection.
	* We want to turn this:
	* 
	* [UpstreamNode] ----> [DownstreamNode(s)]
	* 
	* to this:
	*
	* [UpstreamNode] ----> [NewNode] ----> [DownstreamNode(s)]
	*
	* 
	* @param NewNodeTypeName The type of node to create, by name
	* @param UpstreamNode Node to connect the new node to
	* @param ConnectionTypeName The type of output of the upstream node to connect our new node to
	* @return The newly-created node
	*/
	UEdGraphNode* CreateAndConnectNewNode(
		const FName& NewNodeTypeName,
		UEdGraphNode& UpstreamNode,
		const FName& ConnectionTypeName);

	void SetClothCollection(UE::Chaos::ClothAsset::EClothPatternVertexType ViewMode, TWeakPtr<FManagedArrayCollection> ClothCollection);

	const TWeakPtr<const FManagedArrayCollection> GetSelectedClothCollection() const { return SelectedClothCollection; }
	UE::Chaos::ClothAsset::EClothPatternVertexType GetConstructionViewMode() const { return ConstructionViewMode; }
private:

	TWeakPtr<SDataflowGraphEditor> DataflowGraphEditor;

	UPROPERTY()
	TObjectPtr<UDataflow> DataflowGraph = nullptr;

	UE::Chaos::ClothAsset::EClothPatternVertexType ConstructionViewMode;
	TWeakPtr<const FManagedArrayCollection> SelectedClothCollection;
};


