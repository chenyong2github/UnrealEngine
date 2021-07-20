// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Mesh/InterchangeStaticMeshFactory.h"

#include "Components.h"
#include "Engine/StaticMesh.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "InterchangeStaticMeshLodDataNode.h"
#include "InterchangeStaticMeshFactoryNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
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


UObject* UInterchangeStaticMeshFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments) const
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


UObject* UInterchangeStaticMeshFactory::CreateAsset(const UInterchangeStaticMeshFactory::FCreateAssetParams& Arguments) const
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import static mesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetObjectClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = Cast<UInterchangeStaticMeshFactoryNode>(Arguments.AssetNode);
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

	if (StaticMeshObject)
	{
		// Currently static mesh re-import will not touch the static mesh at all
		// TODO design a re-import process for the static mesh (expressions and input connections)
		if (!Arguments.ReimportObject)
		{
			UStaticMesh* StaticMesh = Cast<UStaticMesh>(StaticMeshObject);
			if (!ensure(StaticMesh))
			{
				UE_LOG(LogInterchangeImport, Error, TEXT("Could not create StaticMesh asset %s"), *Arguments.AssetName);
				return nullptr;
			}
			
			int32 LodCount = StaticMeshFactoryNode->GetLodDataCount();
			StaticMesh->SetNumSourceModels(LodCount);

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
							MeshReference.SceneNode->GetCustomMeshDependencyUid(MeshDependencyUid);
							MeshReference.MeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MeshDependencyUid));

							// Cache the scene node global matrix, we will use this matrix to bake the vertices
							FTransform SceneNodeGlobalTransform;
							if (MeshReference.SceneNode->GetCustomGlobalTransform(SceneNodeGlobalTransform))
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
					TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = Attributes.GetVertexInstanceColors();
					bool bReplaceVertexColor = false;
					StaticMeshFactoryNode->GetCustomVertexColorReplace(bReplaceVertexColor);
					if (!bReplaceVertexColor)
					{
						bool bIgnoreVertexColor = false;
						StaticMeshFactoryNode->GetCustomVertexColorIgnore(bIgnoreVertexColor);
						if (bIgnoreVertexColor)
						{
							//Flush the vertex color, if we re-import we have to fill it with the old data
							for (const FVertexInstanceID& VertexInstanceID : LodMeshDescription->VertexInstances().GetElementIDs())
							{
								VertexInstanceColors[VertexInstanceID] = FVector4(FLinearColor(FColor::White));
							}
						}
						else
						{
							FColor OverrideVertexColor;
							if (StaticMeshFactoryNode->GetCustomVertexColorOverride(OverrideVertexColor))
							{
								for (const FVertexInstanceID& VertexInstanceID : LodMeshDescription->VertexInstances().GetElementIDs())
								{
									VertexInstanceColors[VertexInstanceID] = FVector4(FLinearColor(OverrideVertexColor));
								}
							}
						}
					}
				}

				StaticMesh->CommitMeshDescription(CurrentLodIndex);

				// Handle materials
				// @TODO: move this outside of the LOD loop?
				TArray<FString> FactoryDependencies;
				StaticMeshFactoryNode->GetFactoryDependencies(FactoryDependencies);

				for (int32 DependencyIndex = 0; DependencyIndex < FactoryDependencies.Num(); ++DependencyIndex)
				{
					const UInterchangeMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeMaterialFactoryNode>(Arguments.NodeContainer->GetNode(FactoryDependencies[DependencyIndex]));
					if (!MaterialFactoryNode || !MaterialFactoryNode->ReferenceObject.IsValid())
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

				// Set static mesh build settings
				FStaticMeshSourceModel& SrcModel = StaticMesh->GetSourceModel(CurrentLodIndex);
				SrcModel.ReductionSettings.MaxDeviation = 0.0f;
				SrcModel.ReductionSettings.PercentTriangles = 1.0f;
				SrcModel.ReductionSettings.PercentVertices = 1.0f;

				CurrentLodIndex++;
			}

			// Apply all SkeletalMeshFactoryNode custom attributes to the material asset
			StaticMeshFactoryNode->ApplyAllCustomAttributeToAsset(StaticMesh);
		}
		
		// Getting the file Hash will cache it into the source data
		Arguments.SourceData->GetFileContentHash();

		// The interchange completion task (called in the GameThread after the factories pass), will call PostEditChange
		// which will trigger another asynchronous system that will build all static meshes in parallel
	}
	else
	{
		// The static mesh is not a UStaticMesh
		StaticMeshObject->RemoveFromRoot();
		StaticMeshObject->MarkPendingKill();
	}
	return StaticMeshObject;

#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}


/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets */
void UInterchangeStaticMeshFactory::PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments) const
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