// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"

class UAssetImportData;
class UInterchangeBaseNode;
class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class UObject;

namespace UE
{
	namespace Interchange
	{
		/**
		 * All the code we cannot put in the base factory class because of dependencies (like Engine dep)
		 * Will be available here.
		 */
		class FFactoryCommon
		{
		public:

			struct FUpdateImportAssetDataParameters
			{
				UObject* AssetImportDataOuter = nullptr;
				UAssetImportData* AssetImportData = nullptr;
				const UInterchangeSourceData* SourceData = nullptr;
				FString NodeUniqueID;
				UInterchangeBaseNodeContainer* NodeContainer;

				FUpdateImportAssetDataParameters(UObject* InAssetImportDataOuter
																	, UAssetImportData* InAssetImportData
																	, const UInterchangeSourceData* InSourceData
																	, FString InNodeUniqueID
																	, UInterchangeBaseNodeContainer* InNodeContainer);
			};

			/**
			 * Update the AssetImportData source file of the specified asset in the parameters. Also update the node container and the node unique id.
			 * If the AssetImportData is null it will create one. If the AssetImportData is not an UInterchangeAssetImportData it will create a new one.
			 * @return The source data that should be stored on the asset or nullptr if a parameter is invalid
			 */
			static UAssetImportData* UpdateImportAssetData(FUpdateImportAssetDataParameters& Parameters);

			/**
			 * Apply the current strategy to the PipelineAssetNode
			 */
			static void ApplyReimportStrategyToAsset(const EReimportStrategyFlags ReimportStrategyFlags
											  , UObject* Asset
											  , UInterchangeBaseNode* PreviousAssetNode
											  , UInterchangeBaseNode* CurrentAssetNode
											  , UInterchangeBaseNode* PipelineAssetNode);
		};
	}
}
