// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxScene.h"

#include "CoreMinimal.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "FbxMaterial.h"
#include "FbxMesh.h"
#include "InterchangeSceneNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			namespace Scene
			{
				void CreateMeshNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNode* FbxSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
				{
					UInterchangeBaseNode* MeshNode = nullptr;
					if (NodeAttribute->GetAttributeType() == FbxNodeAttribute::eMesh)
					{
						FbxMesh* Mesh = static_cast<FbxMesh*>(NodeAttribute);
						if (ensure(Mesh))
						{
							FString MeshRefString = FFbxMesh::GetMeshUniqueID(Mesh);
							MeshNode = NodeContainer.GetNode(MeshRefString);
						}
					}
					else if (NodeAttribute->GetAttributeType() == FbxNodeAttribute::eShape)
					{
						//We do not add a dependency for shape on the scene node since shapes are a MeshNode dependency.
					}

					if (MeshNode)
					{
						UnrealSceneNode->AddAssetDependency(MeshNode->GetUniqueID());
					}
				}

				void CreateCameraNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNode* FbxSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
				{
				}

				void CreateLightNodeReference(UInterchangeSceneNode* UnrealSceneNode, FbxNode* FbxSceneNode, FbxNodeAttribute* NodeAttribute, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
				{

				}

				UInterchangeSceneNode* AddHierarchyRecursively(FbxNode* Node, FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
				{
					FString NodeName = FFbxHelper::GetFbxObjectName(Node);
					FString NodeUniqueID = FFbxHelper::GetFbxNodeHierarchyName(Node);

					UInterchangeSceneNode* UnrealNode = FFbxScene::CreateTransformNode(NodeContainer, NodeName, NodeUniqueID, JSonErrorMessages);
					check(UnrealNode);
					
					FTransform GlobalTransform = FFbxConvert::ConvertTransform(Node->EvaluateGlobalTransform());
					UnrealNode->SetCustomGlobalTransform(GlobalTransform);
					FbxNode* FbxParentNode = Node->GetParent();
					FTransform LocalTransform;
					if (FbxParentNode)
					{
						FTransform ParentGlobalTransform = FFbxConvert::ConvertTransform(FbxParentNode->EvaluateGlobalTransform());
						LocalTransform = ParentGlobalTransform.Inverse() * GlobalTransform;
					}
					else
					{
						LocalTransform = GlobalTransform;
					}
					UnrealNode->SetCustomLocalTransform(LocalTransform);

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
								FString SpecializedType = TEXT("Joint");
								UnrealNode->AddSpecializedType(SpecializedType);
								break;
							}
							case FbxNodeAttribute::eMesh:
							{
								//For Mesh attribute we add the fbx nodes materials
								FFbxMaterial::AddAllNodeMaterials(UnrealNode, Node, NodeContainer, JSonErrorMessages);
								CreateMeshNodeReference(UnrealNode, Node, NodeAttribute, NodeContainer, JSonErrorMessages);
								break;
							}
							case FbxNodeAttribute::eLODGroup:
							{
								FString SpecializedType = TEXT("LodGroup");
								UnrealNode->AddSpecializedType(SpecializedType);
								break;
							}
							case FbxNodeAttribute::eCamera:
							{
								//Add the Camera asset
								CreateCameraNodeReference(UnrealNode, Node, NodeAttribute, NodeContainer, JSonErrorMessages);
								break;
							}
							case FbxNodeAttribute::eLight:
							{
								//Add the Light asset
								CreateLightNodeReference(UnrealNode, Node, NodeAttribute, NodeContainer, JSonErrorMessages);
								break;
							}
						}
					}

					const int32 ChildCount = Node->GetChildCount();
					for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
					{
						FbxNode* ChildNode = Node->GetChild(ChildIndex);
						UInterchangeSceneNode* UnrealChildNode = AddHierarchyRecursively(ChildNode, SDKScene, NodeContainer, JSonErrorMessages);
						if (UnrealChildNode)
						{
							UnrealChildNode->SetParentUid(UnrealNode->GetUniqueID());
						}
					}
					return UnrealNode;
				}
			} //ns Scene

			UInterchangeSceneNode* FFbxScene::CreateTransformNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID, TArray<FString>& JSonErrorMessages)
			{
				FString DisplayLabel(NodeName);
				FString NodeUid(NodeUniqueID);
				UInterchangeSceneNode* TransformNode = NewObject<UInterchangeSceneNode>(&NodeContainer, NAME_None);
				if (!ensure(TransformNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UMaterialInterface
				TransformNode->InitializeNode(NodeUid, DisplayLabel, EInterchangeNodeContainerType::NodeContainerType_TranslatedScene);
				NodeContainer.AddNode(TransformNode);
				return TransformNode;
			}

			void FFbxScene::AddHierarchy(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
			{
				 FbxNode* RootNode = SDKScene->GetRootNode();
				 Scene::AddHierarchyRecursively(RootNode, SDKScene, NodeContainer, JSonErrorMessages);
			}
		} //ns Private
	} //ns Interchange
}//ns UE
