// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxScene.h"

#include "CoreMinimal.h"
#include "FbxConvert.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "FbxMaterial.h"
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
				UInterchangeSceneNode* AddHierarchyRecursively(FbxNode* Node, FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer, TArray<FString>& JSonErrorMessages)
				{
					FString NodeName = FFbxHelper::GetFbxObjectName(Node);
					FString NodeUniqueID = FFbxHelper::GetFbxNodeHierarchyName(Node);
					
					UInterchangeSceneNode* UnrealNode = nullptr;

					int32 AttributeCount = Node->GetNodeAttributeCount();
					for (int32 AttributeIndex = 0; AttributeIndex < AttributeCount; ++AttributeIndex)
					{
						const FbxNodeAttribute* NodeAttribute = Node->GetNodeAttributeByIndex(AttributeIndex);
						switch (NodeAttribute->GetAttributeType())
						{
							case FbxNodeAttribute::eLODGroup:
							case FbxNodeAttribute::eMesh:
							case FbxNodeAttribute::eSkeleton:
							case FbxNodeAttribute::eCamera:
							case FbxNodeAttribute::eCameraSwitcher:
							case FbxNodeAttribute::eLight:
							default:
								//Add a transform node
								UnrealNode = FFbxScene::CreateTransformNode(NodeContainer, NodeName, NodeUniqueID, JSonErrorMessages);
								break;
						}
					}
					if (UnrealNode)
					{
						NodeContainer.AddNode(UnrealNode);
						FTransform LocalTransform = FFbxConvert::ConvertTransform(Node->EvaluateLocalTransform());
						UnrealNode->SetCustomLocalTransform(LocalTransform);
						FTransform GlobalTransform = FFbxConvert::ConvertTransform(Node->EvaluateGlobalTransform());
						UnrealNode->SetCustomGlobalTransform(GlobalTransform);
						NodeContainer.AddNode(UnrealNode);
						//Add the dependencies of the material on the correct order
						FFbxMaterial::AddAllNodeMaterials(UnrealNode, Node, NodeContainer, JSonErrorMessages);

						const int32 ChildCount = Node->GetChildCount();
						for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
						{
							FbxNode* ChildNode = Node->GetChild(ChildIndex);
							UInterchangeSceneNode* UnrealChildNode = AddHierarchyRecursively(ChildNode, SDKScene, NodeContainer, JSonErrorMessages);
							if (UnrealChildNode)
							{
								UnrealChildNode->SetParentUID(UnrealNode->GetUniqueID());
							}
						}
					}
					return UnrealNode;
				}
			} //ns Scene

			UInterchangeSceneNode* FFbxScene::CreateTransformNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeName, const FString& NodeUniqueID, TArray<FString>& JSonErrorMessages)
			{
				FName DisplayLabel(*NodeName);
				FName NodeUID(*NodeUniqueID);
				UInterchangeSceneNode* TransformNode = NewObject<UInterchangeSceneNode>(&NodeContainer, NAME_None);
				if (!ensure(TransformNode))
				{
					JSonErrorMessages.Add(TEXT("{\"Msg\" : {\"Type\" : \"Error\",\n\"Msg\" : \"Cannot allocate a node when importing fbx\"}}"));
					return nullptr;
				}
				// Creating a UMaterialInterface
				TransformNode->InitializeNode(NodeUID, DisplayLabel);
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
