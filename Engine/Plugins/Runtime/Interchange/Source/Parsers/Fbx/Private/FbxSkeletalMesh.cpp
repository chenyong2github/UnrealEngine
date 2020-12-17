// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxSkeletalMesh.h"

#include "CoreMinimal.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "FbxMesh.h"
#include "InterchangeMaterialNode.h"
#include "InterchangeSkeletonNode.h"
#include "InterchangeSkeletalMeshNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeTextureNode.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Serialization/LargeMemoryWriter.h"
#include "SkeletalMeshAttributes.h"

#define GeneratedLODNameSuffix "_GeneratedLOD_"

#define INTERCHANGE_MAX_TEXCOORDS 4

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			bool FSkeletalMeshGeometryPayload::AddFbxMeshToMeshDescription(int32 MeshIndex, FMeshDescription& SkeletalMeshDescription, FSkeletalMeshAttributes& SkeletalMeshAttribute, TArray<FString>& JSonErrorMessages)
			{
				if (!ensure(SDKScene) || !ensure(SDKGeometryConverter))
				{
					return false;
				}

				if (!ensure(SkelMeshNodeArray.IsValidIndex(MeshIndex)))
				{
					return false;
				}

				FbxNode* Node = SkelMeshNodeArray[MeshIndex];
				if (!ensure(Node))
				{
					return false;
				}
				FbxMesh* Mesh = Node->GetMesh();
				if (!ensure(Mesh))
				{
					return false;
				}

				FMeshDescriptionImporter MeshDescriptionImporter(&SkeletalMeshDescription, Node, SDKScene, SDKGeometryConverter);
				if (!MeshDescriptionImporter.FillSkinnedMeshDescriptionFromFbxMesh(&SortedJoints))
				{
					return false;
				}

				return true;
			}

			

			bool FSkeletalMeshGeometryPayload::AddFbxShapeToMeshDescription(int32 MeshIndex, FbxShape* Shape, FMeshDescription& StaticMeshDescription, FStaticMeshAttributes& StaticMeshAttribute, TArray<FString>& JSonErrorMessages)
			{
				if (!ensure(SDKScene) || !ensure(SDKGeometryConverter))
				{
					return false;
				}

				if (!ensure(SkelMeshNodeArray.IsValidIndex(MeshIndex)))
				{
					return false;
				}

				FbxNode* Node = SkelMeshNodeArray[MeshIndex];
				if (!ensure(Node))
				{
					return false;
				}

				FMeshDescriptionImporter MeshDescriptionImporter(&StaticMeshDescription, Node, SDKScene, SDKGeometryConverter);
				if (!MeshDescriptionImporter.FillMeshDescriptionFromFbxShape(Shape))
				{
					return false;
				}

				return true;
			}

			bool FSkeletalMeshGeometryPayload::FetchPayloadToFile(const FString& PayloadFilepath, TArray<FString>& JSonErrorMessages)
			{
				if (!ensure(SDKScene != nullptr))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot fetch fbx skeletalmesh geometry payload because the fbx scene is null\"}}"));
					return false;
				}

				//TODO move MAX_BONES define outside of engine so we can use it here
				if (SortedJoints.Num() > 65536)
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot fetch skeletal payload because it use more then 65536 bones\"}}"));
					return false;
				}

				//Retreive all meshes point by SkelMeshNodeArray into a FMeshDescription using the skeletalmesh attributes
				FMeshDescription SkeletalMeshDescription;
				FSkeletalMeshAttributes SkeletalMeshAttribute(SkeletalMeshDescription);
				SkeletalMeshAttribute.Register();

				for (int32 MeshIndex = 0; MeshIndex < SkelMeshNodeArray.Num(); ++MeshIndex)
				{
					if (!AddFbxMeshToMeshDescription(MeshIndex, SkeletalMeshDescription, SkeletalMeshAttribute, JSonErrorMessages))
					{
						continue;
					}
				}

				//Import the BlendShape
				TMap<FString, FMeshDescription> BlendShapes;
				if (ShapeNameToShapeArray.Num() > 0)
				{
					for (const TPair<FString, TArray<FbxShape*>>& Pair : ShapeNameToShapeArray)
					{
						const TArray<FbxShape*> FbxShapes = Pair.Value;
						FMeshDescription& BlendShapeDescription = BlendShapes.FindOrAdd(Pair.Key);
						FStaticMeshAttributes StaticMeshAttribute(BlendShapeDescription);
						StaticMeshAttribute.Register();
						ensure(FbxShapes.Num() == SkelMeshNodeArray.Num());
						for (int32 MeshIndex = 0; MeshIndex < SkelMeshNodeArray.Num(); ++MeshIndex)
						{
							if (!FbxShapes.IsValidIndex(MeshIndex))
							{
								//TODO warn the user
								break;
							}
							FbxShape* Shape = FbxShapes[MeshIndex];
							if (!AddFbxShapeToMeshDescription(MeshIndex, Shape, BlendShapeDescription, StaticMeshAttribute, JSonErrorMessages))
							{
								continue;
							}
						}
					}
				}
				//Dump the MeshDescription to a file
				{
					FLargeMemoryWriter Ar;
					SkeletalMeshDescription.Serialize(Ar);

					//Add the LOD BlendShape Array to the archive
					Ar << BlendShapes;

					uint8* ArchiveData = Ar.GetData();
					int64 ArchiveSize = Ar.TotalSize();
					TArray64<uint8> Buffer(ArchiveData, ArchiveSize);
					FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
				}

				return true;
			}

			void FFbxSkeletalMesh::AddAllSceneSkeletalMeshes(FbxScene* SDKScene, FbxGeometryConverter* SDKGeometryConverter, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages, TMap<FString, TSharedPtr<FPayloadContextBase>>& PayloadContexts, const FString& SourceFilename)
			{
				TArray<TArray<FbxNode*>> SkeletalMeshes;
				const bool bCombineSkeletalMesh = false;
				const bool bForceFindRigid = false;
				FFbxHelper::FindSkeletalMeshes(SDKScene, SkeletalMeshes, bCombineSkeletalMesh, bForceFindRigid);
				int32 SkeletalMeshCount = SkeletalMeshes.Num();
				for (int32 SkeletalMeshIndex = 0; SkeletalMeshIndex < SkeletalMeshCount; ++SkeletalMeshIndex)
				{
					TArray<FbxNode*>& NodeArray = SkeletalMeshes[SkeletalMeshIndex];
					int32 MaxNumberOfLOD = 1;
					for (int32 NodeIndex = 0; NodeIndex < NodeArray.Num(); NodeIndex++)
					{
						FbxNode* Node = NodeArray[NodeIndex];
						if (Node->GetNodeAttribute() && Node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eLODGroup)
						{
							// get max LODgroup level
							if (MaxNumberOfLOD < Node->GetChildCount())
							{
								MaxNumberOfLOD = Node->GetChildCount();
							}
						}
					}

					UInterchangeSkeletalMeshNode* SkeletalMeshNode = nullptr;

					for (int32 LODIndex = 0; LODIndex < MaxNumberOfLOD; LODIndex++)
					{
						//Array use to retrieve the payload data, so we easily find the request payload data
						TArray<FbxNode*> SkelMeshNodeArray;
						TArray<FbxNode*> SortedJoints;
						TMap<FString, TArray<FbxShape*>> ShapeNameToShapeArray;
						for (int32 j = 0; j < NodeArray.Num(); j++)
						{
							FbxNode* Node = NodeArray[j];
							if (Node->GetNodeAttribute() && Node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eLODGroup)
							{
								TArray<FbxNode*> NodeInLod;
								if (Node->GetChildCount() > LODIndex)
								{
									FFbxHelper::FindAllLODGroupNode(NodeInLod, Node, LODIndex);
								}
								else // in less some LODGroups have less level, use the last level
								{
									FFbxHelper::FindAllLODGroupNode(NodeInLod, Node, Node->GetChildCount() - 1);
								}

								for (FbxNode* MeshNode : NodeInLod)
								{
									SkelMeshNodeArray.Add(MeshNode);
								}
							}
							else
							{
								SkelMeshNodeArray.Add(Node);
							}
						}
						if (SkelMeshNodeArray.Num() <= 0)
						{
							//Nothing to import for this LOD
							continue;
						}
						//Find the skeleton for this skeletal mesh LOD
						
						FbxArray<FbxAMatrix> LocalsPerLink;
						if (!FFbxHelper::FindSkeletonJoints(SDKScene, SkelMeshNodeArray, SortedJoints, LocalsPerLink))
						{
							continue;
						}
						//A skeleton must have at least one joint
						if (SortedJoints.Num() <= 0)
						{
							continue;
						}
						FString SkeletonName = FFbxHelper::GetFbxObjectName(SortedJoints[0]) + TEXT("_Skeleton");
						if (LODIndex > 0)
						{
							SkeletonName += TEXT("_") + FString::FromInt(LODIndex);
						}

						FString SkeletonUniqueID = TEXT("\\Skeleton\\") + FFbxHelper::GetFbxNodeHierarchyName(SortedJoints[0]);

						UInterchangeSkeletonNode* SkeletonNode = Cast<UInterchangeSkeletonNode>(NodeContainer.GetNode(*SkeletonUniqueID));
						if (!SkeletonNode)
						{
							JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Missing fbx skeleton dependency to import a skeletal mesh\"}}"));
							//Skip this LOD since the skeleton is already created
							continue;
						}
						//The base LOD will create the SkeletalMeshNode
						FbxNode* RootNode = SkelMeshNodeArray[0];
						if (LODIndex == 0)
						{
							FString SkeletalMeshName = FFbxHelper::GetFbxObjectName(RootNode);
							FString SkeletalMeshUniqueID = TEXT("\\SkeletalMesh\\") + FFbxHelper::GetFbxNodeHierarchyName(RootNode);
							SkeletalMeshNode = CreateSkeletalMeshNode(NodeContainer, SkeletalMeshName, SkeletalMeshUniqueID, JSonErrorMessages);
						}
						if (!ensure(SkeletalMeshNode))
						{
							JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Missing fbx skeletal mesh base LOD\"}}"));
							break;
						}
						FString SkeletalMeshLodDataName = TEXT("LodData") + FString::FromInt(LODIndex);
						FString LODDataPrefix = TEXT("\\LodData") + (LODIndex > 0 ? FString::FromInt(LODIndex) : TEXT("")) + TEXT("\\");
						FString SkeletalMeshLodDataUniqueID = LODDataPrefix + FFbxHelper::GetFbxNodeHierarchyName(RootNode);
						//Add the data for the LOD (skeleton Unique ID and all the mesh node fbx path, so we can find them when we will create the payload data)
						UInterchangeSkeletalMeshLodDataNode* LodDataNode = CreateSkeletalMeshLodDataNode(NodeContainer, SkeletalMeshLodDataName, SkeletalMeshLodDataUniqueID, JSonErrorMessages);
						LodDataNode->SetParentUID(SkeletalMeshNode->GetUniqueID());
						LodDataNode->SetCustomSkeletonID(*SkeletonUniqueID);
						//Add The node unique ids (the node full path) in the LodDataNode
						//We will use this information to retrieve the payload
						FSHA1 Sha;
						
						for (int32 NodeIndex = 0; NodeIndex < SkelMeshNodeArray.Num(); ++NodeIndex)
						{
							FbxNode* MeshNode = SkelMeshNodeArray[NodeIndex];
							FString MeshNodePath = FFbxHelper::GetFbxNodeHierarchyName(MeshNode);
						
							int32 NodeMaterialCount = MeshNode->GetMaterialCount();
							for (int32 MaterialIndex = 0; MaterialIndex < NodeMaterialCount; ++MaterialIndex)
							{
								FbxSurfaceMaterial* SurfaceMaterial = MeshNode->GetMaterial(MaterialIndex);
								if (!SurfaceMaterial)
								{
									continue;
								}
								//Find an existing material node
								FString MaterialName = FFbxHelper::GetFbxObjectName(SurfaceMaterial);
								FString NodeUID(MaterialName);
								UInterchangeBaseNode* MaterialNode = NodeContainer.GetNode(NodeUID);
								if (MaterialNode != nullptr && Cast<UInterchangeMaterialNode>(MaterialNode))
								{
									//Set a dependency on this material
									SkeletalMeshNode->SetDependencyUID(NodeUID);
								}
							}

							TArray<TCHAR> IDArray = MeshNodePath.GetCharArray();
							Sha.Update((uint8*)IDArray.GetData(), IDArray.Num()* IDArray.GetTypeSize());

							//Add the Blendshape names to the node
							FbxGeometry* Geometry = (FbxGeometry*)MeshNode->GetNodeAttribute();
							if (Geometry)
							{
								const int32 BlendShapeDeformerCount = Geometry->GetDeformerCount(FbxDeformer::eBlendShape);
								if (BlendShapeDeformerCount > 0)
								{
									for (int32 BlendShapeIndex = 0; BlendShapeIndex < BlendShapeDeformerCount; ++BlendShapeIndex)
									{
										FbxBlendShape* BlendShape = (FbxBlendShape*)Geometry->GetDeformer(BlendShapeIndex, FbxDeformer::eBlendShape);
										const int32 BlendShapeChannelCount = BlendShape->GetBlendShapeChannelCount();
										FString BlendShapeName = FFbxHelper::GetFbxObjectName(BlendShape);
										// see below where this is used for explanation...
										const bool bMightBeBadMAXFile = (BlendShapeName == FString("Morpher"));
										for (int32 ChannelIndex = 0; ChannelIndex < BlendShapeChannelCount; ++ChannelIndex)
										{
											FbxBlendShapeChannel* Channel = BlendShape->GetBlendShapeChannel(ChannelIndex);
											if (Channel)
											{
												//Find which shape should we use according to the weight.
												const int32 CurrentChannelShapeCount = Channel->GetTargetShapeCount();
												FString ChannelName = FFbxHelper::GetFbxObjectName(Channel);
												// Maya adds the name of the blendshape and an underscore to the front of the channel name, so remove it
												if (ChannelName.StartsWith(BlendShapeName))
												{
													ChannelName.RightInline(ChannelName.Len() - (BlendShapeName.Len() + 1), false);
												}
												for (int32 ShapeIndex = 0; ShapeIndex < CurrentChannelShapeCount; ++ShapeIndex)
												{
													FbxShape* Shape = Channel->GetTargetShape(ShapeIndex);
													FString ShapeName;
													if (CurrentChannelShapeCount > 1)
													{
														ShapeName = FFbxHelper::GetFbxObjectName(Shape);
													}
													else
													{
														if (bMightBeBadMAXFile)
														{
															ShapeName = FFbxHelper::GetFbxObjectName(Shape);
														}
														else
														{
															// Maya concatenates the number of the shape to the end of its name, so instead use the name of the channel
															ShapeName = ChannelName;
														}
													}

													TArray<FbxShape*>& ShapeArray = ShapeNameToShapeArray.FindOrAdd(ShapeName);
													if (ShapeArray.Num() == 0)
													{
														//Add the Shape name to the LodDataNode so we can retrieve the shapes when someone query the payload
														LodDataNode->AddBlendShape(*ShapeName);
														ShapeArray.AddZeroed(SkelMeshNodeArray.Num());
													}
													ShapeArray[NodeIndex] = Shape;
												}
											}
										}
									}
								}
							}
						}
						Sha.Final();
						// Retrieve the hash and use it to construct a pseudo-GUID. 
						uint32 Hash[5];
						Sha.GetHash((uint8*)Hash);
						FString PayloadKey = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]).ToString(EGuidFormats::Base36Encoded);
						LodDataNode->AddTranslatorMeshKey(*PayloadKey);
						if (ensure(!PayloadContexts.Contains(PayloadKey)))
						{
							TSharedPtr<FSkeletalMeshGeometryPayload> GeoPayload = MakeShared<FSkeletalMeshGeometryPayload>();
							GeoPayload->SkelMeshNodeArray = SkelMeshNodeArray;
							GeoPayload->SortedJoints = SortedJoints;
							GeoPayload->ShapeNameToShapeArray = ShapeNameToShapeArray;
							GeoPayload->SDKScene = SDKScene;
							GeoPayload->SDKGeometryConverter = SDKGeometryConverter;
							PayloadContexts.Add(PayloadKey, GeoPayload);
						}

						SkeletalMeshNode->AddLodDataUniqueId(*SkeletalMeshLodDataUniqueID);
						//Add every LOD skeleton has a dependency for the skeletal mesh node
						//The Skeleton factory will find or create only one skeleton for all LODs
						SkeletalMeshNode->SetDependencyUID(*SkeletonUniqueID);
					}
				}
			}

			UInterchangeSkeletalMeshNode* FFbxSkeletalMesh::CreateSkeletalMeshNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID, TArray<FString>& JSonErrorMessages)
			{
				FString DisplayLabel(NodeName);
				FString NodeUID(NodeUniqueID);
				UInterchangeSkeletalMeshNode* SkeletalMeshNode = NewObject<UInterchangeSkeletalMeshNode>(&NodeContainer, NAME_None);
				if (!ensure(SkeletalMeshNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UMaterialInterface
				SkeletalMeshNode->InitializeSkeletalMeshNode(NodeUID, DisplayLabel, TEXT("SkeletalMesh"));
				NodeContainer.AddNode(SkeletalMeshNode);
				return SkeletalMeshNode;
			}

			UInterchangeSkeletalMeshLodDataNode* FFbxSkeletalMesh::CreateSkeletalMeshLodDataNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID, TArray<FString>& JSonErrorMessages)
			{
				FString DisplayLabel(NodeName);
				FString NodeUID(NodeUniqueID);
				UInterchangeSkeletalMeshLodDataNode* SkeletalMeshLodDataNode = NewObject<UInterchangeSkeletalMeshLodDataNode>(&NodeContainer, NAME_None);
				if (!ensure(SkeletalMeshLodDataNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a skeletal mesh lod data node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UMaterialInterface
				SkeletalMeshLodDataNode->InitializeNode(NodeUID, DisplayLabel);
				NodeContainer.AddNode(SkeletalMeshLodDataNode);
				return SkeletalMeshLodDataNode;
			}
		} //ns Private
	} //ns Interchange
}//ns UE
