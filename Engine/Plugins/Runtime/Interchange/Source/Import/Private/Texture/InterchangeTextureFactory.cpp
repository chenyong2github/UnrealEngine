// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeTextureFactory.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeTextureCubeFactoryNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangeTranslatorBase.h"
#include "Misc/TVariant.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Texture/InterchangeBlockedTexturePayloadInterface.h"
#include "Texture/InterchangeSlicedTexturePayloadInterface.h"
#include "Texture/InterchangeTexturePayloadInterface.h"
#include "TextureCompiler.h"
#include "UObject/ObjectMacros.h"

#if WITH_EDITORONLY_DATA

#include "EditorFramework/AssetImportData.h"

#endif //WITH_EDITORONLY_DATA



namespace UE::Interchange::Private::InterchangeTextureFactory
{
	/**
	 * Return the supported class if the node is one otherwise return nullptr
	 */
	UClass* GetSupportedFactoryNodeClass(const UInterchangeBaseNode* AssetNode)
	{
		UClass* TextureCubeFactoryClass = UInterchangeTextureCubeFactoryNode::StaticClass();
		UClass* TextureFactoryClass = UInterchangeTextureFactoryNode::StaticClass();
		UClass* AssetClass = AssetNode->GetClass();
#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
		if(AssetClass->IsChildOf(TextureFactoryClass))
		{
			return TextureFactoryClass;
		}
		else if(AssetClass->IsChildOf(TextureCubeFactoryClass))
		{
			return TextureCubeFactoryClass;
		}
#else
		while (AssetClass)
		{
			if (AssetClass == TextureCubeFactoryClass || AssetClass == TextureFactoryClass)
			{
				return AssetClass;
			}

			AssetClass = AssetClass->GetSuperClass();
		}
#endif

		return nullptr;
	}

	using FTextureFactoryNodeVariant = TVariant<FEmptyVariantState, UInterchangeTextureFactoryNode*, UInterchangeTextureCubeFactoryNode*>; 

	FTextureFactoryNodeVariant GetAsTextureFactoryNodeVariant(UInterchangeBaseNode* AssetNode, UClass* SupportedFactoryNodeClass)
	{
		if (AssetNode)
		{
			if (!SupportedFactoryNodeClass)
			{
				SupportedFactoryNodeClass = GetSupportedFactoryNodeClass(AssetNode);
			}

			if (SupportedFactoryNodeClass == UInterchangeTextureFactoryNode::StaticClass())
			{
				return FTextureFactoryNodeVariant(TInPlaceType<UInterchangeTextureFactoryNode*>(), static_cast<UInterchangeTextureFactoryNode*>(AssetNode));
			}

			if (SupportedFactoryNodeClass == UInterchangeTextureCubeFactoryNode::StaticClass())
			{
				return FTextureFactoryNodeVariant(TInPlaceType<UInterchangeTextureCubeFactoryNode*>(), static_cast<UInterchangeTextureCubeFactoryNode*>(AssetNode));
			}
		}

		return {};
	}

	using FTextureNodeVariant = TVariant<FEmptyVariantState, const UInterchangeTextureNode*, const UInterchangeTextureCubeNode*>; 

	FTextureNodeVariant GetTextureNodeVariantFromFactoryVariant(const FTextureFactoryNodeVariant& FactoryVariant, const UInterchangeBaseNodeContainer* NodeContainer)
	{
		FString TextureNodeUniqueID;

		if (UInterchangeTextureFactoryNode* const* TextureFactoryNode = FactoryVariant.TryGet<UInterchangeTextureFactoryNode*>())
		{
			if ((*TextureFactoryNode)->GetCustomTranslatedTextureNodeUid(TextureNodeUniqueID))
			{
				if (const UInterchangeTextureNode* TextureTranslatedNode = Cast<UInterchangeTextureNode>(NodeContainer->GetNode(TextureNodeUniqueID)))
				{
					return FTextureNodeVariant(TInPlaceType<const UInterchangeTextureNode*>(), TextureTranslatedNode);
				}
			}
		}
		else if (UInterchangeTextureCubeFactoryNode* const* TextureCubeFactoryNode = FactoryVariant.TryGet<UInterchangeTextureCubeFactoryNode*>())
		{
			if ((*TextureCubeFactoryNode)->GetCustomTranslatedTextureNodeUid(TextureNodeUniqueID))
			{
				if (const UInterchangeTextureCubeNode* TextureCubeTranslatedNode = Cast<UInterchangeTextureCubeNode>(NodeContainer->GetNode(TextureNodeUniqueID)))
				{
					return FTextureNodeVariant(TInPlaceType<const UInterchangeTextureCubeNode*>(), TextureCubeTranslatedNode);
				}
			}
		}

		return {};
	}

	bool HasPayloadKey(const FTextureNodeVariant& TextureNodeVariant)
	{
		if (const UInterchangeTextureNode* const* TextureNode =  TextureNodeVariant.TryGet<const UInterchangeTextureNode*>())
		{
			return (*TextureNode)->GetPayLoadKey().IsSet();
		}

		if (const UInterchangeTextureCubeNode* const* TextureCubeNode = TextureNodeVariant.TryGet<const UInterchangeTextureCubeNode*>())
		{
			return (*TextureCubeNode)->GetPayLoadKey().IsSet();
		}

	return false;
	}

	TOptional<FString> GetPayloadKey(const FTextureNodeVariant& TextureNodeVariant)
	{
		if (const UInterchangeTextureNode* const* TextureNode =  TextureNodeVariant.TryGet<const UInterchangeTextureNode*>())
		{
			return (*TextureNode)->GetPayLoadKey();
		}

		if (const UInterchangeTextureCubeNode* const* TextureCubeNode = TextureNodeVariant.TryGet<const UInterchangeTextureCubeNode*>())
		{
			return (*TextureCubeNode)->GetPayLoadKey();
		}

		return {};
	}

	using FTexturePayloadVariant = TVariant<FEmptyVariantState, TOptional<FImportImage>,  TOptional<FImportBlockedImage>,  TOptional<FImportSlicedImage>>;

	FTexturePayloadVariant GetTexturePayload(const UInterchangeSourceData* SourceData, const FString& PayloadKey, const FTextureNodeVariant& TextureNodeVariant, const UInterchangeTranslatorBase* Translator)
	{
		// Standard texture 2D payload
		if (const UInterchangeTextureNode* const* TextureNode =  TextureNodeVariant.TryGet<const UInterchangeTextureNode*>())
		{
			if (const IInterchangeTexturePayloadInterface* TextureTranslator = Cast<IInterchangeTexturePayloadInterface>(Translator))
			{
				return FTexturePayloadVariant(TInPlaceType<TOptional<FImportImage>>(), TextureTranslator->GetTexturePayloadData(SourceData, PayloadKey));
			}
			else if (const IInterchangeBlockedTexturePayloadInterface* BlockedTextureTranslator = Cast<IInterchangeBlockedTexturePayloadInterface>(Translator))
			{
				return FTexturePayloadVariant(TInPlaceType<TOptional<FImportBlockedImage>>(), BlockedTextureTranslator->GetBlockedTexturePayloadData((*TextureNode)->GetSourceBlocks(), SourceData));
			}
		}

		// Cube texture payload
		if (TextureNodeVariant.IsType<const UInterchangeTextureCubeNode*>())
		{
			if (const IInterchangeSlicedTexturePayloadInterface* SlicedTextureTranslator = Cast<IInterchangeSlicedTexturePayloadInterface>(Translator))
			{
				return FTexturePayloadVariant(TInPlaceType<TOptional<FImportSlicedImage>>(), SlicedTextureTranslator->GetSlicedTexturePayloadData(SourceData, PayloadKey));
			}
		}

		return {};
	}

#if WITH_EDITORONLY_DATA
	void SetupTexture2DSourceData(UTexture2D* Texture2D, const FImportImage& Image)
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
	}

	bool SetupTexture2DSourceData(UTexture2D* Texture2D, const FImportBlockedImage& BlockedImage)
	{
		if (BlockedImage.IsBlockedData())
		{
			if (BlockedImage.HasData())
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

				if (BlockedImage.ImagesData[0].MipGenSettings.IsSet())
				{
					// if the source has mips we keep the mips by default, unless the user changes that
					Texture2D->MipGenSettings = BlockedImage.ImagesData[0].MipGenSettings.GetValue();
				}

				return true;
			}
		}
		else
		{
			//Import as a normal texture
			if (BlockedImage.ImagesData.Num() == 1)
			{
				SetupTexture2DSourceData(Texture2D, BlockedImage.ImagesData[0]);
				return true;
			}
		}

		return false;
	}

	bool SetupTexture2DSourceData(UTexture2D* Texture2D, const FTexturePayloadVariant& TexturePayload)
	{
		if (const TOptional<FImportBlockedImage>* BlockedImage = TexturePayload.TryGet<TOptional<FImportBlockedImage>>())
		{
			if (BlockedImage->IsSet())
			{
				return SetupTexture2DSourceData(Texture2D, BlockedImage->GetValue());
			}
		}
		else if (const TOptional<FImportImage>* Image = TexturePayload.TryGet<TOptional<FImportImage>>())
		{
			if (Image->IsSet())
			{
				SetupTexture2DSourceData(Texture2D, Image->GetValue());
				return true;
			}
		}

		return false;
	}

	bool SetupTextureCubeSourceData(UTextureCube* TextureCube, const FTexturePayloadVariant& TexturePayload)
	{
		if (const TOptional<FImportSlicedImage>* OptionalSlicedImage = TexturePayload.TryGet<TOptional<FImportSlicedImage>>())
		{
			if (OptionalSlicedImage->IsSet())
			{
				const FImportSlicedImage& SlicedImage = OptionalSlicedImage->GetValue();

				// Cube texture always have six slice
				if (SlicedImage.NumSlice == 6)
				{
					TextureCube->Source.Init(
						SlicedImage.SizeX,
						SlicedImage.SizeY,
						SlicedImage.NumSlice,
						SlicedImage.NumMips,
						SlicedImage.Format
					);

					TextureCube->CompressionSettings = SlicedImage.CompressionSettings;
					TextureCube->SRGB = SlicedImage.bSRGB;


					uint8* DestMipData[MAX_TEXTURE_MIP_COUNT] = {0};
					int32 MipSize[MAX_TEXTURE_MIP_COUNT] = {0};
					for (int32 MipIndex = 0; MipIndex < SlicedImage.NumMips; ++MipIndex)
					{
						DestMipData[MipIndex] = TextureCube->Source.LockMip(MipIndex);
						MipSize[MipIndex] = TextureCube->Source.CalcMipSize(MipIndex) / 6;
					}

					for (int32 SliceIndex = 0; SliceIndex < 6; ++SliceIndex)
					{
						const uint8* SrcMipData = SlicedImage.GetMipData(0, SliceIndex);
						for (int32 MipIndex = 0; MipIndex < SlicedImage.NumMips; ++MipIndex)
						{
							FMemory::Memcpy(DestMipData[MipIndex] + MipSize[MipIndex] * SliceIndex, SrcMipData, MipSize[MipIndex]);
							SrcMipData += MipSize[MipIndex];
						}
					}

					for (int32 MipIndex = 0; MipIndex < SlicedImage.NumMips; ++MipIndex)
					{
						TextureCube->Source.UnlockMip(MipIndex);
					}

					if (SlicedImage.MipGenSettings.IsSet())
					{
						// if the source has mips we keep the mips by default, unless the user changes that
						TextureCube->MipGenSettings = SlicedImage.MipGenSettings.GetValue();
					}

					return true;
				}
			}
		}

		return false;
	}
#endif // WITH_EDITORONLY_DATA
 }


UClass* UInterchangeTextureFactory::GetFactoryClass() const
{
	return UTexture::StaticClass();
}

UObject* UInterchangeTextureFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments) const
{
	using namespace  UE::Interchange::Private::InterchangeTextureFactory;

	UObject* Texture = nullptr;

#if WITH_EDITORONLY_DATA
	if (!Arguments.AssetNode)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is null."));
		return nullptr;
	}

	const UClass* TextureClass = Arguments.AssetNode->GetAssetClass();
	if (!TextureClass || !TextureClass->IsChildOf(UTexture::StaticClass()))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter class doesnt derive from UTexture."));
		return nullptr;
	}

	UClass* SupportedFactoryNodeClass = GetSupportedFactoryNodeClass(Arguments.AssetNode);
	if (SupportedFactoryNodeClass == nullptr)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is not a UInterchangeTextureFactoryNode or UInterchangeTextureCubeFactoryNode."));
		return nullptr;
	}


	FTextureNodeVariant TextureNodeVariant = GetTextureNodeVariantFromFactoryVariant(GetAsTextureFactoryNodeVariant(Arguments.AssetNode, SupportedFactoryNodeClass), Arguments.NodeContainer);
	if (TextureNodeVariant.IsType<FEmptyVariantState>())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset factory node (%s) do not reference a valid texture translated node.")
				, *SupportedFactoryNodeClass->GetAuthoredName()
			);
		return nullptr;
	}

	if (!HasPayloadKey(TextureNodeVariant))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Texture translated node doesnt have a payload key."));
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

	using namespace  UE::Interchange::Private::InterchangeTextureFactory;

	if (!Arguments.AssetNode)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is null."));
		return nullptr;
	}

	const UClass* TextureClass = Arguments.AssetNode->GetAssetClass();
	if (!TextureClass || !TextureClass->IsChildOf(UTexture::StaticClass()))
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter class doesnt derive from UTexture."));
		return nullptr;
	}

	UClass* SupportedFactoryNodeClass = GetSupportedFactoryNodeClass(Arguments.AssetNode);
	if (SupportedFactoryNodeClass == nullptr)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is not a UInterchangeTextureFactoryNode or UInterchangeTextureCubeFactoryNode."));
		return nullptr;
	}

	FTextureFactoryNodeVariant TextureFactoryNodeVariant = GetAsTextureFactoryNodeVariant(Arguments.AssetNode, SupportedFactoryNodeClass);
	FTextureNodeVariant TextureNodeVariant = GetTextureNodeVariantFromFactoryVariant(TextureFactoryNodeVariant, Arguments.NodeContainer);
	if (TextureNodeVariant.IsType<FEmptyVariantState>())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset factory node (%s) do not reference a valid texture translated node.")
				, *SupportedFactoryNodeClass->GetAuthoredName()
			);
		return nullptr;
	}

	const TOptional<FString>& PayLoadKey = GetPayloadKey(TextureNodeVariant);
	if (!PayLoadKey.IsSet())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Texture translated node (UInterchangeTextureNode) doesnt have a payload key."));
		return nullptr;
	}

	FTexturePayloadVariant TexturePayload = GetTexturePayload(Arguments.SourceData, PayLoadKey.GetValue(), TextureNodeVariant, Arguments.Translator);

	if(TexturePayload.IsType<FEmptyVariantState>())
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Invalid translator couldn't retrive a payload."));
		return nullptr;
	}

	// create an asset if it doesn't exist
	UObject* ExistingAsset = StaticFindObject(nullptr, Arguments.Parent, *Arguments.AssetName);

	UTexture* Texture = nullptr;
	// create a new texture or overwrite existing asset, if possible
	if (!ExistingAsset)
	{
		//NewObject is not thread safe, the asset registry directory watcher tick on the main thread can trig before we finish initializing the UObject and will crash
		//The UObject should have been create by calling CreateEmptyAsset on the main thread.
		check(IsInGameThread());
		Texture = NewObject<UTexture>(Arguments.Parent, TextureClass, *Arguments.AssetName, RF_Public | RF_Standalone);
	}
	else if(ExistingAsset->GetClass()->IsChildOf(TextureClass))
	{
		//This is a reimport, we are just re-updating the source data
		Texture = static_cast<UTexture*>(ExistingAsset);
	}

	if (!Texture)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Could not create texture asset %s"), *Arguments.AssetName);
		return nullptr;
	}


	// Setup source data
	if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		if (!SetupTexture2DSourceData(Texture2D, TexturePayload))
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: The Payload was invalid for a 2D Texture"), *Arguments.AssetName);
			return nullptr;
		}
	}
	else if (UTextureCube* TextureCube = Cast<UTextureCube>(Texture))
	{
		if (!SetupTextureCubeSourceData(TextureCube, TexturePayload))
		{
			UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: The Payload was invalid for a TextureCube"), *Arguments.AssetName);
			return nullptr;
		}
	}
	else
	{
		//The texture is not a UTexture2D
		Texture->RemoveFromRoot();
		Texture->MarkPendingKill();
		return nullptr;
	}

	UInterchangeBaseNode* TextureFactoryNode = Arguments.AssetNode;
	if(!Arguments.ReimportObject)
	{
		/** Apply all TextureNode custom attributes to the texture asset */
		TextureFactoryNode->ApplyAllCustomAttributeToAsset(Texture);
	}
	else
	{
		UInterchangeAssetImportData* InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(Texture->AssetImportData);
		UInterchangeBaseNode* PreviousNode = nullptr;
		if (InterchangeAssetImportData)
		{
			PreviousNode = InterchangeAssetImportData->NodeContainer->GetNode(InterchangeAssetImportData->NodeUniqueID);
		}

		UInterchangeBaseNode* CurrentNode = NewObject<UInterchangeBaseNode>(GetTransientPackage(), SupportedFactoryNodeClass);
		UInterchangeBaseNode::CopyStorage(TextureFactoryNode, CurrentNode);
		CurrentNode->FillAllCustomAttributeFromAsset(Texture);
		//Apply reimport strategy
		UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(Arguments.ReimportStrategyFlags, Texture, PreviousNode, CurrentNode, TextureFactoryNode);
	}
		
	//Getting the file Hash will cache it into the source data
	Arguments.SourceData->GetFileContentHash();

	//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all texture in parallel

	return Texture;
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