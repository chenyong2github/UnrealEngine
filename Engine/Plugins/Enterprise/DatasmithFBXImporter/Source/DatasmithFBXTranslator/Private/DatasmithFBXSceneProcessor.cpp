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

struct FNodeMergeHelper
{
	void MergeScene(FDatasmithFBXScene* Scene)
	{
		//todo: output some stats
		// Merge all root's children which are marked for merging
		RecursiveMergeScene(Scene->RootNode);
		// Merge all root's children which aren't marked (this works like if we had RootNode also marked)
		MergeNodesWithParent(Scene->RootNode);
	}

protected:
	/** Traverse the scene tree and execute merging for every marked node */
	void RecursiveMergeScene(TSharedPtr<FDatasmithFBXSceneNode>& Node)
	{
		for (int32 NodeIndex = 0; NodeIndex < Node->Children.Num(); NodeIndex++)
		{
			TSharedPtr<FDatasmithFBXSceneNode>& Child = Node->Children[NodeIndex];

			// Execute merging operation for every marked node.
			if (Child->bShouldKeepThisNode)
			{
				MergeNodesWithParent(Child);
			}

			// Go down to children even if we just merged all its children together, because
			// it is very likely that part of its hierarchy was not processed because it was
			// marked as separate geometry
			RecursiveMergeScene(Child);
		}
	}

	/** Merge all children of particular node into a single geometry */
	void MergeNodesWithParent(TSharedPtr<FDatasmithFBXSceneNode>& InParentNode)
	{
		TArray<FNodeInfo> AllMatchingNodeInfos;
		// Look for nodes to merge
		RecursiveFindNodesToMerge(InParentNode, AllMatchingNodeInfos);
		// Sort nodes by hash to improve correlation between different scene parts
		AllMatchingNodeInfos.Sort();

		if (AllMatchingNodeInfos.Num() >= 2)
		{
			NodeInfos.Empty();
			int32 NodeInfosTotalVertexCount = 0;
			int32 NodeInfosTotalTriangleCount = 0;

			for (auto& NodeIfo: AllMatchingNodeInfos)
			{
				TSharedPtr<FDatasmithFBXSceneNode>& Node = NodeIfo.Node;
				TSharedPtr<FDatasmithFBXSceneMesh>& Mesh = Node->Mesh;

				int32 VertexCount = Mesh->MeshDescription.Vertices().Num();
				int32 TriangleCount = DatasmithMeshHelper::GetTriangleCount(Mesh->MeshDescription);

				// don't make too huge meshes(e.g. overflowing)
				if (NodeInfosTotalVertexCount + VertexCount > MAX_MERGE_VERTEX_COUNT
					|| NodeInfosTotalTriangleCount + TriangleCount > MAX_MERGE_TRIANGLE_COUNT)
				{
					if (NodeInfos.Num() > 1)
					{
						MergeMeshNodes(InParentNode);
					}

					NodeInfos.Empty();
					NodeInfosTotalVertexCount = 0;
					NodeInfosTotalTriangleCount = 0;
				}

				NodeInfos.Add(NodeIfo);
				NodeInfosTotalVertexCount += VertexCount;
				NodeInfosTotalTriangleCount += TriangleCount;
			}

			if (NodeInfos.Num() > 1)
			{
				MergeMeshNodes(InParentNode);
			}
		}
	}

	struct FNodeInfo
	{
		TSharedPtr<FDatasmithFBXSceneNode> Node;
		FTransform Transform;

		FNodeInfo(TSharedPtr<FDatasmithFBXSceneNode>& InNode)
			: Node(InNode)
		{
		}

		const FMD5Hash& GetHash() const
		{
			if (!Hash.IsValid())
			{
				FMD5 Md5;
				// Hash mesh
				FDatasmithFBXHashUtils::UpdateHash(Md5, Node->Mesh->GetHash());
				// Hash materials
				for (int32 i = 0; i < Node->Materials.Num(); i++)
				{
					const TSharedPtr<FDatasmithFBXSceneMaterial>& Material = Node->Materials[i];
					FDatasmithFBXHashUtils::UpdateHash(Md5, Material->Name);
				}
				// Hash transform - use node's local transform for better correlation
				FDatasmithFBXHashUtils::UpdateHash(Md5, Node->LocalTransform);
				// Finalize hash computation
				Hash.Set(Md5);
			}
			return Hash;
		}

		// Helper function for sorting
		bool operator<(const FNodeInfo& Other) const
		{
			return GetHash() < Other.GetHash();
		}

	protected:
		mutable FMD5Hash Hash;
	};

	/** Find all nodes which should be merged together, stopping a 'marked' nodes */
	void RecursiveFindNodesToMerge(TSharedPtr<FDatasmithFBXSceneNode>& Node, TArray<FNodeInfo>& OutNodeInfos)
	{
		if (Node->Mesh.IsValid())
		{
			// This node has a mesh, remember it
			OutNodeInfos.Add(FNodeInfo(Node));
		}

		for (int32 NodeIndex = 0; NodeIndex < Node->Children.Num(); NodeIndex++)
		{
			TSharedPtr<FDatasmithFBXSceneNode>& Child = Node->Children[NodeIndex];

			// Skip marked nodes, they should be merged separately
			if (!Child->bShouldKeepThisNode)
			{
				RecursiveFindNodesToMerge(Child, OutNodeInfos);
			}
		}
	}

	/** Analyze usefulness of transform when selecting a node for being pivot. Smaller values
	 *  indicates a better transform. "Ideal" transform has weight 0.
	 */
	static float GetTransformWeight(const FTransform& Transform)
	{
		/** Warning: This function needs to be resilient against floating point and conversion errors in
			the transform values. This because we use it's return value to pick the pivot transform for reused merged
			meshes.

			If the return value varies too much with small changes (e.g. 1 degree of rotation), it might
			cause us to pick different transforms for different instances of the reused mesh, leading to errors, given
			that we bake the pivot transform into the vertex positions of the merged mesh
			(see MergeMeshes, search for ".Transform")
		*/

		// Best transform has identity scale. Using a large scale for pivot will force other meshes to
		// be downscaled. The same applies to small scale, i.e. large and small scales are equally bad.
		float Determinant = FMath::Abs(Transform.GetDeterminant());
		if (Determinant < 1.0f)
		{
			// Small scale
			Determinant = 1.0f / Determinant;
		}
		// Now Determinant is >= 1.0f. Make identity scale returning zero value.
		float ScaleWeight = FMath::Pow(Determinant, 1.0f/3.0f) - 1.0f;

		// Measure scale uniformness
		FVector Scale = Transform.GetScale3D().GetAbs();
		float Uniformness = ( FMath::Abs(Scale.X - Scale.Y) + FMath::Abs(Scale.X - Scale.Z) + FMath::Abs(Scale.Y - Scale.Z) + 0.1f ) / (Scale.X + Scale.Y + Scale.Z + 0.1f);
		ScaleWeight += Uniformness * 10;

		// Now analyze rotation. Best transforms will have rotation which are multiple of 90 degrees.
		FRotator Rotation = Transform.GetRotation().Rotator().GetDenormalized();
		float RotValues[3];
		RotValues[0] = Rotation.Yaw;
		RotValues[1] = Rotation.Pitch;
		RotValues[2] = Rotation.Roll;
		float RotationWeight = 0.0f;
		for (float Value : RotValues)
		{
			// This value is in range 0..360 now due to GetDenormalized call above
			Value = FMath::Fmod(Value, 90.0f);
			float AxisWeight = (Value > 45.0f) ? 90.0f - Value : Value;
			RotationWeight += AxisWeight / 45.0f;

			// Huge tolerance of 1 degree since it's common for rotations to go through several conversions
			// and end up with huge compounded errors
			if (!FMath::IsNearlyZero(Value, 1))
			{
				// Non-zero rotation is worse than zero
				RotationWeight += 10;
			}
		}

		return ScaleWeight + RotationWeight;
	}

	/** Merge all found mesh nodes into a single mesh node */
	void MergeMeshNodes(TSharedPtr<FDatasmithFBXSceneNode>& InParentNode)
	{
		// Compute parent-related transforms for each node
		for (FNodeInfo& NodeInfo: NodeInfos)
		{
			NodeInfo.Transform = NodeInfo.Node->GetTransformRelativeToParent(InParentNode);
		}

		// Use one of mesh nodes as "pivot" for node group. All other nodes will be repositioned as
		// children of that node. This allows us to avoid any differences in parent node transforms, for
		// example when we have the 2 instances of the same node group with and without mirror transform
		// in hierarchy - such groups still will be considered as identical.
		FTransform PivotTransform;
		float BestNodeWeight = 0.0f;
		for (int32 Index = 0; Index < NodeInfos.Num(); Index++)
		{
			const FTransform& Transform = NodeInfos[Index].Transform;
			float Weight = GetTransformWeight(Transform);
			if (Index == 0 || Weight < BestNodeWeight)
			{
				PivotTransform = Transform;
				BestNodeWeight = Weight;
			}
		}
		// Reposition nodes
		FTransform InversePivotTransform = PivotTransform.Inverse();
		for (FNodeInfo& NodeInfo : NodeInfos)
		{
			NodeInfo.Transform = NodeInfo.Transform * InversePivotTransform;
		}

		// Compute hash of the node set
		FMD5 Md5;
		for (FNodeInfo& NodeInfo : NodeInfos)
		{
			FDatasmithFBXHashUtils::UpdateHash(Md5, NodeInfo.GetHash());
		}
		FMD5Hash NodeSetHash;
		NodeSetHash.Set(Md5);

		FString NewNodeName = FString::Printf(TEXT("%s") MERGED_SUFFIX, *InParentNode->Name);

//		UE_LOG(LogDatasmithFBXImport, Log, TEXT("Merging %d meshes into node \"%s\", hash = %s"), NodeInfos.Num(), *NewNodeName, *Lex::ToString(NodeSetHash));

		// Make a node for the new mesh
		TSharedPtr<FDatasmithFBXSceneNode> NewNode(new FDatasmithFBXSceneNode());
		NewNode->Name = NewNodeName;
		NewNode->Parent = InParentNode;
		NewNode->LocalTransform = PivotTransform;
		NewNode->OriginalName = NewNode->Name; // So that it becomes a Tag later
		InParentNode->Children.Add(NewNode);

		// Find if we already have identical set of nodes merged
		const TSharedPtr<FDatasmithFBXSceneNode>* PreviousMergedNode = MergedNodes.Find(NodeSetHash);
		if (PreviousMergedNode == nullptr)
		{
			// Make an empty mesh
			TSharedPtr<FDatasmithFBXSceneMesh> NewMesh(new FDatasmithFBXSceneMesh());
			NewMesh->Name = NewNodeName;
			NewNode->Mesh = NewMesh;

			// Merge geometries
			MergeMeshes(NewNode);

			// Remember node and mesh for later reuse when needed
			MergedNodes.Add(NodeSetHash, NewNode);
		}
		else
		{
//			UE_LOG(LogDatasmithFBXImport, Log, TEXT("... reusing mesh"));
			// Do not merge if the same mesh set already exists
			NewNode->Mesh = (*PreviousMergedNode)->Mesh;
			// Copy materials too
			NewNode->Materials = (*PreviousMergedNode)->Materials;
		}

		// Release merged meshes and materials
		for (FNodeInfo& NodeInfo : NodeInfos)
		{
			TSharedPtr<FDatasmithFBXSceneNode>& Node = NodeInfo.Node;
			Node->Mesh.Reset();
			Node->Materials.Empty();
		}
	}

	void MergeMeshes(TSharedPtr<FDatasmithFBXSceneNode>& MergedNode)
	{
		if (!ensure(MergedNode && MergedNode->Mesh))
		{
			return;
		}

		int32 MergedVertexCount = 0;
		int32 MergedVertexInstanceCount = 0;
		int32 MergedEdgeCount = 0;
		int32 MergedPolygonCount = 0;

		TArray< TSharedPtr<FDatasmithFBXSceneMaterial> >& TargetMaterials = MergedNode->Materials;
		for (int32 MeshIndex = 0; MeshIndex < NodeInfos.Num(); MeshIndex++)
		{
			TSharedPtr<FDatasmithFBXSceneNode>& Node = NodeInfos[MeshIndex].Node;
			if (!ensure(Node && Node->Mesh))
			{
				return;
			}
			FMeshDescription& SourceMeshDescription = Node->Mesh->MeshDescription;

			MergedVertexCount += SourceMeshDescription.Vertices().Num();
			MergedVertexInstanceCount += SourceMeshDescription.VertexInstances().Num();
			MergedEdgeCount += SourceMeshDescription.Edges().Num();
			MergedPolygonCount += SourceMeshDescription.Polygons().Num();

			// Merge materials
			for (int32 Index = 0; Index < Node->Materials.Num(); Index++)
			{
				TargetMaterials.AddUnique(Node->Materials[Index]);
			}
		}

		// Prepare destination mesh
		TSharedPtr<FDatasmithFBXSceneMesh> MergedMesh = MergedNode->Mesh;
		FMeshDescription& MergedMeshDescription = MergedMesh->MeshDescription;
		DatasmithMeshHelper::PrepareAttributeForStaticMesh(MergedMeshDescription);
		MergedMeshDescription.ReserveNewPolygonGroups(TargetMaterials.Num());
		MergedMeshDescription.ReserveNewVertexInstances(MergedVertexCount);
		MergedMeshDescription.ReserveNewEdges(MergedVertexInstanceCount);
		MergedMeshDescription.ReserveNewVertices(MergedEdgeCount);
		MergedMeshDescription.ReserveNewPolygons(MergedPolygonCount);

		TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = MergedMeshDescription.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

		// Now merge meshes
		for (int32 MeshIndex = 0; MeshIndex < NodeInfos.Num(); MeshIndex++)
		{
			FNodeInfo& NodeInfo = NodeInfos[MeshIndex];
			TSharedPtr<FDatasmithFBXSceneNode>& Node = NodeInfo.Node;
			FMeshDescription& SourceMeshDescription = Node->Mesh->MeshDescription;

			// Remap from material indices since we might or might not add new materials to target
			// given that we don't allow repeated entries
			// Should we? If we don't the user can't individually reassign materials to segments of the mesh that
			// were originally distinct...
			const TArray<TSharedPtr<FDatasmithFBXSceneMaterial>>& SourceMaterials = Node->Materials;
			TArray<int32> SourceToTargetMatID;
			if (SourceMaterials.Num() > 0)
			{
				for (int32 SourceIndex = 0; SourceIndex < SourceMaterials.Num(); SourceIndex++)
				{
					int32 TargetIndex = TargetMaterials.Find(SourceMaterials[SourceIndex]);
					check(TargetIndex >= 0);
					SourceToTargetMatID.Add(TargetIndex);  // SourceToTargetMatID[SourceIndex] -> TargetIndex
				}
			}
			else
			{
				SourceToTargetMatID.Add(0);
			}

			// Expand target mesh polygon groups to fit as many different groups as we need to support our new source mesh
			while (MergedMeshDescription.PolygonGroups().Num() - 1 < FMath::Max(SourceToTargetMatID))
			{
				FPolygonGroupID NewGroup = MergedMeshDescription.CreatePolygonGroup();
				PolygonGroupImportedMaterialSlotNames[NewGroup] = DatasmithMeshHelper::DefaultSlotName(NewGroup.GetValue());
			}

			FMeshDescriptionOperations::FAppendSettings AppendSettings;

			bool bRecalculateNormals = false;
			const FTransform& Transform = NodeInfo.Transform;
			if (Transform.IsValid())
			{
				AppendSettings.MeshTransform = Transform;

				// Odd negative scales on the *node* transform don't affect winding order as the mesh normals/faces remain
				// pointing to the same direction (e.g. always outward). Exception to that is when we'll bake the scaling
				// in the vertex positions, which is just about to happen. In here, we flip triangle winding order
				// to compensate for that
				if (Transform.GetDeterminant() < 0.0f && !Node->Mesh->bFlippedFaces)
				{
					SourceMeshDescription.ReverseAllPolygonFacing();
					Node->Mesh->bFlippedFaces = true;

					bRecalculateNormals = true;
				}
				// We need to check whether the mesh has flipped faces or not, though.
				// Example: Two wheels (same mesh), one with negative scale. If the negative scale one is merged first,
				// the above if will flip the faces. When the non-negative scale wheel is merged, it will remain with
				// flipped faces (equal meshes are shared on import). This reverses that flip
				else if (Transform.GetDeterminant() > 0.0f && Node->Mesh->bFlippedFaces)
				{
					SourceMeshDescription.ReverseAllPolygonFacing();
					Node->Mesh->bFlippedFaces = false;

					bRecalculateNormals = true;
				}
			}

			// RecomputeNormalsAndTangentsIfNeeded expects the normals, tangents and binormals arrays to be allocated,
			// so we need to make sure those exist, or else we will have no opportunity to fix our normals and tangents before
			// merging
			TPolygonAttributesConstRef<FVector> PolygonNormals = SourceMeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Normal);
			TPolygonAttributesConstRef<FVector> PolygonTangents = SourceMeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Tangent);
			TPolygonAttributesConstRef<FVector> PolygonBinormals = SourceMeshDescription.PolygonAttributes().GetAttributesRef<FVector>(MeshAttribute::Polygon::Binormal);
			if (!PolygonNormals.IsValid())
			{
				SourceMeshDescription.PolygonAttributes().RegisterAttribute<FVector>(MeshAttribute::Polygon::Normal, 1, FVector::ZeroVector);
			}
			if (!PolygonTangents.IsValid())
			{
				SourceMeshDescription.PolygonAttributes().RegisterAttribute<FVector>(MeshAttribute::Polygon::Tangent, 1, FVector::ZeroVector);
			}
			if (!PolygonBinormals.IsValid())
			{
				SourceMeshDescription.PolygonAttributes().RegisterAttribute<FVector>(MeshAttribute::Polygon::Binormal, 1, FVector::ZeroVector);
			}

			//Use MikktSpace
			uint32 TangentOptions = FMeshDescriptionOperations::ETangentOptions::UseMikkTSpace;
			//Use weighted normals
			//TangentOptions |= FMeshDescriptionOperations::ETangentOptions::UseWeightedAreaAndAngle;
			FMeshDescriptionOperations::RecomputeNormalsAndTangentsIfNeeded(SourceMeshDescription, (FMeshDescriptionOperations::ETangentOptions)TangentOptions, bRecalculateNormals, bRecalculateNormals);

			// The default merge process (FMeshDescriptionOperations::AppendMeshDescription) will merge polygon groups if
			// the slot names are the same. So if mesh A has just material slots 0 and 1 with default names, and mesh B has just slot 0,
			// also with a default name, we will get a mesh C with slots 0 and 1 with default names.
			// Assuming the materials are all discrete however, we would want instead to keep the polygon groups separate,
			// so that we can have discrete slots 0, 1 and 2
			AppendSettings.PolygonGroupsDelegate.BindLambda([&SourceToTargetMatID](const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, PolygonGroupMap& RemapPolygonGroup)
			{
				for (FPolygonGroupID SourcePolygonGroupID : SourceMesh.PolygonGroups().GetElementIDs())
				{
					RemapPolygonGroup.Add(SourcePolygonGroupID, FPolygonGroupID(SourceToTargetMatID[SourcePolygonGroupID.GetValue()]));
				}
			});

			FMeshDescriptionOperations::AppendMeshDescription(SourceMeshDescription, MergedMeshDescription, AppendSettings);
		}
	}

	/** List of nodes which has meshes and which should be merged together */
	TArray<FNodeInfo> NodeInfos;

	/** Map between hash of node set and combined node, to reuse previously combined meshes if node sets matches */
	TMap< FMD5Hash, TSharedPtr<FDatasmithFBXSceneNode> > MergedNodes;
};

void FDatasmithFBXSceneProcessor::MergeSceneNodes()
{
	FNodeMergeHelper Helper;
	Helper.MergeScene(Scene);
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

void FDatasmithFBXSceneProcessor::DecomposeRotationPivotsForNode(TSharedPtr<FDatasmithFBXSceneNode> Node, TMap<FString, FDatasmithFBXSceneAnimNode*>& NodeNameToAnimNode, TArray<FDatasmithFBXSceneAnimNode>& NewAnimNodes)
{
	if (Node->RotationPivot.IsNearlyZero())
	{
		return;
	}

	FVector RotPivot = Node->RotationPivot;
	FVector NodeLocation = Node->LocalTransform.GetTranslation();
	FQuat NodeRotation = Node->LocalTransform.GetRotation();

	Node->RotationPivot.Set(0.0f, 0.0f, 0.0f);
	Node->LocalTransform.SetTranslation(-RotPivot);
	Node->LocalTransform.SetRotation(FQuat::Identity);

	TSharedPtr<FDatasmithFBXSceneNode> Dummy = MakeShared<FDatasmithFBXSceneNode>();
	Dummy->Name = Node->Name + TEXT("_RotationPivot");
	Dummy->OriginalName = Dummy->Name;
	Dummy->SplitNodeID = Node->SplitNodeID;
	Dummy->LocalTransform.SetTranslation(NodeLocation + RotPivot);
	Dummy->LocalTransform.SetRotation(NodeRotation);

	// Move any rotation curves to Dummy
	if (FDatasmithFBXSceneAnimNode** FoundNode = NodeNameToAnimNode.Find(Node->OriginalName))
	{
		FDatasmithFBXSceneAnimNode* NewAnimNode = nullptr;

		for (FDatasmithFBXSceneAnimBlock& Block : (*FoundNode)->Blocks)
		{
			TArray<FDatasmithFBXSceneAnimCurve> RotCurves;
			TArray<FDatasmithFBXSceneAnimCurve> TransCurves;

			// Collect all rotation curves frm the original animnode
			for (int32 CurveIndex = Block.Curves.Num() - 1; CurveIndex >= 0; --CurveIndex)
			{
				FDatasmithFBXSceneAnimCurve& ThisCurve = Block.Curves[CurveIndex];

				if (ThisCurve.Type == EDatasmithFBXSceneAnimationCurveType::Rotation)
				{
					RotCurves.Add(ThisCurve);
					Block.Curves.RemoveAt(CurveIndex);
				}
				else if (ThisCurve.Type == EDatasmithFBXSceneAnimationCurveType::Translation)
				{
					for (FDatasmithFBXSceneAnimPoint& Pt : ThisCurve.Points)
					{
						Pt.Value += (RotPivot[(uint8)ThisCurve.Component] + Node->RotationOffset[(uint8)ThisCurve.Component] + Node->ScalingOffset[(uint8)ThisCurve.Component]);
					}
					TransCurves.Add(ThisCurve);
					Block.Curves.RemoveAt(CurveIndex);
				}
			}

			// Move curves to a new block on the new animnode
			if (RotCurves.Num() > 0 || TransCurves.Num() > 0)
			{
				if (NewAnimNode == nullptr)
				{
					NewAnimNode = new(NewAnimNodes) FDatasmithFBXSceneAnimNode;
					NewAnimNode->Name = Dummy->Name;
					Dummy->MarkMovableNode();
				}

				FDatasmithFBXSceneAnimBlock* NewBlock = new(NewAnimNode->Blocks) FDatasmithFBXSceneAnimBlock;
				NewBlock->Name = Block.Name;
				NewBlock->Curves = MoveTemp(RotCurves);
				NewBlock->Curves.Append(TransCurves);
			}
		}
	}

	// Fix hierarchy (place Dummy between Node and Parent)
	TSharedPtr<FDatasmithFBXSceneNode> NodeParent = Node->Parent.Pin();
	Dummy->AddChild(Node);
	if (NodeParent.IsValid())
	{
		NodeParent->Children.Remove(Node);
		NodeParent->AddChild(Dummy);
	}
}

void FDatasmithFBXSceneProcessor::DecomposeRotationPivots()
{
	TMap<FString, FDatasmithFBXSceneAnimNode*> NodeNameToAnimNode;
	for (FDatasmithFBXSceneAnimNode& AnimNode : Scene->AnimNodes)
	{
		NodeNameToAnimNode.Add(AnimNode.Name, &AnimNode);
	}

	TArray<FDatasmithFBXSceneAnimNode> NewNodes;

	for (TSharedPtr<FDatasmithFBXSceneNode> Node : Scene->GetAllNodes())
	{
		DecomposeRotationPivotsForNode(Node, NodeNameToAnimNode, NewNodes);
	}

	Scene->AnimNodes.Append(NewNodes);
}

void FDatasmithFBXSceneProcessor::DecomposeScalingPivotsForNode(TSharedPtr<FDatasmithFBXSceneNode> Node, TMap<FString, FDatasmithFBXSceneAnimNode*>& NodeNameToAnimNode, TArray<FDatasmithFBXSceneAnimNode>& NewAnimNodes)
{
	if (Node->ScalingPivot.IsNearlyZero())
	{
		return;
	}

	FVector ScalingPivot = Node->ScalingPivot;
	FVector NodeLocation = Node->LocalTransform.GetTranslation();
	FVector NodeScaling = Node->LocalTransform.GetScale3D();

	Node->ScalingPivot.Set(0.0f, 0.0f, 0.0f);
	Node->LocalTransform.SetTranslation(-ScalingPivot);
	Node->LocalTransform.SetScale3D(FVector::OneVector);

	TSharedPtr<FDatasmithFBXSceneNode> Dummy = MakeShared<FDatasmithFBXSceneNode>();
	Dummy->Name = Node->Name + TEXT("_ScalingPivot");
	Dummy->OriginalName = Dummy->Name;
	Dummy->SplitNodeID = Node->SplitNodeID;
	Dummy->LocalTransform.SetTranslation(NodeLocation + ScalingPivot);
	Dummy->LocalTransform.SetScale3D(NodeScaling);

	// Move any rotation curves to Dummy
	if (FDatasmithFBXSceneAnimNode** FoundNode = NodeNameToAnimNode.Find(Node->OriginalName))
	{
		FDatasmithFBXSceneAnimNode* NewAnimNode = nullptr;

		for (FDatasmithFBXSceneAnimBlock& Block : (*FoundNode)->Blocks)
		{
			TArray<FDatasmithFBXSceneAnimCurve> ScaleCurves;
			TArray<FDatasmithFBXSceneAnimCurve> TransCurves;

			// Collect all rotation curves frm the original animnode
			for (int32 CurveIndex = Block.Curves.Num() - 1; CurveIndex >= 0; --CurveIndex)
			{
				FDatasmithFBXSceneAnimCurve& ThisCurve = Block.Curves[CurveIndex];

				if (ThisCurve.Type == EDatasmithFBXSceneAnimationCurveType::Scale)
				{
					ScaleCurves.Add(ThisCurve);
					Block.Curves.RemoveAt(CurveIndex);
				}
				else if (ThisCurve.Type == EDatasmithFBXSceneAnimationCurveType::Translation)
				{
					for (FDatasmithFBXSceneAnimPoint& Pt : ThisCurve.Points)
					{
						Pt.Value += (ScalingPivot[(uint8)ThisCurve.Component] + Node->RotationOffset[(uint8)ThisCurve.Component] + Node->ScalingOffset[(uint8)ThisCurve.Component]);
					}
					TransCurves.Add(ThisCurve);
					Block.Curves.RemoveAt(CurveIndex);
				}
			}

			// Move curves to a new block on the new animnode
			if (ScaleCurves.Num() > 0 || TransCurves.Num() > 0)
			{
				if (NewAnimNode == nullptr)
				{
					NewAnimNode = new(NewAnimNodes) FDatasmithFBXSceneAnimNode;
					NewAnimNode->Name = Dummy->Name;
					Dummy->MarkMovableNode();
				}

				FDatasmithFBXSceneAnimBlock* NewBlock = new(NewAnimNode->Blocks) FDatasmithFBXSceneAnimBlock;
				NewBlock->Name = Block.Name;
				NewBlock->Curves = MoveTemp(ScaleCurves);
				NewBlock->Curves.Append(TransCurves);
			}
		}
	}

	// Fix hierarchy (place Dummy between Node and Parent)
	TSharedPtr<FDatasmithFBXSceneNode> NodeParent = Node->Parent.Pin();
	Dummy->AddChild(Node);
	if (NodeParent.IsValid())
	{
		NodeParent->Children.Remove(Node);
		NodeParent->AddChild(Dummy);
	}
}

void FDatasmithFBXSceneProcessor::DecomposeScalingPivots()
{
	TMap<FString, FDatasmithFBXSceneAnimNode*> NodeNameToAnimNode;
	for (FDatasmithFBXSceneAnimNode& AnimNode : Scene->AnimNodes)
	{
		NodeNameToAnimNode.Add(AnimNode.Name, &AnimNode);
	}

	TArray<FDatasmithFBXSceneAnimNode> NewNodes;

	for (TSharedPtr<FDatasmithFBXSceneNode> Node : Scene->GetAllNodes())
	{
		DecomposeScalingPivotsForNode(Node, NodeNameToAnimNode, NewNodes);
	}

	Scene->AnimNodes.Append(NewNodes);
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

