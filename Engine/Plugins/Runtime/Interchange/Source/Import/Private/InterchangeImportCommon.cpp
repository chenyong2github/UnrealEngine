// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeImportCommon.h"

#include "CoreMinimal.h"
#include "EditorFramework/AssetImportData.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "UObject/Object.h"

namespace UE
{
	namespace Interchange
	{
		FFactoryCommon::FUpdateImportAssetDataParameters::FUpdateImportAssetDataParameters(UObject* InAssetImportDataOuter
																							, UAssetImportData* InAssetImportData
																							, const UInterchangeSourceData* InSourceData
																							, FString InNodeUniqueID
																							, UInterchangeBaseNodeContainer* InNodeContainer)
			: AssetImportDataOuter(InAssetImportDataOuter)
			, AssetImportData(InAssetImportData)
			, SourceData(InSourceData)
			, NodeUniqueID(InNodeUniqueID)
			, NodeContainer(InNodeContainer)
		{
			ensure(AssetImportDataOuter);
			ensure(SourceData);
			ensure(!NodeUniqueID.IsEmpty());
			ensure(NodeContainer);
		}

		UAssetImportData* FFactoryCommon::UpdateImportAssetData(FUpdateImportAssetDataParameters& Parameters)
		{
#if WITH_EDITORONLY_DATA
			if (!ensure(IsInGameThread()))
			{
				return nullptr;
			}
			if (!ensure(Parameters.SourceData && Parameters.AssetImportDataOuter))
			{
				return nullptr;
			}
			//Set the asset import data file source to allow reimport. TODO: manage MD5 Hash properly
			TOptional<FMD5Hash> FileContentHash = Parameters.SourceData->GetFileContentHash();

			UInterchangeAssetImportData* AssetImportData = nullptr;
				
			if (Parameters.AssetImportData)
			{
				AssetImportData = Cast<UInterchangeAssetImportData>(Parameters.AssetImportData);
				if (!AssetImportData)
				{
					AssetImportData = NewObject<UInterchangeAssetImportData>(Parameters.AssetImportDataOuter, NAME_None);
					TArray<FString> Filenames;
					Parameters.AssetImportData->ExtractFilenames(Filenames);
					TArray<FString> Labels;
					Parameters.AssetImportData->ExtractDisplayLabels(Labels);
					if (Filenames.Num() > 1)
					{
						for (int32 FileIndex = 0; FileIndex < Filenames.Num(); ++FileIndex)
						{
							FString Filename = Filenames[FileIndex];
							FString Label;
							if (Labels.IsValidIndex(FileIndex))
							{
								Label = Labels[FileIndex];
							}
							//This is slow since it will hash the file, TODO make sure hashing is done in the create asset task
							AssetImportData->AddFileName(Filename, FileIndex, Label);
						}
					}
				}
			}
			else
			{
				AssetImportData = NewObject<UInterchangeAssetImportData>(Parameters.AssetImportDataOuter, NAME_None);
			}

			ensure(AssetImportData);
			//Update the first filename, TODO for asset using multiple source file we have to update the correct index
			AssetImportData->Update(Parameters.SourceData->GetFilename(), FileContentHash.IsSet() ? &FileContentHash.GetValue() : nullptr);
			//Set the interchange node graph data
			AssetImportData->NodeUniqueID = Parameters.NodeUniqueID;
			FObjectDuplicationParameters DupParam(Parameters.NodeContainer, AssetImportData);
			AssetImportData->NodeContainer = CastChecked<UInterchangeBaseNodeContainer>(StaticDuplicateObjectEx(DupParam));

			// Return the asset import data so it can be set on the imported asset.
			return AssetImportData;
#endif //#if WITH_EDITORONLY_DATA
			return nullptr;
		}

		void FFactoryCommon::ApplyReimportStrategyToAsset(const EReimportStrategyFlags ReimportStrategyFlags
										  , UObject* Asset
										  , UInterchangeBaseNode* PreviousAssetNode
										  , UInterchangeBaseNode* CurrentAssetNode
										  , UInterchangeBaseNode* PipelineAssetNode)
		{
			if (!ensure(PreviousAssetNode) || !ensure(PipelineAssetNode) || !ensure(CurrentAssetNode))
			{
				return;
			}

			switch (ReimportStrategyFlags)
			{
				case EReimportStrategyFlags::ApplyNoProperties:
				{
					//We want to have no effect, we want to keep the original pipeline node.
					//So we copy the previous asset node into the pipeline mode, the pipeline node will be saved
					//in the import asset data, and it will save the original import node.
					UInterchangeBaseNode::CopyStorage(PreviousAssetNode, PipelineAssetNode);
					break;
				}
					
				case EReimportStrategyFlags::ApplyPipelineProperties:
				{
					//Directly apply pipeline node attribute to the asset
					PipelineAssetNode->ApplyAllCustomAttributeToAsset(Asset);
					break;
				}
				
				case EReimportStrategyFlags::ApplyEditorChangedProperties:
				{
					TArray<FAttributeKey> RemovedAttributes;
					TArray<FAttributeKey> AddedAttributes;
					TArray<FAttributeKey> ModifiedAttributes;
					UInterchangeBaseNode::CompareNodeStorage(PreviousAssetNode, CurrentAssetNode, RemovedAttributes, AddedAttributes, ModifiedAttributes);

					//set all ModifedAttributes from the CurrentAssetNode to the pipeline node. This will put back all user changes
					UInterchangeBaseNode::CopyStorageAttributes(CurrentAssetNode, PipelineAssetNode, ModifiedAttributes);
					//Now apply the pipeline node attribute to the asset
					PipelineAssetNode->ApplyAllCustomAttributeToAsset(Asset);
					break;
				}
			}
		}
	}
}
