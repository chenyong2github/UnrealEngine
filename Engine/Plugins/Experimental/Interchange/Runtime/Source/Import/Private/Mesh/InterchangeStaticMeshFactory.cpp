// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Mesh/InterchangeStaticMeshFactory.h"

#include "Components.h"
#include "Engine/StaticMesh.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "InterchangeStaticMeshLodDataNode.h"
#include "InterchangeStaticMeshFactoryNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Mesh/InterchangeStaticMeshPayload.h"
#include "Mesh/InterchangeStaticMeshPayloadInterface.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"

#if WITH_EDITORONLY_DATA

#include "EditorFramework/AssetImportData.h"

#endif //WITH_EDITORONLY_DATA


UClass* UInterchangeStaticMeshFactory::GetFactoryClass() const
{
	return UStaticMesh::StaticClass();
}


UObject* UInterchangeStaticMeshFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments)
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import StaticMesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else
	UStaticMesh* StaticMesh = nullptr;
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = Cast<UInterchangeStaticMeshFactoryNode>(Arguments.AssetNode);
	if (StaticMeshFactoryNode == nullptr)
	{
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	// create a new static mesh or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		StaticMesh = NewObject<UStaticMesh>(Arguments.Parent, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if (ExistingAsset->GetClass()->IsChildOf(UStaticMesh::StaticClass()))
	{
		//This is a reimport, we are just re-updating the source data
		StaticMesh = Cast<UStaticMesh>(ExistingAsset);
	}
	
	if (!StaticMesh)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create StaticMesh asset %s"), *Arguments.AssetName);
		return nullptr;
	}
	
	StaticMesh->PreEditChange(nullptr);	
	return StaticMesh;
#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}


UObject* UInterchangeStaticMeshFactory::CreateAsset(const FCreateAssetParams& Arguments)
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import static mesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = Cast<UInterchangeStaticMeshFactoryNode>(Arguments.AssetNode);
	if (StaticMeshFactoryNode == nullptr)
	{
		return nullptr;
	}

	const IInterchangeStaticMeshPayloadInterface* StaticMeshTranslatorPayloadInterface = Cast<IInterchangeStaticMeshPayloadInterface>(Arguments.Translator);
	if (!StaticMeshTranslatorPayloadInterface)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import static mesh, the translator does not implement the IInterchangeStaticMeshPayloadInterface."));
		return nullptr;
	}

	const UClass* StaticMeshClass = StaticMeshFactoryNode->GetObjectClass();
	check(StaticMeshClass && StaticMeshClass->IsChildOf(GetFactoryClass()));

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UObject* StaticMeshObject = nullptr;

	// create a new static mesh or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		// NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		// The UObject should have been created by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		StaticMeshObject = NewObject<UObject>(Arguments.Parent, StaticMeshClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if(ExistingAsset->GetClass()->IsChildOf(StaticMeshClass))
	{
		//This is a reimport, we are just re-updating the source data
		StaticMeshObject = ExistingAsset;
	}

	if (!StaticMeshObject)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Could not create StaticMesh asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	UStaticMesh* StaticMesh = Cast<UStaticMesh>(StaticMeshObject);
	if (!ensure(StaticMesh))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Could not create StaticMesh asset %s"), *Arguments.AssetName);
		return nullptr;
	}
			
	int32 LodCount = StaticMeshFactoryNode->GetLodDataCount();
	int32 FinalLodCount = LodCount;
	TMap<FVector, FColor> ExisitingVertexColorData;
	if (Arguments.ReimportObject)
	{
		if(ExistingAsset)
		{
			StaticMesh->GetVertexColorData(ExisitingVertexColorData);
		}
		//When we reimport we dont want to reduce the number of existing LODs
		FinalLodCount = FMath::Max(StaticMesh->GetNumLODs(), LodCount);
	}
	StaticMesh->SetNumSourceModels(FinalLodCount);

	TArray<FString> LodDataUniqueIds;
	StaticMeshFactoryNode->GetLodDataUniqueIds(LodDataUniqueIds);
	ensure(LodDataUniqueIds.Num() == LodCount);

	int32 CurrentLodIndex = 0;
	for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		FString LodUniqueId = LodDataUniqueIds[LodIndex];
		const UInterchangeStaticMeshLodDataNode* LodDataNode = Cast<UInterchangeStaticMeshLodDataNode>(Arguments.NodeContainer->GetNode(LodUniqueId));
		if (!LodDataNode)
		{
			UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD when importing StaticMesh asset %s"), *Arguments.AssetName);
			continue;
		}

		// Get the mesh node context for each MeshUids
		struct FMeshNodeContext
		{
			const UInterchangeMeshNode* MeshNode = nullptr;
			const UInterchangeSceneNode* SceneNode = nullptr;
			TOptional<FTransform> SceneGlobalTransform;
			FString TranslatorPayloadKey;
		};
		TArray<FMeshNodeContext> MeshReferences;

		// Scope to query the mesh node
		{
			TArray<FString> MeshUids;
			LodDataNode->GetMeshUids(MeshUids);
			MeshReferences.Reserve(MeshUids.Num());

			for (const FString& MeshUid : MeshUids)
			{
				FMeshNodeContext MeshReference;
				MeshReference.MeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MeshUid));
				if (!MeshReference.MeshNode)
				{
					// The reference is a scene node and we need to bake the geometry
					MeshReference.SceneNode = Cast<UInterchangeSceneNode>(Arguments.NodeContainer->GetNode(MeshUid));
					if (!ensure(MeshReference.SceneNode != nullptr))
					{
						UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD mesh reference when importing StaticMesh asset %s"), *Arguments.AssetName);
						continue;
					}

					FString MeshDependencyUid;
					MeshReference.SceneNode->GetCustomAssetInstanceUid(MeshDependencyUid);
					MeshReference.MeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MeshDependencyUid));

					// Cache the scene node global matrix, we will use this matrix to bake the vertices
					FTransform SceneNodeGlobalTransform;
					if (MeshReference.SceneNode->GetCustomGlobalTransform(Arguments.NodeContainer, SceneNodeGlobalTransform))
					{
						MeshReference.SceneGlobalTransform = SceneNodeGlobalTransform;
					}
				}

				if (!ensure(MeshReference.MeshNode != nullptr))
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD mesh reference when importing StaticMesh asset %s"), *Arguments.AssetName);
					continue;
				}

				TOptional<FString> MeshPayloadKey = MeshReference.MeshNode->GetPayLoadKey();
				if (MeshPayloadKey.IsSet())
				{
					MeshReference.TranslatorPayloadKey = MeshPayloadKey.GetValue();
				}
				else
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Empty LOD mesh reference payload when importing StaticMesh asset %s"), *Arguments.AssetName);
					continue;
				}

				MeshReferences.Add(MeshReference);
			}
		}

		// Add the lod mesh data to the static mesh
		FMeshDescription* LodMeshDescription = StaticMesh->CreateMeshDescription(CurrentLodIndex);

		FStaticMeshOperations::FAppendSettings AppendSettings;
		for (int32 ChannelIdx = 0; ChannelIdx < FStaticMeshOperations::FAppendSettings::MAX_NUM_UV_CHANNELS; ++ChannelIdx)
		{
			AppendSettings.bMergeUVChannels[ChannelIdx] = true;
		}

		// Fetch the payloads for all mesh references in parallel
		TMap<FString, TFuture<TOptional<UE::Interchange::FStaticMeshPayloadData>>> MeshPayloads;
		for (const FMeshNodeContext& MeshNodeContext : MeshReferences)
		{
			const FString& PayloadKey = MeshNodeContext.TranslatorPayloadKey;
			MeshPayloads.Add(PayloadKey, StaticMeshTranslatorPayloadInterface->GetStaticMeshPayloadData(PayloadKey));
		}
				
		// Fill the lod mesh description using all combined mesh parts
		for (const FMeshNodeContext& MeshNodeContext : MeshReferences)
		{
			TOptional<UE::Interchange::FStaticMeshPayloadData> LodMeshPayload = MeshPayloads.FindChecked(MeshNodeContext.TranslatorPayloadKey).Get();
			if (!LodMeshPayload.IsSet())
			{
				UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid static mesh payload key [%s] StaticMesh asset %s"), *MeshNodeContext.TranslatorPayloadKey, *Arguments.AssetName);
				continue;
			}

			// Bake the payload, with the provided transform
			if (MeshNodeContext.SceneGlobalTransform.IsSet())
			{
				AppendSettings.MeshTransform = MeshNodeContext.SceneGlobalTransform;
			}
			else
			{
				AppendSettings.MeshTransform.Reset();
			}

			FStaticMeshOperations::AppendMeshDescription(LodMeshPayload->MeshDescription, *LodMeshDescription, AppendSettings);
		}

		//////////////////////////////////////////////////////////////////////////
		//Manage vertex color
		//Replace -> do nothing, we want to use the translated source data
		//Ignore -> remove vertex color from import data (when we re-import, ignore have to put back the current mesh vertex color)
		//Override -> replace the vertex color by the override color
		{
			FStaticMeshAttributes Attributes(*LodMeshDescription);
			TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors = Attributes.GetVertexInstanceColors();
			bool bReplaceVertexColor = false;
			StaticMeshFactoryNode->GetCustomVertexColorReplace(bReplaceVertexColor);
			if (!bReplaceVertexColor)
			{
				bool bIgnoreVertexColor = false;
				StaticMeshFactoryNode->GetCustomVertexColorIgnore(bIgnoreVertexColor);
				if (bIgnoreVertexColor)
				{
					for (const FVertexInstanceID& VertexInstanceID : LodMeshDescription->VertexInstances().GetElementIDs())
					{
						//If we have old vertex color (reimport), we want to keep it if the option is ignore
						if (ExisitingVertexColorData.Num() > 0)
						{
							FVector3f VertexPosition = LodMeshDescription->GetVertexPosition(LodMeshDescription->GetVertexInstanceVertex(VertexInstanceID));
							const FColor* PaintedColor = ExisitingVertexColorData.Find(VertexPosition);
							if (PaintedColor)
							{
								// A matching color for this vertex was found
								VertexInstanceColors[VertexInstanceID] = FVector4f(FLinearColor(*PaintedColor));
							}
							else
							{
								//Flush the vertex color
								VertexInstanceColors[VertexInstanceID] = FVector4f(FLinearColor(FColor::White));
							}
						}
						else
						{
							//Flush the vertex color
							VertexInstanceColors[VertexInstanceID] = FVector4f(FLinearColor(FColor::White));
						}
					}
				}
				else
				{
					FColor OverrideVertexColor;
					if (StaticMeshFactoryNode->GetCustomVertexColorOverride(OverrideVertexColor))
					{
						for (const FVertexInstanceID& VertexInstanceID : LodMeshDescription->VertexInstances().GetElementIDs())
						{
							VertexInstanceColors[VertexInstanceID] = FVector4f(FLinearColor(OverrideVertexColor));
						}
					}
				}
			}
		}

		UStaticMesh::FCommitMeshDescriptionParams CommitMeshDescriptionParams;
		CommitMeshDescriptionParams.bMarkPackageDirty = false; // Marking packages dirty isn't threadsafe
		StaticMesh->CommitMeshDescription(CurrentLodIndex, CommitMeshDescriptionParams);

		// Handle materials
		// @TODO: move this outside of the LOD loop?
		TArray<FString> FactoryDependencies;
		StaticMeshFactoryNode->GetFactoryDependencies(FactoryDependencies);

		for (int32 DependencyIndex = 0; DependencyIndex < FactoryDependencies.Num(); ++DependencyIndex)
		{
			const UInterchangeBaseMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeBaseMaterialFactoryNode>(Arguments.NodeContainer->GetNode(FactoryDependencies[DependencyIndex]));
			if (!MaterialFactoryNode || !MaterialFactoryNode->ReferenceObject.IsValid())
			{
				continue;
			}
			if (!MaterialFactoryNode->IsEnabled())
			{
				continue;
			}

			FName MaterialSlotName = *MaterialFactoryNode->GetDisplayLabel();
			UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(MaterialFactoryNode->ReferenceObject.ResolveObject());
			if (!MaterialInterface)
			{
				MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
			}

			int32 MaterialSlotIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(MaterialSlotName);
			if (MaterialSlotIndex == INDEX_NONE)
			{
				StaticMesh->GetStaticMaterials().Emplace(MaterialInterface, MaterialSlotName);
			}
		}

		FStaticMeshConstAttributes StaticMeshAttributes(*LodMeshDescription);
		TPolygonGroupAttributesRef<const FName> SlotNames = StaticMeshAttributes.GetPolygonGroupMaterialSlotNames();
		int32 SectionIndex = 0;
		for (FPolygonGroupID PolygonGroupID : LodMeshDescription->PolygonGroups().GetElementIDs())
		{
			int32 MaterialSlotIndex = StaticMesh->GetMaterialIndexFromImportedMaterialSlotName(SlotNames[PolygonGroupID]);
					
			if (MaterialSlotIndex == INDEX_NONE)
			{
				// If no material was found with this slot name, it is probably because the pipeline is configured to not import materials.
				// Fill out a blank slot instead.
				MaterialSlotIndex = StaticMesh->GetStaticMaterials().Emplace(nullptr, SlotNames[PolygonGroupID]);
			}

			FMeshSectionInfo Info = StaticMesh->GetSectionInfoMap().Get(CurrentLodIndex, SectionIndex);
			Info.MaterialIndex = MaterialSlotIndex;
			StaticMesh->GetSectionInfoMap().Remove(CurrentLodIndex, SectionIndex);
			StaticMesh->GetSectionInfoMap().Set(CurrentLodIndex, SectionIndex, Info);

			SectionIndex++;
		}
		if (CurrentLodIndex >= FinalLodCount)
		{
			// Set static mesh build settings
			FStaticMeshSourceModel& SrcModel = StaticMesh->GetSourceModel(CurrentLodIndex);
			SrcModel.ReductionSettings.MaxDeviation = 0.0f;
			SrcModel.ReductionSettings.PercentTriangles = 1.0f;
			SrcModel.ReductionSettings.PercentVertices = 1.0f;
		}

		CurrentLodIndex++;
	}

	if (!Arguments.ReimportObject)
	{
		// Apply all StaticMeshFactoryNode custom attributes to the static mesh asset
		StaticMeshFactoryNode->ApplyAllCustomAttributeToObject(StaticMesh);
	}
	else
	{
		//Apply the re import strategy 
		UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(StaticMesh->GetAssetImportData());
		UInterchangeBaseNode* PreviousNode = nullptr;
		if (InterchangeAssetImportData)
		{
			PreviousNode = InterchangeAssetImportData->NodeContainer->GetNode(InterchangeAssetImportData->NodeUniqueID);
		}
		UInterchangeBaseNode* CurrentNode = NewObject<UInterchangeBaseNode>(GetTransientPackage(), UInterchangeStaticMeshFactoryNode::StaticClass());
		UInterchangeBaseNode::CopyStorage(StaticMeshFactoryNode, CurrentNode);
		CurrentNode->FillAllCustomAttributeFromObject(StaticMesh);
		UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(StaticMesh, PreviousNode, CurrentNode, StaticMeshFactoryNode);
	}
		
	// Getting the file Hash will cache it into the source data
	Arguments.SourceData->GetFileContentHash();

	return StaticMeshObject;

#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}


/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets */
void UInterchangeStaticMeshFactory::PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
{
	check(IsInGameThread());
	Super::PreImportPreCompletedCallback(Arguments);

	// TODO: make sure this works at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		// We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(Arguments.ImportedObject);

		UAssetImportData* ImportDataPtr = StaticMesh->GetAssetImportData();
		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(StaticMesh, ImportDataPtr, Arguments.SourceData, Arguments.NodeUniqueID, Arguments.NodeContainer, Arguments.Pipelines);
		ImportDataPtr = UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
		StaticMesh->SetAssetImportData(ImportDataPtr);
	}
#endif
}