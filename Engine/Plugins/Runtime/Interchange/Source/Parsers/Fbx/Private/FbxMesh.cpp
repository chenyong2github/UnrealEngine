// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxMesh.h"

#include "CoreMinimal.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "InterchangeMeshNode.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Serialization/LargeMemoryWriter.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			// Wraps some common code useful for multiple fbx import code path
			struct FFBXUVs
			{
				// constructor
				FFBXUVs(FbxMesh* Mesh)
					: UniqueUVCount(0)
				{
					check(Mesh);

					//
					//	store the UVs in arrays for fast access in the later looping of triangles 
					//
					// mapping from UVSets to Fbx LayerElementUV
					// Fbx UVSets may be duplicated, remove the duplicated UVSets in the mapping 
					int32 LayerCount = Mesh->GetLayerCount();
					if (LayerCount > 0)
					{
						int32 UVLayerIndex;
						for (UVLayerIndex = 0; UVLayerIndex < LayerCount; UVLayerIndex++)
						{
							FbxLayer* lLayer = Mesh->GetLayer(UVLayerIndex);
							int UVSetCount = lLayer->GetUVSetCount();
							if (UVSetCount)
							{
								FbxArray<FbxLayerElementUV const*> EleUVs = lLayer->GetUVSets();
								for (int UVIndex = 0; UVIndex < UVSetCount; UVIndex++)
								{
									FbxLayerElementUV const* ElementUV = EleUVs[UVIndex];
									if (ElementUV)
									{
										const char* UVSetName = ElementUV->GetName();
										FString LocalUVSetName = UTF8_TO_TCHAR(UVSetName);
										if (LocalUVSetName.IsEmpty())
										{
											LocalUVSetName = TEXT("UVmap_") + FString::FromInt(UVLayerIndex);
										}

										UVSets.AddUnique(LocalUVSetName);
									}
								}
							}
						}
					}


					// If the the UV sets are named using the following format (UVChannel_X; where X ranges from 1 to 4)
					// we will re-order them based on these names.  Any UV sets that do not follow this naming convention
					// will be slotted into available spaces.
					if (UVSets.Num())
					{
						for (int32 ChannelNumIdx = 0; ChannelNumIdx < 4; ChannelNumIdx++)
						{
							FString ChannelName = FString::Printf(TEXT("UVChannel_%d"), ChannelNumIdx + 1);
							int32 SetIdx = UVSets.Find(ChannelName);

							// If the specially formatted UVSet name appears in the list and it is in the wrong spot,
							// we will swap it into the correct spot.
							if (SetIdx != INDEX_NONE && SetIdx != ChannelNumIdx)
							{
								// If we are going to swap to a position that is outside the bounds of the
								// array, then we pad out to that spot with empty data.
								for (int32 ArrSize = UVSets.Num(); ArrSize < ChannelNumIdx + 1; ArrSize++)
								{
									UVSets.Add(FString(TEXT("")));
								}
								//Swap the entry into the appropriate spot.
								UVSets.Swap(SetIdx, ChannelNumIdx);
							}
						}
					}
				}

				void Phase2(FbxMesh* Mesh)
				{
					//
					//	store the UVs in arrays for fast access in the later looping of triangles 
					//
					UniqueUVCount = UVSets.Num();
					if (UniqueUVCount > 0)
					{
						LayerElementUV.AddZeroed(UniqueUVCount);
						UVReferenceMode.AddZeroed(UniqueUVCount);
						UVMappingMode.AddZeroed(UniqueUVCount);
					}
					for (int32 UVIndex = 0; UVIndex < UniqueUVCount; UVIndex++)
					{
						LayerElementUV[UVIndex] = NULL;
						for (int32 UVLayerIndex = 0, LayerCount = Mesh->GetLayerCount(); UVLayerIndex < LayerCount; UVLayerIndex++)
						{
							FbxLayer* lLayer = Mesh->GetLayer(UVLayerIndex);
							int UVSetCount = lLayer->GetUVSetCount();
							if (UVSetCount)
							{
								FbxArray<FbxLayerElementUV const*> EleUVs = lLayer->GetUVSets();
								for (int32 FbxUVIndex = 0; FbxUVIndex < UVSetCount; FbxUVIndex++)
								{
									FbxLayerElementUV const* ElementUV = EleUVs[FbxUVIndex];
									if (ElementUV)
									{
										const char* UVSetName = ElementUV->GetName();
										FString LocalUVSetName = UTF8_TO_TCHAR(UVSetName);
										if (LocalUVSetName.IsEmpty())
										{
											LocalUVSetName = TEXT("UVmap_") + FString::FromInt(UVLayerIndex);
										}
										if (LocalUVSetName == UVSets[UVIndex])
										{
											LayerElementUV[UVIndex] = ElementUV;
											UVReferenceMode[UVIndex] = ElementUV->GetReferenceMode();
											UVMappingMode[UVIndex] = ElementUV->GetMappingMode();
											break;
										}
									}
								}
							}
						}
					}

					if (UniqueUVCount > MAX_MESH_TEXTURE_COORDS_MD)
					{
						//TODO Error Message
						//FbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Warning, FText::Format(LOCTEXT("Error_TooMuchUVChannel", "Reached the maximum number of UV Channels for a Static Mesh({0}) - discarding {1} UV Channels"), FText::AsNumber(MAX_MESH_TEXTURE_COORDS_MD), FText::AsNumber(UniqueUVCount - MAX_MESH_TEXTURE_COORDS_MD))), FFbxErrors::Generic_Mesh_TooMuchUVChannels);
					}

					UniqueUVCount = FMath::Min<int32>(UniqueUVCount, MAX_MESH_TEXTURE_COORDS_MD);
				}

				// @param FaceCornerIndex usually TriangleIndex * 3 + CornerIndex but more complicated for mixed n-gons
				int32 ComputeUVIndex(int32 UVLayerIndex, int32 lControlPointIndex, int32 FaceCornerIndex) const
				{
					int32 UVMapIndex = (UVMappingMode[UVLayerIndex] == FbxLayerElement::eByControlPoint) ? lControlPointIndex : FaceCornerIndex;

					int32 Ret;

					if (UVReferenceMode[UVLayerIndex] == FbxLayerElement::eDirect)
					{
						Ret = UVMapIndex;
					}
					else
					{
						FbxLayerElementArrayTemplate<int>& Array = LayerElementUV[UVLayerIndex]->GetIndexArray();
						Ret = Array.GetAt(UVMapIndex);
					}

					return Ret;
				}

				// todo: is that needed? could the dtor do it?
				void Cleanup()
				{
					//
					// clean up.  This needs to happen before the mesh is destroyed
					//
					LayerElementUV.Empty();
					UVReferenceMode.Empty();
					UVMappingMode.Empty();
				}

				TArray<FString> UVSets;
				TArray<FbxLayerElementUV const*> LayerElementUV;
				TArray<FbxLayerElement::EReferenceMode> UVReferenceMode;
				TArray<FbxLayerElement::EMappingMode> UVMappingMode;
				int32 UniqueUVCount;
			};

			FMeshDescriptionImporter::FMeshDescriptionImporter(FMeshDescription* InMeshDescription, FbxScene* InSDKScene, FbxGeometryConverter* InSDKGeometryConverter)
				: MeshDescription(InMeshDescription)
				, SDKScene(InSDKScene)
				, SDKGeometryConverter(InSDKGeometryConverter)
			{
				bInitialized = ensure(MeshDescription) && ensure(SDKScene) && ensure(SDKGeometryConverter);
			}
			
			bool FMeshDescriptionImporter::FillStaticMeshDescriptionFromFbxMesh(FbxMesh* Mesh)
			{
				if (!ensure(bInitialized) || !ensure(MeshDescription) || !ensure(Mesh) || !ensure(Mesh->GetDeformerCount() == 0))
				{
					return false;
				}
				TArray<FString> UnusedArray;
				return FillMeshDescriptionFromFbxMesh(Mesh, UnusedArray, EMeshType::Static);
			}

			bool FMeshDescriptionImporter::FillSkinnedMeshDescriptionFromFbxMesh(FbxMesh* Mesh, TArray<FString>& OutJointUniqueNames)
			{
				if (!ensure(bInitialized) || !ensure(MeshDescription) || !ensure(Mesh) || !ensure(Mesh->GetDeformerCount() > 0))
				{
					return false;
				}

				return FillMeshDescriptionFromFbxMesh(Mesh, OutJointUniqueNames, EMeshType::Skinned);
			}

			bool FMeshDescriptionImporter::FillMeshDescriptionFromFbxShape(FbxShape* Shape)
			{
				//For the shape we just need the vertex positions
				if (!ensure(bInitialized) || !ensure(MeshDescription))
				{
					return false;
				}
				FStaticMeshAttributes Attributes(*MeshDescription);

				//The shape should be for a specified mesh
				if (!ensure(Shape))
				{
					return false;
				}

				//Extract the points into a simplified MeshDescription
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(GatherMorphTargetPoints);
					MeshDescription->SuspendVertexIndexing();

					// Construct the matrices for the conversion from right handed to left handed system
					FbxAMatrix TotalMatrix;
					FbxAMatrix TotalMatrixForNormal;
					TotalMatrix.SetIdentity(); //We use the identity since we want to bake in the interchange factory if the pipeline request it
					TotalMatrixForNormal = TotalMatrix.Inverse();
					TotalMatrixForNormal = TotalMatrixForNormal.Transpose();

					FbxGeometryBase* GeoBase = (FbxGeometryBase*)Shape;

					int32 VertexCount = GeoBase->GetControlPointsCount();

					TVertexAttributesRef<FVector> VertexPositions = Attributes.GetVertexPositions();
					int32 VertexOffset = MeshDescription->Vertices().Num();
					// The below code expects Num() to be equivalent to GetArraySize(), i.e. that all added elements are appended, not inserted into existing gaps
					check(VertexOffset == MeshDescription->Vertices().GetArraySize());
					//Fill the vertex array
					auto GetFinalPosition = [&TotalMatrix](FbxVector4& FbxPosition)
					{
						FbxPosition = TotalMatrix.MultT(FbxPosition);
						return FFbxConvert::ConvertPos(FbxPosition);
					};

					MeshDescription->ReserveNewVertices(VertexCount);
					for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
					{
						int32 RealVertexIndex = VertexOffset + VertexIndex;
						const FVector VertexPosition = GetFinalPosition(GeoBase->GetControlPoints()[VertexIndex]);
						//Maybe we want to do some work here like computing the deltas
						//const FVector MeshVertexPosition = GetFinalPosition(Mesh->GetControlPoints()[VertexIndex]);
						FVertexID AddedVertexId = MeshDescription->CreateVertex();
						if (AddedVertexId.GetValue() != RealVertexIndex)
						{
							//TODO add an error log
							//AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("Error_CannotCreateVertex", "Cannot create valid vertex for mesh '{0}'"), FText::FromString(Mesh->GetName()))), FFbxErrors::StaticMesh_BuildError);
							return false;
						}
						//Add the delta position, so we do not have to recompute it later
						VertexPositions[AddedVertexId] = VertexPosition;// -MeshVertexPosition;
					}
					MeshDescription->ResumeVertexIndexing();
					FElementIDRemappings OutRemappings;
					MeshDescription->Compact(OutRemappings);
				}
				return true;
			}

			bool FMeshDescriptionImporter::FillMeshDescriptionFromFbxMesh(FbxMesh* Mesh, TArray<FString>& OutJointUniqueNames, EMeshType MeshType)
			{
				if (!ensure(bInitialized) || !ensure(MeshDescription) || !ensure(Mesh))
				{
					return false;
				}

				FStaticMeshAttributes Attributes(*MeshDescription);
				Attributes.Register();

				//Get the base layer of the mesh
				FbxLayer* BaseLayer = Mesh->GetLayer(0);
				if (BaseLayer == NULL)
				{
					//TODO add error message
					return false;
				}

				FFBXUVs FBXUVs(Mesh);

				FbxNode* MeshNode = Mesh->GetNodeCount() > 0 ? Mesh->GetNode() : nullptr; //Get the first node using the mesh to setup default asset materials
				//
				// create materials
				//
				//Create a material name array in the node order, also fill the Meshdescription PolygonGroup
				TArray<FName> MaterialNames;
				int32 MaterialCount = (MeshNode != nullptr) ? MeshNode->GetMaterialCount() : Mesh->GetElementMaterialCount();
				if (MeshNode)
				{
					MaterialCount = MeshNode->GetMaterialCount();
					MaterialNames.Reserve(MaterialCount);
					for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
					{
						FbxSurfaceMaterial* FbxMaterial = MeshNode->GetMaterial(MaterialIndex);
						MaterialNames.Add(*FFbxHelper::GetFbxObjectName(FbxMaterial));
					}
				}

				// Must do this before triangulating the mesh due to an FBX bug in TriangulateMeshAdvance
				int32 LayerSmoothingCount = Mesh->GetLayerCount(FbxLayerElement::eSmoothing);
				for (int32 i = 0; i < LayerSmoothingCount; i++)
				{
					FbxLayerElementSmoothing const* SmoothingInfo = Mesh->GetLayer(0)->GetSmoothing();
					if (SmoothingInfo && SmoothingInfo->GetMappingMode() != FbxLayerElement::eByPolygon)
					{
						SDKGeometryConverter->ComputePolygonSmoothingFromEdgeSmoothing(Mesh, i);
					}
				}

				if (!Mesh->IsTriangleMesh())
				{
					const bool bReplace = true;
					FbxNodeAttribute* ConvertedNode = SDKGeometryConverter->Triangulate(Mesh, bReplace);

					if (ConvertedNode != NULL && ConvertedNode->GetAttributeType() == FbxNodeAttribute::eMesh)
					{
						Mesh = (fbxsdk::FbxMesh*)ConvertedNode;
					}
					else
					{
						//TODO add an error message
						MeshDescription->Empty();
						return false;
					}
				}

				// renew the base layer
				BaseLayer = Mesh->GetLayer(0);

				//
				// get the smoothing group layer
				//
				bool bSmoothingAvailable = false;

				FbxLayerElementSmoothing* SmoothingInfo = BaseLayer->GetSmoothing();
				FbxLayerElement::EReferenceMode SmoothingReferenceMode(FbxLayerElement::eDirect);
				FbxLayerElement::EMappingMode SmoothingMappingMode(FbxLayerElement::eByEdge);
				if (SmoothingInfo)
				{
					if (SmoothingInfo->GetMappingMode() == FbxLayerElement::eByPolygon)
					{
						//Convert the base layer to edge smoothing
						SDKGeometryConverter->ComputeEdgeSmoothingFromPolygonSmoothing(Mesh, 0);
						BaseLayer = Mesh->GetLayer(0);
						SmoothingInfo = BaseLayer->GetSmoothing();
					}

					if (SmoothingInfo->GetMappingMode() == FbxLayerElement::eByEdge)
					{
						bSmoothingAvailable = true;
					}

					SmoothingReferenceMode = SmoothingInfo->GetReferenceMode();
					SmoothingMappingMode = SmoothingInfo->GetMappingMode();

					//If all smooth group are 0's, we can say that there is no valid smoothing data, but only for skeletal mesh
					//Static mesh will be faceted in this case since we will not compute smooth group from normals like we do here
					//This is legacy code to get the same result has before
					if (MeshType == EMeshType::Skinned)
					{
						// Check and see if the smooothing data is valid.  If not generate it from the normals
						BaseLayer = Mesh->GetLayer(0);
						if (BaseLayer)
						{
							const FbxLayerElementSmoothing* SmoothingLayer = BaseLayer->GetSmoothing();

							if (SmoothingLayer)
							{
								bool bValidSmoothingData = false;
								FbxLayerElementArrayTemplate<int32>& Array = SmoothingLayer->GetDirectArray();
								for (int32 SmoothingIndex = 0; SmoothingIndex < Array.GetCount(); ++SmoothingIndex)
								{
									if (Array[SmoothingIndex] != 0)
									{
										bValidSmoothingData = true;
										break;
									}
								}

								if (!bValidSmoothingData && Mesh->GetPolygonVertexCount() > 0)
								{
									SDKGeometryConverter->ComputeEdgeSmoothingFromNormals(Mesh);
									BaseLayer = Mesh->GetLayer(0);
									SmoothingInfo = BaseLayer->GetSmoothing();
								}
							}
						}
					}
				}

				//
				//	get the "material index" layer.  Do this AFTER the triangulation step as that may reorder material indices
				//
				FbxLayerElementMaterial* LayerElementMaterial = BaseLayer->GetMaterials();
				FbxLayerElement::EMappingMode MaterialMappingMode = LayerElementMaterial ?
					LayerElementMaterial->GetMappingMode() : FbxLayerElement::eByPolygon;

				//	todo second phase UV, ok to put in first phase?
				FBXUVs.Phase2(Mesh);

				//
				// get the first vertex color layer
				//
				FbxLayerElementVertexColor* LayerElementVertexColor = BaseLayer->GetVertexColors();
				FbxLayerElement::EReferenceMode VertexColorReferenceMode(FbxLayerElement::eDirect);
				FbxLayerElement::EMappingMode VertexColorMappingMode(FbxLayerElement::eByControlPoint);
				if (LayerElementVertexColor)
				{
					VertexColorReferenceMode = LayerElementVertexColor->GetReferenceMode();
					VertexColorMappingMode = LayerElementVertexColor->GetMappingMode();
				}

				//
				// get the first normal layer
				//
				FbxLayerElementNormal* LayerElementNormal = BaseLayer->GetNormals();
				FbxLayerElementTangent* LayerElementTangent = BaseLayer->GetTangents();
				FbxLayerElementBinormal* LayerElementBinormal = BaseLayer->GetBinormals();

				//whether there is normal, tangent and binormal data in this mesh
				bool bHasNTBInformation = LayerElementNormal && LayerElementTangent && LayerElementBinormal;

				FbxLayerElement::EReferenceMode NormalReferenceMode(FbxLayerElement::eDirect);
				FbxLayerElement::EMappingMode NormalMappingMode(FbxLayerElement::eByControlPoint);
				if (LayerElementNormal)
				{
					NormalReferenceMode = LayerElementNormal->GetReferenceMode();
					NormalMappingMode = LayerElementNormal->GetMappingMode();
				}

				FbxLayerElement::EReferenceMode TangentReferenceMode(FbxLayerElement::eDirect);
				FbxLayerElement::EMappingMode TangentMappingMode(FbxLayerElement::eByControlPoint);
				if (LayerElementTangent)
				{
					TangentReferenceMode = LayerElementTangent->GetReferenceMode();
					TangentMappingMode = LayerElementTangent->GetMappingMode();
				}

				FbxLayerElement::EReferenceMode BinormalReferenceMode(FbxLayerElement::eDirect);
				FbxLayerElement::EMappingMode BinormalMappingMode(FbxLayerElement::eByControlPoint);
				if (LayerElementBinormal)
				{
					BinormalReferenceMode = LayerElementBinormal->GetReferenceMode();
					BinormalMappingMode = LayerElementBinormal->GetMappingMode();
				}

				bool bHasNonDegeneratePolygons = false;
				{
					MeshDescription->SuspendVertexInstanceIndexing();
					MeshDescription->SuspendEdgeIndexing();
					MeshDescription->SuspendPolygonIndexing();
					MeshDescription->SuspendPolygonGroupIndexing();
					MeshDescription->SuspendUVIndexing();

					TRACE_CPUPROFILER_EVENT_SCOPE(BuildTriangles);

					// Construct the matrices for the conversion from right handed to left handed system
					FbxAMatrix TotalMatrix;
					FbxAMatrix TotalMatrixForNormal;
					TotalMatrix.SetIdentity();
					TotalMatrixForNormal = TotalMatrix.Inverse();
					TotalMatrixForNormal = TotalMatrixForNormal.Transpose();
					int32 PolygonCount = Mesh->GetPolygonCount();

					if (PolygonCount == 0)
					{
						//TODO add an error log
						//AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("Error_NoPolygonFoundInMesh", "No polygon were found on mesh  '{0}'"), FText::FromString(Mesh->GetName()))), FFbxErrors::StaticMesh_NoTriangles);
						return false;
					}

					int32 VertexCount = Mesh->GetControlPointsCount();
					bool OddNegativeScale = IsOddNegativeScale(TotalMatrix);

					TVertexAttributesRef<FVector> VertexPositions = Attributes.GetVertexPositions();
					TVertexInstanceAttributesRef<FVector> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
					TVertexInstanceAttributesRef<FVector> VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
					TVertexInstanceAttributesRef<float> VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
					TVertexInstanceAttributesRef<FVector4> VertexInstanceColors = Attributes.GetVertexInstanceColors();
					TVertexInstanceAttributesRef<FVector2D> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
					TEdgeAttributesRef<bool> EdgeHardnesses = Attributes.GetEdgeHardnesses();
					TPolygonGroupAttributesRef<FName> PolygonGroupImportedMaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

					int32 VertexOffset = MeshDescription->Vertices().Num();
					int32 VertexInstanceOffset = MeshDescription->VertexInstances().Num();
					int32 PolygonOffset = MeshDescription->Polygons().Num();

					// The below code expects Num() to be equivalent to GetArraySize(), i.e. that all added elements are appended, not inserted into existing gaps
					check(VertexOffset == MeshDescription->Vertices().GetArraySize());
					check(VertexInstanceOffset == MeshDescription->VertexInstances().GetArraySize());
					check(PolygonOffset == MeshDescription->Polygons().GetArraySize());

					TMap<int32, FPolygonGroupID> PolygonGroupMapping;

					// When importing multiple mesh pieces to the same static mesh.  Ensure each mesh piece has the same number of Uv's
					int32 ExistingUVCount = VertexInstanceUVs.GetNumChannels();

					int32 NumUVs = FMath::Max(FBXUVs.UniqueUVCount, ExistingUVCount);
					NumUVs = FMath::Min<int32>(MAX_MESH_TEXTURE_COORDS_MD, NumUVs);
					// At least one UV set must exist.  
					// @todo: this needn't be true; we should be able to handle zero UV channels
					NumUVs = FMath::Max(1, NumUVs);

					//Make sure all Vertex instance have the correct number of UVs
					VertexInstanceUVs.SetNumChannels(NumUVs);
					MeshDescription->SetNumUVChannels(NumUVs);

					TArray<int32> UVOffsets;
					UVOffsets.SetNumUninitialized(NumUVs);
					for (int UVChannel = 0; UVChannel < NumUVs; UVChannel++)
					{
						UVOffsets[UVChannel] = MeshDescription->UVs(UVChannel).GetArraySize();
					}

					//Fill the vertex array
					MeshDescription->ReserveNewVertices(VertexCount);
					for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
					{
						int32 RealVertexIndex = VertexOffset + VertexIndex;
						FbxVector4 FbxPosition = Mesh->GetControlPoints()[VertexIndex];
						FbxPosition = TotalMatrix.MultT(FbxPosition);
						const FVector VertexPosition = FFbxConvert::ConvertPos(FbxPosition);

						FVertexID AddedVertexId = MeshDescription->CreateVertex();
						VertexPositions[AddedVertexId] = VertexPosition;
						if (AddedVertexId.GetValue() != RealVertexIndex)
						{
							//TODO add an error log
							//AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("Error_CannotCreateVertex", "Cannot create valid vertex for mesh '{0}'"), FText::FromString(Mesh->GetName()))), FFbxErrors::StaticMesh_BuildError);
							return false;
						}
					}

					// Fill the UV arrays
					for (int32 UVLayerIndex = 0; UVLayerIndex < FBXUVs.UniqueUVCount; UVLayerIndex++)
					{
						check(FBXUVs.LayerElementUV[UVLayerIndex]);
						if (FBXUVs.LayerElementUV[UVLayerIndex] != nullptr)
						{
							int32 UVCount = FBXUVs.LayerElementUV[UVLayerIndex]->GetDirectArray().GetCount();
							TUVAttributesRef<FVector2D> UVCoordinates = MeshDescription->UVAttributes(UVLayerIndex).GetAttributesRef<FVector2D>(MeshAttribute::UV::UVCoordinate);
							MeshDescription->ReserveNewUVs(UVCount, UVLayerIndex);
							for (int32 UVIndex = 0; UVIndex < UVCount; UVIndex++)
							{
								FUVID UVID = MeshDescription->CreateUV(UVLayerIndex);
								FbxVector2 UVVector = FBXUVs.LayerElementUV[UVLayerIndex]->GetDirectArray().GetAt(UVIndex);
								UVCoordinates[UVID] = FVector2D(static_cast<float>(UVVector[0]), 1.0f - static_cast<float>(UVVector[1]));	// flip the Y of UVs for DirectX
							}
						}
					}

					TMap<uint64, int32> RemapEdgeID;
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(BuildMeshEdgeVertices);
						Mesh->BeginGetMeshEdgeVertices();

						//Fill the edge array
						int32 FbxEdgeCount = Mesh->GetMeshEdgeCount();
						RemapEdgeID.Reserve(FbxEdgeCount * 2);
						for (int32 FbxEdgeIndex = 0; FbxEdgeIndex < FbxEdgeCount; ++FbxEdgeIndex)
						{
							int32 EdgeStartVertexIndex = -1;
							int32 EdgeEndVertexIndex = -1;
							Mesh->GetMeshEdgeVertices(FbxEdgeIndex, EdgeStartVertexIndex, EdgeEndVertexIndex);
							// Skip invalid edges, i.e. one of the ends is invalid, or degenerated ones
							if (EdgeStartVertexIndex == -1 || EdgeEndVertexIndex == -1 || EdgeStartVertexIndex == EdgeEndVertexIndex)
							{
								//TODO log a user error
								//UE_LOG(LogFbx, Warning, TEXT("Skipping invalid edge on mesh %s"), *FString(Mesh->GetName()));
								continue;
							}
							FVertexID EdgeVertexStart(EdgeStartVertexIndex + VertexOffset);
							check(MeshDescription->Vertices().IsValid(EdgeVertexStart));
							FVertexID EdgeVertexEnd(EdgeEndVertexIndex + VertexOffset);
							check(MeshDescription->Vertices().IsValid(EdgeVertexEnd));
							uint64 CompactedKey = (((uint64)EdgeVertexStart.GetValue()) << 32) | ((uint64)EdgeVertexEnd.GetValue());
							RemapEdgeID.Add(CompactedKey, FbxEdgeIndex);
							//Add the other edge side
							CompactedKey = (((uint64)EdgeVertexEnd.GetValue()) << 32) | ((uint64)EdgeVertexStart.GetValue());
							RemapEdgeID.Add(CompactedKey, FbxEdgeIndex);
						}
						//Call this after all GetMeshEdgeIndexForPolygon call this is for optimization purpose.
						Mesh->EndGetMeshEdgeVertices();
					}

					{
						TRACE_CPUPROFILER_EVENT_SCOPE(BuildMeshEdgeIndexForPolygon);

						// Compute and reserve memory to be used for vertex instances
						{
							int32 TotalVertexCount = 0;
							for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; PolygonIndex++)
							{
								TotalVertexCount += Mesh->GetPolygonSize(PolygonIndex);
							}

							MeshDescription->ReserveNewPolygons(PolygonCount);
							MeshDescription->ReserveNewVertexInstances(TotalVertexCount);
							MeshDescription->ReserveNewEdges(TotalVertexCount);
						}

						bool  bBeginGetMeshEdgeIndexForPolygonCalled = false;
						bool  bBeginGetMeshEdgeIndexForPolygonRequired = true;
						int32 CurrentVertexInstanceIndex = 0;
						int32 SkippedVertexInstance = 0;

						// keep those for all iterations to avoid heap allocations
						TArray<FVertexInstanceID> CornerInstanceIDs;
						TArray<FVertexID> CornerVerticesIDs;
						TArray<FVector, TInlineAllocator<3>> P;

						//Polygons
						for (int32 PolygonIndex = 0; PolygonIndex < PolygonCount; PolygonIndex++)
						{
							int32 PolygonVertexCount = Mesh->GetPolygonSize(PolygonIndex);
							//Verify if the polygon is degenerate, in this case do not add them
							{
								float ComparisonThreshold = SMALL_NUMBER;
								P.Reset();
								P.AddUninitialized(PolygonVertexCount);
								for (int32 CornerIndex = 0; CornerIndex < PolygonVertexCount; CornerIndex++)
								{
									const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, CornerIndex);
									const FVertexID VertexID(VertexOffset + ControlPointIndex);
									P[CornerIndex] = VertexPositions[VertexID];
								}
								check(P.Num() > 2); //triangle is the smallest polygon we can have
								const FVector Normal = ((P[1] - P[2]) ^ (P[0] - P[2])).GetSafeNormal(ComparisonThreshold);
								//Check for degenerated polygons, avoid NAN
								if (Normal.IsNearlyZero(ComparisonThreshold) || Normal.ContainsNaN())
								{
									SkippedVertexInstance += PolygonVertexCount;
									continue;
								}
							}

							int32 RealPolygonIndex = PolygonOffset + PolygonIndex;
							CornerInstanceIDs.Reset();
							CornerInstanceIDs.AddUninitialized(PolygonVertexCount);
							CornerVerticesIDs.Reset();
							CornerVerticesIDs.AddUninitialized(PolygonVertexCount);
							for (int32 CornerIndex = 0; CornerIndex < PolygonVertexCount; CornerIndex++)
							{
								int32 VertexInstanceIndex = VertexInstanceOffset + CurrentVertexInstanceIndex + CornerIndex;
								int32 RealFbxVertexIndex = SkippedVertexInstance + CurrentVertexInstanceIndex + CornerIndex;
								const FVertexInstanceID VertexInstanceID(VertexInstanceIndex);
								CornerInstanceIDs[CornerIndex] = VertexInstanceID;
								const int32 ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, CornerIndex);
								const FVertexID VertexID(VertexOffset + ControlPointIndex);
								const FVector VertexPosition = VertexPositions[VertexID];
								CornerVerticesIDs[CornerIndex] = VertexID;

								FVertexInstanceID AddedVertexInstanceId = MeshDescription->CreateVertexInstance(VertexID);

								//Make sure the Added vertex instance ID is matching the expected vertex instance ID
								check(AddedVertexInstanceId == VertexInstanceID);

								if (AddedVertexInstanceId.GetValue() != VertexInstanceIndex)
								{
									//TODO add an error log
									//AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("Error_CannotCreateVertexInstance", "Cannot create valid vertex instance for mesh '{0}'"), FText::FromString(Mesh->GetName()))), FFbxErrors::StaticMesh_BuildError);
									return false;
								}

								//UVs attributes
								for (int32 UVLayerIndex = 0; UVLayerIndex < FBXUVs.UniqueUVCount; UVLayerIndex++)
								{
									FVector2D FinalUVVector(0.0f, 0.0f);
									if (FBXUVs.LayerElementUV[UVLayerIndex] != NULL)
									{
										int UVMapIndex = (FBXUVs.UVMappingMode[UVLayerIndex] == FbxLayerElement::eByControlPoint) ? ControlPointIndex : RealFbxVertexIndex;
										int32 UVIndex = (FBXUVs.UVReferenceMode[UVLayerIndex] == FbxLayerElement::eDirect) ?
											UVMapIndex : FBXUVs.LayerElementUV[UVLayerIndex]->GetIndexArray().GetAt(UVMapIndex);

										FbxVector2	UVVector = FBXUVs.LayerElementUV[UVLayerIndex]->GetDirectArray().GetAt(UVIndex);
										FinalUVVector.X = static_cast<float>(UVVector[0]);
										FinalUVVector.Y = 1.f - static_cast<float>(UVVector[1]);   //flip the Y of UVs for DirectX
									}
									VertexInstanceUVs.Set(AddedVertexInstanceId, UVLayerIndex, FinalUVVector);
								}

								//Color attribute
								if (LayerElementVertexColor)
								{
									int32 VertexColorMappingIndex = (VertexColorMappingMode == FbxLayerElement::eByControlPoint) ?
										Mesh->GetPolygonVertex(PolygonIndex, CornerIndex) : (RealFbxVertexIndex);

									int32 VectorColorIndex = (VertexColorReferenceMode == FbxLayerElement::eDirect) ?
										VertexColorMappingIndex : LayerElementVertexColor->GetIndexArray().GetAt(VertexColorMappingIndex);

									FbxColor VertexColor = LayerElementVertexColor->GetDirectArray().GetAt(VectorColorIndex);

									FColor VertexInstanceColor(
										uint8(255.f * VertexColor.mRed),
										uint8(255.f * VertexColor.mGreen),
										uint8(255.f * VertexColor.mBlue),
										uint8(255.f * VertexColor.mAlpha)
									);
									VertexInstanceColors[AddedVertexInstanceId] = FVector4(FLinearColor(VertexInstanceColor));
								}

								if (LayerElementNormal)
								{
									//normals may have different reference and mapping mode than tangents and binormals
									int NormalMapIndex = (NormalMappingMode == FbxLayerElement::eByControlPoint) ?
										ControlPointIndex : RealFbxVertexIndex;
									int NormalValueIndex = (NormalReferenceMode == FbxLayerElement::eDirect) ?
										NormalMapIndex : LayerElementNormal->GetIndexArray().GetAt(NormalMapIndex);

									FbxVector4 TempValue = LayerElementNormal->GetDirectArray().GetAt(NormalValueIndex);
									TempValue = TotalMatrixForNormal.MultT(TempValue);
									FVector TangentZ = FFbxConvert::ConvertDir(TempValue);
									VertexInstanceNormals[AddedVertexInstanceId] = TangentZ.GetSafeNormal();
									//tangents and binormals share the same reference, mapping mode and index array
									if (bHasNTBInformation)
									{
										int TangentMapIndex = (TangentMappingMode == FbxLayerElement::eByControlPoint) ?
											ControlPointIndex : RealFbxVertexIndex;
										int TangentValueIndex = (TangentReferenceMode == FbxLayerElement::eDirect) ?
											TangentMapIndex : LayerElementTangent->GetIndexArray().GetAt(TangentMapIndex);

										TempValue = LayerElementTangent->GetDirectArray().GetAt(TangentValueIndex);
										TempValue = TotalMatrixForNormal.MultT(TempValue);
										FVector TangentX = FFbxConvert::ConvertDir(TempValue);
										VertexInstanceTangents[AddedVertexInstanceId] = TangentX.GetSafeNormal();

										int BinormalMapIndex = (BinormalMappingMode == FbxLayerElement::eByControlPoint) ?
											ControlPointIndex : RealFbxVertexIndex;
										int BinormalValueIndex = (BinormalReferenceMode == FbxLayerElement::eDirect) ?
											BinormalMapIndex : LayerElementBinormal->GetIndexArray().GetAt(BinormalMapIndex);

										TempValue = LayerElementBinormal->GetDirectArray().GetAt(BinormalValueIndex);
										TempValue = TotalMatrixForNormal.MultT(TempValue);
										FVector TangentY = -FFbxConvert::ConvertDir(TempValue);
										VertexInstanceBinormalSigns[AddedVertexInstanceId] = FbxGetBasisDeterminantSign(TangentX.GetSafeNormal(), TangentY.GetSafeNormal(), TangentZ.GetSafeNormal());
									}
								}
							}

							// Check if the polygon just discovered is non-degenerate if we haven't found one yet
							//TODO check all polygon vertex, not just the first 3 vertex
							if (!bHasNonDegeneratePolygons)
							{
								float TriangleComparisonThreshold = THRESH_POINTS_ARE_SAME;
								FVector VertexPosition[3];
								VertexPosition[0] = VertexPositions[CornerVerticesIDs[0]];
								VertexPosition[1] = VertexPositions[CornerVerticesIDs[1]];
								VertexPosition[2] = VertexPositions[CornerVerticesIDs[2]];
								if (!(VertexPosition[0].Equals(VertexPosition[1], TriangleComparisonThreshold)
									|| VertexPosition[0].Equals(VertexPosition[2], TriangleComparisonThreshold)
									|| VertexPosition[1].Equals(VertexPosition[2], TriangleComparisonThreshold)))
								{
									bHasNonDegeneratePolygons = true;
								}
							}

							//
							// material index
							//
							int32 MaterialIndex = 0;
							if (MaterialCount > 0)
							{
								if (LayerElementMaterial)
								{
									switch (MaterialMappingMode)
									{
										// material index is stored in the IndexArray, not the DirectArray (which is irrelevant with 2009.1)
										case FbxLayerElement::eAllSame:
										{
											MaterialIndex = LayerElementMaterial->GetIndexArray().GetAt(0);
										}
										break;
										case FbxLayerElement::eByPolygon:
										{
											MaterialIndex = LayerElementMaterial->GetIndexArray().GetAt(PolygonIndex);
										}
										break;
									}
								}
							}

							if (MaterialIndex < 0)
							{
								//TODO log an error message
								MaterialIndex = 0;
							}
							//Add missing material name with generated name
							while (MaterialIndex >= MaterialNames.Num())
							{
								FString MaterialName = TEXT("Material_") + FString::FromInt(MaterialNames.Num());
								MaterialNames.Add((*MaterialName));
							}

							//Material name should always exist since we create up to index materials
							check(MaterialNames.IsValidIndex(MaterialIndex));
							{
								//Create a polygon with the 3 vertex instances Add it to the material group
								FName MaterialName = MaterialNames[MaterialIndex];
								if (!PolygonGroupMapping.Contains(MaterialIndex))
								{
									FPolygonGroupID ExistingPolygonGroup = INDEX_NONE;
									for (const FPolygonGroupID PolygonGroupID : MeshDescription->PolygonGroups().GetElementIDs())
									{
										if (PolygonGroupImportedMaterialSlotNames[PolygonGroupID] == MaterialName)
										{
											ExistingPolygonGroup = PolygonGroupID;
											break;
										}
									}
									if (ExistingPolygonGroup == INDEX_NONE)
									{
										ExistingPolygonGroup = MeshDescription->CreatePolygonGroup();
										PolygonGroupImportedMaterialSlotNames[ExistingPolygonGroup] = MaterialName;
									}
									PolygonGroupMapping.Add(MaterialIndex, ExistingPolygonGroup);
								}
							}

							// Create polygon edges
							{
								// Add the edges of this polygon
								for (uint32 PolygonEdgeNumber = 0; PolygonEdgeNumber < (uint32)PolygonVertexCount; ++PolygonEdgeNumber)
								{
									//Find the matching edge ID
									uint32 CornerIndices[2];
									CornerIndices[0] = (PolygonEdgeNumber + 0) % PolygonVertexCount;
									CornerIndices[1] = (PolygonEdgeNumber + 1) % PolygonVertexCount;

									FVertexID EdgeVertexIDs[2];
									EdgeVertexIDs[0] = CornerVerticesIDs[CornerIndices[0]];
									EdgeVertexIDs[1] = CornerVerticesIDs[CornerIndices[1]];

									FEdgeID MatchEdgeId = MeshDescription->GetVertexPairEdge(EdgeVertexIDs[0], EdgeVertexIDs[1]);
									if (MatchEdgeId == INDEX_NONE)
									{
										MatchEdgeId = MeshDescription->CreateEdge(EdgeVertexIDs[0], EdgeVertexIDs[1]);
									}

									//RawMesh do not have edges, so by ordering the edge with the triangle construction we can ensure back and forth conversion with RawMesh
									//When raw mesh will be completely remove we can create the edges right after the vertex creation.
									int32 EdgeIndex = INDEX_NONE;
									uint64 CompactedKey = (((uint64)EdgeVertexIDs[0].GetValue()) << 32) | ((uint64)EdgeVertexIDs[1].GetValue());
									if (RemapEdgeID.Contains(CompactedKey))
									{
										EdgeIndex = RemapEdgeID[CompactedKey];
									}
									else
									{
										// Call BeginGetMeshEdgeIndexForPolygon lazily only if we enter the code path calling GetMeshEdgeIndexForPolygon
										if (bBeginGetMeshEdgeIndexForPolygonRequired)
										{
											//Call this before all GetMeshEdgeIndexForPolygon for optimization purpose.
											//But do not spend time precomputing stuff if the mesh has no edge since 
											//GetMeshEdgeIndexForPolygon is going to always returns -1 anyway without going into the slow path.
											if (Mesh->GetMeshEdgeCount() > 0)
											{
												Mesh->BeginGetMeshEdgeIndexForPolygon();
												bBeginGetMeshEdgeIndexForPolygonCalled = true;
											}
											bBeginGetMeshEdgeIndexForPolygonRequired = false;
										}

										EdgeIndex = Mesh->GetMeshEdgeIndexForPolygon(PolygonIndex, PolygonEdgeNumber);
									}

									if (!EdgeHardnesses[MatchEdgeId])
									{
										if (bSmoothingAvailable && SmoothingInfo)
										{
											if (SmoothingMappingMode == FbxLayerElement::eByEdge)
											{
												int32 lSmoothingIndex = (SmoothingReferenceMode == FbxLayerElement::eDirect) ? EdgeIndex : SmoothingInfo->GetIndexArray().GetAt(EdgeIndex);
												//Set the hard edges
												int32 SmoothingFlag = SmoothingInfo->GetDirectArray().GetAt(lSmoothingIndex);
												EdgeHardnesses[MatchEdgeId] = (SmoothingFlag == 0);
											}
											else
											{
												EdgeHardnesses[MatchEdgeId] = false;
												//TODO add an error log
												//AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Warning, FText::Format(LOCTEXT("Error_UnsupportedSmoothingGroup", "Unsupported Smoothing group mapping mode on mesh  '{0}'"), FText::FromString(Mesh->GetName()))), FFbxErrors::Generic_Mesh_UnsupportingSmoothingGroup);
											}
										}
										else
										{
											//When there is no smoothing group we set all edge to: hard (faceted mesh) for static mesh and smooth for skinned and rigid
											EdgeHardnesses[MatchEdgeId] = MeshType == EMeshType::Static ? true : false;
										}
									}
								}
							}

							FPolygonGroupID PolygonGroupID = PolygonGroupMapping[MaterialIndex];
							// Insert a triangle into the mesh
							// @note: we only ever import triangulated meshes currently. This could easily change as we have the infrastructure set up for arbitrary ngons.
							// However, I think either we need to triangulate in the exact same way as Maya/Max if we do that.
							check(CornerInstanceIDs.Num() == 3);
							check(PolygonVertexCount == 3);
							TArray<FEdgeID> NewEdgeIDs;
							const FTriangleID NewTriangleID = MeshDescription->CreateTriangle(PolygonGroupID, CornerInstanceIDs, &NewEdgeIDs);
							check(NewEdgeIDs.Num() == 0);

							for (int32 UVLayerIndex = 0; UVLayerIndex < FBXUVs.UniqueUVCount; UVLayerIndex++)
							{
								FUVID UVIDs[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
								if (FBXUVs.LayerElementUV[UVLayerIndex] != NULL)
								{
									for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
									{
										int UVMapIndex = (FBXUVs.UVMappingMode[UVLayerIndex] == FbxLayerElement::eByControlPoint)
											? Mesh->GetPolygonVertex(PolygonIndex, VertexIndex)
											: SkippedVertexInstance + CurrentVertexInstanceIndex + VertexIndex;
										int32 UVIndex = (FBXUVs.UVReferenceMode[UVLayerIndex] == FbxLayerElement::eDirect)
											? UVMapIndex
											: FBXUVs.LayerElementUV[UVLayerIndex]->GetIndexArray().GetAt(UVMapIndex);

										if (UVIndex != -1)
										{
											UVIDs[VertexIndex] = UVIndex + UVOffsets[UVLayerIndex];

											check(MeshDescription->VertexInstanceAttributes().GetAttribute<FVector2D>(CornerInstanceIDs[VertexIndex], MeshAttribute::VertexInstance::TextureCoordinate, UVLayerIndex) ==
												  MeshDescription->UVAttributes(UVLayerIndex).GetAttribute<FVector2D>(UVIndex + UVOffsets[UVLayerIndex], MeshAttribute::UV::UVCoordinate));
										}
										else
										{
											// TODO: what does it mean to have a UV index of -1?
											// Investigate this case more carefully and handle it properly.
										}
									}
								}

								MeshDescription->SetTriangleUVIndices(NewTriangleID, UVIDs, UVLayerIndex);
							}

							CurrentVertexInstanceIndex += PolygonVertexCount;
						}

						//Call this after all GetMeshEdgeIndexForPolygon call this is for optimization purpose.
						if (bBeginGetMeshEdgeIndexForPolygonCalled)
						{
							Mesh->EndGetMeshEdgeIndexForPolygon();
						}

						MeshDescription->ResumeVertexInstanceIndexing();
						MeshDescription->ResumeEdgeIndexing();
						MeshDescription->ResumePolygonIndexing();
						MeshDescription->ResumePolygonGroupIndexing();
						MeshDescription->ResumeUVIndexing();

						if (SkippedVertexInstance > 0)
						{
							//We must compact the sparse array before reserving new space
							//When we reserve it will make a hole in the sparse array if the last reserve was not fully use
							//The importer assume there will be no hole when importing a mesh
							FElementIDRemappings OutRemappings;
							MeshDescription->Compact(OutRemappings);
						}
					}

					if (MeshType == EMeshType::Skinned)
					{
						FSkeletalMeshAttributes SkeletalMeshAttributes(*MeshDescription);
						SkeletalMeshAttributes.Register();

						using namespace UE::AnimationCore;
						TMap<FVertexID, TArray<FBoneWeight>> RawBoneWeights;
						
						//Add the influence data in the skeletalmesh description
						FSkinWeightsVertexAttributesRef VertexSkinWeights = SkeletalMeshAttributes.GetVertexSkinWeights();

						FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(0, FbxDeformer::eSkin);
						if (!ensure(Skin))
						{
							//TODO log an error
							return false;
						}
						const int32 ClusterCount = Skin->GetClusterCount();
						TArray<FbxNode*> SortedJoints;
						SortedJoints.Reserve(ClusterCount);
						OutJointUniqueNames.Reserve(ClusterCount);
						// create influences for each cluster
						for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ClusterIndex++)
						{
							FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
							// When Maya plug-in exports rigid binding, it will generate "CompensationCluster" for each ancestor links.
							// FBX writes these "CompensationCluster" out. The CompensationCluster also has weight 1 for vertices.
							// Unreal importer should skip these clusters.
							if (!Cluster || (FCStringAnsi::Strcmp(Cluster->GetUserDataID(), "Maya_ClusterHint") == 0 && FCStringAnsi::Strcmp(Cluster->GetUserData(), "CompensationCluster") == 0))
							{
								continue;
							}

							FbxNode* Link = Cluster->GetLink();
							// find the bone index
							int32 BoneIndex = -1;
							if (!SortedJoints.Find(Link, BoneIndex))
							{
								BoneIndex = SortedJoints.Add(Link);
								FString JointNodeUniqueID = FFbxHelper::GetFbxObjectName(Link);
								OutJointUniqueNames.Add(JointNodeUniqueID);
							}

							//	get the vertex indices
							int32 ControlPointIndicesCount = Cluster->GetControlPointIndicesCount();
							int32* ControlPointIndices = Cluster->GetControlPointIndices();
							double* Weights = Cluster->GetControlPointWeights();

							//	for each vertex index in the cluster
							for (int32 ControlPointIndex = 0; ControlPointIndex < ControlPointIndicesCount; ++ControlPointIndex)
							{
								FVertexID VertexID(ControlPointIndices[ControlPointIndex] + VertexOffset);
								if (!ensure(MeshDescription->IsVertexValid(VertexID)))
								{
									//Invalid Influence
									continue;
								}
								float Weight = static_cast<float>(Weights[ControlPointIndex]);

								TArray<FBoneWeight> *BoneWeights = RawBoneWeights.Find(VertexID);
								if (BoneWeights)
								{
									// Do we already have a weight for this bone? Keep the greater weight.
									bool bShouldAdd = true;
									for (int32 WeightIndex = 0; WeightIndex < BoneWeights->Num(); WeightIndex++)
									{
										FBoneWeight &BoneWeight = (*BoneWeights)[WeightIndex];
										if (BoneWeight.GetBoneIndex() == BoneIndex)
										{
											if (BoneWeight.GetWeight() < Weight)
											{
												BoneWeight.SetWeight(Weight);
											}
											bShouldAdd = false;
											break;
										}
									}
									if (bShouldAdd)
									{
										BoneWeights->Add(FBoneWeight(BoneIndex, Weight));
									}
								}
								else
								{
									RawBoneWeights.Add(VertexID).Add(FBoneWeight(BoneIndex, Weight));
								}
							}
						}

						// Add all the raw bone weights. This will cause the weights to be sorted and re-normalized after culling to max influences.
						for (const TTuple<FVertexID, TArray<FBoneWeight>> &Item: RawBoneWeights)
						{
							VertexSkinWeights.Set(Item.Key, Item.Value);
						}
					}
				}
				// needed?
				FBXUVs.Cleanup();

				if (!bHasNonDegeneratePolygons)
				{
					//TODO log an error we have imported no valid triangle in this fbx mesh node
					//CreateTokenizedErrorForDegeneratedPart(this, StaticMesh->GetName(), FbxNodeName);
				}

				return bHasNonDegeneratePolygons;
			}

			bool FMeshDescriptionImporter::IsOddNegativeScale(FbxAMatrix& TotalMatrix)
			{
				FbxVector4 Scale = TotalMatrix.GetS();
				int32 NegativeNum = 0;

				if (Scale[0] < 0) NegativeNum++;
				if (Scale[1] < 0) NegativeNum++;
				if (Scale[2] < 0) NegativeNum++;

				return NegativeNum == 1 || NegativeNum == 3;
			}


			//////////////////////////////////////////////////////////////////////////
			/// FMeshPayloadContext implementation

			bool FMeshPayloadContext::FetchPayloadToFile(const FString& PayloadFilepath, TArray<FString>& JSonErrorMessages)
			{
				if (!ensure(SDKScene != nullptr))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot fetch fbx mesh payload because the fbx scene is null\"}}"));
					return false;
				}

				if (!ensure(Mesh != nullptr))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot fetch fbx mesh payload because the fbx mesh is null\"}}"));
					return false;
				}

				if (!ensure(SDKGeometryConverter != nullptr))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot fetch fbx mesh payload because the fbx scene geometry converter is null\"}}"));
					return false;
				}

				bool bFetchSkinnedData = (Mesh->GetDeformerCount(FbxDeformer::eSkin) > 0);
				
				TArray<FString> JointUniqueNames;

				FMeshDescription MeshDescription;
				if (bFetchSkinnedData)
				{
					FSkeletalMeshAttributes SkeletalMeshAttribute(MeshDescription);
					SkeletalMeshAttribute.Register();
					FMeshDescriptionImporter MeshDescriptionImporter(&MeshDescription, SDKScene, SDKGeometryConverter);
					if (!MeshDescriptionImporter.FillSkinnedMeshDescriptionFromFbxMesh(Mesh, JointUniqueNames))
					{
						JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot fetch skinned mesh payload because there was an error when creating the FMeshDescription\"}}"));
						return false;
					}
				}
				else
				{
					FStaticMeshAttributes StaticMeshAttribute(MeshDescription);
					StaticMeshAttribute.Register();
					FMeshDescriptionImporter MeshDescriptionImporter(&MeshDescription, SDKScene, SDKGeometryConverter);
					if (!MeshDescriptionImporter.FillStaticMeshDescriptionFromFbxMesh(Mesh))
					{
						JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot fetch static mesh payload because there was an error when creating the FMeshDescription\"}}"));
						return false;
					}
				}
				//Dump the MeshDescription to a file
				{
					FLargeMemoryWriter Ar;
					MeshDescription.Serialize(Ar);

					Ar << bFetchSkinnedData;
					if (bFetchSkinnedData)
					{
						//When passing a skinned MeshDescription, We want to pass the joint Node ID so we can know what the influence bone index refer to
						Ar << JointUniqueNames;
					}
					uint8* ArchiveData = Ar.GetData();
					int64 ArchiveSize = Ar.TotalSize();
					TArray64<uint8> Buffer(ArchiveData, ArchiveSize);
					FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
				}

				return true;
			}

			//////////////////////////////////////////////////////////////////////////
			/// FShapePayloadContext implementation

			bool FShapePayloadContext::FetchPayloadToFile(const FString& PayloadFilepath, TArray<FString>& JSonErrorMessages)
			{
				if (!ensure(Shape))
				{
					//Todo log an error
					return false;
				}
				//Import the BlendShape
				FMeshDescription ShapeMeshDescription;
				FStaticMeshAttributes StaticMeshAttribute(ShapeMeshDescription);
				StaticMeshAttribute.Register();
				FMeshDescriptionImporter MeshDescriptionImporter(&ShapeMeshDescription, SDKScene, SDKGeometryConverter);
				if (!MeshDescriptionImporter.FillMeshDescriptionFromFbxShape(Shape))
				{
					//Todo log an error
					return false;
				}

				//Dump the MeshDescription to a file
				{
					FLargeMemoryWriter Ar;
					ShapeMeshDescription.Serialize(Ar);
					uint8* ArchiveData = Ar.GetData();
					int64 ArchiveSize = Ar.TotalSize();
					TArray64<uint8> Buffer(ArchiveData, ArchiveSize);
					FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
				}
				return true;
			}

			//////////////////////////////////////////////////////////////////////////
			/// FFbxMesh implementation
			
			FString FFbxMesh::GetUniqueIDString(const uint64 UniqueID)
			{
				FStringFormatNamedArguments FormatArguments;
				FormatArguments.Add(TEXT("UniqueID"), UniqueID);
				return FString::Format(TEXT("{UniqueID}"), FormatArguments);
			}

			FString FFbxMesh::GetMeshName(FbxGeometryBase* Mesh)
			{
				FString MeshName = FFbxHelper::GetFbxObjectName(Mesh);
				if (MeshName.IsEmpty())
				{
					if (Mesh->GetNodeCount() > 0)
					{
						if (Mesh->GetAttributeType() == FbxNodeAttribute::eMesh)
						{
							MeshName = TEXT("Mesh_");
						}
						else if (Mesh->GetAttributeType() == FbxNodeAttribute::eShape)
						{
							MeshName = TEXT("Shape_");
						}
						MeshName += FFbxHelper::GetFbxObjectName(Mesh->GetNode(0));
					}
					else
					{
						uint64 UniqueFbxObjectID = Mesh->GetUniqueID();
						MeshName += GetUniqueIDString(UniqueFbxObjectID);
					}
				}
				return MeshName;
			}
			
			FString FFbxMesh::GetMeshUniqueID(FbxGeometryBase* Mesh)
			{
				FString MeshUniqueID;
				if (Mesh->GetAttributeType() == FbxNodeAttribute::eMesh)
				{
					MeshUniqueID = TEXT("\\Mesh\\");
				}
				else if (Mesh->GetAttributeType() == FbxNodeAttribute::eShape)
				{
					MeshUniqueID = TEXT("\\Shape\\");
				}
				FString MeshName = FFbxHelper::GetFbxObjectName(Mesh);
				if (MeshName.IsEmpty())
				{
					if (Mesh->GetNodeCount() > 0)
					{
						MeshUniqueID += FFbxHelper::GetFbxNodeHierarchyName(Mesh->GetNode(0));
					}
					else
					{
						MeshUniqueID += GetMeshName(Mesh);
					}
				}
				else
				{
					MeshUniqueID += MeshName;
				}
				return MeshUniqueID;
			}

			void FFbxMesh::ExtractSkinnedMeshNodeJoints(FbxScene* SDKScene, FbxMesh* Mesh, UInterchangeMeshNode* MeshNode, TArray<FString>& JSonErrorMessages)
			{
				TArray<FString> JointNodeUniqueIDs;
				const int32 SkinDeformerCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
				for (int32 DeformerIndex = 0; DeformerIndex < SkinDeformerCount; DeformerIndex++)
				{
					FbxSkin* Skin = (FbxSkin*)Mesh->GetDeformer(DeformerIndex, FbxDeformer::eSkin);
					if (!ensure(Skin))
					{
						continue;
					}
					const int32 ClusterCount = Skin->GetClusterCount();
					JointNodeUniqueIDs.Reserve(ClusterCount);
					for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ClusterIndex++)
					{
						FbxCluster* Cluster = Skin->GetCluster(ClusterIndex);
						// When Maya plug-in exports rigid binding, it will generate "CompensationCluster" for each ancestor links.
						// FBX writes these "CompensationCluster" out. The CompensationCluster also has weight 1 for vertices.
						// Unreal importer should skip these clusters.
						if (!Cluster || (FCStringAnsi::Strcmp(Cluster->GetUserDataID(), "Maya_ClusterHint") == 0 && FCStringAnsi::Strcmp(Cluster->GetUserData(), "CompensationCluster") == 0))
						{
							continue;
						}
						FbxNode* Link = Cluster->GetLink();
						FString JointNodeUniqueID = FFbxHelper::GetFbxNodeHierarchyName(Link);
						// find the bone index
						if (!JointNodeUniqueIDs.Contains(JointNodeUniqueID))
						{
							JointNodeUniqueIDs.Add(JointNodeUniqueID);
							MeshNode->SetSkeletonDependencyUid(JointNodeUniqueID);
						}
					}
				}
			}

			void FFbxMesh::AddAllMeshes(FbxScene* SDKScene, FbxGeometryConverter* SDKGeometryConverter, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages, TMap<FString, TSharedPtr<FPayloadContextBase>>& PayloadContexts)
			{
				int32 GeometryCount = SDKScene->GetGeometryCount();
				for (int32 GeometryIndex = 0; GeometryIndex < GeometryCount; ++GeometryIndex)
				{
					FbxGeometry* Geometry = SDKScene->GetGeometry(GeometryIndex);
					if (Geometry->GetAttributeType() != FbxNodeAttribute::eMesh)
					{
						continue;
					}
					FbxMesh* Mesh = static_cast<FbxMesh*>(Geometry);
					if (!Mesh)
					{
						continue;
					}
					FString MeshName = GetMeshName(Mesh);
					FString MeshUniqueID = GetMeshUniqueID(Mesh);
					UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(MeshUniqueID));
					if (!MeshNode)
					{
						MeshNode = CreateMeshNode(NodeContainer, MeshName, MeshUniqueID, JSonErrorMessages);
						if (Geometry->GetDeformerCount(FbxDeformer::eSkin) > 0)
						{
							//Set the skinned mesh attribute
							MeshNode->SetSkinnedMesh(true);
							ExtractSkinnedMeshNodeJoints(SDKScene, Mesh, MeshNode, JSonErrorMessages);
						}
						Mesh->ComputeBBox();
						const int32 MeshVertexCount = Mesh->GetControlPointsCount();
						MeshNode->SetCustomVertexCount(MeshVertexCount);
						const int32 MeshPolygonCount = Mesh->GetPolygonCount();
						MeshNode->SetCustomPolygonCount(MeshPolygonCount);
						const FBox MeshBoundingBox = FBox(FFbxConvert::ConvertPos(Mesh->BBoxMin.Get()), FFbxConvert::ConvertPos(Mesh->BBoxMax.Get()));
						MeshNode->SetCustomBoundingBox(MeshBoundingBox);
						const bool bMeshHasVertexNormal = Mesh->GetElementNormalCount() > 0;
						MeshNode->SetCustomHasVertexNormal(bMeshHasVertexNormal);
						const bool bMeshHasVertexBinormal = Mesh->GetElementBinormalCount() > 0;
						MeshNode->SetCustomHasVertexBinormal(bMeshHasVertexBinormal);
						const bool bMeshHasVertexTangent = Mesh->GetElementTangentCount() > 0;
						MeshNode->SetCustomHasVertexTangent(bMeshHasVertexTangent);
						const bool bMeshHasSmoothGroup = Mesh->GetElementSmoothingCount() > 0;
						MeshNode->SetCustomHasSmoothGroup(bMeshHasSmoothGroup);
						const bool bMeshHasVertexColor = Mesh->GetElementVertexColorCount() > 0;
						MeshNode->SetCustomHasVertexColor(bMeshHasVertexColor);
						const int32 MeshUVCount = Mesh->GetElementUVCount();
						MeshNode->SetCustomUVCount(MeshUVCount);
						
						//Add Material dependencies, we use always the first fbx node that instance the fbx geometry to grab the materials.
						if (FbxNode* FbxMeshNode = Mesh->GetNode())
						{
							const int32 MaterialCount = FbxMeshNode->GetMaterialCount();
							for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
							{
								FbxSurfaceMaterial* FbxMaterial = FbxMeshNode->GetMaterial(MaterialIndex);
								FString MaterialName = FFbxHelper::GetFbxObjectName(FbxMaterial);
								FString MaterialUid = TEXT("\\Material\\") + MaterialName;
								MeshNode->SetMaterialDependencyUid(MaterialUid);
							}
						}
					}
					FString PayLoadKey = MeshUniqueID;
					if (ensure(!PayloadContexts.Contains(PayLoadKey)))
					{
						TSharedPtr<FMeshPayloadContext> GeoPayload = MakeShared<FMeshPayloadContext>();
						GeoPayload->Mesh = Mesh;
						GeoPayload->SDKScene = SDKScene;
						GeoPayload->SDKGeometryConverter = SDKGeometryConverter;
						PayloadContexts.Add(PayLoadKey, GeoPayload);
					}
					MeshNode->SetPayLoadKey(PayLoadKey);
					
					//Add all BlendShapes for this geometry node
					TMap<FString, FbxShape*> ShapeNameToFbxShape;
					const int32 BlendShapeCount = Geometry->GetDeformerCount(FbxDeformer::eBlendShape);
					if (BlendShapeCount > 0)
					{
						for (int32 BlendShapeIndex = 0; BlendShapeIndex < BlendShapeCount; ++BlendShapeIndex)
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
										ensure(!ShapeNameToFbxShape.Contains(ShapeName));
										ShapeNameToFbxShape.Add(ShapeName, Shape);
										FString ShapeAttributeName = GetMeshName(Shape);
										FString ShapeUniqueID = GetMeshUniqueID(Shape);
										UInterchangeMeshNode* ShapeNode = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(ShapeUniqueID));
										if (!ShapeNode)
										{
											ShapeNode = CreateMeshNode(NodeContainer, ShapeAttributeName, ShapeUniqueID, JSonErrorMessages);
											const bool bIsBlendShape = true;
											ShapeNode->SetBlendShape(bIsBlendShape);
											ShapeNode->SetBlendShapeName(ShapeName);
											//Create a Mesh node dependency, so the mesh node can retrieve is associate blendshape
											MeshNode->SetShapeDependencyUid(ShapeUniqueID);
										}
										FString ShapePayLoadKey = ShapeUniqueID;
										if (ensure(!PayloadContexts.Contains(ShapePayLoadKey)))
										{
											TSharedPtr<FShapePayloadContext> GeoPayload = MakeShared<FShapePayloadContext>();
											GeoPayload->Shape = Shape;
											GeoPayload->SDKScene = SDKScene;
											GeoPayload->SDKGeometryConverter = SDKGeometryConverter;
											PayloadContexts.Add(ShapePayLoadKey, GeoPayload);
										}
										ShapeNode->SetPayLoadKey(ShapePayLoadKey);
									}
								}
							}
						}
					}
				}
			}

			UInterchangeMeshNode* FFbxMesh::CreateMeshNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID, TArray<FString>& JSonErrorMessages)
			{
				FString DisplayLabel(NodeName);
				FString NodeUid(NodeUniqueID);
				UInterchangeMeshNode* MeshNode = NewObject<UInterchangeMeshNode>(&NodeContainer, NAME_None);
				if (!ensure(MeshNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a mesh node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UMaterialInterface
				MeshNode->InitializeNode(NodeUid, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_TranslatedAsset);
				NodeContainer.AddNode(MeshNode);
				return MeshNode;
			}
		} //ns Private
	} //ns Interchange
}//ns UE
