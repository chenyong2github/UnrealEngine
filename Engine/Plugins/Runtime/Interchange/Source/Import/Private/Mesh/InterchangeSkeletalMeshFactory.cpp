// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Mesh/InterchangeSkeletalMeshFactory.h"

#include "Components.h"
#include "Engine/SkeletalMesh.h"
#include "GPUSkinPublicDefs.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Mesh/InterchangeSkeletalMeshPayload.h"
#include "Mesh/InterchangeSkeletalMeshPayloadInterface.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshAttributes.h"
#include "SkeletalMeshOperations.h"

#if WITH_EDITORONLY_DATA

#include "EditorFramework/AssetImportData.h"

#endif //WITH_EDITORONLY_DATA

#if WITH_EDITOR
namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			struct FJointInfo
			{
				FString Name;
				int32 ParentIndex;  // 0 if this is the root bone.  
				FTransform	LocalTransform; // local transform
			};

			void RecursiveAddBones(const UInterchangeBaseNodeContainer* NodeContainer, const FString& JointNodeId, TArray <FJointInfo>& JointInfos, int32 ParentIndex, TArray<SkeletalMeshImportData::FBone>& RefBonesBinary)
			{
				const UInterchangeSceneNode* JointNode = Cast<UInterchangeSceneNode>(NodeContainer->GetNode(JointNodeId));
				if (!JointNode || !JointNode->IsSpecializedTypeContains(FSceneNodeStaticData::GetJointSpecializeTypeString()))
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton Joint"));
					return;
				}

				int32 JointInfoIndex = JointInfos.Num();
				FJointInfo& Info = JointInfos.AddZeroed_GetRef();
				Info.Name = JointNode->GetDisplayLabel();
				ensure(JointNode->GetCustomLocalTransform(Info.LocalTransform));
				Info.ParentIndex = ParentIndex;

				SkeletalMeshImportData::FBone& Bone = RefBonesBinary.AddZeroed_GetRef();
				Bone.Name = Info.Name;
				Bone.BonePos.Transform = Info.LocalTransform;
				Bone.ParentIndex = ParentIndex;
				//Fill the scrap we do not need
				Bone.BonePos.Length = 0.0f;
				Bone.BonePos.XSize = 1.0f;
				Bone.BonePos.YSize = 1.0f;
				Bone.BonePos.ZSize = 1.0f;
				
				const TArray<FString> ChildrenIds = NodeContainer->GetNodeChildrenUids(JointNodeId);
				Bone.NumChildren = ChildrenIds.Num();
				for (int32 ChildIndex = 0; ChildIndex < ChildrenIds.Num(); ++ChildIndex)
				{
					RecursiveAddBones(NodeContainer, ChildrenIds[ChildIndex], JointInfos, JointInfoIndex, RefBonesBinary);
				}
			}

			bool ProcessImportMeshSkeleton(const USkeleton* SkeletonAsset, FReferenceSkeleton& RefSkeleton, int32& SkeletalDepth, const UInterchangeBaseNodeContainer* NodeContainer, const FString& RootJointNodeId, TArray<SkeletalMeshImportData::FBone>& RefBonesBinary)
			{
				auto FixupBoneName = [](FString BoneName)
				{
					BoneName.TrimStartAndEndInline();
					BoneName.ReplaceInline(TEXT(" "), TEXT("-"), ESearchCase::IgnoreCase);
					return BoneName;
				};

				RefBonesBinary.Empty();
				// Setup skeletal hierarchy + names structure.
				RefSkeleton.Empty();

				FReferenceSkeletonModifier RefSkelModifier(RefSkeleton, SkeletonAsset);
				TArray <FJointInfo> JointInfos;
				RecursiveAddBones(NodeContainer, RootJointNodeId, JointInfos, INDEX_NONE, RefBonesBinary);
				// Digest bones to the serializable format.
				for (int32 b = 0; b < JointInfos.Num(); b++)
				{
					const FJointInfo& BinaryBone = JointInfos[b];

					const FString BoneName = FixupBoneName(BinaryBone.Name);
					const FMeshBoneInfo BoneInfo(FName(*BoneName, FNAME_Add), BinaryBone.Name, BinaryBone.ParentIndex);
					const FTransform BoneTransform(BinaryBone.LocalTransform);
					if (RefSkeleton.FindRawBoneIndex(BoneInfo.Name) != INDEX_NONE)
					{
						UE_LOG(LogInterchangeImport, Error, TEXT("Invalid Skeleton because of non-unique bone names [%s]"), *BoneInfo.Name.ToString());
						return false;
					}
					RefSkelModifier.Add(BoneInfo, BoneTransform);
				}

				// Add hierarchy index to each bone and detect max depth.
				SkeletalDepth = 0;

				TArray<int32> SkeletalDepths;
				SkeletalDepths.Empty(JointInfos.Num());
				SkeletalDepths.AddZeroed(JointInfos.Num());
				for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetRawBoneNum(); ++BoneIndex)
				{
					int32 Parent = RefSkeleton.GetRawParentIndex(BoneIndex);
					int32 Depth = 1.0f;

					SkeletalDepths[BoneIndex] = 1.0f;
					if (Parent != INDEX_NONE)
					{
						Depth += SkeletalDepths[Parent];
					}
					if (SkeletalDepth < Depth)
					{
						SkeletalDepth = Depth;
					}
					SkeletalDepths[BoneIndex] = Depth;
				}

				return true;
			}


			bool CopyBlendShapesMeshDescriptionToSkeletalMeshImportData(const TArray<FString>& BlendShapeToImport, const TMap<FString, FMeshDescription>& LodBlendShapeMeshDescriptions, FSkeletalMeshImportData& DestinationSkeletalMeshImportData)
			{
				for (const TPair<FString, FMeshDescription>& Pair : LodBlendShapeMeshDescriptions)
				{
					FString BlendShapeName(Pair.Key);
					//Skip blend shape that is not define in the BlendShapeToImport array
					if (!BlendShapeToImport.Contains(BlendShapeName))
					{
						continue;
					}

					TArray<FVector3f> CompressPoints;
					CompressPoints.Reserve(DestinationSkeletalMeshImportData.Points.Num());

					const FMeshDescription& SourceMeshDescription = Pair.Value;
					FStaticMeshConstAttributes Attributes(SourceMeshDescription);
					TVertexAttributesConstRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();

					//Create the morph target source data
					FString& MorphTargetName = DestinationSkeletalMeshImportData.MorphTargetNames.AddDefaulted_GetRef();
					MorphTargetName = Pair.Key;
					TSet<uint32>& ModifiedPoints = DestinationSkeletalMeshImportData.MorphTargetModifiedPoints.AddDefaulted_GetRef();
					FSkeletalMeshImportData& MorphTargetData = DestinationSkeletalMeshImportData.MorphTargets.AddDefaulted_GetRef();

					//Reserve the point and influences
					MorphTargetData.Points.AddZeroed(SourceMeshDescription.Vertices().Num());

					for (FVertexID VertexID : SourceMeshDescription.Vertices().GetElementIDs())
					{
						//We can use GetValue because the Meshdescription was compacted before the copy
						MorphTargetData.Points[VertexID.GetValue()] = VertexPositions[VertexID];
					}

					for (int32 PointIdx = 0; PointIdx < DestinationSkeletalMeshImportData.Points.Num(); ++PointIdx)
					{
						int32 OriginalPointIdx = DestinationSkeletalMeshImportData.PointToRawMap[PointIdx];
						//Rebuild the data with only the modified point
						if ((MorphTargetData.Points[OriginalPointIdx] - DestinationSkeletalMeshImportData.Points[PointIdx]).SizeSquared() > FMath::Square(THRESH_POINTS_ARE_SAME))
						{
							ModifiedPoints.Add(PointIdx);
							CompressPoints.Add(MorphTargetData.Points[OriginalPointIdx]);
						}
					}
					MorphTargetData.Points = CompressPoints;
				}
				return true;
			}

			/**
			 * Fill the Materials array using the raw skeletalmesh geometry data (using material imported name)
			 * Find the material from the dependencies of the skeletalmesh before searching in all package.
			 */
			//TODO: the pipeline should search for existing material and hook those before the factory is called
			void ProcessImportMeshMaterials(TArray<FSkeletalMaterial>& Materials, FSkeletalMeshImportData& ImportData, TMap<FString, UMaterialInterface*>& AvailableMaterials)
			{
				TArray <SkeletalMeshImportData::FMaterial>& ImportedMaterials = ImportData.Materials;
				// If direct linkup of materials is requested, try to find them here - to get a texture name from a 
				// material name, cut off anything in front of the dot (beyond are special flags).
				int32 SkinOffset = INDEX_NONE;
				for (int32 MatIndex = 0; MatIndex < ImportedMaterials.Num(); ++MatIndex)
				{
					const SkeletalMeshImportData::FMaterial& ImportedMaterial = ImportedMaterials[MatIndex];

					UMaterialInterface* Material = nullptr;

					const FName SearchMaterialSlotName(*ImportedMaterial.MaterialImportName);
					int32 MaterialIndex = 0;
					FSkeletalMaterial* SkeletalMeshMaterialFind = Materials.FindByPredicate([&SearchMaterialSlotName, &MaterialIndex](const FSkeletalMaterial& ItemMaterial)
					{
						//Imported material slot name is available only WITH_EDITOR
						FName ImportedMaterialSlot = NAME_None;
						ImportedMaterialSlot = ItemMaterial.ImportedMaterialSlotName;
						if (ImportedMaterialSlot != SearchMaterialSlotName)
						{
							MaterialIndex++;
							return false;
						}
						return true;
					});

					if (SkeletalMeshMaterialFind != nullptr)
					{
						Material = SkeletalMeshMaterialFind->MaterialInterface;
					}

					if(!Material)
					{
						//Try to find the material in the skeletal mesh node dependencies (Materials are import before skeletal mesh when there is a dependency)
						if (AvailableMaterials.Contains(ImportedMaterial.MaterialImportName))
						{
							Material = AvailableMaterials.FindChecked(ImportedMaterial.MaterialImportName);
						}
						else
						{
							//We did not found any material in the dependencies so try to find material everywhere
							Material = FindObject<UMaterialInterface>(ANY_PACKAGE, *ImportedMaterial.MaterialImportName);
						}
					}
					
					const bool bEnableShadowCasting = true;
					const bool bInRecomputeTangent = false;
					Materials.Add(FSkeletalMaterial(Material, bEnableShadowCasting, bInRecomputeTangent, Material != nullptr ? Material->GetFName() : FName(*ImportedMaterial.MaterialImportName), FName(*(ImportedMaterial.MaterialImportName))));
				}

				int32 NumMaterialsToAdd = FMath::Max<int32>(ImportedMaterials.Num(), ImportData.MaxMaterialIndex + 1);

				// Pad the material pointers
				while (NumMaterialsToAdd > Materials.Num())
				{
					UMaterialInterface* NullMaterialInterface = nullptr;
					Materials.Add(FSkeletalMaterial(NullMaterialInterface));
				}
			}

			void ProcessImportMeshInfluences(const int32 WedgeCount, TArray<SkeletalMeshImportData::FRawBoneInfluence>& Influences)
			{

				// Sort influences by vertex index.
				struct FCompareVertexIndex
				{
					bool operator()(const SkeletalMeshImportData::FRawBoneInfluence& A, const SkeletalMeshImportData::FRawBoneInfluence& B) const
					{
						if (A.VertexIndex > B.VertexIndex) return false;
						else if (A.VertexIndex < B.VertexIndex) return true;
						else if (A.Weight < B.Weight) return false;
						else if (A.Weight > B.Weight) return true;
						else if (A.BoneIndex > B.BoneIndex) return false;
						else if (A.BoneIndex < B.BoneIndex) return true;
						else									  return  false;
					}
				};
				Influences.Sort(FCompareVertexIndex());

				TArray <SkeletalMeshImportData::FRawBoneInfluence> NewInfluences;
				int32	LastNewInfluenceIndex = 0;
				int32	LastVertexIndex = INDEX_NONE;
				int32	InfluenceCount = 0;

				float TotalWeight = 0.f;
				const float MINWEIGHT = 0.01f;

				int MaxVertexInfluence = 0;
				float MaxIgnoredWeight = 0.0f;

				//We have to normalize the data before filtering influences
				//Because influence filtering is base on the normalize value.
				//Some DCC like Daz studio don't have normalized weight
				for (int32 i = 0; i < Influences.Num(); i++)
				{
					// if less than min weight, or it's more than 8, then we clear it to use weight
					InfluenceCount++;
					TotalWeight += Influences[i].Weight;
					// we have all influence for the same vertex, normalize it now
					if (i + 1 >= Influences.Num() || Influences[i].VertexIndex != Influences[i + 1].VertexIndex)
					{
						// Normalize the last set of influences.
						if (InfluenceCount && (TotalWeight != 1.0f))
						{
							float OneOverTotalWeight = 1.f / TotalWeight;
							for (int r = 0; r < InfluenceCount; r++)
							{
								Influences[i - r].Weight *= OneOverTotalWeight;
							}
						}

						if (MaxVertexInfluence < InfluenceCount)
						{
							MaxVertexInfluence = InfluenceCount;
						}

						// clear to count next one
						InfluenceCount = 0;
						TotalWeight = 0.f;
					}

					if (InfluenceCount > MAX_TOTAL_INFLUENCES && Influences[i].Weight > MaxIgnoredWeight)
					{
						MaxIgnoredWeight = Influences[i].Weight;
					}
				}

				// warn about too many influences
				if (MaxVertexInfluence > MAX_TOTAL_INFLUENCES)
				{
					//TODO log a display message to the user
					//UE_LOG(LogLODUtilities, Display, TEXT("Skeletal mesh (%s) influence count of %d exceeds max count of %d. Influence truncation will occur. Maximum Ignored Weight %f"), *MeshName, MaxVertexInfluence, MAX_TOTAL_INFLUENCES, MaxIgnoredWeight);
				}

				for (int32 i = 0; i < Influences.Num(); i++)
				{
					// we found next verts, normalize it now
					if (LastVertexIndex != Influences[i].VertexIndex)
					{
						// Normalize the last set of influences.
						if (InfluenceCount && (TotalWeight != 1.0f))
						{
							float OneOverTotalWeight = 1.f / TotalWeight;
							for (int r = 0; r < InfluenceCount; r++)
							{
								NewInfluences[LastNewInfluenceIndex - r].Weight *= OneOverTotalWeight;
							}
						}

						// now we insert missing verts
						if (LastVertexIndex != INDEX_NONE)
						{
							int32 CurrentVertexIndex = Influences[i].VertexIndex;
							for (int32 j = LastVertexIndex + 1; j < CurrentVertexIndex; j++)
							{
								// Add a 0-bone weight if none other present (known to happen with certain MAX skeletal setups).
								LastNewInfluenceIndex = NewInfluences.AddUninitialized();
								NewInfluences[LastNewInfluenceIndex].VertexIndex = j;
								NewInfluences[LastNewInfluenceIndex].BoneIndex = 0;
								NewInfluences[LastNewInfluenceIndex].Weight = 1.f;
							}
						}

						// clear to count next one
						InfluenceCount = 0;
						TotalWeight = 0.f;
						LastVertexIndex = Influences[i].VertexIndex;
					}

					// if less than min weight, or it's more than 8, then we clear it to use weight
					if (Influences[i].Weight > MINWEIGHT && InfluenceCount < MAX_TOTAL_INFLUENCES)
					{
						LastNewInfluenceIndex = NewInfluences.Add(Influences[i]);
						InfluenceCount++;
						TotalWeight += Influences[i].Weight;
					}
				}

				Influences = NewInfluences;

				// Ensure that each vertex has at least one influence as e.g. CreateSkinningStream relies on it.
				// The below code relies on influences being sorted by vertex index.
				if (Influences.Num() == 0)
				{
					// warn about no influences
					//TODO add a user log
					//UE_LOG(LogLODUtilities, Warning, TEXT("Warning skeletal mesh (%s) has no vertex influences"), *MeshName);
					// add one for each wedge entry
					Influences.AddUninitialized(WedgeCount);
					for (int32 WedgeIdx = 0; WedgeIdx < WedgeCount; WedgeIdx++)
					{
						Influences[WedgeIdx].VertexIndex = WedgeIdx;
						Influences[WedgeIdx].BoneIndex = 0;
						Influences[WedgeIdx].Weight = 1.0f;
					}
					for (int32 i = 0; i < Influences.Num(); i++)
					{
						int32 CurrentVertexIndex = Influences[i].VertexIndex;

						if (LastVertexIndex != CurrentVertexIndex)
						{
							for (int32 j = LastVertexIndex + 1; j < CurrentVertexIndex; j++)
							{
								// Add a 0-bone weight if none other present (known to happen with certain MAX skeletal setups).
								Influences.InsertUninitialized(i, 1);
								Influences[i].VertexIndex = j;
								Influences[i].BoneIndex = 0;
								Influences[i].Weight = 1.f;
							}
							LastVertexIndex = CurrentVertexIndex;
						}
					}
				}
			}


		} //Namespace Private
	} //namespace Interchange
} //namespace UE

#endif //#if WITH_EDITOR


UClass* UInterchangeSkeletalMeshFactory::GetFactoryClass() const
{
	return USkeletalMesh::StaticClass();
}

UObject* UInterchangeSkeletalMeshFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments) const
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import skeletalMesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else
	USkeletalMesh* SkeletalMesh = nullptr;
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.AssetNode);
	if (SkeletalMeshFactoryNode == nullptr)
	{
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		SkeletalMesh = NewObject<USkeletalMesh>(Arguments.Parent, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if (ExistingAsset->GetClass()->IsChildOf(USkeletalMesh::StaticClass()))
	{
		//This is a reimport, we are just re-updating the source data
		SkeletalMesh = Cast<USkeletalMesh>(ExistingAsset);
	}
	
	if (!SkeletalMesh)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create SkeletalMesh asset %s"), *Arguments.AssetName);
		return nullptr;
	}
	
	SkeletalMesh->PreEditChange(nullptr);
	//Allocate the LODImport data in the main thread
	SkeletalMesh->ReserveLODImportData(SkeletalMeshFactoryNode->GetLodDataCount());
	
	return SkeletalMesh;
#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}

UObject* UInterchangeSkeletalMeshFactory::CreateAsset(const UInterchangeSkeletalMeshFactory::FCreateAssetParams& Arguments) const
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import skeletalMesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode = Cast<UInterchangeSkeletalMeshFactoryNode>(Arguments.AssetNode);
	if (SkeletalMeshFactoryNode == nullptr)
	{
		return nullptr;
	}

	const IInterchangeSkeletalMeshPayloadInterface* SkeletalMeshTranslatorPayloadInterface = Cast<IInterchangeSkeletalMeshPayloadInterface>(Arguments.Translator);
	if (!SkeletalMeshTranslatorPayloadInterface)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import skeletalMesh, the translator do not implement the IInterchangeSkeletalMeshPayloadInterface."));
		return nullptr;
	}

	const UClass* SkeletalMeshClass = SkeletalMeshFactoryNode->GetAssetClass();
	check(SkeletalMeshClass && SkeletalMeshClass->IsChildOf(GetFactoryClass()));

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UObject* SkeletalMeshObject = nullptr;
	// create a new material or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		//NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		//The UObject should have been create by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		SkeletalMeshObject = NewObject<UObject>(Arguments.Parent, SkeletalMeshClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if(ExistingAsset->GetClass()->IsChildOf(SkeletalMeshClass))
	{
		//This is a reimport, we are just re-updating the source data
		SkeletalMeshObject = ExistingAsset;
	}

	if (!SkeletalMeshObject)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("Could not create SkeletalMesh asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	if (SkeletalMeshObject)
	{
		//Currently material re-import will not touch the material at all
		//TODO design a re-import process for the material (expressions and input connections)
		if(!Arguments.ReimportObject)
		{
			USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(SkeletalMeshObject);
			if (!ensure(SkeletalMesh))
			{
				UE_LOG(LogInterchangeImport, Error, TEXT("Could not create SkeletalMesh asset %s"), *Arguments.AssetName);
				return nullptr;
			}
			//Dirty the DDC Key for any imported Skeletal Mesh
			SkeletalMesh->InvalidateDeriveDataCacheGUID();

			FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
			if (!ensure(ImportedResource->LODModels.Num() == 0))
			{
				ImportedResource->LODModels.Empty();
			}
			USkeleton* SkeletonReference = nullptr;
			int32 LodCount = SkeletalMeshFactoryNode->GetLodDataCount();
			TArray<FString> LodDataUniqueIds;
			SkeletalMeshFactoryNode->GetLodDataUniqueIds(LodDataUniqueIds);
			ensure(LodDataUniqueIds.Num() == LodCount);
			int32 CurrentLodIndex = 0;
			for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
			{
				FString LodUniqueId = LodDataUniqueIds[LodIndex];
				const UInterchangeSkeletalMeshLodDataNode* LodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(Arguments.NodeContainer->GetNode(LodUniqueId));
				if (!LodDataNode)
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}

				//Get the mesh node context for each MeshUids
				struct FMeshNodeContext
				{
					const UInterchangeMeshNode* MeshNode = nullptr;
					const UInterchangeSceneNode* SceneNode = nullptr;
					FTransform SceneGlobalTransform = FTransform::Identity;
					FString TranslatorPayloadKey;
				};
				TArray<FMeshNodeContext> MeshReferences;
				//Scope to query the mesh node
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
							//The reference is a scene node and we need to bake the geometry
							MeshReference.SceneNode = Cast<UInterchangeSceneNode>(Arguments.NodeContainer->GetNode(MeshUid));
							if (!ensure(MeshReference.SceneNode != nullptr))
							{
								UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD mesh reference when importing SkeletalMesh asset %s"), *Arguments.AssetName);
								continue;
							}
							FString MeshDependencyUid;
							MeshReference.SceneNode->GetCustomMeshDependencyUid(MeshDependencyUid);
							MeshReference.MeshNode = Cast<UInterchangeMeshNode>(Arguments.NodeContainer->GetNode(MeshDependencyUid));
							//Cache the scene node global matrix, we will use this matrix to bake the vertices
							MeshReference.SceneNode->GetCustomGlobalTransform(MeshReference.SceneGlobalTransform);
						}
						if (!ensure(MeshReference.MeshNode != nullptr))
						{
							UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid LOD mesh reference when importing SkeletalMesh asset %s"), *Arguments.AssetName);
							continue;
						}
						TOptional<FString> MeshPayloadKey = MeshReference.MeshNode->GetPayLoadKey();
						if (MeshPayloadKey.IsSet())
						{
							MeshReference.TranslatorPayloadKey = MeshPayloadKey.GetValue();
						}
						else
						{
							UE_LOG(LogInterchangeImport, Warning, TEXT("Empty LOD mesh reference payload when importing SkeletalMesh asset %s"), *Arguments.AssetName);
							continue;
						}
						MeshReferences.Add(MeshReference);
					}
				}

				FString SkeletonNodeUid;
				if (!LodDataNode->GetCustomSkeletonUid(SkeletonNodeUid))
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}
				const UInterchangeSkeletonFactoryNode* SkeletonNode = Cast<UInterchangeSkeletonFactoryNode>(Arguments.NodeContainer->GetNode(SkeletonNodeUid));
				if (!SkeletonNode)
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}
				
				if(SkeletonReference == nullptr)
				{
					UObject* SkeletonObject = SkeletonNode->ReferenceObject.ResolveObject();
					if (SkeletonObject)
					{
						SkeletonReference = Cast<USkeleton>(SkeletonObject);
					}
					if (!ensure(SkeletonReference))
					{
						UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
						break;
					}
				}

				FString RootJointNodeId;
				if (!SkeletonNode->GetCustomRootJointUid(RootJointNodeId))
				{
					UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeleton LOD Root Joint when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}
				
				int32 SkeletonDepth = 0;
				TArray<SkeletalMeshImportData::FBone> RefBonesBinary;
				UE::Interchange::Private::ProcessImportMeshSkeleton(SkeletonReference, SkeletalMesh->GetRefSkeleton(), SkeletonDepth, Arguments.NodeContainer, RootJointNodeId, RefBonesBinary);

				//Add the lod mesh data to the skeletalmesh
				FSkeletalMeshImportData SkeletalMeshImportData;
				FMeshDescription LodMeshDescription;
				FSkeletalMeshAttributes SkeletalMeshAttributes(LodMeshDescription);
				SkeletalMeshAttributes.Register();
				FStaticMeshOperations::FAppendSettings AppendSettings;
				for (int32 ChannelIdx = 0; ChannelIdx < FStaticMeshOperations::FAppendSettings::MAX_NUM_UV_CHANNELS; ++ChannelIdx)
				{
					AppendSettings.bMergeUVChannels[ChannelIdx] = true;
				}
				//Fill the lod mesh description using all combined mesh part
				for (const FMeshNodeContext& MeshNodeContext : MeshReferences)
				{
					TOptional<UE::Interchange::FSkeletalMeshLodPayloadData> LodMeshPayload = SkeletalMeshTranslatorPayloadInterface->GetSkeletalMeshLodPayloadData(MeshNodeContext.TranslatorPayloadKey);
					if (!LodMeshPayload.IsSet())
					{
						UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeletal mesh payload key [%s] SkeletalMesh asset %s"), *MeshNodeContext.TranslatorPayloadKey, *Arguments.AssetName);
						continue;
					}
					const int32 VertexOffset = LodMeshDescription.Vertices().Num();
					const int32 VertexInstanceOffset = LodMeshDescription.VertexInstances().Num();
					const int32 TriangleOffset = LodMeshDescription.Triangles().Num();
					
					FSkeletalMeshOperations::FSkeletalMeshAppendSettings SkeletalMeshAppendSettings;
					SkeletalMeshAppendSettings.SourceVertexIDOffset = VertexOffset;
					FElementIDRemappings ElementIDRemappings;
					LodMeshPayload->LodMeshDescription.Compact(ElementIDRemappings);
					//Remap the influence vertex index to point on the correct index
					if (LodMeshPayload->JointNames.Num() > 0)
					{
						const int32 LocalJointCount = LodMeshPayload->JointNames.Num();
						const int32 RefBoneCount = RefBonesBinary.Num();
						SkeletalMeshAppendSettings.SourceRemapBoneIndex.AddZeroed(LocalJointCount);
						for(int32 LocalJointIndex = 0; LocalJointIndex < LocalJointCount; ++LocalJointIndex)
						{
							SkeletalMeshAppendSettings.SourceRemapBoneIndex[LocalJointIndex] = LocalJointIndex;
							const FString& LocalJointName = LodMeshPayload->JointNames[LocalJointIndex];
							for(int32 RefBoneIndex = 0; RefBoneIndex < RefBoneCount; ++RefBoneIndex)
							{
								const SkeletalMeshImportData::FBone& Bone = RefBonesBinary[RefBoneIndex];
								if(Bone.Name.Equals(LocalJointName))
								{
									SkeletalMeshAppendSettings.SourceRemapBoneIndex[LocalJointIndex] = RefBoneIndex;
									break;
								}
							}
						}
					}

					FStaticMeshOperations::AppendMeshDescription(LodMeshPayload->LodMeshDescription, LodMeshDescription, AppendSettings);
					FSkeletalMeshOperations::AppendSkinWeight(LodMeshPayload->LodMeshDescription, LodMeshDescription, SkeletalMeshAppendSettings);

					/*
					TArray<FString> BlendShapeToImport;
					MeshNodeContext.MeshNode->GetShapeDependencies(BlendShapeToImport);
					for(const FString)
					//Copy also the blend shapes data (we need to pass the offset in the Main MeshDescription, so we have the correct vertex index mapping)
					UE::Interchange::Private::CopyBlendShapesMeshDescriptionToSkeletalMeshImportData(BlendShapeToImport, LodMeshPayload->LodBlendShapeMeshDescriptions, SkeletalMeshImportData);
					*/
					
				}

				SkeletalMeshImportData = FSkeletalMeshImportData::CreateFromMeshDescription(LodMeshDescription);
				SkeletalMeshImportData.RefBonesBinary = RefBonesBinary;
/*

				//Use the first geo LOD since we are suppose to have only one Key
				if(TranslatorMeshKeys.IsValidIndex(0))
				{
					int32 MeshLodIndex = 0;

					TOptional<UE::Interchange::FSkeletalMeshLodPayloadData> LodMeshPayload = SkeletalMeshTranslatorPayloadInterface->GetSkeletalMeshLodPayloadData(TranslatorMeshKeys[MeshLodIndex]);
					if (!LodMeshPayload.IsSet())
					{
						UE_LOG(LogInterchangeImport, Warning, TEXT("Invalid Skeletal mesh payload key [%s] SkeletalMesh asset %s"), *TranslatorMeshKeys[MeshLodIndex], *Arguments.AssetName);
						break;
					}
					//TODO use the mesh description as the source import data for skeletalmesh
					//Convert the meshdescription to skeletalmesh import data and set the data for the LOD, so the build will work
					//Make sure all IDs are compact so we can use them directly when copying the data
					FElementIDRemappings ElementIDRemappings;
					LodMeshPayload->LodMeshDescription.Compact(ElementIDRemappings);
					UE::Interchange::Private::CopyMeshDescriptionToSkeletalMeshImportData(LodMeshPayload->LodMeshDescription, SkeletalMeshImportData);
					SkeletalMeshImportData.RefBonesBinary = RefBonesBinary;

					//Pipeline can remove names from the list to control which blendshapes they import
					TArray<FString> BlendShapeToImport;
					LodDataNode->GetBlendShapes(BlendShapeToImport);
					//Copy also the blend shapes data
					UE::Interchange::Private::CopyBlendShapesMeshDescriptionToSkeletalMeshImportData(BlendShapeToImport, LodMeshPayload->LodBlendShapeMeshDescriptions, SkeletalMeshImportData);
				}
*/

				ensure(ImportedResource->LODModels.Add(new FSkeletalMeshLODModel()) == CurrentLodIndex);
				FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[CurrentLodIndex];

				TMap<FString, UMaterialInterface*> AvailableMaterials;
				TArray<FString> FactoryDependencies;
				SkeletalMeshFactoryNode->GetFactoryDependencies(FactoryDependencies);
				for (int32 DependencyIndex = 0; DependencyIndex < FactoryDependencies.Num(); ++DependencyIndex)
				{
					const UInterchangeMaterialFactoryNode* MaterialFactoryNode = Cast<UInterchangeMaterialFactoryNode>(Arguments.NodeContainer->GetNode(FactoryDependencies[DependencyIndex]));
					if (!MaterialFactoryNode || !MaterialFactoryNode->ReferenceObject.IsValid())
					{
						continue;
					}
					UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(MaterialFactoryNode->ReferenceObject.ResolveObject());
					if (!MaterialInterface)
					{
						continue;
					}
					AvailableMaterials.Add(MaterialFactoryNode->GetDisplayLabel(), MaterialInterface);
				}

				UE::Interchange::Private::ProcessImportMeshMaterials(SkeletalMesh->GetMaterials(), SkeletalMeshImportData, AvailableMaterials);
				UE::Interchange::Private::ProcessImportMeshInfluences(SkeletalMeshImportData.Wedges.Num(), SkeletalMeshImportData.Influences);
				//Store the original fbx import data the SkelMeshImportDataPtr should not be modified after this
				SkeletalMesh->SaveLODImportedData(CurrentLodIndex, SkeletalMeshImportData);
				//We reimport both
				SkeletalMesh->SetLODImportedDataVersions(CurrentLodIndex, ESkeletalMeshGeoImportVersions::LatestVersion, ESkeletalMeshSkinningImportVersions::LatestVersion);
				FSkeletalMeshLODInfo& NewLODInfo = SkeletalMesh->AddLODInfo();
				NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
				NewLODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
				NewLODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
				NewLODInfo.LODHysteresis = 0.02f;
				NewLODInfo.bImportWithBaseMesh = true;

				//Add the bound to the skeletal mesh
				if (SkeletalMesh->GetImportedBounds().BoxExtent.IsNearlyZero())
				{
					FBox BoundingBox(SkeletalMeshImportData.Points.GetData(), SkeletalMeshImportData.Points.Num());
					const FVector BoundingBoxSize = BoundingBox.GetSize();

					if (SkeletalMeshImportData.Points.Num() > 2 && BoundingBoxSize.X < THRESH_POINTS_ARE_SAME && BoundingBoxSize.Y < THRESH_POINTS_ARE_SAME && BoundingBoxSize.Z < THRESH_POINTS_ARE_SAME)
					{
						//TODO log a user error
						//AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("FbxSkeletaLMeshimport_ErrorMeshTooSmall", "Cannot import this mesh, the bounding box of this mesh is smaller than the supported threshold[{0}]."), FText::FromString(FString::Printf(TEXT("%f"), THRESH_POINTS_ARE_SAME)))), FFbxErrors::SkeletalMesh_FillImportDataFailed);
					}
					SkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));
				}

				CurrentLodIndex++;
			}

			SkeletonReference->MergeAllBonesToBoneTree(SkeletalMesh);
			if (SkeletalMesh->GetSkeleton() != SkeletonReference)
			{
				SkeletalMesh->SetSkeleton(SkeletonReference);
			}

			SkeletalMesh->CalculateInvRefMatrices();

			/** Apply all SkeletalMeshFactoryNode custom attributes to the material asset */
			SkeletalMeshFactoryNode->ApplyAllCustomAttributeToAsset(SkeletalMesh);
		}
		
		//Getting the file Hash will cache it into the source data
		Arguments.SourceData->GetFileContentHash();

		//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all material in parallel
	}
	else
	{
		//The material is not a UMaterialInterface
		SkeletalMeshObject->RemoveFromRoot();
		SkeletalMeshObject->MarkPendingKill();
	}
	return SkeletalMeshObject;

#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}

/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
void UInterchangeSkeletalMeshFactory::PostImportGameThreadCallback(const FPostImportGameThreadCallbackParams& Arguments) const
{
	check(IsInGameThread());
	Super::PostImportGameThreadCallback(Arguments);

	//TODO make sure this work at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		//We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Arguments.ImportedObject);

		UAssetImportData* ImportDataPtr = SkeletalMesh->GetAssetImportData();
		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(SkeletalMesh
																										  , ImportDataPtr
																										  , Arguments.SourceData
																										  , Arguments.NodeUniqueID
																										  , Arguments.NodeContainer);

		ImportDataPtr = UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
		SkeletalMesh->SetAssetImportData(ImportDataPtr);
	}
#endif
}