// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Texture/InterchangeTextureFactory.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangeTranslatorBase.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Texture/InterchangeBlockedTexturePayloadInterface.h"
#include "Texture/InterchangeTexturePayloadInterface.h"
#include "TextureCompiler.h"


#if WITH_EDITORONLY_DATA

#include "EditorFramework/AssetImportData.h"

#endif //WITH_EDITORONLY_DATA

UClass* UInterchangeTextureFactory::GetFactoryClass() const
{
	return UTexture::StaticClass();
}

UObject* UInterchangeTextureFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments) const
{
	UObject* Texture = nullptr;

#if WITH_EDITORONLY_DATA
	if (!Arguments.AssetNode)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is null."));
		return nullptr;
	}

	if (!Arguments.AssetNode->GetAssetClass()->IsChildOf(UTexture::StaticClass()))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter class doesnt derive from UTexture."));
		return nullptr;
	}

	UInterchangeTextureFactoryNode* TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(Arguments.AssetNode);
	if (TextureFactoryNode == nullptr)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is not a UInterchangeTextureFactoryNode."));
		return nullptr;
	}

	FString TextureNodeUniqueID;
	const UInterchangeTextureNode* TextureTranslatedNode = nullptr;
	if (TextureFactoryNode->GetCustomTranslatedTextureNodeUid(TextureNodeUniqueID))
	{
		TextureTranslatedNode = Cast<UInterchangeTextureNode>(Arguments.NodeContainer->GetNode(TextureNodeUniqueID));
	}

	if (!TextureTranslatedNode)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset factory node (UInterchangeTextureFactoryNode) do not reference a valid UInterchangeTextureNode translated node."));
		return nullptr;
	}

	const TOptional<FString>& PayLoadKey = TextureTranslatedNode->GetPayLoadKey();
	if (!PayLoadKey.IsSet())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Texture translated node (UInterchangeTextureNode) doesnt have a payload key."));
		return nullptr;
	}

	const UClass* TextureClass = TextureFactoryNode->GetAssetClass();
	if (!TextureClass || !TextureClass->IsChildOf(GetFactoryClass()))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Texture factory node asset class doesnt match Texture factory supported class."));
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	// create a new texture or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		Texture = NewObject<UObject>(Arguments.Parent, TextureClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if (ExistingAsset->GetClass()->IsChildOf(TextureClass))
	{
		//This is a reimport, we are just re-updating the source data
		Texture = ExistingAsset;
	}

	if (!Texture)
	{
		UE_LOG(LogInterchangeImport, Warning, TEXT("Could not create texture asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	Texture->PreEditChange(nullptr);

#endif //WITH_EDITORONLY_DATA
	return Texture;
}

UObject* UInterchangeTextureFactory::CreateAsset(const UInterchangeTextureFactory::FCreateAssetParams& Arguments) const
{
#if !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import texture asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is null."));
		return nullptr;
	}

	if (!Arguments.AssetNode->GetAssetClass()->IsChildOf(UTexture::StaticClass()))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter class doesnt derive from UTexture."));
		return nullptr;
	}

	UInterchangeTextureFactoryNode* TextureFactoryNode = Cast<UInterchangeTextureFactoryNode>(Arguments.AssetNode);
	if (TextureFactoryNode == nullptr)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is not a UInterchangeTextureFactoryNode."));
		return nullptr;
	}

	FString TextureNodeUniqueID;
	const UInterchangeTextureNode* TextureTranslatedNode = nullptr;
	if (TextureFactoryNode->GetCustomTranslatedTextureNodeUid(TextureNodeUniqueID))
	{
		TextureTranslatedNode = Cast<UInterchangeTextureNode>(Arguments.NodeContainer->GetNode(TextureNodeUniqueID));
	}

	if (!TextureTranslatedNode)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset factory node (UInterchangeTextureFactoryNode) do not reference a valid UInterchangeTextureNode translated node."));
		return nullptr;
	}

	const TOptional<FString>& PayLoadKey = TextureTranslatedNode->GetPayLoadKey();
	if (!PayLoadKey.IsSet())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Texture translated node (UInterchangeTextureNode) doesnt have a payload key."));
		return nullptr;
	}

	TOptional<UE::Interchange::FImportImage> PayloadData;
	TOptional<UE::Interchange::FImportBlockedImage> BlockedPayloadData;
	if (const IInterchangeTexturePayloadInterface* TextureTranslator = Cast<IInterchangeTexturePayloadInterface>(Arguments.Translator))
	{
		 PayloadData = TextureTranslator->GetTexturePayloadData(Arguments.SourceData, PayLoadKey.GetValue());
	}
	else if (const IInterchangeBlockedTexturePayloadInterface* BlockedTextureTranslator = Cast<IInterchangeBlockedTexturePayloadInterface>(Arguments.Translator))
	{
		/**
		 * Possible improvement store when a file was last modified and use that as heuristic to skip the file assuming that the file is same as the one in the previous import
		 * (how to deal with engine changes or translator changes/hotfix?)
		 */ 
		BlockedPayloadData = BlockedTextureTranslator->GetBlockedTexturePayloadData(TextureTranslatedNode->GetSourceBlocks(), Arguments.SourceData);
	}

	if(!PayloadData.IsSet() && !BlockedPayloadData.IsSet())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Invalid payload."));
		return nullptr;
	}

	const UClass* TextureClass = TextureFactoryNode->GetAssetClass();
	if (!TextureClass || !TextureClass->IsChildOf(GetFactoryClass()))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Texture factory node asset class doesnt match Texture factory supported class."));
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UObject* Texture = nullptr;
	// create a new texture or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		//NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		//The UObject should have been create by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		Texture = NewObject<UObject>(Arguments.Parent, TextureClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if(ExistingAsset->GetClass()->IsChildOf(TextureClass))
	{
		//This is a reimport, we are just re-updating the source data
		Texture = ExistingAsset;
	}

	if (!Texture)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Could not create texture asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
	if (Texture2D)
	{
		if (BlockedPayloadData)
		{
			//
			// Blocked Texture (also know as UDIM)
			//
			const UE::Interchange::FImportBlockedImage& BlockedImage = BlockedPayloadData.GetValue();

			if (BlockedImage.IsBlockedData())
			{
				ETextureSourceFormat Format = BlockedImage.ImagesData[0].Format;
				TextureCompressionSettings CompressionSettings = BlockedImage.ImagesData[0].CompressionSettings;
				bool bSRGB = BlockedImage.ImagesData[0].SRGB;

				TArray<const uint8*> SourceImageDatasPtr;
				SourceImageDatasPtr.Reserve(BlockedImage.ImagesData.Num());
				for (const  UE::Interchange::FImportImage& Image : BlockedImage.ImagesData)
				{
					SourceImageDatasPtr.Add(Image.RawData.GetData());
				}

				Texture2D->Source.InitBlocked(
					&Format,
					BlockedImage.BlocksData.GetData(),
					/*NumSlices=*/ 1,
					BlockedImage.BlocksData.Num(),
					SourceImageDatasPtr.GetData()
				);

				Texture2D->CompressionSettings = CompressionSettings;
				Texture2D->SRGB = bSRGB;
				Texture2D->VirtualTextureStreaming = true;
			}
			else
			{
				//Import as a normal texture
				PayloadData = MoveTemp(BlockedPayloadData.GetValue().ImagesData[0]);
			}
		}

		if (PayloadData)
		{
			//
			// Generic 2D Image
			//
			const UE::Interchange::FImportImage& Image = PayloadData.GetValue();
			Texture2D->Source.Init(
				Image.SizeX,
				Image.SizeY,
				/*NumSlices=*/ 1,
				Image.NumMips,
				Image.Format,
				Image.RawData.GetData()
			);
			Texture2D->CompressionSettings = Image.CompressionSettings;
			Texture2D->SRGB = Image.SRGB;

			//If the MipGenSettings was set by the translator, we must apply it before the build
			if (Image.MipGenSettings.IsSet())
			{
				// if the source has mips we keep the mips by default, unless the user changes that
				Texture2D->MipGenSettings = Image.MipGenSettings.GetValue();
			}

		}


		if(!Arguments.ReimportObject)
		{
			/** Apply all TextureNode custom attributes to the texture asset */
			TextureFactoryNode->ApplyAllCustomAttributeToAsset(Texture2D);
		}
		else
		{
			UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(Texture2D->AssetImportData);
			UInterchangeBaseNode* PreviousNode = nullptr;
			if (InterchangeAssetImportData)
			{
				PreviousNode = InterchangeAssetImportData->NodeContainer->GetNode(InterchangeAssetImportData->NodeUniqueID);
			}
			UInterchangeTextureFactoryNode* CurrentNode = NewObject<UInterchangeTextureFactoryNode>();
			UInterchangeBaseNode::CopyStorage(TextureFactoryNode, CurrentNode);
			CurrentNode->FillAllCustomAttributeFromAsset(Texture2D);
			//Apply reimport strategy
			UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(Arguments.ReimportStrategyFlags, Texture2D, PreviousNode, CurrentNode, TextureFactoryNode);
		}
		
		//Getting the file Hash will cache it into the source data
		Arguments.SourceData->GetFileContentHash();

		//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all texture in parallel
	}
	else
	{
		//The texture is not a UTexture2D
		Texture->RemoveFromRoot();
		Texture->MarkPendingKill();
	}
	return Texture2D;
#endif
}

/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
void UInterchangeTextureFactory::PostImportGameThreadCallback(const FPostImportGameThreadCallbackParams& Arguments) const
{
	check(IsInGameThread());
	Super::PostImportGameThreadCallback(Arguments);
	//TODO make sure this work at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Arguments.ImportedObject && Arguments.SourceData))
	{
		//We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		UTexture* ImportedTexture = CastChecked<UTexture>(Arguments.ImportedObject);
		
		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(ImportedTexture
																						 , ImportedTexture->AssetImportData
																						 , Arguments.SourceData
																						 , Arguments.NodeUniqueID
																						 , Arguments.NodeContainer);
		ImportedTexture->AssetImportData = UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
	}
#endif
}