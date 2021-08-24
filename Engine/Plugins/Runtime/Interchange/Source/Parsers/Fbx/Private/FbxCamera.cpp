// Copyright Epic Games, Inc. All Rights Reserved.

#include "FbxCamera.h"

#include "CoreMinimal.h"
#include "FbxAPI.h"
#include "FbxHelper.h"
#include "FbxInclude.h"
#include "InterchangeCameraNode.h"
#include "InterchangeResultsContainer.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#define LOCTEXT_NAMESPACE "InterchangeFbxCamera"

namespace UE
{
	namespace Interchange
	{
		namespace Private
		{
			UInterchangeCameraNode* FFbxCamera::CreateCameraNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUid, const FString& NodeName)
			{
				UInterchangeCameraNode* CameraNode = NewObject<UInterchangeCameraNode>(&NodeContainer, NAME_None);
				if (!ensure(CameraNode))
				{
					UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
					Message->Text = LOCTEXT("CannotAllocateNode", "Cannot allocate a node when importing FBX.");
					return nullptr;
				}

				CameraNode->InitializeNode(NodeUid, NodeName, EInterchangeNodeContainerType::NodeContainerType_TranslatedAsset);
				NodeContainer.AddNode(CameraNode);

				return CameraNode;
			}

			void FFbxCamera::AddCamerasRecursively(FbxNode* Node, UInterchangeBaseNodeContainer& NodeContainer)
			{
				int32 AttributeCount = Node->GetNodeAttributeCount();
				for (int32 AttributeIndex = 0; AttributeIndex < AttributeCount; ++AttributeIndex)
				{
					FbxNodeAttribute* NodeAttribute = Node->GetNodeAttributeByIndex(AttributeIndex);

					if (NodeAttribute->GetAttributeType() == FbxNodeAttribute::eCamera)
					{
						FString NodeName = FFbxHelper::GetNodeAttributeName(NodeAttribute, UInterchangeCameraNode::StaticAssetTypeName());
						FString NodeUid = FFbxHelper::GetNodeAttributeUniqueID(NodeAttribute, UInterchangeCameraNode::StaticAssetTypeName());

						UInterchangeCameraNode* CameraNode = Cast<UInterchangeCameraNode>(NodeContainer.GetNode(NodeUid));

						if (!CameraNode)
						{
							CreateCameraNode(NodeContainer, NodeUid, NodeName);
						}
					}
				}

				const int32 ChildCount = Node->GetChildCount();
				for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
				{
					FbxNode* ChildNode = Node->GetChild(ChildIndex);
					AddCamerasRecursively(ChildNode, NodeContainer);
				}
			}

			void FFbxCamera::AddAllCameras(FbxScene* SDKScene, UInterchangeBaseNodeContainer& NodeContainer)
			{
				AddCamerasRecursively(SDKScene->GetRootNode(), NodeContainer);
			}
		} //ns Private
	} //ns Interchange
}//ns UE

#undef LOCTEXT_NAMESPACE
