// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxScene.h"

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "FbxMaterial.h"
#include "FbxMesh.h"
#include "InterchangeCameraNode.h"
#include "InterchangeLightNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeResultsContainer.h"
#include "InterchangeSceneNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#define LOCTEXT_NAMESPACE "InterchangeFbxScene"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{

			void FFbxScene::CreateMeshNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer, const FTransform& GeometricTransform)
			{
				UInterchangeMeshNode* MeshNode = nullptr;
				if (NodeAttribute->GetAttributeType() == FbxNodeAttribute::eMesh)
				{
					FbxMesh* Mesh = static_cast<FbxMesh*>(NodeAttribute);
					if (ensure(Mesh))
					{
						FString MeshRefString = FFbxHelper::GetMeshUniqueID(Mesh);
						MeshNode = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(MeshRefString));
					}
				}
				else if (NodeAttribute->GetAttributeType() == FbxNodeAttribute::eShape)
				{
					//We do not add a dependency for shape on the scene node since shapes are a MeshNode dependency.
				}

				if (MeshNode)
				{
					UnrealSceneNode->SetCustomAssetInstanceUid(MeshNode->GetUniqueID());
					MeshNode->SetSceneInstanceUid(UnrealSceneNode->GetUniqueID());
					UnrealSceneNode->SetCustomGeometricTransform(GeometricTransform);
				}
			}

			void CreateAssetNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer, const FStringView TypeName)
			{
				const FString AssetUniqueID = FFbxHelper::GetNodeAttributeUniqueID(NodeAttribute, TypeName);

				if (UInterchangeBaseNode* AssetNode = NodeContainer.GetNode(AssetUniqueID))
				{
					UnrealSceneNode->SetCustomAssetInstanceUid(AssetNode->GetUniqueID());
				}
			}

			void FFbxScene::CreateCameraNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer)
			{
				CreateAssetNodeReference(UnrealSceneNode, NodeAttribute, NodeContainer, UInterchangeCameraNode::StaticAssetTypeName());
			}

			void FFbxScene::CreateLightNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer)
			{
				CreateAssetNodeReference(UnrealSceneNode, NodeAttribute, NodeContainer, UInterchangeLightNode::StaticAssetTypeName());
			}

			void FFbxScene::AddHierarchyRecursively(UInterchangeSceneNode* UnrealParentNode, FbxNode* Node, FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer)
			{
				FString NodeName = FFbxHelper::GetFbxObjectName(Node);
				FString NodeUniqueID = FFbxHelper::GetFbxNodeHierarchyName(Node);

				UInterchangeSceneNode* UnrealNode = CreateTransformNode(NodeContainer, NodeName, NodeUniqueID);
				check(UnrealNode);
				if (UnrealParentNode)
				{
					UnrealNode->SetParentUid(UnrealParentNode->GetUniqueID());
				}
				
				auto GetConvertedTransform = [Node](FbxAMatrix& NewFbxMatrix)
				{
					FTransform Transform;
					FbxVector4 NewLocalT = NewFbxMatrix.GetT();
					FbxVector4 NewLocalS = NewFbxMatrix.GetS();
					FbxQuaternion NewLocalQ = NewFbxMatrix.GetQ();
					Transform.SetTranslation(FFbxConvert::ConvertPos(NewLocalT));
					Transform.SetScale3D(FFbxConvert::ConvertScale(NewLocalS));
					Transform.SetRotation(FFbxConvert::ConvertRotToQuat(NewLocalQ));

					if (FbxNodeAttribute* NodeAttribute = Node->GetNodeAttribute())
					{
						switch (NodeAttribute->GetAttributeType())
						{
						case FbxNodeAttribute::eCamera:
							Transform = FFbxConvert::AdjustCameraTransform(Transform);
							break;
						case FbxNodeAttribute::eLight:
							Transform = FFbxConvert::AdjustLightTransform(Transform);
							break;
						}
					}

					return Transform;
				};

				//Set the node default transform
				{
					FbxAMatrix GlobalFbxMatrix = Node->EvaluateGlobalTransform();
					FTransform GlobalTransform = GetConvertedTransform(GlobalFbxMatrix);
					UnrealNode->SetCustomGlobalTransform(GlobalTransform);
					if (FbxNode* ParentNode = Node->GetParent())
					{
						FbxAMatrix GlobalFbxParentMatrix = ParentNode->EvaluateGlobalTransform();
						FbxAMatrix	LocalFbxMatrix = GlobalFbxParentMatrix.Inverse() * GlobalFbxMatrix;
						FTransform LocalTransform = GetConvertedTransform(LocalFbxMatrix);
						UnrealNode->SetCustomLocalTransform(LocalTransform);
					}
					else
					{
						//No parent, set the same matrix has the global
						UnrealNode->SetCustomLocalTransform(GlobalTransform);
					}
				}

				int32 AttributeCount = Node->GetNodeAttributeCount();
				for (int32 AttributeIndex = 0; AttributeIndex < AttributeCount; ++AttributeIndex)
				{
					FbxNodeAttribute* NodeAttribute = Node->GetNodeAttributeByIndex(AttributeIndex);
					switch (NodeAttribute->GetAttributeType())
					{
						case FbxNodeAttribute::eUnknown:
						case FbxNodeAttribute::eOpticalReference:
						case FbxNodeAttribute::eOpticalMarker:
						case FbxNodeAttribute::eCachedEffect:
						case FbxNodeAttribute::eMarker:
						case FbxNodeAttribute::eCameraStereo:
						case FbxNodeAttribute::eCameraSwitcher:
						case FbxNodeAttribute::eNurbs:
						case FbxNodeAttribute::ePatch:
						case FbxNodeAttribute::eNurbsCurve:
						case FbxNodeAttribute::eTrimNurbsSurface:
						case FbxNodeAttribute::eBoundary:
						case FbxNodeAttribute::eNurbsSurface:
						case FbxNodeAttribute::eSubDiv:
						case FbxNodeAttribute::eLine:
							//Unsupported attribute
							break;

						case FbxNodeAttribute::eShape: //We do not add a dependency for shape on the scene node since shapes are a MeshNode dependency.
							break;

						case FbxNodeAttribute::eNull:
						case FbxNodeAttribute::eSkeleton:
							//eNull node will be set has a skeleton node
						{
							//Add the joint specialized type
							UnrealNode->AddSpecializedType(FSceneNodeStaticData::GetJointSpecializeTypeString());
							//Get the bind pose transform for this joint
							FbxAMatrix GlobalBindPoseJointMatrix;
							if (FFbxMesh::GetGlobalJointBindPoseTransform(SDKScene, Node, GlobalBindPoseJointMatrix))
							{
								FTransform GlobalBindPoseJointTransform = GetConvertedTransform(GlobalBindPoseJointMatrix);
								UnrealNode->SetCustomBindPoseGlobalTransform(GlobalBindPoseJointTransform);
								//We grab the fbx parent node to compute the local transform
								if (FbxNode* ParentNode = Node->GetParent())
								{
									FbxAMatrix GlobalFbxParentMatrix = ParentNode->EvaluateGlobalTransform();
									FFbxMesh::GetGlobalJointBindPoseTransform(SDKScene, ParentNode, GlobalFbxParentMatrix);
									FbxAMatrix	LocalFbxMatrix = GlobalFbxParentMatrix.Inverse() * GlobalBindPoseJointMatrix;
									FTransform LocalBindPoseJointTransform = GetConvertedTransform(LocalFbxMatrix);
									UnrealNode->SetCustomBindPoseLocalTransform(LocalBindPoseJointTransform);
								}
								else
								{
									//No parent, set the same matrix has the global
									UnrealNode->SetCustomBindPoseLocalTransform(GlobalBindPoseJointTransform);
								}
							}

							//Get time Zero transform for this joint
							{
								//Set the global node transform
								FbxAMatrix GlobalFbxMatrix = Node->EvaluateGlobalTransform(FBXSDK_TIME_ZERO);
								FTransform GlobalTransform = GetConvertedTransform(GlobalFbxMatrix);
								UnrealNode->SetCustomTimeZeroGlobalTransform(GlobalTransform);
								if (FbxNode* ParentNode = Node->GetParent())
								{
									FbxAMatrix GlobalFbxParentMatrix = ParentNode->EvaluateGlobalTransform(FBXSDK_TIME_ZERO);
									FbxAMatrix	LocalFbxMatrix = GlobalFbxParentMatrix.Inverse() * GlobalFbxMatrix;
									FTransform LocalTransform = GetConvertedTransform(LocalFbxMatrix);
									UnrealNode->SetCustomTimeZeroLocalTransform(LocalTransform);
								}
								else
								{
									//No parent, set the same matrix has the global
									UnrealNode->SetCustomTimeZeroLocalTransform(GlobalTransform);
								}
							}

							break;
						}

						case FbxNodeAttribute::eMesh:
						{
							//For Mesh attribute we add the fbx nodes materials
							FFbxMaterial FbxMaterial(Parser);
							FbxMaterial.AddAllNodeMaterials(UnrealNode, Node, NodeContainer);
							//Get the Geometric offset transform and set it in the mesh node
							//The geometric offset is not part of the hierarchy transform, it is not inherited
							FbxAMatrix Geometry;
							FbxVector4 Translation, Rotation, Scaling;
							Translation = Node->GetGeometricTranslation(FbxNode::eSourcePivot);
							Rotation = Node->GetGeometricRotation(FbxNode::eSourcePivot);
							Scaling = Node->GetGeometricScaling(FbxNode::eSourcePivot);
							Geometry.SetT(Translation);
							Geometry.SetR(Rotation);
							Geometry.SetS(Scaling);
							FTransform GeometricTransform = GetConvertedTransform(Geometry);
							CreateMeshNodeReference(UnrealNode, NodeAttribute, NodeContainer, GeometricTransform);
							break;
						}
						case FbxNodeAttribute::eLODGroup:
						{
							UnrealNode->AddSpecializedType(FSceneNodeStaticData::GetLodGroupSpecializeTypeString());
							break;
						}
						case FbxNodeAttribute::eCamera:
						{
							//Add the Camera asset
							CreateCameraNodeReference(UnrealNode, NodeAttribute, NodeContainer);
							break;
						}
						case FbxNodeAttribute::eLight:
						{
							//Add the Light asset
							CreateLightNodeReference(UnrealNode, NodeAttribute, NodeContainer);
							break;
						}
					}
				}

				const int32 ChildCount = Node->GetChildCount();
				for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
				{
					FbxNode* ChildNode = Node->GetChild(ChildIndex);
					AddHierarchyRecursively(UnrealNode, ChildNode, SDKScene, NodeContainer);
				}
			}

			UInterchangeSceneNode* FFbxScene::CreateTransformNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID)
			{
				FString DisplayLabel(NodeName);
				FString NodeUid(NodeUniqueID);
				UInterchangeSceneNode* TransformNode = NewObject<UInterchangeSceneNode>(&NodeContainer, NAME_None);
				if (!ensure(TransformNode))
				{
					UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
					Message->Text = LOCTEXT("NodeAllocationError", "Unable to allocate a node when importing FBX.");
					return nullptr;
				}
				// Creating a UMaterialInterface
				TransformNode->InitializeNode(NodeUid, DisplayLabel, EInterchangeNodeContainerType::TranslatedScene);
				NodeContainer.AddNode(TransformNode);
				return TransformNode;
			}

			void FFbxScene::AddHierarchy(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer)
			{
				 FbxNode* RootNode = SDKScene->GetRootNode();
				 AddHierarchyRecursively(nullptr, RootNode, SDKScene, NodeContainer);
			}
		} //ns Private
	} //ns Interchange
}//ns UE

#undef LOCTEXT_NAMESPACE
