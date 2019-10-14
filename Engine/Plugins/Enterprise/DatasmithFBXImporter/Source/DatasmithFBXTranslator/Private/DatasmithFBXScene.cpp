// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithFBXScene.h"

#include "DatasmithFBXHashUtils.h"
#include "DatasmithFBXImporterLog.h"
#include "DatasmithMeshHelper.h"

#include "StaticMeshAttributes.h"
#include "Misc/EnumClassFlags.h"

int32 FDatasmithFBXSceneNode::NodeCounter = 1;

FDatasmithFBXSceneMaterial::FDatasmithFBXSceneMaterial()
{
}

FDatasmithFBXSceneMesh::FDatasmithFBXSceneMesh()
	: ImportMaterialCount(0)
	, bFlippedFaces(false)
{
}

FDatasmithFBXSceneMesh::~FDatasmithFBXSceneMesh()
{
}

const FMD5Hash& FDatasmithFBXSceneMesh::GetHash()
{
	if (!Hash.IsValid())
	{
		FMD5 Md5;
		DatasmithMeshHelper::HashMeshDescription(MeshDescription, Md5);
		Hash.Set(Md5);
	}
	return Hash;
}

bool FDatasmithFBXSceneMesh::HasNormals() const
{
	const TVertexInstanceAttributesConstRef<FVector> Normals = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Normal);
	return Normals.GetNumElements() > 0 && Normals[MeshDescription.VertexInstances().GetFirstValidID()].SizeSquared() > 0;
}

bool FDatasmithFBXSceneMesh::HasTangents() const
{
	const TVertexInstanceAttributesConstRef<FVector> Tangents = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector>(MeshAttribute::VertexInstance::Tangent);
	return Tangents.GetNumElements() > 0 && Tangents[MeshDescription.VertexInstances().GetFirstValidID()].SizeSquared() > 0;
}

FDatasmithFBXSceneCamera::FDatasmithFBXSceneCamera()
{
}

FDatasmithFBXSceneCamera::~FDatasmithFBXSceneCamera()
{
}

FDatasmithFBXSceneNode::FDatasmithFBXSceneNode()
	: Name(FString())
	, SplitNodeID(NodeCounter++)
	, Visibility(1.0f)
	, bVisibilityInheritance(false)
	, OriginalName(FString())
	, LocalTransform(FTransform::Identity)
	, RotationPivot(0.0f, 0.0f, 0.0f)
	, ScalingPivot(0.0f, 0.0f, 0.0f)
	, RotationOffset(0.0f, 0.0f, 0.0f)
	, ScalingOffset(0.0f, 0.0f, 0.0f)
	, bShouldKeepThisNode(false)
	, NodeType(ENodeType::Node)
{
}

FDatasmithFBXSceneNode::~FDatasmithFBXSceneNode()
{
	if (SharedContent.IsValid())
	{
		// When removing a node which is one of SharedContent owners, remove it from content's parents
		for (int32 i = SharedContent->SharedParent.Num() - 1; i >= 0; i--)
		{
			TWeakPtr<FDatasmithFBXSceneNode>& SharedContentParent = SharedContent->SharedParent[i];
			FDatasmithFBXSceneNode* CheckedNode = SharedContentParent.Pin().Get();
			// Current node is being removed, so all TSharedPtr to it will be null.
			if (CheckedNode == this || CheckedNode == nullptr)
			{
				SharedContent->SharedParent.RemoveAt(i);
			}
		}
		if (Children.Num())
		{
			// This node owns SharedContent - now we should reattach it to a different shared parent to keep content alive
			check(Children.Num() == 1);
			//check(Children[0] == SharedContent);
			if (this == SharedContent->Parent.Pin().Get())
			{
				if (SharedContent->SharedParent.Num())
				{
					// SharedContent has another valid parent
					TSharedPtr<FDatasmithFBXSceneNode> NewParent = SharedContent->SharedParent[0].Pin();
					check(NewParent.IsValid());
					check(NewParent->Children.Num() == 0);
					NewParent->Children.Add(SharedContent);
					SharedContent->Parent = NewParent;
				}
				// else - SharedContent is not used anywhere else, it will be automatically destroyed
			}
		}
	}
}

FTransform FDatasmithFBXSceneNode::GetTransformRelativeToParent(TSharedPtr<FDatasmithFBXSceneNode>& InParent) const
{
	if (InParent.Get() == this)
	{
		return FTransform();
	}

	FTransform Transform = LocalTransform;
	for (TSharedPtr<FDatasmithFBXSceneNode> Node = Parent.Pin(); Node.IsValid() && Node != InParent; Node = Node->Parent.Pin())
	{
		FTransform B;
		FTransform::Multiply(&B, &Transform, &Node->LocalTransform);
		Transform = B;
	}
	return Transform;
}

FTransform FDatasmithFBXSceneNode::GetWorldTransform() const
{
	FTransform Transform = LocalTransform;
	for (TSharedPtr<FDatasmithFBXSceneNode> Node = Parent.Pin(); Node.IsValid(); Node = Node->Parent.Pin())
	{
		FTransform B;
		FTransform::Multiply(&B, &Transform, &Node->LocalTransform);
		Transform = B;
	}
	return Transform;
}

void FDatasmithFBXSceneNode::RemoveNode()
{
	check(Children.Num() == 0);

	// Unlink this node from its parent. This should initiate node destruction because it is holded
	// by TSharedPtr.

	TSharedPtr<FDatasmithFBXSceneNode> ParentNode = Parent.Pin();
	check(ParentNode.IsValid());

	for (int32 ChildIndex = 0; ChildIndex < ParentNode->Children.Num(); ChildIndex++)
	{
		if (ParentNode->Children[ChildIndex].Get() == this)
		{
			ParentNode->Children.RemoveAt(ChildIndex);
			return;
		}
	}

	// Should not get here
	UE_LOG(LogDatasmithFBXImport, Fatal, TEXT("Unexpected behavior"));
}

int32 FDatasmithFBXSceneNode::GetChildrenCountRecursive(bool bIncludeSharedContent) const
{
	if (EnumHasAnyFlags(NodeType, ENodeType::SharedNode) && !bIncludeSharedContent)
	{
		// Do not recurse into shared node children, count only self.
		return 1;
	}
	int32 Result = Children.Num();
	for (const TSharedPtr<FDatasmithFBXSceneNode>& Child : Children)
	{
		Result += Child->GetChildrenCountRecursive(bIncludeSharedContent);
	}
	return Result;
}

void FDatasmithFBXSceneNode::MarkLightNode()
{
	NodeType |= ENodeType::Movable;  // Force creation of a BP_SceneNode
	bShouldKeepThisNode = true;
}

void FDatasmithFBXSceneNode::MarkCameraNode()
{
	NodeType |= ENodeType::Movable;  // Force creation of a BP_SceneNode
	bShouldKeepThisNode = true;
}

void FDatasmithFBXSceneNode::MarkSwitchNode()
{
	NodeType |= ENodeType::Switch;
	bShouldKeepThisNode = true;

	// For switch, we should also keep all its children persistent because they are representing variants
	for (int32 NodeIndex = 0; NodeIndex < Children.Num(); NodeIndex++)
	{
		Children[NodeIndex]->bShouldKeepThisNode = true;
	}
}

void FDatasmithFBXSceneNode::MarkAnimatedNode()
{
	NodeType |= ENodeType::Animated | ENodeType::Movable;
	bShouldKeepThisNode = true;
}

void FDatasmithFBXSceneNode::MarkSwitchMaterialNode()
{
	NodeType |= ENodeType::Material;
	bShouldKeepThisNode = true;
}

void FDatasmithFBXSceneNode::MarkMovableNode()
{
	NodeType |= ENodeType::Movable;
	bShouldKeepThisNode = true;
}

void FDatasmithFBXSceneNode::MarkToggleNode()
{
	NodeType |= ENodeType::Toggle;
	bShouldKeepThisNode = true;
}

void FDatasmithFBXSceneNode::MarkSharedNode(TSharedPtr<FDatasmithFBXSceneNode> Content)
{
	NodeType |= ENodeType::SharedNode;
	bShouldKeepThisNode = true;

	SharedContent = Content;
	Content->SharedParent.Add(AsShared());
	SharedContent->bShouldKeepThisNode = true;
}

void FDatasmithFBXSceneNode::ResetNodeType()
{
	if (NodeType != ENodeType::Node)
	{
		check(Children.Num() == 0);
		check(!SharedContent.IsValid());
		NodeType = ENodeType::Node;
	}
}

const FMD5Hash& FDatasmithFBXSceneNode::GetHash()
{
	if (!Hash.IsValid())
	{
		FMD5 Md5;

		if (bShouldKeepThisNode)
		{
			// For special nodes (switches, their children, etc), we should hash node names
			FDatasmithFBXHashUtils::UpdateHash(Md5, OriginalName);
		}

		if (Mesh.IsValid())
		{
			// Hash for geometry
			const FMD5Hash& MeshHash = Mesh->GetHash();
			FDatasmithFBXHashUtils::UpdateHash(Md5, MeshHash);

			// Hash for materials
			FDatasmithFBXHashUtils::UpdateHash(Md5, Materials.Num());
			for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); MaterialIndex++)
			{
				FDatasmithFBXHashUtils::UpdateHash(Md5, Materials[MaterialIndex]->Name);
			}
		}

		if (NodeType != ENodeType::SharedNode)
		{
			// Hash children
			FDatasmithFBXHashUtils::UpdateHash(Md5, Children.Num());
			// Sort children by hash to make hash invariant to children order
			TArray< TSharedPtr<FDatasmithFBXSceneNode> > SortedChildren = Children;
			SortedChildren.StableSort([](const TSharedPtr<FDatasmithFBXSceneNode>& A, const TSharedPtr<FDatasmithFBXSceneNode>& B)
				{
					return A->GetHash() < B->GetHash();
				});
			for (int32 ChildIndex = 0; ChildIndex < SortedChildren.Num(); ChildIndex++)
			{
				TSharedPtr<FDatasmithFBXSceneNode>& Node = SortedChildren[ChildIndex];
				// Use child hash
				FDatasmithFBXHashUtils::UpdateHash(Md5, Node->GetHash());
				// and its local transform related to this node
				FDatasmithFBXHashUtils::UpdateHash(Md5, Node->LocalTransform);
			}
		}
		else
		{
			FDatasmithFBXHashUtils::UpdateHash(Md5, int32(0));
			// For shared node should hash its content instead of Children
			TSharedPtr<FDatasmithFBXSceneNode>& Node = SharedContent;
			// Use child hash
			FDatasmithFBXHashUtils::UpdateHash(Md5, Node->GetHash());
			// and its local transform related to this node
			FDatasmithFBXHashUtils::UpdateHash(Md5, Node->LocalTransform);
		}

		Hash.Set(Md5);
	}
	return Hash;
}

FDatasmithFBXScene::FDatasmithFBXScene()
	: TagTime(FLT_MAX)
	, ScaleFactor(1.0f)
{
}

FDatasmithFBXScene::~FDatasmithFBXScene()
{
}

void FDatasmithFBXScene::CollectAllObjects(MeshUseCountType* Meshes, MaterialUseCountType* InMaterials)
{
	RecursiveCollectAllObjects(Meshes, InMaterials, nullptr, RootNode);
}

TArray<TSharedPtr<FDatasmithFBXSceneNode>> FDatasmithFBXScene::GetAllNodes()
{
	TArray<TSharedPtr<FDatasmithFBXSceneNode>> Result;
	FDatasmithFBXSceneNode::Traverse(RootNode, [&Result](TSharedPtr<FDatasmithFBXSceneNode> Node)
	{
		Result.Add(Node);
	});
	return Result;
}

FDatasmithFBXScene::FStats FDatasmithFBXScene::GetStats()
{
	FStats Stats;

	MeshUseCountType CollectedMeshes;
	MaterialUseCountType CollectedMaterials;
	RecursiveCollectAllObjects(&CollectedMeshes, &CollectedMaterials, &Stats.NodeCount, RootNode);

	// Count all mesh instances in scene
	for (auto& MeshInfo : CollectedMeshes)
	{
		Stats.MeshCount += MeshInfo.Value;
	}

	Stats.GeometryCount = CollectedMeshes.Num();
	Stats.MaterialCount = CollectedMaterials.Num();
	return Stats;
}

void FDatasmithFBXScene::RecursiveCollectAllObjects(MeshUseCountType* Meshes, MaterialUseCountType* InMaterials, int32* NodeCount, const TSharedPtr<FDatasmithFBXSceneNode>& InNode) const
{
	FDatasmithFBXSceneNode::Traverse(InNode, [&](TSharedPtr<FDatasmithFBXSceneNode> Node)
	{
		if (NodeCount != nullptr)
		{
			// Count nodes
			(*NodeCount)++;
		}

		// Count meshes
		if (Node->Mesh.IsValid() && Meshes != nullptr)
		{
			int32* FoundMesh = Meshes->Find(Node->Mesh);
			if (FoundMesh == nullptr)
			{
				Meshes->Add(Node->Mesh, 1);
			}
			else
			{
				(*FoundMesh)++;
			}
		}

		// Count materials
		if (InMaterials != nullptr)
		{
			for (int32 MaterialIndex = 0; MaterialIndex < Node->Materials.Num(); MaterialIndex++)
			{
				TSharedPtr<FDatasmithFBXSceneMaterial>& Material = Node->Materials[MaterialIndex];
				int32* FoundMaterial = InMaterials->Find(Material);
				if (FoundMaterial == nullptr)
				{
					InMaterials->Add(Material, 1);
				}
				else
				{
					(*FoundMaterial)++;
				}
			}
		}
	});
}

static FArchive& operator << (FArchive& Ar, FDatasmithFBXSceneMaterial::FTextureParams& Data)
{
	Ar << Data.Path;
	Ar << Data.Translation;
	Ar << Data.Rotation;
	Ar << Data.Scale;
	return Ar;
}

static FArchive& operator << (FArchive& Ar, FDatasmithFBXSceneAnimPoint& Pt)
{
	Ar << Pt.InterpolationMode;
	Ar << Pt.TangentMode;
	Ar << Pt.Time;
	Ar << Pt.Value;
	Ar << Pt.ArriveTangent;
	Ar << Pt.LeaveTangent;
	return Ar;
}

static FArchive& operator << (FArchive& Ar, FDatasmithFBXSceneAnimCurve& Curve)
{
	Ar << Curve.DSID;
	Ar << Curve.Type;
	Ar << Curve.Component;
	Ar << Curve.Points;
	Ar << Curve.StartTimeSeconds;
	return Ar;
}

static FArchive& operator << (FArchive& Ar, FDatasmithFBXSceneAnimBlock& Block)
{
	Ar << Block.Name;
	Ar << Block.Curves;
	return Ar;
}

static FArchive& operator << (FArchive& Ar, FDatasmithFBXSceneAnimNode& Node)
{
	Ar << Node.Name;
	Ar << Node.Blocks;
	return Ar;
}

class FDatasmithFBXSceneSerializer
{
	FArchive& Ar;
	FDatasmithFBXScene& Scene;
	TArray<TSharedPtr<FDatasmithFBXSceneNode>> Nodes;
	TArray<TSharedPtr<FDatasmithFBXSceneMesh>> Meshes;
	TArray<TSharedPtr<FDatasmithFBXSceneMaterial>> Materials;

	enum { FormatVersion = 21 };

public:
	FDatasmithFBXSceneSerializer(FArchive& InAr, FDatasmithFBXScene& InDeltaGenScene)
		: Ar(InAr)
		, Scene(InDeltaGenScene)
	{
	}

	template<typename T>
	void SerializeArrayOfSmartPointer(TArray<TSharedPtr<T>>& ArrayToSerialize, const TArray<TSharedPtr<T>>& ArrayToIndex)
	{
		int32 MaterialCount;

		if (Ar.IsSaving())
		{
			MaterialCount = ArrayToSerialize.Num();
		}

		Ar << MaterialCount;
		if (Ar.IsSaving())
		{
			for (int MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				int32 Index = ArrayToIndex.Find(ArrayToSerialize[MaterialIndex]);
				Ar << Index;
			}
		}
		else
		{
			ArrayToSerialize.Reserve(MaterialCount);
			for (int MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				int32 Index;
				Ar << Index;
				ArrayToSerialize.Add((Index < 0) ? nullptr : ArrayToIndex[Index]);
			}
		}
	}

	void SerializeNode(FDatasmithFBXSceneNode& Node)
	{
		Ar << Node.Name;
		Ar << Node.OriginalName;
		Ar << Node.Visibility;
		Ar << Node.bVisibilityInheritance;
		Ar << Node.LocalTransform;
		Ar << Node.RotationPivot;
		Ar << Node.ScalingPivot;
		Ar << Node.RotationOffset;
		Ar << Node.ScalingOffset;

		// Parent
		int32 ParentIndex;
		if (Ar.IsSaving())
		{
			ParentIndex = Nodes.Find(Node.Parent.Pin());
		}
		Ar << ParentIndex;
		if (!Ar.IsSaving())
		{
			if (ParentIndex >= 0)
			{
				Node.Parent = Nodes[ParentIndex];
			}
		}

		SerializeArrayOfSmartPointer(Node.Materials, Materials);

		int32 MeshIndex;
		int32 ChildCount;

		if (Ar.IsSaving())
		{
			MeshIndex = Meshes.Find(Node.Mesh);
			ChildCount = Node.Children.Num();
		}

		Ar << MeshIndex;
		if (!Ar.IsSaving())
		{
			if (MeshIndex >= 0)
			{
				Node.Mesh = Meshes[MeshIndex];
			}
		}

		SerializeArrayOfSmartPointer(Node.Children, Nodes);
	}

	void SerializeMesh(FDatasmithFBXSceneMesh& Mesh)
	{
		Ar << Mesh.Name;
		Ar << Mesh.MeshDescription;
	}

	void SerializeMaterial(FDatasmithFBXSceneMaterial& Material)
	{
		Ar << Material.Name;

		Ar << Material.BoolParams;
		Ar << Material.ScalarParams;
		Ar << Material.VectorParams;
		Ar << Material.TextureParams;
	}

	bool SerializeScene()
	{
		int32 Version = FormatVersion;
		Ar << Version;
		if (!Ar.IsSaving())
		{
			if (Version != FormatVersion)
			{
				UE_LOG(LogDatasmithFBXImport, Log, TEXT("Different version than the importer code, skipping loading of intermediate file"));
				return false;
			}
		}

		int32 NodeCount = 0;
		int32 MeshCount;
		int32 MaterialCount;

		int32 RootNodeIndex;

		if (Ar.IsSaving())
		{
			FDatasmithFBXScene::MeshUseCountType MeshesCounts;
			FDatasmithFBXScene::MaterialUseCountType MaterialsCounts;
			Scene.RecursiveCollectAllObjects(&MeshesCounts, &MaterialsCounts, &NodeCount, Scene.RootNode);

			Nodes.Reserve(NodeCount);
			FDatasmithFBXSceneNode::Traverse(Scene.RootNode, [&](TSharedPtr<FDatasmithFBXSceneNode> Node)
			{
				Nodes.Add(Node);
			});

			MeshesCounts.GetKeys(Meshes);
			//MaterialsCounts.GetKeys(Materials);
			Materials = Scene.Materials;

			MeshCount = Meshes.Num();
			MaterialCount = Materials.Num();

			RootNodeIndex = Nodes.Find(Scene.RootNode);
		}

		Ar << MaterialCount;
		Ar << MeshCount;
		Ar << NodeCount;

		if (!Ar.IsSaving())
		{
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				Materials.Add(MakeShareable<FDatasmithFBXSceneMaterial>(new FDatasmithFBXSceneMaterial));
			}
			for (int32 MeshIndex = 0; MeshIndex < MeshCount; ++MeshIndex)
			{
				Meshes.Add(MakeShareable<FDatasmithFBXSceneMesh>(new FDatasmithFBXSceneMesh));
			}
			for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
			{
				Nodes.Add(MakeShareable<FDatasmithFBXSceneNode>(new FDatasmithFBXSceneNode));
			}
		}

		for (auto& Node : Nodes)
		{
			SerializeNode(*Node);
		}

		for (auto& Mesh : Meshes)
		{
			SerializeMesh(*Mesh);
		}

		for (auto& Material : Materials)
		{
			SerializeMaterial(*Material);
		}
		Scene.Materials = Materials;

		Ar << Scene.AnimNodes;

		Ar << RootNodeIndex;

		if (!Ar.IsSaving())
		{
			if (RootNodeIndex >= 0)
			{
				Scene.RootNode = Nodes[RootNodeIndex];
			}
		}

		Ar << Scene.ScaleFactor;
		Ar << Scene.TagTime;
		Ar << Scene.BaseTime;
		Ar << Scene.PlaybackSpeed;

		return true;
	}
};

bool FDatasmithFBXScene::Serialize(FArchive& Ar)
{
	FDatasmithFBXSceneSerializer SceneSerializer(Ar, *this);
	return SceneSerializer.SerializeScene();
}
