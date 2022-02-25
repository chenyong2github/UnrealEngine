// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"

class UAssetImportData;
class UInterchangeAssetImportData;
class UInterchangeBaseNode;
class UInterchangeBaseNodeContainer;
class UInterchangePipelineBase;
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
		class INTERCHANGEENGINE_API FFactoryCommon
		{
		public:

			struct INTERCHANGEENGINE_API FUpdateImportAssetDataParameters
			{
				UObject* AssetImportDataOuter = nullptr;
				UAssetImportData* AssetImportData = nullptr;
				const UInterchangeSourceData* SourceData = nullptr;
				FString NodeUniqueID;
				UInterchangeBaseNodeContainer* NodeContainer;
				const TArray<UInterchangePipelineBase*> Pipelines;

				FUpdateImportAssetDataParameters(UObject* InAssetImportDataOuter
																	, UAssetImportData* InAssetImportData
																	, const UInterchangeSourceData* InSourceData
																	, FString InNodeUniqueID
																	, UInterchangeBaseNodeContainer* InNodeContainer
																	, const TArray<UInterchangePipelineBase*>& InPipelines);
			};

			/**
			 * Update the AssetImportData source file of the specified asset in the parameters. Also update the node container and the node unique id.
			 * If the AssetImportData is null it will create one. If the AssetImportData is not an UInterchangeAssetImportData it will create a new one.
			 * @return The source data that should be stored on the asset or nullptr if a parameter is invalid
			 */
			static UAssetImportData* UpdateImportAssetData(FUpdateImportAssetDataParameters& Parameters);

			/**
			 * Update the AssetImportData of the specified asset in the parameters. Also update the node container and the node unique id.
			 * If the AssetImportData is null it will create one. If the AssetImportData is not an UInterchangeAssetImportData it will create a new one.
			 * The file source update is done by calling the function parameter CustomFileSourceUpdate, so its the client responsability to properly update the file source.
			 * @return The source data that should be stored on the asset or nullptr if a parameter is invalid
			 */
			static UAssetImportData* UpdateImportAssetData(FUpdateImportAssetDataParameters& Parameters, TFunctionRef<void(UInterchangeAssetImportData*)> CustomFileSourceUpdate);

#if WITH_EDITORONLY_DATA
			struct INTERCHANGEENGINE_API FSetImportAssetDataParameters : public FUpdateImportAssetDataParameters
			{
				// Allow the factory to provide is own list of source files.
				TArray<FAssetImportInfo::FSourceFile> SourceFiles;

				FSetImportAssetDataParameters(UObject* InAssetImportDataOuter
					, UAssetImportData* InAssetImportData
					, const UInterchangeSourceData* InSourceData
					, FString InNodeUniqueID
					, UInterchangeBaseNodeContainer* InNodeContainer
					, const TArray<UInterchangePipelineBase*>& InPipelines);
			};

			/**
			 * Set the AssetImportData source file of the specified asset in the parameters. Also update the node container and the node unique id.
			 * If the AssetImportData is null it will create one. If the AssetImportData is not an UInterchangeAssetImportData it will create a new one.
			 * @return The source data that should be stored on the asset or nullptr if a parameter is invalid
			 */
			static UAssetImportData* SetImportAssetData(FSetImportAssetDataParameters& Parameters);
#endif // WITH_EDITORONLY_DATA

			/**
			 * Apply the current strategy to the PipelineAssetNode
			 */
			static void ApplyReimportStrategyToAsset(UObject* Asset
											  , UInterchangeBaseNode* PreviousAssetNode
											  , UInterchangeBaseNode* CurrentAssetNode
											  , UInterchangeBaseNode* PipelineAssetNode);
		};
	}
}
