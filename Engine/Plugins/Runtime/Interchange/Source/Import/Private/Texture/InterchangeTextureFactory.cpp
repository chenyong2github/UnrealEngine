// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeTextureFactory.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureLightProfile.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeImportCommon.h"
#include "InterchangeImportLog.h"
#include "InterchangeTexture2DArrayFactoryNode.h"
#include "InterchangeTexture2DArrayNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTextureCubeFactoryNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTextureFactoryNode.h"
#include "InterchangeTextureLightProfileFactoryNode.h"
#include "InterchangeTextureLightProfileNode.h"
#include "InterchangeTextureNode.h"
#include "InterchangeTranslatorBase.h"
#include "Nodes/InterchangeBaseNode.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Texture/InterchangeBlockedTexturePayloadInterface.h"
#include "Texture/InterchangeSlicedTexturePayloadInterface.h"
#include "Texture/InterchangeTextureLightProfilePayloadInterface.h"
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
		UClass* Texture2DArrayFactoryClass = UInterchangeTexture2DArrayFactoryNode::StaticClass();
		UClass* TextureLightProfileFactoryClass = UInterchangeTextureLightProfileFactoryNode::StaticClass();
		UClass* AssetClass = AssetNode->GetClass();
#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
		if (AssetClass->IsChildOf(Texture2DArrayFactoryClass))
		{
			return Texture2DArrayFactoryClass;
		}
		else if (AssetClass->IsChildOf(TextureCubeFactoryClass))
		{
			return TextureCubeFactoryClass;
		}
		else if (AssetClass->IsChildOf(TextureLightProfileFactoryClass))
		{
			return TextureLightProfileFactoryClass;
		}
		else if (AssetClass->IsChildOf(TextureFactoryClass))
		{
			return TextureFactoryClass;
		}
#else
		while (AssetClass)
		{
			if (AssetClass == TextureCubeFactoryClass
				|| AssetClass == TextureFactoryClass
				|| AssetClass == Texture2DArrayFactoryClass
				|| AssetClass == TextureLightProfileFactoryClass)
			{
				return AssetClass;
			}

			AssetClass = AssetClass->GetSuperClass();
		}
#endif

		return nullptr;
	}

	using FTextureFactoryNodeVariant = TVariant<FEmptyVariantState
		, UInterchangeTextureFactoryNode*
		, UInterchangeTextureCubeFactoryNode*
		, UInterchangeTexture2DArrayFactoryNode*
		,UInterchangeTextureLightProfileFactoryNode* >;

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

			if (SupportedFactoryNodeClass == UInterchangeTexture2DArrayFactoryNode::StaticClass())
			{
				return FTextureFactoryNodeVariant(TInPlaceType<UInterchangeTexture2DArrayFactoryNode*>(), static_cast<UInterchangeTexture2DArrayFactoryNode*>(AssetNode));
			}

			if (SupportedFactoryNodeClass == UInterchangeTextureLightProfileFactoryNode::StaticClass())
			{
				return FTextureFactoryNodeVariant(TInPlaceType<UInterchangeTextureLightProfileFactoryNode*>(), static_cast<UInterchangeTextureLightProfileFactoryNode*>(AssetNode));
			}
		}

		return {};
	}

	using FTextureNodeVariant = TVariant<FEmptyVariantState
		, const UInterchangeTexture2DNode*
		, const UInterchangeTextureCubeNode*
		, const UInterchangeTexture2DArrayNode*
		, const UInterchangeTextureLightProfileNode* >;

	FTextureNodeVariant GetTextureNodeVariantFromFactoryVariant(const FTextureFactoryNodeVariant& FactoryVariant, const UInterchangeBaseNodeContainer* NodeContainer)
	{
		FString TextureNodeUniqueID;

		if (UInterchangeTextureFactoryNode* const* TextureFactoryNode = FactoryVariant.TryGet<UInterchangeTextureFactoryNode*>())
		{
			(*TextureFactoryNode)->GetCustomTranslatedTextureNodeUid(TextureNodeUniqueID);
		}
		else if (UInterchangeTextureCubeFactoryNode* const* TextureCubeFactoryNode = FactoryVariant.TryGet<UInterchangeTextureCubeFactoryNode*>())
		{
			(*TextureCubeFactoryNode)->GetCustomTranslatedTextureNodeUid(TextureNodeUniqueID);
		}
		else if (UInterchangeTexture2DArrayFactoryNode* const* Texture2DArrayFactoryNode = FactoryVariant.TryGet<UInterchangeTexture2DArrayFactoryNode*>())
		{
			(*Texture2DArrayFactoryNode)->GetCustomTranslatedTextureNodeUid(TextureNodeUniqueID);
		}
		else if (UInterchangeTextureLightProfileFactoryNode* const* TextureLightProfileFactoryNode = FactoryVariant.TryGet<UInterchangeTextureLightProfileFactoryNode*>())
		{
			(*TextureLightProfileFactoryNode)->GetCustomTranslatedTextureNodeUid(TextureNodeUniqueID);
		}

		if (const UInterchangeBaseNode* TranslatedNode = NodeContainer->GetNode(TextureNodeUniqueID))
		{
			if (const UInterchangeTextureCubeNode* TextureCubeTranslatedNode = Cast<UInterchangeTextureCubeNode>(TranslatedNode))
			{
				return FTextureNodeVariant(TInPlaceType<const UInterchangeTextureCubeNode*>(), TextureCubeTranslatedNode);
			}

			if (const UInterchangeTexture2DArrayNode* Texture2DArrayTranslatedNode = Cast<UInterchangeTexture2DArrayNode>(TranslatedNode))
			{
				return FTextureNodeVariant(TInPlaceType<const UInterchangeTexture2DArrayNode*>(), Texture2DArrayTranslatedNode);
			}

			if (const UInterchangeTextureLightProfileNode* TextureLightProfileTranslatedNode = Cast<UInterchangeTextureLightProfileNode>(TranslatedNode))
			{
				return FTextureNodeVariant(TInPlaceType<const UInterchangeTextureLightProfileNode*>(), TextureLightProfileTranslatedNode);
			}

			if (const UInterchangeTexture2DNode* TextureTranslatedNode = Cast<UInterchangeTexture2DNode>(TranslatedNode))
			{
				return FTextureNodeVariant(TInPlaceType<const UInterchangeTexture2DNode*>(), TextureTranslatedNode);
			}
		}

		return {};
	}

	bool HasPayloadKey(const FTextureNodeVariant& TextureNodeVariant)
	{
		if (const UInterchangeTexture2DNode* const* TextureNode =  TextureNodeVariant.TryGet<const UInterchangeTexture2DNode*>())
		{
			return (*TextureNode)->GetPayLoadKey().IsSet();
		}

		if (const UInterchangeTextureCubeNode* const* TextureCubeNode = TextureNodeVariant.TryGet<const UInterchangeTextureCubeNode*>())
		{
			return (*TextureCubeNode)->GetPayLoadKey().IsSet();
		}

		if (const UInterchangeTexture2DArrayNode* const* Texture2DArrayNode = TextureNodeVariant.TryGet<const UInterchangeTexture2DArrayNode*>())
		{
			return (*Texture2DArrayNode)->GetPayLoadKey().IsSet();
		}

		if (const UInterchangeTextureLightProfileNode* const* TextureLightProfileNode = TextureNodeVariant.TryGet<const UInterchangeTextureLightProfileNode*>())
		{
			return (*TextureLightProfileNode)->GetPayLoadKey().IsSet();
		}

		return false;
	}

	TOptional<FString> GetPayloadKey(const FTextureNodeVariant& TextureNodeVariant)
	{
		if (const UInterchangeTexture2DNode* const* TextureNode =  TextureNodeVariant.TryGet<const UInterchangeTexture2DNode*>())
		{
			return (*TextureNode)->GetPayLoadKey();
		}

		if (const UInterchangeTextureCubeNode* const* TextureCubeNode = TextureNodeVariant.TryGet<const UInterchangeTextureCubeNode*>())
		{
			return (*TextureCubeNode)->GetPayLoadKey();
		}

		if (const UInterchangeTexture2DArrayNode* const* Texture2DArrayNode = TextureNodeVariant.TryGet<const UInterchangeTexture2DArrayNode*>())
		{
			return (*Texture2DArrayNode)->GetPayLoadKey();
		}

		if (const UInterchangeTextureLightProfileNode* const* TextureLightProfileNode = TextureNodeVariant.TryGet<const UInterchangeTextureLightProfileNode*>())
		{
			return (*TextureLightProfileNode)->GetPayLoadKey();
		}

		return {};
	}

	FTexturePayloadVariant GetTexturePayload(const UInterchangeSourceData* SourceData, const FString& PayloadKey, const FTextureNodeVariant& TextureNodeVariant, const UInterchangeTranslatorBase* Translator)
	{
		// Standard texture 2D payload
		if (const UInterchangeTexture2DNode* const* TextureNode =  TextureNodeVariant.TryGet<const UInterchangeTexture2DNode*>())
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

		// Cube or array texture payload 
		if (TextureNodeVariant.IsType<const UInterchangeTextureCubeNode*>() || TextureNodeVariant.IsType<const UInterchangeTexture2DArrayNode*>())
		{
			if (const IInterchangeSlicedTexturePayloadInterface* SlicedTextureTranslator = Cast<IInterchangeSlicedTexturePayloadInterface>(Translator))
			{
				return FTexturePayloadVariant(TInPlaceType<TOptional<FImportSlicedImage>>(), SlicedTextureTranslator->GetSlicedTexturePayloadData(SourceData, PayloadKey));
			}
		}

		// Light Profile
		if (TextureNodeVariant.IsType<const UInterchangeTextureLightProfileNode*>())
		{
			if (const IInterchangeTextureLightProfilePayloadInterface* LightProfileTranslator = Cast<IInterchangeTextureLightProfilePayloadInterface>(Translator))
			{
				return FTexturePayloadVariant(TInPlaceType<TOptional<FImportLightProfile>>(), LightProfileTranslator->GetLightProfilePayloadData(SourceData, PayloadKey));
			}
		}

		return {};
	}

#if WITH_EDITORONLY_DATA
	void SetupTextureSourceData(UTexture* Texture, FImportImage& Image)
	{
		Texture->Source.Init(
			Image.SizeX,
			Image.SizeY,
			/*NumSlices=*/ 1,
			Image.NumMips,
			Image.Format,
			Image.RawData.MoveToShared()
		);

		Texture->CompressionSettings = Image.CompressionSettings;
		Texture->SRGB = Image.bSRGB;

		//If the MipGenSettings was set by the translator, we must apply it before the build
		if (Image.MipGenSettings.IsSet())
		{
			// if the source has mips we keep the mips by default, unless the user changes that
			Texture->MipGenSettings = Image.MipGenSettings.GetValue();
		}
	}

	bool SetupTexture2DSourceData(UTexture2D* Texture2D, FImportBlockedImage& BlockedImage)
	{
		if (BlockedImage.IsValid())
		{
			if (BlockedImage.BlocksData.Num() > 1)
			{

				Texture2D->Source.InitBlocked(
					&BlockedImage.Format,
					BlockedImage.BlocksData.GetData(),
					/*InNumLayers=*/ 1,
					BlockedImage.BlocksData.Num(),
					BlockedImage.RawData.MoveToShared()
					);

				Texture2D->CompressionSettings = BlockedImage.CompressionSettings;
				Texture2D->SRGB = BlockedImage.bSRGB;
				Texture2D->VirtualTextureStreaming = true;

				if (BlockedImage.MipGenSettings.IsSet())
				{
					// if the source has mips we keep the mips by default, unless the user changes that
					Texture2D->MipGenSettings = BlockedImage.MipGenSettings.GetValue();
				}

				return true;
			}
			else
			{
				//Import as a normal texture
				FImportImage Image;
				Image.Format = BlockedImage.Format;
				Image.CompressionSettings = BlockedImage.CompressionSettings;
				Image.bSRGB = BlockedImage.bSRGB;
				Image.MipGenSettings = BlockedImage.MipGenSettings;

				const FTextureSourceBlock& Block = BlockedImage.BlocksData[0];
				Image.SizeX = Block.SizeX;
				Image.SizeY = Block.SizeY;
				Image.NumMips = Block.NumMips;

				Image.RawData = MoveTemp(BlockedImage.RawData);

				SetupTextureSourceData(Texture2D, Image);
				return true;
			}
		}

		return false;
	}

	void SetupTextureSourceData(UTexture* Texture, FImportSlicedImage& SlicedImage)
	{
		Texture->Source.InitLayered(
			SlicedImage.SizeX,
			SlicedImage.SizeY,
			SlicedImage.NumSlice,
			1,
			SlicedImage.NumMips,
			&SlicedImage.Format,
			SlicedImage.RawData.MoveToShared()
			);

		Texture->CompressionSettings = SlicedImage.CompressionSettings;
		Texture->SRGB = SlicedImage.bSRGB;

		if (SlicedImage.MipGenSettings.IsSet())
		{
			// if the source has mips we keep the mips by default, unless the user changes that
			Texture->MipGenSettings = SlicedImage.MipGenSettings.GetValue();
		}
	}

	void SetupTextureSourceData(UTextureLightProfile* TextureLightProfile, FImportLightProfile& LightProfile)
	{
		FImportImage& ImportImage = LightProfile;
		SetupTextureSourceData(TextureLightProfile, ImportImage);

		TextureLightProfile->Brightness = LightProfile.Brightness;
		TextureLightProfile->TextureMultiplier = LightProfile.TextureMultiplier;
	}

	bool CanSetupTexture2DSourceData(FTexturePayloadVariant& TexturePayload)
	{
		if (TOptional<FImportBlockedImage>* BlockedImage = TexturePayload.TryGet<TOptional<FImportBlockedImage>>())
		{
			if (BlockedImage->IsSet())
			{
				return (*BlockedImage)->IsValid();
			}
		}
		else if (TOptional<FImportImage>* Image = TexturePayload.TryGet<TOptional<FImportImage>>())
		{
			if (Image->IsSet())
			{
				return (*Image)->IsValid();
			}
		}
		else if (TOptional<FImportLightProfile>* LightProfile = TexturePayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (LightProfile->IsSet())
			{
				return (*LightProfile)->IsValid();
			}
		}

		return false;
	}

	bool SetupTexture2DSourceData(UTexture2D* Texture2D, FTexturePayloadVariant& TexturePayload)
	{
		if (TOptional<FImportBlockedImage>* BlockedImage = TexturePayload.TryGet<TOptional<FImportBlockedImage>>())
		{
			if (BlockedImage->IsSet())
			{
				return SetupTexture2DSourceData(Texture2D, BlockedImage->GetValue());
			}
		}
		else if (TOptional<FImportImage>* Image = TexturePayload.TryGet<TOptional<FImportImage>>())
		{
			if (Image->IsSet())
			{
				SetupTextureSourceData(Texture2D, Image->GetValue());
				return true;
			}
		}
		else if (TOptional<FImportLightProfile>* OptionalLightProfile = TexturePayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (OptionalLightProfile->IsSet())
			{
				FImportLightProfile& LightProfile = OptionalLightProfile->GetValue();

				if (UTextureLightProfile* TextureLightProfile = Cast<UTextureLightProfile>(Texture2D))
				{
					SetupTextureSourceData(TextureLightProfile, LightProfile);
				}
				else
				{
					SetupTextureSourceData(Texture2D, LightProfile);
				}
			}

			return true;
		}

		return false;
	}

	bool CanSetupTextureCubeSourceData(FTexturePayloadVariant& TexturePayload)
	{
		if (TOptional<FImportSlicedImage>* SlicedImage = TexturePayload.TryGet<TOptional<FImportSlicedImage>>())
		{
			if (SlicedImage->IsSet())
			{
				return (*SlicedImage)->IsValid() && (*SlicedImage)->NumSlice == 6;
			}
		}
		else if (TOptional<FImportImage>* Image = TexturePayload.TryGet<TOptional<FImportImage>>())
		{
			if (Image->IsSet())
			{
				return (*Image)->IsValid();
			}
		}
		else if (TOptional<FImportLightProfile>* LightProfile = TexturePayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (LightProfile->IsSet())
			{
				return (*LightProfile)->IsValid();
			}
		}

		return false;
	}

	bool SetupTextureCubeSourceData(UTextureCube* TextureCube, FTexturePayloadVariant& TexturePayload)
	{
		if (TOptional<FImportSlicedImage>* OptionalSlicedImage = TexturePayload.TryGet<TOptional<FImportSlicedImage>>())
		{
			if (OptionalSlicedImage->IsSet())
			{
				FImportSlicedImage& SlicedImage = OptionalSlicedImage->GetValue();

				// Cube texture always have six slice
				if (SlicedImage.NumSlice == 6)
				{
					SetupTextureSourceData(TextureCube, SlicedImage);
					return true;
				}
			}
		}
		else if (TOptional<FImportImage>* OptionalImage = TexturePayload.TryGet<TOptional<FImportImage>>())
		{
			if (OptionalImage->IsSet())
			{
				FImportImage& Image = OptionalImage->GetValue();
				SetupTextureSourceData(TextureCube, Image);
				return true;
			}
		}
		else if (TOptional<FImportLightProfile>* OptionalLightProfile = TexturePayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (OptionalLightProfile->IsSet())
			{
				FImportLightProfile& LightProfile = OptionalLightProfile->GetValue();
				SetupTextureSourceData(TextureCube, LightProfile);
				return true;
			}
		}

		return false;
	}

	bool CanSetupTexture2DArraySourceData(FTexturePayloadVariant& TexturePayload)
	{
		if (TOptional<FImportSlicedImage>* SlicedImage = TexturePayload.TryGet<TOptional<FImportSlicedImage>>())
		{
			if (SlicedImage->IsSet())
			{
				return (*SlicedImage)->IsValid();
			}
		}
		else if (TOptional<FImportImage>* Image = TexturePayload.TryGet<TOptional<FImportImage>>())
		{
			if (Image->IsSet())
			{
				return (*Image)->IsValid();
			}
		}
		else if (TOptional<FImportLightProfile>* LightProfile = TexturePayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (LightProfile->IsSet())
			{
				return (*LightProfile)->IsValid();
			}
		}

		return false;
	}

	bool SetupTexture2DArraySourceData(UTexture2DArray* Texture2DArray, FTexturePayloadVariant& TexturePayload)
	{
		if (TOptional<FImportSlicedImage>* OptionalSlicedImage = TexturePayload.TryGet<TOptional<FImportSlicedImage>>())
		{
			if (OptionalSlicedImage->IsSet())
			{
				FImportSlicedImage& SlicedImage = OptionalSlicedImage->GetValue();

				SetupTextureSourceData(Texture2DArray, SlicedImage);
				return true;
			}
		}
		else if (TOptional<FImportImage>* OptionalImage = TexturePayload.TryGet<TOptional<FImportImage>>())
		{
			if (OptionalImage->IsSet())
			{
				FImportImage& Image = OptionalImage->GetValue();
				SetupTextureSourceData(Texture2DArray, Image);
				return true;
			}
		}
		else if (TOptional<FImportLightProfile>* OptionalLightProfile = TexturePayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (OptionalLightProfile->IsSet())
			{
				FImportLightProfile& LightProfile = OptionalLightProfile->GetValue();
				SetupTextureSourceData(Texture2DArray, LightProfile);
			}
			return true;
		}

		return false;
	}

	void LogErrorInvalidPayload(const FString& TextureClass, const FString& ObjectName)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: The Payload was invalid for a %s. (%s)"), *TextureClass, *ObjectName);
	}

#endif // WITH_EDITORONLY_DATA
 }


UClass* UInterchangeTextureFactory::GetFactoryClass() const
{
	return UTexture::StaticClass();
}

UObject* UInterchangeTextureFactory::CreateEmptyAsset(const FCreateAssetParams& Arguments)
{
	using namespace  UE::Interchange::Private::InterchangeTextureFactory;

	UObject* Texture = nullptr;

#if WITH_EDITORONLY_DATA
	if (!Arguments.AssetNode)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is null."));
		return nullptr;
	}

	const UClass* TextureClass = Arguments.AssetNode->GetObjectClass();
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

#endif //WITH_EDITORONLY_DATA
	return Texture;
}

// The payload fetching and the heavy operations are done here
UObject* UInterchangeTextureFactory::CreateAsset(const FCreateAssetParams& Arguments)
{
#if !WITH_EDITORONLY_DATA

	UE_LOG(LogInterchangeImport, Error, TEXT("Cannot import texture asset in runtime, this is an editor only feature."));
	return nullptr;

#else

	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeTextureFactory::CreateAsset);

	using namespace  UE::Interchange::Private::InterchangeTextureFactory;

	if (!Arguments.AssetNode)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Asset node parameter is null."));
		return nullptr;
	}

	const UClass* TextureClass = Arguments.AssetNode->GetObjectClass();
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
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: Texture translated node (UInterchangeTexture2DNode) doesnt have a payload key."));
		return nullptr;
	}

	TexturePayload = GetTexturePayload(Arguments.SourceData, PayLoadKey.GetValue(), TextureNodeVariant, Arguments.Translator);

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


	bool bCanSetup = false;
	// Check if the payload is valid for the Texture
	if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
	{
		bCanSetup = CanSetupTexture2DSourceData(TexturePayload);
	}
	else if (UTextureCube* TextureCube = Cast<UTextureCube>(Texture))
	{
		bCanSetup = CanSetupTextureCubeSourceData(TexturePayload);
	}
	else if (UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Texture))
	{
		bCanSetup = CanSetupTexture2DArraySourceData(TexturePayload);
	}

	if (!bCanSetup)
	{
		LogErrorInvalidPayload(Texture->GetClass()->GetName(), Texture->GetName());

		// The texture is not supported
		if (!Arguments.ReimportObject)
		{
			Texture->RemoveFromRoot();
			Texture->MarkPendingKill();
		}
		return nullptr;
	}

	//Getting the file Hash will cache it into the source data
	Arguments.SourceData->GetFileContentHash();

	//The interchange completion task (call in the GameThread after the factories pass), will call PostEditChange which will trig another asynchronous system that will build all texture in parallel

	return Texture;
#endif
}

/* This function is call in the completion task on the main thread, use it to call main thread post creation step for your assets*/
void UInterchangeTextureFactory::PreImportPreCompletedCallback(const FImportPreCompletedCallbackParams& Arguments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeTextureFactory::PreImportPreCompletedCallback);

	check(IsInGameThread());

	UTexture* Texture = Cast<UTexture>(Arguments.ImportedObject);

#if WITH_EDITOR

	// Finish the import on the game thread by doing the setup on the texture here
	if (Texture)
	{
		Texture->PreEditChange(nullptr);

		using namespace UE::Interchange::Private::InterchangeTextureFactory;

		// Setup source data
		if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
		{
			if (!SetupTexture2DSourceData(Texture2D, TexturePayload))
			{
				LogErrorInvalidPayload(Arguments.ImportedObject->GetClass()->GetName(), Arguments.ImportedObject->GetName());
				return;			
			}
		}
		else if (UTextureCube* TextureCube = Cast<UTextureCube>(Texture))
		{
			if (!SetupTextureCubeSourceData(TextureCube, TexturePayload))
			{
				LogErrorInvalidPayload(Arguments.ImportedObject->GetClass()->GetName(), Arguments.ImportedObject->GetName());
				return;
			}
		}
		else if (UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Arguments.ImportedObject))
		{
			if (!SetupTexture2DArraySourceData(Texture2DArray, TexturePayload))
			{
				LogErrorInvalidPayload(Arguments.ImportedObject->GetClass()->GetName(), Arguments.ImportedObject->GetName());
				return;
			}
		}
		else
		{
			// This should never happen.
			ensure(false);
		}


		UInterchangeBaseNode* TextureFactoryNode = Arguments.FactoryNode;
		if (!Arguments.bIsReimport)
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

			UInterchangeBaseNode* CurrentNode = NewObject<UInterchangeBaseNode>(GetTransientPackage(), GetSupportedFactoryNodeClass(TextureFactoryNode));
			UInterchangeBaseNode::CopyStorage(TextureFactoryNode, CurrentNode);
			CurrentNode->FillAllCustomAttributeFromAsset(Texture);
			//Apply reimport strategy
			UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(Arguments.ReimportStrategyFlags, Texture, PreviousNode, CurrentNode, TextureFactoryNode);
		}
	}
#endif //WITH_EDITOR

	Super::PreImportPreCompletedCallback(Arguments);

	//TODO make sure this work at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Texture && Arguments.SourceData))
	{
		//We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		UE::Interchange::FFactoryCommon::FUpdateImportAssetDataParameters UpdateImportAssetDataParameters(Texture
																						 , Texture->AssetImportData
																						 , Arguments.SourceData
																						 , Arguments.NodeUniqueID
																						 , Arguments.NodeContainer
																						 , Arguments.Pipelines);
		Texture->AssetImportData = UE::Interchange::FFactoryCommon::UpdateImportAssetData(UpdateImportAssetDataParameters);
	}
#endif
}