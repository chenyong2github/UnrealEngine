// Copyright Epic Games, Inc. All Rights Reserved.

#include "Texture/InterchangeTextureFactory.h"

#include "Async/TaskGraphInterfaces.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureDefines.h"
#include "Engine/TextureLightProfile.h"
#include "HAL/FileManagerGeneric.h"
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
#include "TextureImportSettings.h"
#include "UDIMUtilities.h"
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
	void SetupTextureSourceData(UTexture* Texture, const FImportImage& Image, UE::Serialization::FEditorBulkData::FSharedBufferWithID&& BufferAndId)
	{
		Texture->Source.Init(
			Image.SizeX,
			Image.SizeY,
			/*NumSlices=*/ 1,
			Image.NumMips,
			Image.Format,
			MoveTemp(BufferAndId)
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

	void SetupTexture2DSourceData(UTexture2D* Texture2D, const FImportBlockedImage& BlockedImage, UE::Serialization::FEditorBulkData::FSharedBufferWithID&& BufferAndId)
	{
		if (BlockedImage.BlocksData.Num() > 1)
		{
			Texture2D->Source.InitBlocked(
				&BlockedImage.Format,
				BlockedImage.BlocksData.GetData(),
				/*InNumLayers=*/ 1,
				BlockedImage.BlocksData.Num(),
				MoveTemp(BufferAndId)
				);

			Texture2D->CompressionSettings = BlockedImage.CompressionSettings;
			Texture2D->SRGB = BlockedImage.bSRGB;
			Texture2D->VirtualTextureStreaming = true;

			if (BlockedImage.MipGenSettings.IsSet())
			{
				// if the source has mips we keep the mips by default, unless the user changes that
				Texture2D->MipGenSettings = BlockedImage.MipGenSettings.GetValue();
			}
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

			SetupTextureSourceData(Texture2D, Image, MoveTemp(BufferAndId));
		}
	}

	void SetupTextureSourceData(UTexture* Texture, const FImportSlicedImage& SlicedImage, UE::Serialization::FEditorBulkData::FSharedBufferWithID&& BufferAndId)
	{
		Texture->Source.InitLayered(
			SlicedImage.SizeX,
			SlicedImage.SizeY,
			SlicedImage.NumSlice,
			1,
			SlicedImage.NumMips,
			&SlicedImage.Format,
			MoveTemp(BufferAndId)
			);

		Texture->CompressionSettings = SlicedImage.CompressionSettings;
		Texture->SRGB = SlicedImage.bSRGB;

		if (SlicedImage.MipGenSettings.IsSet())
		{
			// if the source has mips we keep the mips by default, unless the user changes that
			Texture->MipGenSettings = SlicedImage.MipGenSettings.GetValue();
		}
	}

	void SetupTextureSourceData(UTextureLightProfile* TextureLightProfile, const FImportLightProfile& LightProfile, UE::Serialization::FEditorBulkData::FSharedBufferWithID&& BufferAndId)
	{
		const FImportImage& ImportImage = LightProfile;
		SetupTextureSourceData(TextureLightProfile, ImportImage, MoveTemp(BufferAndId));

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

	void SetupTexture2DSourceData(UTexture2D* Texture2D, FProcessedPayload& ProcessedPayload)
	{
		if (TOptional<FImportBlockedImage>* BlockedImage = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportBlockedImage>>())
		{
			if (BlockedImage->IsSet())
			{
				SetupTexture2DSourceData(Texture2D, BlockedImage->GetValue(), MoveTemp(ProcessedPayload.PayloadAndId));
			}
		}
		else if (TOptional<FImportImage>* Image = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportImage>>())
		{
			if (Image->IsSet())
			{
				SetupTextureSourceData(Texture2D, Image->GetValue(), MoveTemp(ProcessedPayload.PayloadAndId));
			}
		}
		else if (TOptional<FImportLightProfile>* OptionalLightProfile = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (OptionalLightProfile->IsSet())
			{
				FImportLightProfile& LightProfile = OptionalLightProfile->GetValue();

				if (UTextureLightProfile* TextureLightProfile = Cast<UTextureLightProfile>(Texture2D))
				{
					SetupTextureSourceData(TextureLightProfile, LightProfile, MoveTemp(ProcessedPayload.PayloadAndId));
				}
				else
				{
					SetupTextureSourceData(Texture2D, LightProfile, MoveTemp(ProcessedPayload.PayloadAndId));
				}
			}
		}
		else
		{
			// The TexturePayload should be validated before calling this function
			checkNoEntry();
		}

		// The texture has been imported and has no editor specific changes applied so we clear the painted flag.
		Texture2D->bHasBeenPaintedInEditor = false;

		// If the texture is larger than a certain threshold make it VT. This is explicitly done after the
		// application of the existing settings above, so if a texture gets reimported at a larger size it will
		// still be properly flagged as a VT (note: What about reimporting at a lower resolution?)
		static const TConsoleVariableData<int32>* CVarVirtualTexturesEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextures"));
		check(CVarVirtualTexturesEnabled);

		if (CVarVirtualTexturesEnabled->GetValueOnGameThread())
		{
			const int VirtualTextureAutoEnableThreshold = GetDefault<UTextureImportSettings>()->AutoVTSize;
			const int VirtualTextureAutoEnableThresholdPixels = VirtualTextureAutoEnableThreshold * VirtualTextureAutoEnableThreshold;

			// We do this in pixels so a 8192 x 128 texture won't get VT enabled 
			// We use the Source size instead of simple Texture2D->GetSizeX() as this uses the size of the platform data
			// however for a new texture platform data may not be generated yet, and for an reimport of a texture this is the size of the
			// old texture. 
			// Using source size gives one small caveat. It looks at the size before mipmap power of two padding adjustment.
			if (Texture2D->Source.GetSizeX() * Texture2D->Source.GetSizeY() >= VirtualTextureAutoEnableThresholdPixels)
			{
				Texture2D->VirtualTextureStreaming = true;
			}
		}
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

	void SetupTextureCubeSourceData(UTextureCube* TextureCube, FProcessedPayload& ProcessedPayload)
	{
		if (TOptional<FImportSlicedImage>* OptionalSlicedImage = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportSlicedImage>>())
		{
			if (OptionalSlicedImage->IsSet())
			{
				FImportSlicedImage& SlicedImage = OptionalSlicedImage->GetValue();

				// Cube texture always have six slice
				if (SlicedImage.NumSlice == 6)
				{
					SetupTextureSourceData(TextureCube, SlicedImage, MoveTemp(ProcessedPayload.PayloadAndId));
				}
			}
		}
		else if (TOptional<FImportImage>* OptionalImage = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportImage>>())
		{
			if (OptionalImage->IsSet())
			{
				FImportImage& Image = OptionalImage->GetValue();
				SetupTextureSourceData(TextureCube, Image, MoveTemp(ProcessedPayload.PayloadAndId));
			}
		}
		else if (TOptional<FImportLightProfile>* OptionalLightProfile = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (OptionalLightProfile->IsSet())
			{
				FImportLightProfile& LightProfile = OptionalLightProfile->GetValue();
				SetupTextureSourceData(TextureCube, LightProfile, MoveTemp(ProcessedPayload.PayloadAndId));
			}
		}
		else
		{
			// The TexturePayload should be validated before calling this function
			checkNoEntry();
		}
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

	void SetupTexture2DArraySourceData(UTexture2DArray* Texture2DArray, FProcessedPayload& ProcessedPayload)
	{
		if (TOptional<FImportSlicedImage>* OptionalSlicedImage = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportSlicedImage>>())
		{
			if (OptionalSlicedImage->IsSet())
			{
				FImportSlicedImage& SlicedImage = OptionalSlicedImage->GetValue();

				SetupTextureSourceData(Texture2DArray, SlicedImage, MoveTemp(ProcessedPayload.PayloadAndId));
			}
		}
		else if (TOptional<FImportImage>* OptionalImage = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportImage>>())
		{
			if (OptionalImage->IsSet())
			{
				FImportImage& Image = OptionalImage->GetValue();
				SetupTextureSourceData(Texture2DArray, Image, MoveTemp(ProcessedPayload.PayloadAndId));
			}
		}
		else if (TOptional<FImportLightProfile>* OptionalLightProfile = ProcessedPayload.SettingsFromPayload.TryGet<TOptional<FImportLightProfile>>())
		{
			if (OptionalLightProfile->IsSet())
			{
				FImportLightProfile& LightProfile = OptionalLightProfile->GetValue();
				SetupTextureSourceData(Texture2DArray, LightProfile, MoveTemp(ProcessedPayload.PayloadAndId));
			}
		}
		else
		{
			// The TexturePayload should be validated before calling this function
			checkNoEntry();
		}
	}

	void LogErrorInvalidPayload(const FString& TextureClass, const FString& ObjectName)
	{
		UE_LOG(LogInterchangeImport, Error, TEXT("UInterchangeTextureFactory: The Payload was invalid for a %s. (%s)"), *TextureClass, *ObjectName);
	}

	FSharedBuffer MoveRawDataToSharedBuffer(FTexturePayloadVariant& TexturePayload)
	{
		if (TOptional<FImportBlockedImage>* BlockedImage = TexturePayload.TryGet<TOptional<FImportBlockedImage>>())
		{
			return (*BlockedImage)->RawData.MoveToShared();
		}
		else if (TOptional<FImportImage>* Image = TexturePayload.TryGet<TOptional<FImportImage>>())
		{
			return (*Image)->RawData.MoveToShared();
		}
		else if (TOptional<FImportSlicedImage>* SlicedImage = TexturePayload.TryGet<TOptional<FImportSlicedImage>>())
		{
			return (*SlicedImage)->RawData.MoveToShared();
		}
		else if (TOptional<FImportLightProfile>* LightProfile = TexturePayload.TryGet<TOptional<FImportLightProfile>>())
		{
			return (*LightProfile)->RawData.MoveToShared();
		}

		// The TexturePayload should be validated before calling this function
		checkNoEntry();
		return {};
	}

	FProcessedPayload& FProcessedPayload::operator=(UE::Interchange::Private::InterchangeTextureFactory::FTexturePayloadVariant&& InPayloadVariant)
	{
		SettingsFromPayload = MoveTemp(InPayloadVariant);
		PayloadAndId = MoveRawDataToSharedBuffer(SettingsFromPayload);

		return *this;
	}

	bool FProcessedPayload::IsValid() const
	{
		if (SettingsFromPayload.IsType<FEmptyVariantState>())
		{
			return false;
		}

		return true;
	}


	TArray<FString> GetFilesToHash(const FTextureNodeVariant& TextureNodeVariant, const FTexturePayloadVariant& TexturePayload)
	{
		TArray<FString> FilesToHash;
		// Standard texture 2D payload
		if (const UInterchangeTexture2DNode* const* TextureNode = TextureNodeVariant.TryGet<const UInterchangeTexture2DNode*>())
		{
			using namespace UE::Interchange;
			if (const TOptional<FImportBlockedImage>* OptionalBlockedPayload = TexturePayload.TryGet<TOptional<FImportBlockedImage>>())
			{
				if (OptionalBlockedPayload->IsSet())
				{
					const FImportBlockedImage& BlockImage = OptionalBlockedPayload->GetValue();
					TMap<int32, FString> BlockAndFiles = (*TextureNode)->GetSourceBlocks();
					FilesToHash.Reserve(BlockAndFiles.Num());
					for (const FTextureSourceBlock& BlockData : BlockImage.BlocksData)
					{
						if (FString* FilePath = BlockAndFiles.Find(UE::TextureUtilitiesCommon::GetUDIMIndex(BlockData.BlockX, BlockData.BlockY)))
						{
							FilesToHash.Add(*FilePath);
						}
					}
				}

			}
		}
		return FilesToHash;
	}

	FGraphEventArray GenerateHashSourceFilesTasks(const UInterchangeSourceData* SourceData, TArray<FString>&& FilesToHash, TArray<FAssetImportInfo::FSourceFile>& OutSourceFiles)
	{
		struct FHashSourceTaskBase
		{
			/**
				* Returns the name of the thread that this task should run on.
				*
				* @return Always run on any thread.
				*/
			ENamedThreads::Type GetDesiredThread()
			{
				return ENamedThreads::AnyBackgroundThreadNormalTask;
			}

			/**
				* Gets the task's stats tracking identifier.
				*
				* @return Stats identifier.
				*/
			TStatId GetStatId() const
			{
				return GET_STATID(STAT_TaskGraph_OtherTasks);
			}

			/**
				* Gets the mode for tracking subsequent tasks.
				*
				* @return Always track subsequent tasks.
				*/
			static ESubsequentsMode::Type GetSubsequentsMode()
			{
				return ESubsequentsMode::TrackSubsequents;
			}
		};

		FGraphEventArray TasksToDo;

		// We do the hashing of the source files after the import to avoid a bigger memory overhead.
		if (FilesToHash.IsEmpty())
		{
			struct FHashSingleSource : public FHashSourceTaskBase
			{
				FHashSingleSource(const UInterchangeSourceData* InSourceData)
					: SourceData(InSourceData)
				{}

				/**
				 * Performs the actual task.
				 *
				 * @param CurrentThread The thread that this task is executing on.
				 * @param MyCompletionGraphEvent The completion event.
				 */
				void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
				{
					if (SourceData)
					{ 
						//Getting the file Hash will cache it into the source data
						SourceData->GetFileContentHash();
					}
				}

			private:
				const UInterchangeSourceData* SourceData = nullptr;
			};
		
			TasksToDo.Add(TGraphTask<FHashSingleSource>::CreateTask().ConstructAndDispatchWhenReady(SourceData));
		}
		else
		{
			struct FHashMutipleSource : public FHashSourceTaskBase
			{
				FHashMutipleSource(FString&& InFileToHash, FAssetImportInfo::FSourceFile& OutSourceFile)
					: FileToHash(MoveTemp(InFileToHash))
					, SourceFile(OutSourceFile)
				{}

				/**
				 * Performs the actual task.
				 *
				 * @param CurrentThread The thread that this task is executing on.
				 * @param MyCompletionGraphEvent The completion event.
				 */
				void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
				{
					SourceFile.FileHash = FMD5Hash::HashFile(*FileToHash);
					SourceFile.Timestamp =IFileManager::Get().GetTimeStamp(*FileToHash);
					SourceFile.RelativeFilename = MoveTemp(FileToHash);
				}

			private:
				FString FileToHash;
				FAssetImportInfo::FSourceFile& SourceFile;
			};

			OutSourceFiles.AddDefaulted(FilesToHash.Num());

			for (int32 Index = 0; Index < FilesToHash.Num(); ++Index)
			{
				TasksToDo.Add(TGraphTask<FHashMutipleSource>::CreateTask().ConstructAndDispatchWhenReady(MoveTemp(FilesToHash[Index]), OutSourceFiles[Index]));
			}
		}

		return TasksToDo;
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
		UTexture* NewTexture = NewObject<UTexture>(Arguments.Parent, TextureClass, *Arguments.AssetName, RF_Public | RF_Standalone);
		Texture = NewTexture;

		if (UTextureLightProfile* LightProfile = Cast<UTextureLightProfile>(NewTexture))
		{
			LightProfile->AddressX = TA_Clamp;
			LightProfile->AddressY = TA_Clamp;
			LightProfile->MipGenSettings = TMGS_NoMipmaps;
			LightProfile->LODGroup = TEXTUREGROUP_IESLightProfile;
		}
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
		return Texture;
	}

	FGraphEventArray TasksToDo = GenerateHashSourceFilesTasks(Arguments.SourceData, GetFilesToHash(TextureNodeVariant, TexturePayload), SourceFiles);

	// Hash the payload while we hash the source files

	// This will hash the payload to generate a unique ID before passing it to the virtualized bulkdata
	ProcessedPayload = MoveTemp(TexturePayload);

	// Wait for the hashing task(s)
	ENamedThreads::Type NamedThread = IsInGameThread() ? ENamedThreads::GameThread : ENamedThreads::AnyThread;
	FTaskGraphInterface::Get().WaitUntilTasksComplete(TasksToDo, NamedThread);

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
	if (Texture && ProcessedPayload.IsValid())
	{
		Texture->PreEditChange(nullptr);

		using namespace UE::Interchange::Private::InterchangeTextureFactory;

		// Setup source data
		if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
		{
			SetupTexture2DSourceData(Texture2D, ProcessedPayload);
		}
		else if (UTextureCube* TextureCube = Cast<UTextureCube>(Texture))
		{
			SetupTextureCubeSourceData(TextureCube, ProcessedPayload);
		}
		else if (UTexture2DArray* Texture2DArray = Cast<UTexture2DArray>(Arguments.ImportedObject))
		{
			SetupTexture2DArraySourceData(Texture2DArray, ProcessedPayload);
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
			TextureFactoryNode->ApplyAllCustomAttributeToObject(Texture);
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
			CurrentNode->FillAllCustomAttributeFromObject(Texture);
			//Apply reimport strategy
			UE::Interchange::FFactoryCommon::ApplyReimportStrategyToAsset(Texture, PreviousNode, CurrentNode, TextureFactoryNode);
		}
	}
	else
	{
		// The texture is not supported
		if (!Arguments.bIsReimport)
		{
			// Not thread safe. So those should stay on the game thread.
			Texture->RemoveFromRoot();
			Texture->MarkAsGarbage();
		}
	}
#endif //WITH_EDITOR

	Super::PreImportPreCompletedCallback(Arguments);

	//TODO make sure this work at runtime
#if WITH_EDITORONLY_DATA
	if (ensure(Texture && Arguments.SourceData) && ProcessedPayload.IsValid())
	{
		//We must call the Update of the asset source file in the main thread because UAssetImportData::Update execute some delegate we do not control
		UE::Interchange::FFactoryCommon::FSetImportAssetDataParameters SetImportAssetDataParameters(Texture
																						 , Texture->AssetImportData
																						 , Arguments.SourceData
																						 , Arguments.NodeUniqueID
																						 , Arguments.NodeContainer
																						 , Arguments.Pipelines);
		SetImportAssetDataParameters.SourceFiles = MoveTemp(SourceFiles);

		Texture->AssetImportData = UE::Interchange::FFactoryCommon::SetImportAssetData(SetImportAssetDataParameters);
	}
#endif
}
