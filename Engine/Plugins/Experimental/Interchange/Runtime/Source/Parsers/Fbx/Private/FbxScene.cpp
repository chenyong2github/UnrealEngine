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

			void FFbxScene::CreateMeshNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer)
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

			UInterchangeSceneNode* FFbxScene::AddHierarchyRecursively(FbxNode* Node, FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer)
			{
				FString NodeName = FFbxHelper::GetFbxObjectName(Node);
				FString NodeUniqueID = FFbxHelper::GetFbxNodeHierarchyName(Node);

				UInterchangeSceneNode* UnrealNode = CreateTransformNode(NodeContainer, NodeName, NodeUniqueID);
				check(UnrealNode);
				
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

				{
					//Set the global node transform
					FbxAMatrix GlobalFbxMatrix = Node->EvaluateGlobalTransform();
					FTransform GlobalTransform = GetConvertedTransform(GlobalFbxMatrix);
					UnrealNode->SetCustomGlobalTransform(GlobalTransform);

					//Set the local node transform
					FbxAMatrix LocalFbxMatrix = Node->EvaluateLocalTransform();
					FTransform LocalTransform = GetConvertedTransform(LocalFbxMatrix);
					UnrealNode->SetCustomLocalTransform(LocalTransform);
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
						case FbxNodeAttribute::eNull:
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
						case FbxNodeAttribute::eSkeleton:
						{
							//Add the joint specialized type
							UnrealNode->AddSpecializedType(FSceneNodeStaticData::GetJointSpecializeTypeString());
							break;
						}
						case FbxNodeAttribute::eMesh:
						{
							//For Mesh attribute we add the fbx nodes materials
							FFbxMaterial FbxMaterial(Parser);
							FbxMaterial.AddAllNodeMaterials(UnrealNode, Node, NodeContainer);
							CreateMeshNodeReference(UnrealNode, NodeAttribute, NodeContainer);
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
					UInterchangeSceneNode* UnrealChildNode = AddHierarchyRecursively(ChildNode, SDKScene, NodeContainer);
					if (UnrealChildNode)
					{
						UnrealChildNode->SetParentUid(UnrealNode->GetUniqueID());
					}
				}
				return UnrealNode;
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
				TransformNode->InitializeNode(NodeUid, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_TranslatedScene);
				NodeContainer.AddNode(TransformNode);
				return TransformNode;
			}

			void FFbxScene::AddHierarchy(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer)
			{
				 FbxNode* RootNode = SDKScene->GetRootNode();
				 AddHierarchyRecursively(RootNode, SDKScene, NodeContainer);
			}
		} //ns Private
	} //ns Interchange
}//ns UE

#undef LOCTEXT_NAMESPACE
