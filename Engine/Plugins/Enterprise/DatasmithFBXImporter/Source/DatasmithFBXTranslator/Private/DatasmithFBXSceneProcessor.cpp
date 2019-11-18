// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithFBXSceneProcessor.h"

#include "DatasmithFBXHashUtils.h"
#include "DatasmithFBXImporterLog.h"
#include "DatasmithMeshHelper.h"

#include "FbxImporter.h"
#include "FileHelpers.h"
#include "StaticMeshAttributes.h"
#include "MeshDescriptionOperations.h"
#include "MeshUtilities.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

#define ANIMNODE_SUFFIX						TEXT("_AnimNode")
#define MERGED_SUFFIX						TEXT("_Merged")
#define SHARED_CONTENT_SUFFIX				TEXT("_SharedContent")
#define LIGHT_SUFFIX						TEXT("_Light")
#define MESH_SUFFIX							TEXT("_Mesh")
#define CAMERA_SUFFIX						TEXT("_Camera")
#define MIN_TOTAL_NODES_TO_OPTIMIZE			30
#define MIN_NODES_IN_SUBTREE_TO_OPTIMIZE	5

#define MAX_MERGE_VERTEX_COUNT   10000000
#define MAX_MERGE_TRIANGLE_COUNT  3000000

FDatasmithFBXSceneProcessor::FDatasmithFBXSceneProcessor(FDatasmithFBXScene* InScene)
	: Scene(InScene)
{
}

struct FLightMapNodeRemover
{
	TMap< FString, TSharedPtr<FDatasmithFBXSceneMaterial> > NameToMaterial;

	void Recurse(TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		bool IsLightMapMaterialPresent = false;
		bool IsOtherMaterialPresent = false;
		for (int32 MaterialIndex = 0; MaterialIndex < Node->Materials.Num(); MaterialIndex++)
		{
			TSharedPtr<FDatasmithFBXSceneMaterial>& Material = Node->Materials[MaterialIndex];

			if (Material->Name.StartsWith(TEXT("Light_Map")))
			{
				IsLightMapMaterialPresent = true;
			}
			else
			{
				IsOtherMaterialPresent = true;
			}
		}

		for (int32 NodeIndex = 0; NodeIndex < Node->Children.Num(); NodeIndex++)
		{
			Recurse(Node->Children[NodeIndex]);
		}

		bool ShouldRemoveMesh = IsLightMapMaterialPresent && !IsOtherMaterialPresent;

		if (ShouldRemoveMesh)
		{
			Node->Mesh.Reset();
			Node->Materials.Empty();
		}
	}
};

void FDatasmithFBXSceneProcessor::RemoveLightMapNodes()
{
	FLightMapNodeRemover Finder;
	Finder.Recurse(Scene->RootNode);
}

struct FDupMaterialFinder
{
	TMap< FString, TSharedPtr<FDatasmithFBXSceneMaterial> > NameToMaterial;

	void Recurse(TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < Node->Materials.Num(); MaterialIndex++)
		{
			TSharedPtr<FDatasmithFBXSceneMaterial>& Material = Node->Materials[MaterialIndex];
			TSharedPtr<FDatasmithFBXSceneMaterial>* PrevMaterial = NameToMaterial.Find(Material->Name);
			if (PrevMaterial == nullptr)
			{
				// This is the first occurrence of this material name
				NameToMaterial.Add(Material->Name, Material);
			}
			else
			{
				// We already have a material with the same name, use that material
				Material = *PrevMaterial;
			}
		}

		for (int32 NodeIndex = 0; NodeIndex < Node->Children.Num(); NodeIndex++)
		{
			Recurse(Node->Children[NodeIndex]);
		}
	}
};

void FDatasmithFBXSceneProcessor::FindDuplicatedMaterials()
{
	FDupMaterialFinder Finder;
	Finder.Recurse(Scene->RootNode);
}

struct FDupMeshFinder
{
	TMap< FMD5Hash, TSharedPtr<FDatasmithFBXSceneMesh> > HashToMesh;

	void Recurse(TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		if (Node->Mesh.IsValid())
		{
			const FMD5Hash& MeshHash = Node->Mesh->GetHash();
			TSharedPtr<FDatasmithFBXSceneMesh>* PrevMesh = HashToMesh.Find(MeshHash);
			if (PrevMesh != nullptr)
			{
				// We already have the same mesh, replace
				Node->Mesh = *PrevMesh;
			}
			else
			{
				HashToMesh.Add(MeshHash, Node->Mesh);
			}
		}

		for (int32 NodeIndex = 0; NodeIndex < Node->Children.Num(); NodeIndex++)
		{
			Recurse(Node->Children[NodeIndex]);
		}
	}
};

void FDatasmithFBXSceneProcessor::FindDuplicatedMeshes()
{
	FDupMeshFinder Finder;
	Finder.Recurse(Scene->RootNode);
}

void FDatasmithFBXSceneProcessor::RemoveEmptyNodesRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node)
{
	// Recurse to children first. We're iterating in reverse order because iteration may change Children list.
	for (int32 NodeIndex = Node->Children.Num() - 1; NodeIndex >= 0; NodeIndex--)
	{
		RemoveEmptyNodesRecursive(Node->Children[NodeIndex]);
	}

	// Now check if we can remove this node
	if ( !( Node->bShouldKeepThisNode || Node->Children.Num() != 0 || Node->Mesh.IsValid() || !Node->Parent.IsValid() || Node->Camera.IsValid() || Node->Light.IsValid()) )
	{
		// This node doesn't have any children (probably they were removed with recursive call to this function),
		// and it wasn't marked as "read-only", so we can safely delete it.
		Node->RemoveNode();
	}
}

void FDatasmithFBXSceneProcessor::RemoveTempNodesRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node)
{
	// Recurse to children first. We're iterating in reverse order because iteration may change Children list.
	for (int32 NodeIndex = Node->Children.Num() - 1; NodeIndex >= 0; NodeIndex--)
	{
		RemoveTempNodesRecursive(Node->Children[NodeIndex]);
	}

	// Now check if we can remove this node
	if (Node->OriginalName.MatchesWildcard(TEXT("__temp_*")))
	{
		Node->RemoveNode();
	}
}

struct FNodeMarkHelper
{
	TSet<FName> SwitchObjectNames;
	TSet<FName> ToggleObjectNames;
	TSet<FName> ObjectSetObjectNames;
	TSet<FName> AnimatedObjectNames;
	TSet<FName> SwitchMaterialObjectNames;
	TSet<FName> TransformVariantObjectNames;

	void Recurse(TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		//todo: change this logic, perhaps use UI controls to specify nodes etc, store settings as class fields
		const FName NodeName = FName(*Node->OriginalName);
		if (NodeName != NAME_None)
		{
			if (SwitchObjectNames.Contains(NodeName))
			{
				Node->MarkSwitchNode();
			}
			if (ToggleObjectNames.Contains(NodeName))
			{
				Node->MarkToggleNode();
			}
			if (ObjectSetObjectNames.Contains(NodeName))
			{
				Node->MarkMovableNode();
			}
			if (AnimatedObjectNames.Contains(NodeName))
			{
				Node->MarkAnimatedNode();
			}
			if (SwitchMaterialObjectNames.Contains(NodeName))
			{
				Node->MarkSwitchMaterialNode();
			}
			if (TransformVariantObjectNames.Contains(NodeName))
			{
				Node->MarkMovableNode();
			}
			if (Node->Light.IsValid())
			{
				Node->MarkLightNode();
			}
			if (Node->Camera.IsValid())
			{
				Node->MarkCameraNode();
			}

			// mark switch object options as toggle
			auto Parent = Node->Parent.Pin();
			if (Parent.IsValid())
			{
				if (Parent->GetNodeType() == ENodeType::Switch)
				{
					Node->MarkToggleNode();
				}
			}
		}

		for (int32 NodeIndex = 0; NodeIndex < Node->Children.Num(); NodeIndex++)
		{
			Recurse(Node->Children[NodeIndex]);
		}
	}
};

void FDatasmithFBXSceneProcessor::FindPersistentNodes()
{
	FNodeMarkHelper Helper;
	Helper.SwitchMaterialObjectNames.Append(Scene->SwitchObjects);
	Helper.AnimatedObjectNames.Append(Scene->AnimatedObjects);
	Helper.SwitchMaterialObjectNames.Append(Scene->SwitchMaterialObjects);
	Helper.TransformVariantObjectNames.Append(Scene->TransformVariantObjects);
	Helper.ToggleObjectNames.Append(Scene->ToggleObjects);
	Helper.ObjectSetObjectNames.Append(Scene->ObjectSetObjects);

	Helper.Recurse(Scene->RootNode);
}

void FDatasmithFBXSceneProcessor::FixNodeNames()
{
	struct FFixNodeNamesHelper
	{
		void Do(FDatasmithFBXScene* InScene)
		{
			RecursiveFixNodeNames(InScene->RootNode);
		}

	protected:

		// this is copied from XmlFile.cpp to match their logic
		bool IsWhiteSpace(TCHAR Char)
		{
			// Whitespace will be any character that is not a common printed ASCII character (and not space/tab)
			if(Char == TCHAR(' ') ||
				Char == TCHAR('\t') ||
				Char < 32 ) // ' '
			{
				return true;
			}
			return false;
		}


		FString FixName(const FString& Name)
		{
			FString Result;
			bool LastWasWhitespace = false;
			for (int i = 0;i < Name.Len(); ++i)
			{
				TCHAR Char = Name[i];
				if (IsWhiteSpace(Char))
				{
					if (!LastWasWhitespace)
					{
						Result += TEXT(" ");
					}
					LastWasWhitespace = true;
				}
				else
				{
					Result += Char;
					LastWasWhitespace = false;
				}
			}
			return Result;
		}

		void RecursiveFixNodeNames(TSharedPtr<FDatasmithFBXSceneNode>& Node)
		{
			Node->Name = FixName(Node->Name);

			for (int32 NodeIndex = 0; NodeIndex < Node->Children.Num(); NodeIndex++)
			{
				TSharedPtr<FDatasmithFBXSceneNode>& Child = Node->Children[NodeIndex];

				RecursiveFixNodeNames(Child);
			}
		}
	};

	FFixNodeNamesHelper Helper;
	Helper.Do(Scene);
}

void FDatasmithFBXSceneProcessor::SplitLightNodesRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node)
{
	// Recurse first so we don't check a potential separated child node
	for (TSharedPtr<FDatasmithFBXSceneNode> Child : Node->Children)
	{
		SplitLightNodesRecursive(Child);
	}

	if (Node->Light.IsValid())
	{
		TSharedPtr<FDatasmithFBXSceneNode> SeparatedChild = MakeShared<FDatasmithFBXSceneNode>();

		SeparatedChild->Name = Node->Name + FString(LIGHT_SUFFIX);
		SeparatedChild->OriginalName = Node->OriginalName;
		SeparatedChild->Light = Node->Light;
		SeparatedChild->SplitNodeID = Node->SplitNodeID;

		// Match light direction convention
		SeparatedChild->LocalTransform.SetIdentity();
		SeparatedChild->LocalTransform.ConcatenateRotation(FRotator(-90, 0, 0).Quaternion());

		//TODO: Test SharedContent mechanism when splitting nodes
		SeparatedChild->SharedContent = Node->SharedContent;
		SeparatedChild->SharedParent = Node->SharedParent;

		/** Fix hierarchy

			P							P
			|							|
			N (light)		--->		N (node)
		   / \						  / | \
		  C1  C2					 C1 C2 SC (_Light node)

		P: Parent; N: Node; SC: SeparatedChild; C1,2: Children */
		SeparatedChild->Parent = Node;
		SeparatedChild->Children.Empty();
		Node->Children.Add(SeparatedChild);

		//Clean this Node
		Node->Light.Reset();
	}
}

void FDatasmithFBXSceneProcessor::SplitCameraNodesRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node)
{
	// Recurse first so we don't check a potential separated child node
	for (TSharedPtr<FDatasmithFBXSceneNode> Child : Node->Children)
	{
		SplitCameraNodesRecursive(Child);
	}

	if (Node->Camera.IsValid())
	{
		TSharedPtr<FDatasmithFBXSceneNode> SeparatedChild = MakeShared<FDatasmithFBXSceneNode>();

		SeparatedChild->Name = Node->Name + FString(CAMERA_SUFFIX);
		SeparatedChild->OriginalName = Node->OriginalName;
		SeparatedChild->Camera = Node->Camera;
		SeparatedChild->SplitNodeID = Node->SplitNodeID;

		//Now that the camera is separated from the hierarchy we can apply the roll value without consequences
		SeparatedChild->LocalTransform.SetIdentity();
		SeparatedChild->LocalTransform.ConcatenateRotation(FRotator(-90, -90, -Node->Camera->Roll).Quaternion());

		//TODO: Test SharedContent mechanism when splitting nodes
		SeparatedChild->SharedContent = Node->SharedContent;
		SeparatedChild->SharedParent = Node->SharedParent;

		/** Fix hierarchy

		    P							P
			|							|
			N (camera)		--->		N (node)
		   / \						  / | \
		  C1  C2					 C1 C2 SC (_Camera node)

		P: Parent; N: Node; SC: SeparatedChild; C1,2: Children */
		SeparatedChild->Parent = Node;
		SeparatedChild->Children.Empty();
		Node->Children.Add(SeparatedChild);

		//Clean this Node
		Node->Camera.Reset();
	}
}

struct FSwitchState
{
	FSwitchState(TSharedPtr<FDatasmithFBXSceneNode> Node)
	{
		while (true)
		{
			TSharedPtr<FDatasmithFBXSceneNode> Parent = Node->Parent.Pin();
			if (!Parent.IsValid())
			{
				break;
			}
			if (EnumHasAnyFlags(Parent->GetNodeType(), ENodeType::Switch))
			{
				// Parent is a switch, Node is switch value
				check(SwitchValues.Find(Parent->Name) == nullptr);
				SwitchValues.Add(Parent->Name, Node->Name);
			}
			Node = Parent;
		}
	}

	/** Function compares two switch states and returns true if they doesn't have a possibility to be visible together */
	bool AreNodesMutuallyInvisible(const FSwitchState& Other)
	{
		for (auto& It : SwitchValues)
		{
			// Find this switch in Other's switch list
			auto* Found = Other.SwitchValues.Find(It.Key);
			if (Found != nullptr)
			{
				if (It.Value != *Found)
				{
					// At least one of switches has different values
					return true;
				}
			}
		}
		// No identical switches found
		return false;
	}

	/** Mapping switch name to its value */
	TMap<FString, FString> SwitchValues;
};

// An idea behind this optimization is to locate scene subtrees which are the same and which are used
// in different switch combinations. Such subtrees will never be visible at the same time. These subtrees
// will be replaced with a "shared node" blueprint, which will use the single instance of subtree, and
// this subtree will be reattached to "shared node" as child nodes when "shared node" will became visible.
struct FDuplicatedNodeFinder
{
	FDuplicatedNodeFinder(FDatasmithFBXScene* InScene)
	: Scene(InScene)
	{
	}

	void PrepareNodeHashMap()
	{
		PrepareNodeHashMapRecursive(Scene->RootNode);
	}

	void PrepareNodeHashMapRecursive(const TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		// Register node's hash
		const FMD5Hash& NodeHash = Node->GetHash();
		TArray< TSharedPtr<FDatasmithFBXSceneNode> >* HashMap = HashToNodes.Find(NodeHash);
		if (HashMap == nullptr)
		{
			// This hash didn't appear in scene yet
			TArray< TSharedPtr<FDatasmithFBXSceneNode> > NewHashMap;
			HashMap = &HashToNodes.Add(NodeHash, NewHashMap);
		}
		HashMap->Add(Node);

		// Recurse
		for (TSharedPtr<FDatasmithFBXSceneNode>& Child : Node->Children)
		{
			PrepareNodeHashMapRecursive(Child);
		}
	}

	void ExclideFromHashMap(TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		TArray< TSharedPtr<FDatasmithFBXSceneNode> >* NodesWithSameHash = HashToNodes.Find(Node->GetHash());
		check(NodesWithSameHash != nullptr);
		NodesWithSameHash->Remove(Node);
	}

	TArray< TSharedPtr<FDatasmithFBXSceneNode> > FindNodesForSharing(TSharedPtr<FDatasmithFBXSceneNode> Node)
	{
		TArray< TSharedPtr<FDatasmithFBXSceneNode> > FoundNodes;

		TArray< TSharedPtr<FDatasmithFBXSceneNode> >* NodesWithSameHash = HashToNodes.Find(Node->GetHash());
		// We always have 'Node' in this list, so result will never be null
		check(NodesWithSameHash != nullptr);
		int32 InstanceCount = NodesWithSameHash->Num();
		if (InstanceCount < 2)
		{
			// We have just this node, and nothing else, return empty list
			return FoundNodes;
		}

		// This technique is intended to optimize number of nodes for very complex scenes. Check if we're going to
		// optimize a too simple subtree.
		int32 NodesInSubtree = Node->GetChildrenCountRecursive();
		if (NodesInSubtree < MIN_NODES_IN_SUBTREE_TO_OPTIMIZE)
		{
			// It is not worth replacing (say) 2 nodes with 1 node
			return FoundNodes;
		}

		// Verify number of nodes we could release if all instances are suitable for optimization
		int32 NumReleasedNodes = (InstanceCount - 1) * NodesInSubtree;
		if (NumReleasedNodes < MIN_TOTAL_NODES_TO_OPTIMIZE)
		{
			// Too simple optimization, not worth doing.
			return FoundNodes;
		}

		// Verify all nodes with same hash. Check their switch combinations, and find nodes
		// which could be safely reused - in a case if they don't share the same configuration.
		// An example of node which could share configuration: a car have 4 wheels which are
		// usually same, but we can't use shared node for them because all 4 wheels are always
		// visible simultaneously.

		// Build switch configuration for current node
		FSwitchState NodeState(Node);

		// This array will hold all configurations which we will verify. If node A is "compatible"
		// with node B, and node A compatible with node C, this doesn't mean that B is compatible
		// with C, so we'll accumulate verified states here.
		TArray<FSwitchState> States;
		States.Add(NodeState);

		for (TSharedPtr<FDatasmithFBXSceneNode>& NodeToCheck : *NodesWithSameHash)
		{
			if (NodeToCheck == Node)
			{
				continue;
			}

			bool bCompatible = true;
			FSwitchState CheckState(NodeToCheck);
			for (FSwitchState& ExistingState : States)
			{
				if (ExistingState.AreNodesMutuallyInvisible(CheckState) == false)
				{
					bCompatible = false;
					break;
				}
			}

			if (bCompatible)
			{
				FoundNodes.Add(NodeToCheck);
				States.Add(CheckState);
			}
		}

		// Now check optimization effectiveness again
		if (FoundNodes.Num() * NodesInSubtree < MIN_TOTAL_NODES_TO_OPTIMIZE)
		{
			FoundNodes.Empty();
		}

		return FoundNodes;
	}

	void ProcessTreeRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node)
	{
		TArray< TSharedPtr<FDatasmithFBXSceneNode> > Instances = FindNodesForSharing(Node);
		if (Instances.Num())
		{
			// Replace nodes with instances
			//
			// Node
			//   + Child_1
			//   + Child_2
			// Other_Node
			//   + Child_1
			//   + Child_2
			//
			// ... will become
			//
			// SharedNode(Node) -> Node
			//   + Node
			//     + Child_1
			//     + Child_2
			// SharedNode(OtherNode) -> Node

			// Create a shared node (a kind of proxy) which will replace 'Node' and use this 'Node' as SharedContent
			TSharedPtr<FDatasmithFBXSceneNode> SharedContent = Node;
			TSharedPtr<FDatasmithFBXSceneNode> SharedNode(new FDatasmithFBXSceneNode());
			SharedNode->Name = Node->Name;
			SharedNode->OriginalName = Node->Name;
			Node->Name += SHARED_CONTENT_SUFFIX;
			// Insert SharedNode into hierarchy between Node->Parent and Node
			TSharedPtr<FDatasmithFBXSceneNode> Parent = Node->Parent.Pin();
			for (TSharedPtr<FDatasmithFBXSceneNode>& Sibling : Parent->Children)
			{
				if (Sibling == Node)
				{
					// this parent's child is 'Node', replace it with SharedNode
					Sibling = SharedNode;
					SharedNode->Parent = Parent;
					Node->Parent = SharedNode;
					SharedNode->Children.Add(Node);
					break;
				}
			}
			// Finalize creation of SharedNode
			SharedNode->MarkSharedNode(SharedContent);
			// SharedNode (proxy) should have transform of node which we're sharing, so 'Node' will have identity transform
			// and could be correctly reattached to another parent.
			SharedNode->LocalTransform = Node->LocalTransform;
			Node->LocalTransform = FTransform::Identity;
			ExclideFromHashMap(Node);

			// Process instances
			for (int32 InstanceIndex = 0; InstanceIndex < Instances.Num(); InstanceIndex++)
			{
				TSharedPtr<FDatasmithFBXSceneNode>& InstanceNode = Instances[InstanceIndex];
				// Remove instance's children
				UnhashNodesRecursive(InstanceNode);
				InstanceNode->Children.Empty();
				// In a case if this node is a switch, the switch functionality will be in SharedContent
				// node, and InstanceNode should became a SharedNode.
				InstanceNode->ResetNodeType();
				InstanceNode->MarkSharedNode(SharedContent);
			}
		}
		else
		{
			// Recurse to children
			for (TSharedPtr<FDatasmithFBXSceneNode>& Child : Node->Children)
			{
				ProcessTreeRecursive(Child);
			}
		}
	}

	void UnhashNodesRecursive(TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		ExclideFromHashMap(Node);

		// Recurse to children
		for (TSharedPtr<FDatasmithFBXSceneNode>& Child : Node->Children)
		{
			UnhashNodesRecursive(Child);
		}
	}

	void InvalidateHashesRecursive(TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		Node->InvalidateHash();
		for (TSharedPtr<FDatasmithFBXSceneNode>& Child : Node->Children)
		{
			InvalidateHashesRecursive(Child);
		}
	}

	FDatasmithFBXScene* Scene;
	TMap< FMD5Hash, TArray< TSharedPtr<FDatasmithFBXSceneNode> > > HashToNodes;
};

void FDatasmithFBXSceneProcessor::OptimizeDuplicatedNodes()
{
	// FDuplicatedNodeFinder performs optimization of top-level nodes. When it finds a node which could be
	// shared between different parents, it won't optimize content of that node. To perform optimization
	// better, we're doing multiple passes. Doing optimization in a single pass (for example, recursing
	// into shared content nodes immediately after it has been found) would require too complex code
	// because we're using node hash maps there to optimize search - we'd need to modify these structures
	// too often. So, it's easier to do multiple passes instead.

	for (int32 Pass = 1; Pass <= 4; Pass++)
	{
		int32 TotalNodeCount = Scene->RootNode->GetChildrenCountRecursive(true);
		FDuplicatedNodeFinder Finder(Scene);
		Finder.PrepareNodeHashMap();
		Finder.ProcessTreeRecursive(Scene->RootNode);
		int32 NewTotalNodeCount = Scene->RootNode->GetChildrenCountRecursive(true);

		if (NewTotalNodeCount == TotalNodeCount)
		{
			// Nothing has been optimized
			UE_LOG(LogDatasmithFBXImport, Log, TEXT("Optimized duplicated nodes (pass %d): nothing has been done"), Pass);
			break;
		}

		UE_LOG(LogDatasmithFBXImport, Log, TEXT("Optimized duplicated nodes (pass %d): reduced node count from %d to %d"), Pass, TotalNodeCount, NewTotalNodeCount);
		Finder.InvalidateHashesRecursive(Scene->RootNode);
	}
}

void FDatasmithFBXSceneProcessor::RemoveInvisibleNodes()
{
	struct FInvisibleNodesRemover {

		void RemoveNodeTree(TSharedPtr<FDatasmithFBXSceneNode> Node)
		{
			// Recurse to children first. We're iterating in reverse order because iteration may change Children list.
			for (int32 NodeIndex = Node->Children.Num() - 1; NodeIndex >= 0; NodeIndex--)
			{
				RemoveNodeTree(Node->Children[NodeIndex]);
			}
			Node->RemoveNode();
		}

		void RemoveInvisibleNodesRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node)
		{
			// Now check if we can remove this node
			if (!Node->bShouldKeepThisNode && (Node->Visibility < 0.1f))
			{
				RemoveNodeTree(Node);
			}
			else
			{
				// Recurse to children first. We're iterating in reverse order because iteration may change Children list.
				for (int32 NodeIndex = Node->Children.Num() - 1; NodeIndex >= 0; NodeIndex--)
				{
					RemoveInvisibleNodesRecursive(Node->Children[NodeIndex]);
				}
			}
		}
	};

	FInvisibleNodesRemover().RemoveInvisibleNodesRecursive(Scene->RootNode);
}

void FDatasmithFBXSceneProcessor::SimplifyNodeHierarchy()
{
	struct FNodeHierarchySimplifier {

		void RemoveNodeTree(TSharedPtr<FDatasmithFBXSceneNode> Node)
		{
			// Recurse to children first. We're iterating in reverse order because iteration may change Children list.
			for (int32 NodeIndex = Node->Children.Num() - 1; NodeIndex >= 0; NodeIndex--)
			{
				RemoveNodeTree(Node->Children[NodeIndex]);
			}
			Node->RemoveNode();
		}

		void SimplifyHierarchyRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node)
		{
			// Recurse to children first. We're iterating in reverse order because iteration may change Children list.
			for (int32 NodeIndex = Node->Children.Num() - 1; NodeIndex >= 0; NodeIndex--)
			{
				SimplifyHierarchyRecursive(Node->Children[NodeIndex]);
			}

			// Now check if we can remove this node
			if (!Node->bShouldKeepThisNode && !Node->Mesh.IsValid() && Node->LocalTransform.Equals(FTransform::Identity, 0.001f))
			{
				auto Parent = Node->Parent.Pin();
				if (Parent.IsValid())
				{
					// Recurse to children first. We're iterating in reverse order because iteration may change Children list.
					for (int32 NodeIndex = Node->Children.Num() - 1; NodeIndex >= 0; NodeIndex--)
					{
						auto Child = Node->Children[NodeIndex];
						Node->Children.RemoveAt(NodeIndex);
						Parent->Children.Add(Child);
						Child->Parent = Parent;
					}
					Node->RemoveNode();
				}
			}
		}
	};

	FNodeHierarchySimplifier().SimplifyHierarchyRecursive(Scene->RootNode);
}


void FDatasmithFBXSceneProcessor::FixMeshNames()
{
	struct FFixHelper
	{
		void Do(FDatasmithFBXScene* InScene)
		{
			Recurse(InScene->RootNode);
		}

	protected:

		void Recurse(TSharedPtr<FDatasmithFBXSceneNode>& Node)
		{
			if (Node->Mesh.IsValid())
			{
				FString MeshName = Node->Mesh->Name;
				FText Error;
				if (!FFileHelper::IsFilenameValidForSaving(FPaths::GetBaseFilename(MeshName), Error))
				{
					FString MeshNameFixed = MeshName + "_Fixed";
					UE_LOG(LogDatasmithFBXImport, Warning, TEXT("Mesh name \"%s\" is invalid, renaming to \"%s\", error: %s"), *MeshName, *MeshNameFixed, *Error.ToString());
					Node->Mesh->Name = MeshNameFixed;
				}
			}

			for (int32 NodeIndex = 0; NodeIndex < Node->Children.Num(); NodeIndex++)
			{
				TSharedPtr<FDatasmithFBXSceneNode>& Child = Node->Children[NodeIndex];

				Recurse(Child);
			}
		}
	};

	FFixHelper Helper;
	Helper.Do(Scene);
}

