// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/DataflowNodes/DataflowNodes.h"
#include "ChaosClothAsset/DataflowNodes/TerminalNode.h"
#include "ChaosClothAsset/DataflowNodes/DatasmithImportNode.h"
#include "ChaosClothAsset/DataflowNodes/ImportNode.h"
#include "ChaosClothAsset/DataflowNodes/StaticMeshImportNode.h"
#include "ChaosClothAsset/DataflowNodes/PatternSelectionNode.h"
#include "ChaosClothAsset/DataflowNodes/DeleteRenderMeshNode.h"
#include "ChaosClothAsset/DataflowNodes/CopySimulationToRenderMeshNode.h"
#include "ChaosClothAsset/DataflowNodes/ReverseNormalsNode.h"
#include "ChaosClothAsset/DataflowNodes/SimulationDefaultConfigNode.h"
#include "ChaosClothAsset/DataflowNodes/BindToRootBoneNode.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY(LogChaosClothAssetDataflowNodes);

namespace UE::Chaos::ClothAsset::DataflowNodes
{
	void Register()
	{
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetTerminalNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetDatasmithImportNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetImportNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetStaticMeshImportNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetPatternSelectionNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetDeleteRenderMeshNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetCopySimulationToRenderMeshNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetReverseNormalsNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetSimulationDefaultConfigNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FChaosClothAssetBindToRootBoneNode);
	}

	void LogAndToastWarning(const FText& Error)
	{
		FNotificationInfo Info(Error);
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info);

		UE_LOG(LogChaosClothAssetDataflowNodes, Warning, TEXT("%s"), *Error.ToString());
	}
}
