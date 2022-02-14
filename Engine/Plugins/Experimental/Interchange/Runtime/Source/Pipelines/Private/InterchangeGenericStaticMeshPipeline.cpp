// Copyright Epic Games, Inc. All Rights Reserved. 
#include "InterchangeGenericMeshPipeline.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "CoreMinimal.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangePipelineMeshesUtilities.h"
#include "InterchangeSceneNode.h"
#include "InterchangeStaticMeshFactoryNode.h"
#include "InterchangeStaticMeshLodDataNode.h"
#include "InterchangeSourceData.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Tasks/Task.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

void UInterchangeGenericMeshPipeline::ExecutePreImportPipelineStaticMesh()
{
	if (bImportStaticMeshes && (ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_None || ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_StaticMesh))
	{
		const bool bConvertSkeletalMeshToStaticMesh = (ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_StaticMesh);
		if (bCombineStaticMeshes)
		{
			// Combine all the static meshes

			bool bFoundMeshes = false;
			if (bBakeMeshes)
			{
				// If baking transforms, get all the static mesh instance nodes, and group them by LOD
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshInstance(MeshUids, bConvertSkeletalMeshToStaticMesh);

				TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

				for (const FString& MeshUid : MeshUids)
				{
					const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
					for (const auto& LodIndexAndSceneNodeContainer : MeshInstance.SceneNodePerLodIndex)
					{
						const int32 LodIndex = LodIndexAndSceneNodeContainer.Key;
						const FInterchangeLodSceneNodeContainer& SceneNodeContainer = LodIndexAndSceneNodeContainer.Value;

						TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
						for (const UInterchangeSceneNode* SceneNode : SceneNodeContainer.SceneNodes)
						{
							TranslatedNodes.Add(SceneNode->GetUniqueID());
						}
					}
				}

				// If we got some instances, create a static mesh factory node
				if (MeshUidsPerLodIndex.Num() > 0)
				{
					UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
					StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
					bFoundMeshes = true;
				}
			}

			if (!bFoundMeshes)
			{
				// If we haven't yet managed to build a factory node, look at static mesh geometry directly.
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshGeometry(MeshUids, bConvertSkeletalMeshToStaticMesh);

				TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

				for (const FString& MeshUid : MeshUids)
				{
					// MeshGeometry cannot have Lod since LODs are defined in the scene node
					const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(MeshUid);
					const int32 LodIndex = 0;
					TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
					TranslatedNodes.Add(MeshGeometry.MeshUid);
				}

				if (MeshUidsPerLodIndex.Num() > 0)
				{
					UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
					StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
				}
			}

		}
		else
		{
			// Do not combine static meshes

			bool bFoundMeshes = false;
			if (bBakeMeshes)
			{
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshInstance(MeshUids, bConvertSkeletalMeshToStaticMesh);

				for (const FString& MeshUid : MeshUids)
				{
					// @TODO: check this is correct. What happens when we explicitly don't combine scenes with a LOD group node and multiple transform nodes?
					TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

					const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
					for (const auto& LodIndexAndSceneNodeContainer : MeshInstance.SceneNodePerLodIndex)
					{
						const int32 LodIndex = LodIndexAndSceneNodeContainer.Key;
						const FInterchangeLodSceneNodeContainer& SceneNodeContainer = LodIndexAndSceneNodeContainer.Value;

						TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
						for (const UInterchangeSceneNode* SceneNode : SceneNodeContainer.SceneNodes)
						{
							TranslatedNodes.Add(SceneNode->GetUniqueID());
						}
					}

					if (MeshUidsPerLodIndex.Num() > 0)
					{
						UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
						StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
						bFoundMeshes = true;
					}
				}
			}

			if (!bFoundMeshes)
			{
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshGeometry(MeshUids, bConvertSkeletalMeshToStaticMesh);

				for (const FString& MeshUid : MeshUids)
				{
					TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

					const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(MeshUid);
					const int32 LodIndex = 0;
					TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
					TranslatedNodes.Add(MeshGeometry.MeshUid);

					if (MeshUidsPerLodIndex.Num() > 0)
					{
						UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
						StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
					}
				}
			}
		}
	}
}

bool UInterchangeGenericMeshPipeline::MakeMeshFactoryNodeUidAndDisplayLabel(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex, int32 LodIndex, FString& NewNodeUid, FString& DisplayLabel)
{
	int32 SceneNodeCount = 0;

	if (!ensure(LodIndex >= 0 && MeshUidsPerLodIndex.Num() > LodIndex))
	{
		return false;
	}

	for (const TPair<int32, TArray<FString>>& LodIndexAndMeshUids : MeshUidsPerLodIndex)
	{
		if (LodIndex == LodIndexAndMeshUids.Key)
		{
			const TArray<FString>& Uids = LodIndexAndMeshUids.Value;
			if (Uids.Num() > 0)
			{
				const FString& Uid = Uids[0];
				UInterchangeBaseNode* Node = BaseNodeContainer->GetNode(Uid);

				if (UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(Node))
				{
					DisplayLabel = Node->GetDisplayLabel();
					NewNodeUid = Uid;
					return true;
				}

				if (UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(Node))
				{
					FString RefMeshUid;
					if (SceneNode->GetCustomAssetInstanceUid(RefMeshUid))
					{
						UInterchangeBaseNode* MeshNode = BaseNodeContainer->GetNode(RefMeshUid);
						if (MeshNode)
						{
							DisplayLabel = MeshNode->GetDisplayLabel();
							if (Uids.Num() == 1)
							{
								// If we are instancing one scene node, we want to use it to name the mesh
								DisplayLabel = SceneNode->GetDisplayLabel();
							}

							// Use the first scene node uid this LOD references, adding backslash since this uid is not asset typed (\\Mesh\\) like a mesh node
							// @TODO: change this so that scene nodes get a type prefix of their own?
							NewNodeUid = TEXT("\\") + Uid;
							return true;
						}
					}
				}
			}

			// We found the lod but there is no valid Mesh node to return the Uid
			break;
		}
	}

	return false;
}

UInterchangeStaticMeshFactoryNode* UInterchangeGenericMeshPipeline::CreateStaticMeshFactoryNode(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex)
{
	if (MeshUidsPerLodIndex.Num() == 0)
	{
		return nullptr;
	}

	// Create the static mesh factory node, name it according to the first mesh node compositing the meshes
	FString StaticMeshUid_MeshNamePart;
	FString DisplayLabel;
	const int32 BaseLodIndex = 0;
	if (!MakeMeshFactoryNodeUidAndDisplayLabel(MeshUidsPerLodIndex, BaseLodIndex, StaticMeshUid_MeshNamePart, DisplayLabel))
	{
		// Log an error
		return nullptr;
	}

	const FString StaticMeshUid = TEXT("\\StaticMesh") + StaticMeshUid_MeshNamePart;
	UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = NewObject<UInterchangeStaticMeshFactoryNode>(BaseNodeContainer, NAME_None);
	if (!ensure(StaticMeshFactoryNode))
	{
		return nullptr;
	}

	StaticMeshFactoryNode->InitializeStaticMeshNode(StaticMeshUid, DisplayLabel, UStaticMesh::StaticClass()->GetName());
	BaseNodeContainer->AddNode(StaticMeshFactoryNode);

	AddLodDataToStaticMesh(StaticMeshFactoryNode, MeshUidsPerLodIndex);

	switch (VertexColorImportOption)
	{
	case EInterchangeVertexColorImportOption::IVCIO_Replace:
	{
		StaticMeshFactoryNode->SetCustomVertexColorReplace(true);
	}
	break;
	case EInterchangeVertexColorImportOption::IVCIO_Ignore:
	{
		StaticMeshFactoryNode->SetCustomVertexColorIgnore(true);
	}
	break;
	case EInterchangeVertexColorImportOption::IVCIO_Override:
	{
		StaticMeshFactoryNode->SetCustomVertexColorOverride(VertexOverrideColor);
	}
	break;
	}
	return StaticMeshFactoryNode;
}


UInterchangeStaticMeshLodDataNode* UInterchangeGenericMeshPipeline::CreateStaticMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID)
{
	FString DisplayLabel(NodeName);
	FString NodeUID(NodeUniqueID);
	UInterchangeStaticMeshLodDataNode* StaticMeshLodDataNode = NewObject<UInterchangeStaticMeshLodDataNode>(BaseNodeContainer, NAME_None);
	if (!ensure(StaticMeshLodDataNode))
	{
		// @TODO: log error
		return nullptr;
	}

	StaticMeshLodDataNode->InitializeNode(NodeUID, DisplayLabel, EInterchangeNodeContainerType::FactoryData);
	BaseNodeContainer->AddNode(StaticMeshLodDataNode);
	return StaticMeshLodDataNode;
}


void UInterchangeGenericMeshPipeline::AddLodDataToStaticMesh(UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex)
{
	const FString StaticMeshFactoryUid = StaticMeshFactoryNode->GetUniqueID();

	for (const TPair<int32, TArray<FString>>& LodIndexAndNodeUids : NodeUidsPerLodIndex)
	{
		const int32 LodIndex = LodIndexAndNodeUids.Key;
		if (!bImportLods && LodIndex > 0)
		{
			// If the pipeline should not import lods, skip any lod over base lod
			continue;
		}

		const TArray<FString>& NodeUids = LodIndexAndNodeUids.Value;

		// Create a lod data node with all the meshes for this LOD
		const FString StaticMeshLodDataName = TEXT("LodData") + FString::FromInt(LodIndex);
		const FString LODDataPrefix = TEXT("\\LodData") + (LodIndex > 0 ? FString::FromInt(LodIndex) : TEXT(""));
		const FString StaticMeshLodDataUniqueID = LODDataPrefix + StaticMeshFactoryUid;

		// Create LodData node if it doesn't already exist
		UInterchangeStaticMeshLodDataNode* LodDataNode = Cast<UInterchangeStaticMeshLodDataNode>(BaseNodeContainer->GetNode(StaticMeshLodDataUniqueID));
		if (!LodDataNode)
		{
			// Add the data for the LOD (all the mesh node fbx path, so we can find them when we will create the payload data)
			LodDataNode = CreateStaticMeshLodDataNode(StaticMeshLodDataName, StaticMeshLodDataUniqueID);
			LodDataNode->SetParentUid(StaticMeshFactoryUid);
			StaticMeshFactoryNode->AddLodDataUniqueId(StaticMeshLodDataUniqueID);
		}

		for (const FString& NodeUid : NodeUids)
		{
			TArray<FString> MaterialDependencies;
			if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				FString MeshDependency;
				SceneNode->GetCustomAssetInstanceUid(MeshDependency);
				if (BaseNodeContainer->IsNodeUidValid(MeshDependency))
				{
					const UInterchangeMeshNode* MeshDependencyNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(MeshDependency));
					StaticMeshFactoryNode->AddTargetNodeUid(MeshDependency);
					MeshDependencyNode->AddTargetNodeUid(StaticMeshFactoryNode->GetUniqueID());
					MeshDependencyNode->GetMaterialDependencies(MaterialDependencies);
				}
				else
				{
					SceneNode->GetMaterialDependencyUids(MaterialDependencies);
				}
			}
			else if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				StaticMeshFactoryNode->AddTargetNodeUid(NodeUid);
				MeshNode->AddTargetNodeUid(StaticMeshFactoryNode->GetUniqueID());
				MeshNode->GetMaterialDependencies(MaterialDependencies);
			}

			const int32 MaterialCount = MaterialDependencies.Num();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				const FString MaterialFactoryNodeUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MaterialDependencies[MaterialIndex]);
				if (BaseNodeContainer->IsNodeUidValid(MaterialFactoryNodeUid))
				{
					BaseNodeContainer->GetNode(MaterialFactoryNodeUid)->SetEnabled(true);
					// Create a factory dependency so Material asset are imported before the skeletal mesh asset
					TArray<FString> FactoryDependencies;
					StaticMeshFactoryNode->GetFactoryDependencies(FactoryDependencies);
					if (!FactoryDependencies.Contains(MaterialFactoryNodeUid))
					{
						StaticMeshFactoryNode->AddFactoryDependencyUid(MaterialFactoryNodeUid);
					}
				}
			}

			LodDataNode->AddMeshUid(NodeUid);
		}
	}
}
