// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Texture/InterchangeTextureFactory.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeImportCommon.h"
#include "InterchangeTextureNode.h"
#include "InterchangeTranslatorBase.h"
#include "LogInterchangeImportPlugin.h"
#include "Nodes/InterchangeBaseNode.h"
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
	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(UTexture::StaticClass()))
	{
		return nullptr;
	}

	const UInterchangeTextureNode* TextureNode = Cast<UInterchangeTextureNode>(Arguments.AssetNode);
	if (TextureNode == nullptr)
	{
		return nullptr;
	}

	//
	// Generic 2D Image
	//
	const TOptional<FString>& PayLoadKey = TextureNode->GetPayLoadKey();
	if (!PayLoadKey.IsSet())
	{
		return nullptr;
	}

	const UClass* TextureClass = TextureNode->GetAssetClass();
	if (!ensure(TextureClass && TextureClass->IsChildOf(GetFactoryClass())))
	{
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
		UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Could not create texture asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	Texture->PreEditChange(nullptr);

#endif //WITH_EDITORONLY_DATA
	return Texture;
}

UObject* UInterchangeTextureFactory::CreateAsset(const UInterchangeTextureFactory::FCreateAssetParams& Arguments) const
{
#if !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Cannot import texture asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	if (!Arguments.AssetNode || !Arguments.AssetNode->GetAssetClass()->IsChildOf(UTexture::StaticClass()))
	{
		return nullptr;
	}

	UInterchangeTextureNode* TextureNode = Cast<UInterchangeTextureNode>(Arguments.AssetNode);
	if (TextureNode == nullptr)
	{
		return nullptr;
	}

	const IInterchangeTexturePayloadInterface* TextureTranslator = Cast<IInterchangeTexturePayloadInterface>(Arguments.Translator);
	if (!TextureTranslator)
	{
		return nullptr;
	}

	//
	// Generic 2D Image
	//
	const TOptional<FString>& PayLoadKey = TextureNode->GetPayLoadKey();
	if (!PayLoadKey.IsSet())
	{
		return nullptr;
	}
	const TOptional<UE::Interchange::FImportImage> PayloadData = TextureTranslator->GetTexturePayloadData(Arguments.SourceData, PayLoadKey.GetValue());
	if(!PayloadData.IsSet())
	{
		return nullptr;
	}
	const UE::Interchange::FImportImage& Image = PayloadData.GetValue();

	const UClass* TextureClass = TextureNode->GetAssetClass();
	check(TextureClass && TextureClass->IsChildOf(GetFactoryClass()));

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
		UE_LOG(LogInterchangeImportPlugin, Warning, TEXT("Could not create texture asset %s"), *Arguments.AssetName);
		return nullptr;
	}

	UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
	if (Texture2D)
	{
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

		if(!Arguments.ReimportObject)
		{
			/** Apply all TextureNode custom attributes to the texture asset */
			TextureNode->ApplyAllCustomAttributeToAsset(Texture2D);
		}
		else
		{
			UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(Texture2D->AssetImportData);
			UInterchangeBaseNode* PreviousNode = nullptr;
			if (InterchangeAssetImportData)
			{
				PreviousNode = InterchangeAssetImportData->NodeContainer->GetNode(FName(*InterchangeAssetImportData->NodeUniqueID));
			}
			UInterchangeTextureNode* CurrentNode = NewObject<UInterchangeTextureNode>();
			UInterchangeBaseNode::CopyStorage(TextureNode, CurrentNode);
			CurrentNode->FillAllCustomAttributeFromAsset(Texture2D);
			//Apply reimport strategy
			UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(Arguments.ReimportStrategyFlags, Texture2D, PreviousNode, CurrentNode, TextureNode);
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
																						 , &ImportedTexture->AssetImportData
																						 , Arguments.SourceData
																						 , Arguments.NodeUniqueID
																						 , Arguments.NodeContainer);
		UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
	}
#endif
}