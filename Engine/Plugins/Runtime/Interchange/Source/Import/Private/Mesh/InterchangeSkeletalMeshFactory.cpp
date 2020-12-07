// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Mesh/InterchangeSkeletalMeshFactory.h"

#include "Components.h"
#include "Engine/SkeletalMesh.h"
#include "GPUSkinPublicDefs.h"
#include "InterchangeImportCommon.h"
#include "InterchangeJointNode.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletalMeshNode.h"
#include "InterchangeSkeletonNode.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "LogInterchangeImportPlugin.h"
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
				FName Name;
				int32 ParentIndex;  // 0 if this is the root bone.  
				FTransform	LocalTransform; // local transform
			};

			void RecursiveAddBones(const UInterchangeBaseNodeContainer* NodeContainer, FName JointNodeId, TArray <FJointInfo>& JointInfos, int32 ParentIndex, TArray<SkeletalMeshImportData::FBone>& RefBonesBinary)
			{
				const UInterchangeJointNode* JointNode = Cast<UInterchangeJointNode>(NodeContainer->GetNode(JointNodeId));
				if (!JointNode)
				{
					UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Invalid Skeleton Joint"));
					return;
				}

				int32 JointInfoIndex = JointInfos.Num();
				FJointInfo& Info = JointInfos.AddZeroed_GetRef();
				ensure(JointNode->GetCustomName(Info.Name));
				ensure(JointNode->GetCustomLocalTransform(Info.LocalTransform));
				Info.ParentIndex = ParentIndex;

				SkeletalMeshImportData::FBone& Bone = RefBonesBinary.AddZeroed_GetRef();
				Bone.Name = Info.Name.ToString();
				Bone.BonePos.Transform = Info.LocalTransform;
				Bone.ParentIndex = ParentIndex;
				//Fill the scrap we do not need
				Bone.BonePos.Length = 0.0f;
				Bone.BonePos.XSize = 1.0f;
				Bone.BonePos.YSize = 1.0f;
				Bone.BonePos.ZSize = 1.0f;
				
				const TArray<FName> ChildrenIds = NodeContainer->GetNodeChildrenUIDs(JointNodeId);
				Bone.NumChildren = ChildrenIds.Num();
				for (int32 ChildIndex = 0; ChildIndex < ChildrenIds.Num(); ++ChildIndex)
				{
					RecursiveAddBones(NodeContainer, ChildrenIds[ChildIndex], JointInfos, JointInfoIndex, RefBonesBinary);
				}
			}

			bool ProcessImportMeshSkeleton(const USkeleton* SkeletonAsset, FReferenceSkeleton& RefSkeleton, int32& SkeletalDepth, const UInterchangeBaseNodeContainer* NodeContainer, FName RootJointNodeId, TArray<SkeletalMeshImportData::FBone>& RefBonesBinary)
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

					const FString BoneName = FixupBoneName(BinaryBone.Name.ToString());
					const FMeshBoneInfo BoneInfo(FName(*BoneName, FNAME_Add), BinaryBone.Name.ToString(), BinaryBone.ParentIndex);
					const FTransform BoneTransform(BinaryBone.LocalTransform);
					if (RefSkeleton.FindRawBoneIndex(BoneInfo.Name) != INDEX_NONE)
					{
						UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Invalid Skeleton because of non-unique bone names [%s]"), *BoneInfo.Name.ToString());
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

			void CleanUpUnusedMaterials(FSkeletalMeshImportData& ImportData)
			{
				if (ImportData.Materials.Num() <= 0)
				{
					return;
				}

				TArray< SkeletalMeshImportData::FMaterial > ExistingMatList = ImportData.Materials;

				TArray<uint8> UsedMaterialIndex;
				// Find all material that are use by the mesh faces
				int32 FaceNum = ImportData.Faces.Num();
				for (int32 TriangleIndex = 0; TriangleIndex < FaceNum; TriangleIndex++)
				{
					SkeletalMeshImportData::FTriangle& Triangle = ImportData.Faces[TriangleIndex];
					UsedMaterialIndex.AddUnique(Triangle.MatIndex);
				}
				//Remove any unused material.
				if (UsedMaterialIndex.Num() < ExistingMatList.Num())
				{
					TArray<int32> RemapIndex;
					TArray< SkeletalMeshImportData::FMaterial >& NewMatList = ImportData.Materials;
					NewMatList.Empty();
					for (int32 ExistingMatIndex = 0; ExistingMatIndex < ExistingMatList.Num(); ++ExistingMatIndex)
					{
						if (UsedMaterialIndex.Contains((uint8)ExistingMatIndex))
						{
							RemapIndex.Add(NewMatList.Add(ExistingMatList[ExistingMatIndex]));
						}
						else
						{
							RemapIndex.Add(INDEX_NONE);
						}
					}
					ImportData.MaxMaterialIndex = 0;
					//Remap the face material index
					for (int32 TriangleIndex = 0; TriangleIndex < FaceNum; TriangleIndex++)
					{
						SkeletalMeshImportData::FTriangle& Triangle = ImportData.Faces[TriangleIndex];
						check(RemapIndex[Triangle.MatIndex] != INDEX_NONE);
						Triangle.MatIndex = RemapIndex[Triangle.MatIndex];
						ImportData.MaxMaterialIndex = FMath::Max<uint32>(ImportData.MaxMaterialIndex, Triangle.MatIndex);
					}
				}
			}

			void SetMaterialSkinXXOrder(FSkeletalMeshImportData& ImportData)
			{
				TArray<int32> MaterialIndexToSkinIndex;
				TMap<int32, int32> SkinIndexToMaterialIndex;
				TArray<int32> MissingSkinSuffixMaterial;
				TMap<int32, int32> SkinIndexGreaterThenMaterialArraySize;
				{
					int32 MaterialCount = ImportData.Materials.Num();

					bool bNeedsReorder = false;
					for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
					{
						// get skin index
						FString MatName = ImportData.Materials[MaterialIndex].MaterialImportName;

						if (MatName.Len() > 6)
						{
							int32 Offset = MatName.Find(TEXT("_SKIN"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
							if (Offset != INDEX_NONE)
							{
								// Chop off the material name so we are left with the number in _SKINXX
								FString SkinXXNumber = MatName.Right(MatName.Len() - (Offset + 1)).RightChop(4);

								if (SkinXXNumber.IsNumeric())
								{
									bNeedsReorder = true;
									int32 TmpIndex = FPlatformString::Atoi(*SkinXXNumber);
									if (TmpIndex < MaterialCount)
									{
										SkinIndexToMaterialIndex.Add(TmpIndex, MaterialIndex);
									}
									else
									{
										SkinIndexGreaterThenMaterialArraySize.Add(TmpIndex, MaterialIndex);
									}

								}
							}
							else
							{
								MissingSkinSuffixMaterial.Add(MaterialIndex);
							}
						}
						else
						{
							MissingSkinSuffixMaterial.Add(MaterialIndex);
						}
					}

					if (bNeedsReorder && MissingSkinSuffixMaterial.Num() > 0)
					{
						//TODO log a user warning message
						//AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, LOCTEXT("FbxSkeletaLMeshimport_Skinxx_missing", "Cannot mix skinxx suffix materials with no skinxx material, mesh section order will not be right.")), FFbxErrors::Generic_Mesh_SkinxxNameError);
						return;
					}

					//Add greater then material array skinxx at the end sorted by integer the index will be remap correctly in the case of a LOD import
					if (SkinIndexGreaterThenMaterialArraySize.Num() > 0)
					{
						int32 MaxAvailableKey = SkinIndexToMaterialIndex.Num();
						for (int32 AvailableKey = 0; AvailableKey < MaxAvailableKey; ++AvailableKey)
						{
							if (SkinIndexToMaterialIndex.Contains(AvailableKey))
								continue;

							TMap<int32, int32> TempSkinIndexToMaterialIndex;
							for (auto KvpSkinToMat : SkinIndexToMaterialIndex)
							{
								if (KvpSkinToMat.Key > AvailableKey)
								{
									TempSkinIndexToMaterialIndex.Add(KvpSkinToMat.Key - 1, KvpSkinToMat.Value);
								}
								else
								{
									TempSkinIndexToMaterialIndex.Add(KvpSkinToMat.Key, KvpSkinToMat.Value);
								}
							}
							//move all the later key of the array to fill the available index
							SkinIndexToMaterialIndex = TempSkinIndexToMaterialIndex;
							AvailableKey--; //We need to retest the same index it can be empty
						}
						//Reorder the array
						SkinIndexGreaterThenMaterialArraySize.KeySort(TLess<int32>());
						for (auto Kvp : SkinIndexGreaterThenMaterialArraySize)
						{
							SkinIndexToMaterialIndex.Add(SkinIndexToMaterialIndex.Num(), Kvp.Value);
						}
					}

					//Fill the array MaterialIndexToSkinIndex so we order material by _skinXX order
					//This ensure we support skinxx suffixe that are not increment by one like _skin00, skin_01, skin_03, skin_04, skin_08... 
					for (auto kvp : SkinIndexToMaterialIndex)
					{
						int32 MatIndexToInsert = 0;
						for (MatIndexToInsert = 0; MatIndexToInsert < MaterialIndexToSkinIndex.Num(); ++MatIndexToInsert)
						{
							if (*(SkinIndexToMaterialIndex.Find(MaterialIndexToSkinIndex[MatIndexToInsert])) >= kvp.Value)
							{
								break;
							}
						}
						MaterialIndexToSkinIndex.Insert(kvp.Key, MatIndexToInsert);
					}

					if (bNeedsReorder)
					{
						// re-order the materials
						TArray< SkeletalMeshImportData::FMaterial > ExistingMatList = ImportData.Materials;
						for (int32 MissingIndex : MissingSkinSuffixMaterial)
						{
							MaterialIndexToSkinIndex.Insert(MaterialIndexToSkinIndex.Num(), MissingIndex);
						}
						for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
						{
							if (MaterialIndex < MaterialIndexToSkinIndex.Num())
							{
								int32 NewIndex = MaterialIndexToSkinIndex[MaterialIndex];
								if (ExistingMatList.IsValidIndex(NewIndex))
								{
									ImportData.Materials[NewIndex] = ExistingMatList[MaterialIndex];
								}
							}
						}

						// remapping the material index for each triangle
						int32 FaceNum = ImportData.Faces.Num();
						for (int32 TriangleIndex = 0; TriangleIndex < FaceNum; TriangleIndex++)
						{
							SkeletalMeshImportData::FTriangle& Triangle = ImportData.Faces[TriangleIndex];
							if (Triangle.MatIndex < MaterialIndexToSkinIndex.Num())
							{
								Triangle.MatIndex = MaterialIndexToSkinIndex[Triangle.MatIndex];
							}
						}
					}
				}
			}

			namespace SmoothGroupHelper
			{
				struct tFaceRecord
				{
					int32 FaceIndex;
					int32 HoekIndex;
					int32 WedgeIndex;
					uint32 SmoothFlags;
					uint32 FanFlags;
				};

				struct VertsFans
				{
					TArray<tFaceRecord> FaceRecord;
					int32 FanGroupCount;
				};

				struct tInfluences
				{
					TArray<int32> RawInfIndices;
				};

				struct tWedgeList
				{
					TArray<int32> WedgeList;
				};

				struct tFaceSet
				{
					TArray<int32> Faces;
				};

				// Check whether faces have at least two vertices in common. These must be POINTS - don't care about wedges.
				bool FacesAreSmoothlyConnected(FSkeletalMeshImportData& ImportData, int32 Face1, int32 Face2)
				{

					//if( ( Face1 >= Thing->SkinData.Faces.Num()) || ( Face2 >= Thing->SkinData.Faces.Num()) ) return false;

					if (Face1 == Face2)
					{
						return true;
					}

					// Smoothing groups match at least one bit in binary AND ?
					if ((ImportData.Faces[Face1].SmoothingGroups & ImportData.Faces[Face2].SmoothingGroups) == 0)
					{
						return false;
					}

					int32 VertMatches = 0;
					for (int32 i = 0; i < 3; i++)
					{
						int32 Point1 = ImportData.Wedges[ImportData.Faces[Face1].WedgeIndex[i]].VertexIndex;

						for (int32 j = 0; j < 3; j++)
						{
							int32 Point2 = ImportData.Wedges[ImportData.Faces[Face2].WedgeIndex[j]].VertexIndex;
							if (Point2 == Point1)
							{
								VertMatches++;
							}
						}
					}

					return (VertMatches >= 2);
				}
			} //namespace SmoothGroupHelper
			int32 DoUnSmoothVerts(FSkeletalMeshImportData& ImportData)
			{
				//
				// Connectivity: triangles with non-matching smoothing groups will be physically split.
				//
				// -> Splitting involves: the UV+material-contaning vertex AND the 3d point.
				//
				// -> Tally smoothing groups for each and every (textured) vertex.
				//
				// -> Collapse: 
				// -> start from a vertex and all its adjacent triangles - go over
				// each triangle - if any connecting one (sharing more than one vertex) gives a smoothing match,
				// accumulate it. Then IF more than one resulting section, 
				// ensure each boundary 'vert' is split _if not already_ to give each smoothing group
				// independence from all others.
				//

				int32 DuplicatedVertCount = 0;
				int32 RemappedHoeks = 0;

				int32 TotalSmoothMatches = 0;
				int32 TotalConnexChex = 0;

				// Link _all_ faces to vertices.	
				TArray<SmoothGroupHelper::VertsFans>  Fans;
				TArray<SmoothGroupHelper::tInfluences> PointInfluences;
				TArray<SmoothGroupHelper::tWedgeList>  PointWedges;

				Fans.AddZeroed(ImportData.Points.Num());//Fans.AddExactZeroed(			Thing->SkinData.Points.Num() );
				PointInfluences.AddZeroed(ImportData.Points.Num());//PointInfluences.AddExactZeroed( Thing->SkinData.Points.Num() );
				PointWedges.AddZeroed(ImportData.Points.Num());//PointWedges.AddExactZeroed(	 Thing->SkinData.Points.Num() );

				// Existing points map 1:1
				ImportData.PointToRawMap.AddUninitialized(ImportData.Points.Num());
				for (int32 i = 0; i < ImportData.Points.Num(); i++)
				{
					ImportData.PointToRawMap[i] = i;
				}

				for (int32 i = 0; i < ImportData.Influences.Num(); i++)
				{
					if (PointInfluences.Num() <= ImportData.Influences[i].VertexIndex)
					{
						PointInfluences.AddZeroed(ImportData.Influences[i].VertexIndex - PointInfluences.Num() + 1);
					}
					PointInfluences[ImportData.Influences[i].VertexIndex].RawInfIndices.Add(i);
				}

				for (int32 i = 0; i < ImportData.Wedges.Num(); i++)
				{
					if (uint32(PointWedges.Num()) <= ImportData.Wedges[i].VertexIndex)
					{
						PointWedges.AddZeroed(ImportData.Wedges[i].VertexIndex - PointWedges.Num() + 1);
					}

					PointWedges[ImportData.Wedges[i].VertexIndex].WedgeList.Add(i);
				}

				for (int32 f = 0; f < ImportData.Faces.Num(); f++)
				{
					// For each face, add a pointer to that face into the Fans[vertex].
					for (int32 i = 0; i < 3; i++)
					{
						int32 WedgeIndex = ImportData.Faces[f].WedgeIndex[i];
						int32 PointIndex = ImportData.Wedges[WedgeIndex].VertexIndex;
						SmoothGroupHelper::tFaceRecord NewFR;

						NewFR.FaceIndex = f;
						NewFR.HoekIndex = i;
						NewFR.WedgeIndex = WedgeIndex; // This face touches the point courtesy of Wedges[Wedgeindex].
						NewFR.SmoothFlags = ImportData.Faces[f].SmoothingGroups;
						NewFR.FanFlags = 0;
						Fans[PointIndex].FaceRecord.Add(NewFR);
						Fans[PointIndex].FanGroupCount = 0;
					}
				}

				// Investigate connectivity and assign common group numbers (1..+) to the fans' individual FanFlags.
				for (int32 p = 0; p < Fans.Num(); p++) // The fan of faces for each 3d point 'p'.
				{
					// All faces connecting.
					if (Fans[p].FaceRecord.Num() > 0)
					{
						int32 FacesProcessed = 0;
						TArray<SmoothGroupHelper::tFaceSet> FaceSets; // Sets with indices INTO FANS, not into face array.			

						// Digest all faces connected to this vertex (p) into one or more smooth sets. only need to check 
						// all faces MINUS one..
						while (FacesProcessed < Fans[p].FaceRecord.Num())
						{
							// One loop per group. For the current ThisFaceIndex, tally all truly connected ones
							// and put them in a new TArray. Once no more can be connected, stop.

							int32 NewSetIndex = FaceSets.Num(); // 0 to start
							FaceSets.AddZeroed(1);						// first one will be just ThisFaceIndex.

							// Find the first non-processed face. There will be at least one.
							int32 ThisFaceFanIndex = 0;
							{
								int32 SearchIndex = 0;
								while (Fans[p].FaceRecord[SearchIndex].FanFlags == -1) // -1 indicates already  processed. 
								{
									SearchIndex++;
								}
								ThisFaceFanIndex = SearchIndex; //Fans[p].FaceRecord[SearchIndex].FaceIndex; 
							}

							// Initial face.
							FaceSets[NewSetIndex].Faces.Add(ThisFaceFanIndex);   // Add the unprocessed Face index to the "local smoothing group" [NewSetIndex].
							Fans[p].FaceRecord[ThisFaceFanIndex].FanFlags = -1;			  // Mark as processed.
							FacesProcessed++;

							// Find all faces connected to this face, and if there's any
							// smoothing group matches, put it in current face set and mark it as processed;
							// until no more match. 
							int32 NewMatches = 0;
							do
							{
								NewMatches = 0;
								// Go over all current faces in this faceset and set if the FaceRecord (local smoothing groups) has any matches.
								// there will be at least one face already in this faceset - the first face in the fan.
								for (int32 n = 0; n < FaceSets[NewSetIndex].Faces.Num(); n++)
								{
									int32 HookFaceIdx = Fans[p].FaceRecord[FaceSets[NewSetIndex].Faces[n]].FaceIndex;

									//Go over the fan looking for matches.
									for (int32 s = 0; s < Fans[p].FaceRecord.Num(); s++)
									{
										// Skip if same face, skip if face already processed.
										if ((HookFaceIdx != Fans[p].FaceRecord[s].FaceIndex) && (Fans[p].FaceRecord[s].FanFlags != -1))
										{
											TotalConnexChex++;
											// Process if connected with more than one vertex, AND smooth..
											if (SmoothGroupHelper::FacesAreSmoothlyConnected(ImportData, HookFaceIdx, Fans[p].FaceRecord[s].FaceIndex))
											{
												TotalSmoothMatches++;
												Fans[p].FaceRecord[s].FanFlags = -1; // Mark as processed.
												FacesProcessed++;
												// Add 
												FaceSets[NewSetIndex].Faces.Add(s); // Store FAN index of this face index into smoothing group's faces. 
												// Tally
												NewMatches++;
											}
										} // not the same...
									}// all faces in fan
								} // all faces in FaceSet
							} while (NewMatches);

						}// Repeat until all faces processed.

						// For the new non-initialized  face sets, 
						// Create a new point, influences, and uv-vertex(-ices) for all individual FanFlag groups with an index of 2+ and also remap
						// the face's vertex into those new ones.
						if (FaceSets.Num() > 1)
						{
							for (int32 f = 1; f < FaceSets.Num(); f++)
							{
								check(ImportData.Points.Num() == ImportData.PointToRawMap.Num());

								// We duplicate the current vertex. (3d point)
								int32 NewPointIndex = ImportData.Points.Num();
								ImportData.Points.AddUninitialized();
								ImportData.Points[NewPointIndex] = ImportData.Points[p];

								ImportData.PointToRawMap.AddUninitialized();
								ImportData.PointToRawMap[NewPointIndex] = p;

								DuplicatedVertCount++;

								// Duplicate all related weights.
								for (int32 t = 0; t < PointInfluences[p].RawInfIndices.Num(); t++)
								{
									// Add new weight
									int32 NewWeightIndex = ImportData.Influences.Num();
									ImportData.Influences.AddUninitialized();
									ImportData.Influences[NewWeightIndex] = ImportData.Influences[PointInfluences[p].RawInfIndices[t]];
									ImportData.Influences[NewWeightIndex].VertexIndex = NewPointIndex;
								}

								// Duplicate any and all Wedges associated with it; and all Faces' wedges involved.					
								for (int32 w = 0; w < PointWedges[p].WedgeList.Num(); w++)
								{
									int32 OldWedgeIndex = PointWedges[p].WedgeList[w];
									int32 NewWedgeIndex = ImportData.Wedges.Num();
									ImportData.Wedges[OldWedgeIndex].VertexIndex = NewPointIndex;
								}
							}
						} //  if FaceSets.Num(). -> duplicate stuff
					}//	while( FacesProcessed < Fans[p].FaceRecord.Num() )
				} // Fans for each 3d point

				if (!ensure(ImportData.Points.Num() == ImportData.PointToRawMap.Num()))
				{
					//TODO log a warning to the user

					//Create a valid PointtoRawMap but with bad content
					int32 PointNum = ImportData.Points.Num();
					ImportData.PointToRawMap.Empty(PointNum);
					for (int32 PointIndex = 0; PointIndex < PointNum; ++PointIndex)
					{
						ImportData.PointToRawMap[PointIndex] = PointIndex;
					}
				}

				return DuplicatedVertCount;
			}

			/**
			 * Make sure the SourceMeshDescription is compact before calling this function, we assume all element ID are continuous so we can use them as array index
			 */
			bool CopyMeshDescriptionToSkeletalMeshImportData(const FMeshDescription& SourceMeshDescription, FSkeletalMeshImportData& DestinationSkeletalMeshImportData)
			{
				DestinationSkeletalMeshImportData.Empty();

				FSkeletalMeshConstAttributes Attributes(SourceMeshDescription);
				TVertexAttributesConstRef<FVector> VertexPositions = Attributes.GetVertexPositions();
				TVertexAttributesConstRef<TArrayAttribute<int32>> VertexInfluenceBones = Attributes.GetVertexInfluenceBones();
				TVertexAttributesConstRef<TArrayAttribute<float>> VertexInfluenceWeights = Attributes.GetVertexInfluenceWeights();

				TVertexInstanceAttributesConstRef<FVertexID> VertexInstanceVertexIndices = Attributes.GetVertexInstanceVertexIndices();
				TVertexInstanceAttributesConstRef<FVector2D> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
				TVertexInstanceAttributesConstRef<FVector> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
				TVertexInstanceAttributesConstRef<FVector> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
				TVertexInstanceAttributesConstRef<float> VertexInstanceBiNormalSigns = Attributes.GetVertexInstanceBinormalSigns();
				TVertexInstanceAttributesConstRef<FVector4> VertexInstanceColors = Attributes.GetVertexInstanceColors();

				TPolygonGroupAttributesConstRef<FName> PolygonGroupMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
				//Get the per face smoothing
				TArray<uint32> FaceSmoothingMasks;
				FaceSmoothingMasks.AddZeroed(SourceMeshDescription.Triangles().Num());
				FSkeletalMeshOperations::ConvertHardEdgesToSmoothGroup(SourceMeshDescription, FaceSmoothingMasks);

				//////////////////////////////////////////////////////////////////////////
				// Copy the materials
				DestinationSkeletalMeshImportData.Materials.Reserve(SourceMeshDescription.PolygonGroups().Num());
				for (FPolygonGroupID PolygonGroupID : SourceMeshDescription.PolygonGroups().GetElementIDs())
				{
					SkeletalMeshImportData::FMaterial Material;
					Material.MaterialImportName = PolygonGroupMaterialSlotNames[PolygonGroupID].ToString();
					//The material interface will be added later by the factory
					DestinationSkeletalMeshImportData.Materials.Add(Material);
				}
				DestinationSkeletalMeshImportData.MaxMaterialIndex = DestinationSkeletalMeshImportData.Materials.Num()-1;

				//////////////////////////////////////////////////////////////////////////
				//Copy the vertex positions and the influences

				//Reserve the point and influences
				DestinationSkeletalMeshImportData.Points.AddZeroed(SourceMeshDescription.Vertices().Num());
				DestinationSkeletalMeshImportData.Influences.Reserve(SourceMeshDescription.Vertices().Num() * 4);
				
				for (FVertexID VertexID : SourceMeshDescription.Vertices().GetElementIDs())
				{
					//We can use GetValue because the Meshdescription was compacted before the copy
					DestinationSkeletalMeshImportData.Points[VertexID.GetValue()] = VertexPositions[VertexID];
					int32 InfluenceCount = VertexInfluenceBones[VertexID].Num();
					int32 InfluenceOffsetIndex = DestinationSkeletalMeshImportData.Influences.Num();
					DestinationSkeletalMeshImportData.Influences.AddDefaulted(InfluenceCount);
					for (int32 InfluenceIndex = 0; InfluenceIndex < InfluenceCount; ++InfluenceIndex)
					{
						SkeletalMeshImportData::FRawBoneInfluence& BoneInfluence = DestinationSkeletalMeshImportData.Influences[InfluenceOffsetIndex + InfluenceIndex];
						BoneInfluence.VertexIndex = VertexID.GetValue();
						BoneInfluence.BoneIndex = VertexInfluenceBones[VertexID][InfluenceIndex];
						BoneInfluence.Weight = VertexInfluenceWeights[VertexID][InfluenceIndex];
					}
				}

				//////////////////////////////////////////////////////////////////////////
				//Copy the triangle and vertex instances
				DestinationSkeletalMeshImportData.Faces.AddZeroed(SourceMeshDescription.Triangles().Num());
				DestinationSkeletalMeshImportData.Wedges.Reserve(SourceMeshDescription.VertexInstances().Num());
				DestinationSkeletalMeshImportData.NumTexCoords = FMath::Min<int32>(VertexInstanceUVs.GetNumChannels(), (int32)MAX_TEXCOORDS);
				for (FTriangleID TriangleID : SourceMeshDescription.Triangles().GetElementIDs())
				{
					FPolygonGroupID PolygonGroupID = SourceMeshDescription.GetTrianglePolygonGroup(TriangleID);
					TArrayView<const FVertexInstanceID> VertexInstances = SourceMeshDescription.GetTriangleVertexInstances(TriangleID);
					int32 FaceIndex = TriangleID.GetValue();
					if (!ensure(DestinationSkeletalMeshImportData.Faces.IsValidIndex(FaceIndex)))
					{
						//TODO log an error for the user
						break;
					}
					SkeletalMeshImportData::FTriangle& Face = DestinationSkeletalMeshImportData.Faces[FaceIndex];
					Face.MatIndex = PolygonGroupID.GetValue();
					Face.SmoothingGroups = 0;
					if (FaceSmoothingMasks.IsValidIndex(FaceIndex))
					{
						Face.SmoothingGroups = FaceSmoothingMasks[FaceIndex];
					}
					//Create the wedges
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						FVertexInstanceID VertexInstanceID = VertexInstances[Corner];
						SkeletalMeshImportData::FVertex Wedge;
						Wedge.VertexIndex = (uint32)SourceMeshDescription.GetVertexInstanceVertex(VertexInstances[Corner]).GetValue();
						Wedge.MatIndex = Face.MatIndex;
						const bool bSRGB = false; //avoid linear to srgb conversion
						Wedge.Color = FLinearColor(VertexInstanceColors[VertexInstanceID]).ToFColor(bSRGB);
						for (int32 UVChannelIndex = 0; UVChannelIndex < (int32)(DestinationSkeletalMeshImportData.NumTexCoords); ++UVChannelIndex)
						{
							Wedge.UVs[UVChannelIndex] = VertexInstanceUVs.Get(VertexInstanceID, UVChannelIndex);
						}
						Face.TangentX[Corner] = VertexInstanceTangents[VertexInstanceID];
						Face.TangentZ[Corner] = VertexInstanceNormals[VertexInstanceID];
						Face.TangentY[Corner] = FVector::CrossProduct(VertexInstanceNormals[VertexInstanceID], VertexInstanceTangents[VertexInstanceID]).GetSafeNormal() * VertexInstanceBiNormalSigns[VertexInstanceID];

						Face.WedgeIndex[Corner] = DestinationSkeletalMeshImportData.Wedges.Add(Wedge);
					}
				}

				CleanUpUnusedMaterials(DestinationSkeletalMeshImportData);
				// reorder material according to "SKinXX" in material name
				SetMaterialSkinXXOrder(DestinationSkeletalMeshImportData);
				DoUnSmoothVerts(DestinationSkeletalMeshImportData);

				return true;
			}

			/**
			 * Fill the Materials array using the raw skeletalmesh geometry data (using material imported name)
			 * Find the material from the dependencies of the skeletalmesh before searching in all package.
			 */
			void ProcessImportMeshMaterials(TArray<FSkeletalMaterial>& Materials, FSkeletalMeshImportData& ImportData, TMap<FName, UMaterialInterface*>& AvailableMaterials)
			{
				TArray <SkeletalMeshImportData::FMaterial>& ImportedMaterials = ImportData.Materials;
				// If direct linkup of materials is requested, try to find them here - to get a texture name from a 
				// material name, cut off anything in front of the dot (beyond are special flags).
				int32 SkinOffset = INDEX_NONE;
				for (int32 MatIndex = 0; MatIndex < ImportedMaterials.Num(); ++MatIndex)
				{
					const SkeletalMeshImportData::FMaterial& ImportedMaterial = ImportedMaterials[MatIndex];

					UMaterialInterface* Material = nullptr;
					//Remove _SkinXX from the material name
					FString MaterialNameNoSkin = UE::Interchange::Material::RemoveSkinFromName(ImportedMaterial.MaterialImportName);
					const FName SearchMaterialSlotName(*ImportedMaterial.MaterialImportName);
					int32 MaterialIndex = 0;
					FSkeletalMaterial* SkeletalMeshMaterialFind = Materials.FindByPredicate([&SearchMaterialSlotName, &MaterialIndex](const FSkeletalMaterial& ItemMaterial)
					{
						//Imported material slot name is available only WITH_EDITOR
						FName ImportedMaterialSlot = NAME_None;
#if WITH_EDITOR
						ImportedMaterialSlot = ItemMaterial.ImportedMaterialSlotName;
#else
						ImportedMaterialSlot = ItemMaterial.MaterialSlotName;
#endif
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
						const FName MaterialFName = FName(*ImportedMaterial.MaterialImportName);
						if (AvailableMaterials.Contains(FName(MaterialFName)))
						{
							Material = AvailableMaterials.FindChecked(MaterialFName);
						}
						else
						{
							//We did not found any material in the dependencies so try to find material everywhere
							FString MaterialName = ImportedMaterial.MaterialImportName;
							Material = FindObject<UMaterialInterface>(ANY_PACKAGE, *MaterialName);
							if (Material == nullptr && MaterialNameNoSkin.Len() < MaterialName.Len())
							{
								//If we have remove the _skinXX search for material without the suffixe
								Material = FindObject<UMaterialInterface>(ANY_PACKAGE, *MaterialNameNoSkin);
							}
						}
					}
					
					const bool bEnableShadowCasting = true;
					const bool bInRecomputeTangent = false;
					Materials.Add(FSkeletalMaterial(Material, bEnableShadowCasting, bInRecomputeTangent, Material != nullptr ? Material->GetFName() : FName(*MaterialNameNoSkin), FName(*(ImportedMaterial.MaterialImportName))));
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

	UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Cannot import skeletalMesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else
	USkeletalMesh* SkeletalMesh = nullptr;
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeSkeletalMeshNode* SkeletalMeshNode = Cast<UInterchangeSkeletalMeshNode>(Arguments.AssetNode);
	if (SkeletalMeshNode == nullptr)
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
		UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Could not create SkeletalMesh asset %s"), *Arguments.AssetName);
		return nullptr;
	}
	
	SkeletalMesh->PreEditChange(nullptr);
	//Allocate the LODImport data in the main thread
	SkeletalMesh->ReserveLODImportData(SkeletalMeshNode->GetLodDataCount());
	
	return SkeletalMesh;
#endif //else !WITH_EDITOR || !WITH_EDITORONLY_DATA
}

UObject* UInterchangeSkeletalMeshFactory::CreateAsset(const UInterchangeSkeletalMeshFactory::FCreateAssetParams& Arguments) const
{
#if !WITH_EDITOR || !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Cannot import skeletalMesh asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(GetFactoryClass()))
	{
		return nullptr;
	}

	const UInterchangeSkeletalMeshNode* SkeletalMeshNode = Cast<UInterchangeSkeletalMeshNode>(Arguments.AssetNode);
	if (SkeletalMeshNode == nullptr)
	{
		return nullptr;
	}

	const IInterchangeSkeletalMeshPayloadInterface* SkeletalMeshTranslatorPayloadInterface = Cast<IInterchangeSkeletalMeshPayloadInterface>(Arguments.Translator);
	if (!SkeletalMeshTranslatorPayloadInterface)
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Cannot import skeletalMesh, the translator do not implement the IInterchangeSkeletalMeshPayloadInterface."));
		return nullptr;
	}

	const UClass* SkeletalMeshClass = SkeletalMeshNode->GetAssetClass();
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
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Could not create SkeletalMesh asset %s"), *Arguments.AssetName);
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
				UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Could not create SkeletalMesh asset %s"), *Arguments.AssetName);
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
			int32 LodCount = SkeletalMeshNode->GetLodDataCount();
			TArray<FName> LodDataUniqueIds;
			SkeletalMeshNode->GetLodDataUniqueIds(LodDataUniqueIds);
			ensure(LodDataUniqueIds.Num() == LodCount);
			int32 CurrentLodIndex = 0;
			for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
			{
				FName LodUniqueId = LodDataUniqueIds[LodIndex];
				const UInterchangeSkeletalMeshLodDataNode* LodDataNode = Cast<UInterchangeSkeletalMeshLodDataNode>(Arguments.NodeContainer->GetNode(LodUniqueId));
				if (!LodDataNode)
				{
					UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Invalid LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}
				FName SkeletonNodeId = NAME_None;
				if (!LodDataNode->GetCustomSkeletonID(SkeletonNodeId))
				{
					UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}
				const UInterchangeSkeletonNode* SkeletonNode = Cast<UInterchangeSkeletonNode>(Arguments.NodeContainer->GetNode(SkeletonNodeId));
				if (!SkeletonNode)
				{
					UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
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
						UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Invalid Skeleton LOD when importing SkeletalMesh asset %s"), *Arguments.AssetName);
						break;
					}
				}

				FName RootJointNodeId = NAME_None;
				if (!SkeletonNode->GetCustomRootJointID(RootJointNodeId))
				{
					UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Invalid Skeleton LOD Root Joint when importing SkeletalMesh asset %s"), *Arguments.AssetName);
					continue;
				}
				
				int32 SkeletonDepth = 0;
				TArray<SkeletalMeshImportData::FBone> RefBonesBinary;
				UE::Interchange::Private::ProcessImportMeshSkeleton(SkeletonReference, SkeletalMesh->GetRefSkeleton(), SkeletonDepth, Arguments.NodeContainer, RootJointNodeId, RefBonesBinary);

				//Add the lod mesh data to the skeletalmesh
				FSkeletalMeshImportData SkeletalMeshImportData;
				TArray<FName> TranslatorMeshKeys;
				LodDataNode->GetTranslatorMeshKeys(TranslatorMeshKeys);
				//We should have only one key per LOD
				ensure(TranslatorMeshKeys.Num() == 1);
				//Use the first geo LOD since we are suppose to have only one Key
				if(TranslatorMeshKeys.IsValidIndex(0))
				{
					int32 MeshLodIndex = 0;
					SkeletalMeshImportData.RefBonesBinary = RefBonesBinary;

					TOptional<UE::Interchange::FSkeletalMeshLodPayloadData> LodMeshPayload = SkeletalMeshTranslatorPayloadInterface->GetSkeletalMeshLodPayloadData(TranslatorMeshKeys[MeshLodIndex].ToString());
					if (!LodMeshPayload.IsSet())
					{
						UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Invalid Skeletal mesh payload key [%s] SkeletalMesh asset %s"), *TranslatorMeshKeys[MeshLodIndex].ToString(), *Arguments.AssetName);
						break;
					}
					//TODO use the mesh description as the source import data for skeletalmesh
					//Convert the meshdescription to skeletalmesh import data and set the data for the LOD, so the build will work
					//Make sure all IDs are compact so we can use them directly when copying the data
					FElementIDRemappings ElementIDRemappings;
					LodMeshPayload->LodMeshDescription.Compact(ElementIDRemappings);
					UE::Interchange::Private::CopyMeshDescriptionToSkeletalMeshImportData(LodMeshPayload->LodMeshDescription, SkeletalMeshImportData);
				}

				ensure(ImportedResource->LODModels.Add(new FSkeletalMeshLODModel()) == CurrentLodIndex);
				FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[CurrentLodIndex];

				TMap<FName, UMaterialInterface*> AvailableMaterials;
				TArray<FName> Dependencies;
				SkeletalMeshNode->GetDependecies(Dependencies);
				for (int32 DependencyIndex = 0; DependencyIndex < Dependencies.Num(); ++DependencyIndex)
				{
					const UInterchangeMaterialNode* MaterialNode = Cast<UInterchangeMaterialNode>(Arguments.NodeContainer->GetNode(Dependencies[DependencyIndex]));
					if (!MaterialNode || !MaterialNode->ReferenceObject.IsValid())
					{
						continue;
					}
					UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(MaterialNode->ReferenceObject.ResolveObject());
					if (!MaterialInterface)
					{
						continue;
					}
					AvailableMaterials.Add(MaterialNode->GetDisplayLabel(), MaterialInterface);
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

			/** Apply all SkeletalMeshNode custom attributes to the material asset */
			SkeletalMeshNode->ApplyAllCustomAttributeToAsset(SkeletalMesh);
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
																										  , &ImportDataPtr
																										  , Arguments.SourceData
																										  , Arguments.NodeUniqueID
																										  , Arguments.NodeContainer);
		UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
	}
#endif
}