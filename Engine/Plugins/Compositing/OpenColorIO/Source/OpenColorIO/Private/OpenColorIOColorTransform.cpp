// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOColorTransform.h"

#include "Containers/SortedMap.h"
#include "Materials/MaterialInterface.h"
#include "Math/PackedVector.h"
#include "Modules/ModuleManager.h"
#include "OpenColorIOConfiguration.h"
#include "OpenColorIOModule.h"
#include "OpenColorIONativeConfiguration.h"
#include "OpenColorIOSettings.h"
#include "UObject/UObjectIterator.h"
#include "DataDrivenShaderPlatformInfo.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(OpenColorIOColorTransform)


#if WITH_EDITOR
#include "DerivedDataCacheInterface.h"
#include "Editor.h"
#include "Interfaces/ITargetPlatform.h"
#include "OpenColorIODerivedDataVersion.h"
#include "OpenColorIOShader.h"

#if WITH_OCIO
#include "OpenColorIO/OpenColorIO.h"
#include <vector>
#endif

#endif //WITH_EDITOR

namespace {
#if WITH_EDITOR && WITH_OCIO
	/*
	 * Get the processor optimization flag.
	 */
	OCIO_NAMESPACE::OptimizationFlags GetProcessorOptimization()
	{
		using namespace OCIO_NAMESPACE;

		return static_cast<OptimizationFlags>(OptimizationFlags::OPTIMIZATION_DEFAULT | OptimizationFlags::OPTIMIZATION_NO_DYNAMIC_PROPERTIES);
	}

	/*
	 * Get the OpenColorIO processor using the transform's config.
	 * NOTE: By default, the library automatically caches and reuses processors.
	 */
	OCIO_NAMESPACE::ConstProcessorRcPtr GetTransformProcessor(const UOpenColorIOColorTransform* InTransform)
	{
		check(InTransform);
		check(InTransform->ConfigurationOwner);

		OCIO_NAMESPACE::ConstConfigRcPtr Config = InTransform->ConfigurationOwner->GetNativeConfig_Internal()->Get();
		if (Config)
		{
			OCIO_NAMESPACE::ContextRcPtr Context = Config->getCurrentContext()->createEditableCopy();

			for (const TPair<FString, FString>& KeyValue : InTransform->GetContextKeyValues())
			{
				Context->setStringVar(TCHAR_TO_ANSI(*KeyValue.Key), TCHAR_TO_ANSI(*KeyValue.Value));
			}

			EOpenColorIOViewTransformDirection DisplayViewDirection;
			if (InTransform->GetDisplayViewDirection(DisplayViewDirection))
			{
				OCIO_NAMESPACE::TransformDirection OcioDirection = static_cast<OCIO_NAMESPACE::TransformDirection>(DisplayViewDirection);

				return Config->getProcessor(
					Context,
					StringCast<ANSICHAR>(*InTransform->SourceColorSpace).Get(),
					StringCast<ANSICHAR>(*InTransform->Display).Get(),
					StringCast<ANSICHAR>(*InTransform->View).Get(),
					OcioDirection);
			}
			else
			{
				return Config->getProcessor(
					Context,
					StringCast<ANSICHAR>(*InTransform->SourceColorSpace).Get(),
					StringCast<ANSICHAR>(*InTransform->DestinationColorSpace).Get()
				);
			}
		}

		return nullptr;
	}

	TUniquePtr<OCIO_NAMESPACE::PackedImageDesc> GetImageDesc(const FImageView& InImage)
	{
		OCIO_NAMESPACE::ChannelOrdering Ordering;
		OCIO_NAMESPACE::BitDepth BitDepth;

		switch (InImage.Format)
		{
		case ERawImageFormat::BGRA8:
			Ordering = OCIO_NAMESPACE::CHANNEL_ORDERING_BGRA;
			BitDepth = OCIO_NAMESPACE::BIT_DEPTH_UINT8;
			break;
		case ERawImageFormat::RGBA16:
			Ordering = OCIO_NAMESPACE::CHANNEL_ORDERING_RGBA;
			BitDepth = OCIO_NAMESPACE::BIT_DEPTH_UINT16;
			break;
		case ERawImageFormat::RGBA16F:
			Ordering = OCIO_NAMESPACE::CHANNEL_ORDERING_RGBA;
			BitDepth = OCIO_NAMESPACE::BIT_DEPTH_F16;
			break;
		case ERawImageFormat::RGBA32F:
			Ordering = OCIO_NAMESPACE::CHANNEL_ORDERING_RGBA;
			BitDepth = OCIO_NAMESPACE::BIT_DEPTH_F32;
			break;
		default:

			UE_LOG(LogOpenColorIO, Log, TEXT("Unsupported texture format."));
			return nullptr;
		}

		return MakeUnique<OCIO_NAMESPACE::PackedImageDesc>(
			InImage.RawData,
			static_cast<long>(InImage.GetWidth()),
			static_cast<long>(InImage.GetHeight()),
			Ordering,
			BitDepth,
			OCIO_NAMESPACE::AutoStride,
			OCIO_NAMESPACE::AutoStride,
			OCIO_NAMESPACE::AutoStride
		);
	};
#endif
}


void UOpenColorIOColorTransform::SerializeOpenColorIOShaderMaps(const TMap<const ITargetPlatform*, TArray<FOpenColorIOTransformResource*>>* PlatformColorTransformResourcesToSavePtr, FArchive& Ar, TArray<FOpenColorIOTransformResource>&  OutLoadedResources)
{
	if (Ar.IsSaving())
	{
		int32 NumResourcesToSave = 0;
		const TArray<FOpenColorIOTransformResource*>* ColorTransformResourcesToSavePtr = nullptr;
		if (Ar.IsCooking())
		{
			check(PlatformColorTransformResourcesToSavePtr);
			auto& PlatformColorTransformResourcesToSave = *PlatformColorTransformResourcesToSavePtr;

			ColorTransformResourcesToSavePtr = PlatformColorTransformResourcesToSave.Find(Ar.CookingTarget());
			check(ColorTransformResourcesToSavePtr != nullptr || (Ar.GetLinker() == nullptr));
			if (ColorTransformResourcesToSavePtr != nullptr)
			{
				NumResourcesToSave = ColorTransformResourcesToSavePtr->Num();
			}
		}

		Ar << NumResourcesToSave;

		if (ColorTransformResourcesToSavePtr)
		{
			const TArray<FOpenColorIOTransformResource*> &ColorTransformResourcesToSave = *ColorTransformResourcesToSavePtr;
			for (int32 ResourceIndex = 0; ResourceIndex < NumResourcesToSave; ResourceIndex++)
			{
				ColorTransformResourcesToSave[ResourceIndex]->SerializeShaderMap(Ar);
			}
		}

	}
	else if (Ar.IsLoading())
	{
		int32 NumLoadedResources = 0;
		Ar << NumLoadedResources;
		OutLoadedResources.Empty(NumLoadedResources);

		for (int32 ResourceIndex = 0; ResourceIndex < NumLoadedResources; ResourceIndex++)
		{
			FOpenColorIOTransformResource LoadedResource;
			LoadedResource.SerializeShaderMap(Ar);
			OutLoadedResources.Add(LoadedResource);
		}
	}
}

void UOpenColorIOColorTransform::ProcessSerializedShaderMaps(UOpenColorIOColorTransform* Owner, TArray<FOpenColorIOTransformResource>& LoadedResources, FOpenColorIOTransformResource* (&OutColorTransformResourcesLoaded)[ERHIFeatureLevel::Num])
{
	check(IsInGameThread());

	for (int32 ResourceIndex = 0; ResourceIndex < LoadedResources.Num(); ResourceIndex++)
	{
		FOpenColorIOTransformResource& LoadedResource = LoadedResources[ResourceIndex];
		FOpenColorIOShaderMap* LoadedShaderMap = LoadedResource.GetGameThreadShaderMap();

		if (LoadedShaderMap && LoadedShaderMap->GetShaderPlatform() == GMaxRHIShaderPlatform)
		{
			ERHIFeatureLevel::Type LoadedFeatureLevel = LoadedShaderMap->GetShaderMapId().FeatureLevel;
			if (!OutColorTransformResourcesLoaded[LoadedFeatureLevel])
			{
				OutColorTransformResourcesLoaded[LoadedFeatureLevel] = Owner->AllocateResource();
			}

			OutColorTransformResourcesLoaded[LoadedFeatureLevel]->SetInlineShaderMap(LoadedShaderMap);
		}
	}
}

void UOpenColorIOColorTransform::GetOpenColorIOLUTKeyGuid(const FString& InProcessorIdentifier, const FName& InName, FGuid& OutLutGuid)
{
#if WITH_EDITOR
	FString DDCKey = FDerivedDataCacheInterface::BuildCacheKey(TEXT("OCIOLUT"), OPENCOLORIO_DERIVEDDATA_VER, *InProcessorIdentifier);

#if WITH_OCIO
	//Keep library version in the DDC key to invalidate it once we move to a new library
	DDCKey += TEXT("OCIOVersion");
	DDCKey += TEXT(OCIO_VERSION);
#endif //WITH_OCIO

	if (!InName.IsNone())
	{
		DDCKey += InName.ToString();
	}

	const uint32 KeyLength = DDCKey.Len() * sizeof(DDCKey[0]);
	uint32 Hash[5];
	FSHA1::HashBuffer(*DDCKey, KeyLength, reinterpret_cast<uint8*>(Hash));
	OutLutGuid = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
#endif
}

UOpenColorIOColorTransform::UOpenColorIOColorTransform(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UOpenColorIOColorTransform::Initialize(UOpenColorIOConfiguration* InOwner, const FString& InSourceColorSpace, const FString& InDestinationColorSpace, const TMap<FString, FString>& InContextKeyValues)
{
	check(InOwner);
	ConfigurationOwner = InOwner;
	ContextKeyValues = InContextKeyValues;
	
	if (InSourceColorSpace == UOpenColorIOConfiguration::WorkingColorSpaceName)
	{
		WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::Source;
	}
	else if(InDestinationColorSpace == UOpenColorIOConfiguration::WorkingColorSpaceName)
	{
		WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::Destination;
	}
	
	return GenerateColorTransformData(InSourceColorSpace, InDestinationColorSpace);
}

bool UOpenColorIOColorTransform::Initialize(UOpenColorIOConfiguration* InOwner, const FString& InSourceColorSpace, const FString& InDisplay, const FString& InView, EOpenColorIOViewTransformDirection InDirection, const TMap<FString, FString>& InContextKeyValues)
{
	check(InOwner);
	ConfigurationOwner = InOwner;
	ContextKeyValues = InContextKeyValues;

	if (InSourceColorSpace == UOpenColorIOConfiguration::WorkingColorSpaceName)
	{
		if (InDirection == EOpenColorIOViewTransformDirection::Forward)
		{
			WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::Source;
		}
		else
		{
			WorkingColorSpaceTransformType = EOpenColorIOWorkingColorSpaceTransform::Destination;
		}
	}

	return GenerateColorTransformData(InSourceColorSpace, InDisplay, InView, InDirection);
}

void UOpenColorIOColorTransform::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if WITH_EDITOR
	SerializeOpenColorIOShaderMaps(&CachedColorTransformResourcesForCooking, Ar, LoadedTransformResources);
#else
	SerializeOpenColorIOShaderMaps(nullptr, Ar, LoadedTransformResources);
#endif

	SerializeLuts(Ar);
}


void UOpenColorIOColorTransform::CacheResourceShadersForCooking(EShaderPlatform InShaderPlatform, const ITargetPlatform* TargetPlatform, const FString& InShaderHash, const FString& InShaderCode, const FString& InRawConfigHash, TArray<FOpenColorIOTransformResource*>& OutCachedResources)
{
	const ERHIFeatureLevel::Type TargetFeatureLevel = GetMaxSupportedFeatureLevel(InShaderPlatform);

	FOpenColorIOTransformResource* NewResource = AllocateResource();
#if WITH_EDITOR
	FName AssetPath = GetOutermost()->GetFName();
#else
	FName AssetPath;
#endif
	NewResource->SetupResource((ERHIFeatureLevel::Type)TargetFeatureLevel, InShaderHash, InShaderCode, InRawConfigHash, GetTransformFriendlyName(), AssetPath, WorkingColorSpaceTransformType);

	const bool bApplyCompletedShaderMap = false;
	const bool bIsCooking = true;
	CacheShadersForResources(InShaderPlatform, NewResource, bApplyCompletedShaderMap, bIsCooking);

	OutCachedResources.Add(NewResource);
}

void UOpenColorIOColorTransform::SerializeLuts(FArchive& Ar)
{

	if (Ar.IsSaving())
	{
		int32 Num3dLutsToSave = 0;
		if (Ar.IsCooking())
		{
			Num3dLutsToSave = Textures.Num();
		}

		Ar << Num3dLutsToSave;

		if (Num3dLutsToSave > 0)
		{
			Ar << Textures;
		}
	}
	else if (Ar.IsLoading())
	{
		int32 NumLoaded3dLuts = 0;
		Ar << NumLoaded3dLuts;

		if (NumLoaded3dLuts > 0)
		{
			//Will only happen on cooked data
			Ar << Textures;
		}
	}
}

void UOpenColorIOColorTransform::CacheResourceTextures()
{
	if (Textures.IsEmpty())
	{
#if WITH_EDITOR && WITH_OCIO
#if !PLATFORM_EXCEPTIONS_DISABLED
		try
#endif
		{
			OCIO_NAMESPACE::ConstProcessorRcPtr TransformProcessor = GetTransformProcessor(this);
			if (TransformProcessor)
			{
				OCIO_NAMESPACE::GpuShaderDescRcPtr ShaderDescription = OCIO_NAMESPACE::GpuShaderDesc::CreateShaderDesc();
				ShaderDescription->setLanguage(OCIO_NAMESPACE::GPU_LANGUAGE_HLSL_DX11);
				ShaderDescription->setFunctionName(StringCast<ANSICHAR>(OpenColorIOShader::OpenColorIOShaderFunctionName).Get());
				ShaderDescription->setResourcePrefix("Ocio");

				OCIO_NAMESPACE::ConstGPUProcessorRcPtr GPUProcessor = nullptr;
				const UOpenColorIOSettings* Settings = GetDefault<UOpenColorIOSettings>();
				check(Settings);

				if (Settings->bUseLegacyProcessor)
				{
					unsigned int EdgeLength = static_cast<unsigned int>(OpenColorIOShader::Lut3dEdgeLength);
					GPUProcessor = TransformProcessor->getOptimizedLegacyGPUProcessor(GetProcessorOptimization(), EdgeLength);
				}
				else
				{
					GPUProcessor = TransformProcessor->getOptimizedGPUProcessor(GetProcessorOptimization());
				}

				GPUProcessor->extractGpuShaderInfo(ShaderDescription);
				
				ensureMsgf(ShaderDescription->getNumDynamicProperties() == 0, TEXT("Dynamic properties are not currently supported."));

				const FString ProcessorID = StringCast<TCHAR>(GPUProcessor->getCacheID()).Get();

				//In editor, it will use what's on DDC if there's something corresponding to the actual data or use that raw data
				//that OCIO library has on board. The textures will be serialized only when cooking.

				// Process 3D luts
				for (uint32 Index = 0; Index < ShaderDescription->getNum3DTextures(); ++Index)
				{
					const char* TextureName = nullptr;
					const char* SamplerName = nullptr;
					unsigned int EdgeLength;
					OCIO_NAMESPACE::Interpolation Interpolation = OCIO_NAMESPACE::INTERP_TETRAHEDRAL;

					ShaderDescription->get3DTexture(Index, TextureName, SamplerName, EdgeLength, Interpolation);
					checkf(TextureName && *TextureName && SamplerName && *SamplerName && EdgeLength > 0, TEXT("Invalid OCIO 3D texture or sampler."));

					const float* TextureValues = 0x0;
					ShaderDescription->get3DTextureValues(Index, TextureValues);
					checkf(TextureValues, TEXT("Failed to read OCIO 3D LUT data."));

					TextureFilter Filter = TF_Bilinear;
					if (Interpolation == OCIO_NAMESPACE::Interpolation::INTERP_NEAREST || Interpolation == OCIO_NAMESPACE::Interpolation::INTERP_TETRAHEDRAL)
					{
						Filter = TF_Nearest;
					}
					const FName TextureFName = FName(TextureName);
					TObjectPtr<UTexture> Result = CreateTexture3DLUT(ProcessorID, TextureFName, EdgeLength, Filter, TextureValues);

					const int32 SlotIndex = TextureFName.GetNumber() - 1; // Rely on FName's index number extraction for convenience
					Textures.Add(SlotIndex, MoveTemp(Result));
				}

				// Process 1D luts
				for (uint32 Index = 0; Index < ShaderDescription->getNumTextures(); ++Index)
				{
					const char* TextureName = nullptr;
					const char* SamplerName = nullptr;
					unsigned TextureWidth = 0;
					unsigned TextureHeight = 0;
					OCIO_NAMESPACE::GpuShaderDesc::TextureType Channel = OCIO_NAMESPACE::GpuShaderDesc::TEXTURE_RGB_CHANNEL;
					OCIO_NAMESPACE::Interpolation Interpolation = OCIO_NAMESPACE::Interpolation::INTERP_LINEAR;

					ShaderDescription->getTexture(Index, TextureName, SamplerName, TextureWidth, TextureHeight, Channel, Interpolation);
					checkf(TextureName && *TextureName && SamplerName && *SamplerName && TextureWidth > 0, TEXT("Invalid OCIO 1D texture or sampler."));

					const float* TextureValues = 0x0;
					ShaderDescription->getTextureValues(Index, TextureValues);
					checkf(TextureValues, TEXT("Failed to read OCIO 1D LUT data."));

					TextureFilter Filter = Interpolation == OCIO_NAMESPACE::Interpolation::INTERP_NEAREST ? TF_Nearest : TF_Bilinear;
					bool bRedChannelOnly = Channel == OCIO_NAMESPACE::GpuShaderCreator::TEXTURE_RED_CHANNEL;
					const FName TextureFName = FName(TextureName);
					TObjectPtr<UTexture> Result = CreateTexture1DLUT(ProcessorID, TextureFName, TextureWidth, TextureHeight, Filter, bRedChannelOnly, TextureValues);

					const int32 SlotIndex = TextureFName.GetNumber() - 1; // Rely on FName's index number extraction for convenience
					Textures.Add(SlotIndex, MoveTemp(Result));
				}

				ensureAlwaysMsgf(Textures.Num() <= (int32)OpenColorIOShader::MaximumTextureSlots, TEXT("Color transform %s exceeds our current limit of %u texture slots. Use the legacy processor instead."), *GetTransformFriendlyName(), OpenColorIOShader::MaximumTextureSlots);
			}
			else
			{
				UE_LOG(LogOpenColorIO, Error, TEXT("Failed to cache texture resource(s) for color transform %s. Configuration file [%s] was invalid."), *GetTransformFriendlyName(), *ConfigurationOwner->ConfigurationFile.FilePath);
			}
		}
#if !PLATFORM_EXCEPTIONS_DISABLED
		catch (OCIO_NAMESPACE::Exception& exception)
		{
			UE_LOG(LogOpenColorIO, Error, TEXT("Failed to cache texture resource(s) for color transform %s. Error message: %s"), *GetTransformFriendlyName(), StringCast<TCHAR>(exception.what()).Get());
		}
#endif
#endif
	}
}

void UOpenColorIOColorTransform::CacheResourceShadersForRendering(bool bRegenerateId)
{
	if (bRegenerateId)
	{
		FlushResourceShaderMaps();
	}

	if (FApp::CanEverRender())
	{
		//Update shader hash to fetch pre-compiled shader from DDC and grab shader code to be able to compile it on the fly if it's missing
		FString ShaderCodeHash;
		FString ShaderCode;
		FString RawConfigHash;
		if (UpdateShaderInfo(ShaderCodeHash, ShaderCode, RawConfigHash))
		{
			//OCIO shaders are simple, we should be compatible with any feature levels. Use the levels required for materials.
			uint32 FeatureLevelsToCompile = UMaterialInterface::GetFeatureLevelsToCompileForAllMaterials();
			while (FeatureLevelsToCompile != 0)
			{
				ERHIFeatureLevel::Type CacheFeatureLevel = (ERHIFeatureLevel::Type)FBitSet::GetAndClearNextBit(FeatureLevelsToCompile);
				const EShaderPlatform ShaderPlatform = GShaderPlatformForFeatureLevel[CacheFeatureLevel];

				FOpenColorIOTransformResource*& TransformResource = ColorTransformResources[CacheFeatureLevel];
				if (TransformResource == nullptr)
				{
					TransformResource = AllocateResource();
				}

#if WITH_EDITOR
				FName AssetPath = GetOutermost()->GetFName();
#else
				FName AssetPath;
#endif
				TransformResource->SetupResource(CacheFeatureLevel, ShaderCodeHash, ShaderCode, RawConfigHash, GetTransformFriendlyName(), AssetPath, WorkingColorSpaceTransformType);

				const bool bApplyCompletedShaderMap = true;

				// If PIE or -game - we don't want to be doing shader cooking asynchronosly.
				bool bIsSynchronous = FApp::IsGame();
				
				CacheShadersForResources(ShaderPlatform, TransformResource, bApplyCompletedShaderMap, bIsSynchronous);
			}
		}
	}
}

void UOpenColorIOColorTransform::CacheShadersForResources(EShaderPlatform InShaderPlatform, FOpenColorIOTransformResource* InResourceToCache, bool bApplyCompletedShaderMapForRendering, bool bIsSynchronous, const ITargetPlatform* TargetPlatform)
{
	const bool bSuccess = InResourceToCache->CacheShaders(InShaderPlatform, TargetPlatform, bApplyCompletedShaderMapForRendering, bIsSynchronous);

	if (!bSuccess)
	{
		UE_ASSET_LOG(LogOpenColorIO, Warning, this, TEXT("Failed to compile OCIO ColorSpace transform %s shader for platform %s.")
			, *LegacyShaderPlatformToShaderFormat(InShaderPlatform).ToString()
			, *InResourceToCache->GetFriendlyName());

		const TArray<FString>& CompileErrors = InResourceToCache->GetCompileErrors();
		for (int32 ErrorIndex = 0; ErrorIndex < CompileErrors.Num(); ErrorIndex++)
		{
			UE_LOG(LogOpenColorIO, Warning, TEXT("	%s"), *CompileErrors[ErrorIndex]);
		}
	}
}

FOpenColorIOTransformResource* UOpenColorIOColorTransform::AllocateResource()
{
	return new FOpenColorIOTransformResource();
}

bool UOpenColorIOColorTransform::GetRenderResources(ERHIFeatureLevel::Type InFeatureLevel, FOpenColorIOTransformResource*& OutShaderResource, TSortedMap<int32, FTextureResource*>& OutTextureResources)
{
	OutShaderResource = ColorTransformResources[InFeatureLevel];
	
	if (OutShaderResource)
	{
		OutTextureResources.Reserve(Textures.Num());

		for (const TPair<int32, TObjectPtr<UTexture>>& Pair : Textures)
		{
			OutTextureResources.Add(Pair.Key, Pair.Value->GetResource());
		}
	}
	
	return OutShaderResource != nullptr;
}

bool UOpenColorIOColorTransform::AreRenderResourcesReady() const
{
	check(IsInGameThread());

	int32 NumShadersRequired = 0;
	int32 NumShadersReady = 0;
	for (const FOpenColorIOTransformResource* Resource : ColorTransformResources)
	{
		if (Resource)
		{
			NumShadersRequired++;

			if (Resource->IsCompilationFinished())
			{
				NumShadersReady++;
			}
		}
	}

	// All of the required shaders should have finished compiling.
	if (NumShadersRequired != NumShadersReady)
	{
		return false;
	}

	// Textures are optional, depending on the transform.
	for (const TPair<int32, TObjectPtr<UTexture>>& TexturePair : Textures)
	{
		const TObjectPtr<UTexture>& Texture = TexturePair.Value;

		if (Texture->GetResource() == nullptr)
		{
			return false;
		}

#if WITH_EDITOR
		// Note: this check is valid the first time a texture is created, but wouldn't be relevant if we updated existing textures.
		if (Texture->IsCompiling())
		{
			return false;
		}
#endif
	}

	return true;
}

bool UOpenColorIOColorTransform::IsTransform(const FString& InSourceColorSpace, const FString& InDestinationColorSpace) const
{
	if (!bIsDisplayViewType)
	{
		return SourceColorSpace == InSourceColorSpace && DestinationColorSpace == InDestinationColorSpace;
	}

	return false;
}

bool UOpenColorIOColorTransform::IsTransform(const FString& InSourceColorSpace, const FString& InDisplay, const FString& InView, EOpenColorIOViewTransformDirection InDirection) const
{
	if (bIsDisplayViewType)
	{
		return SourceColorSpace == InSourceColorSpace && Display == InDisplay && View == InView && DisplayViewDirection == InDirection;
	}

	return false;
}

#if WITH_EDITOR
bool UOpenColorIOColorTransform::EditorTransformImage(const FImageView& InOutImage) const
{
#if WITH_OCIO
	using namespace OCIO_NAMESPACE;

#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		ConstProcessorRcPtr Processor = GetTransformProcessor(this);
		if (Processor && ConfigurationOwner)
		{
			TUniquePtr<PackedImageDesc> ImageDesc = GetImageDesc(InOutImage);
			if (ImageDesc)
			{
				ConstConfigRcPtr InterchangeConfig = IOpenColorIOModule::Get().GetNativeInterchangeConfig_Internal()->Get();
				ConstConfigRcPtr Config = ConfigurationOwner->GetNativeConfig_Internal()->Get();
				BitDepth BitDepth = ImageDesc->getBitDepth();

				// Conditionally apply a conversion from the working color space to interchange space
				if (WorkingColorSpaceTransformType == EOpenColorIOWorkingColorSpaceTransform::Source)
				{
					ConstProcessorRcPtr	InterchangeProcessor = InterchangeConfig->GetProcessorFromConfigs(
						InterchangeConfig,
						StringCast<ANSICHAR>(UOpenColorIOConfiguration::WorkingColorSpaceName).Get(),
						Config,
						Config->getCanonicalName(OpenColorIOInterchangeName));

					ConstCPUProcessorRcPtr InterchangeCPUProcessor = InterchangeProcessor->getOptimizedCPUProcessor(BitDepth, BitDepth, OPTIMIZATION_DEFAULT);
					InterchangeCPUProcessor->apply(*ImageDesc);
				}

				// Apply the main color transformation
				ConstCPUProcessorRcPtr CPUProcessor = Processor->getOptimizedCPUProcessor(BitDepth, BitDepth, OPTIMIZATION_DEFAULT);
				CPUProcessor->apply(*ImageDesc);

				// Conditionally apply a conversion from the interchange space to the working color space
				if (WorkingColorSpaceTransformType == EOpenColorIOWorkingColorSpaceTransform::Destination)
				{
					ConstProcessorRcPtr	InterchangeProcessor = InterchangeConfig->GetProcessorFromConfigs(
						Config,
						Config->getCanonicalName(OpenColorIOInterchangeName),
						InterchangeConfig,
						StringCast<ANSICHAR>(UOpenColorIOConfiguration::WorkingColorSpaceName).Get());

					ConstCPUProcessorRcPtr InterchangeCPUProcessor = InterchangeProcessor->getOptimizedCPUProcessor(BitDepth, BitDepth, OPTIMIZATION_DEFAULT);
					InterchangeCPUProcessor->apply(*ImageDesc);
				}
				
				return true;
			}
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& exception)
	{
		UE_LOG(LogOpenColorIO, Log, TEXT("Failed to transform image. Error message: %s"), StringCast<TCHAR>(exception.what()).Get());
	}
#endif
#endif // WITH_OCIO

	return false;
}

bool UOpenColorIOColorTransform::EditorTransformImage(const FImageView& SrcImage, const FImageView& DestImage) const
{
#if WITH_OCIO
	using namespace OCIO_NAMESPACE;
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		ConstProcessorRcPtr Processor = GetTransformProcessor(this);
		if (Processor && ConfigurationOwner)
		{
			TUniquePtr<PackedImageDesc> SrcImageDesc = GetImageDesc(SrcImage);
			TUniquePtr<PackedImageDesc> DestImageDesc = GetImageDesc(DestImage);
			if (SrcImageDesc && DestImageDesc)
			{
				ConstConfigRcPtr	InterchangeConfig = IOpenColorIOModule::Get().GetNativeInterchangeConfig_Internal()->Get();
				ConstConfigRcPtr	Config = ConfigurationOwner->GetNativeConfig_Internal()->Get();

				BitDepth SrcBitDepth = SrcImageDesc->getBitDepth();
				BitDepth DestBitDepth = DestImageDesc->getBitDepth();

				// Conditionally apply a conversion from the working color space to interchange space
				if (WorkingColorSpaceTransformType == EOpenColorIOWorkingColorSpaceTransform::Source)
				{
					ConstProcessorRcPtr	InterchangeProcessor = InterchangeConfig->GetProcessorFromConfigs(
						InterchangeConfig,
						StringCast<ANSICHAR>(UOpenColorIOConfiguration::WorkingColorSpaceName).Get(),
						Config,
						Config->getCanonicalName(OpenColorIOInterchangeName));

					ConstCPUProcessorRcPtr InterchangeCPUProcessor = InterchangeProcessor->getOptimizedCPUProcessor(SrcBitDepth, SrcBitDepth, OPTIMIZATION_DEFAULT);
					InterchangeCPUProcessor->apply(*SrcImageDesc);
				}


				// Apply the main color transformation
				ConstCPUProcessorRcPtr CPUProcessor = Processor->getOptimizedCPUProcessor(SrcBitDepth, DestBitDepth, OPTIMIZATION_DEFAULT);
				CPUProcessor->apply(*SrcImageDesc, *DestImageDesc);

				// Conditionally apply a conversion from the interchange space to the working color space
				if (WorkingColorSpaceTransformType == EOpenColorIOWorkingColorSpaceTransform::Destination)
				{
					ConstProcessorRcPtr	InterchangeProcessor = InterchangeConfig->GetProcessorFromConfigs(
						Config,
						Config->getCanonicalName(OpenColorIOInterchangeName),
						InterchangeConfig,
						StringCast<ANSICHAR>(UOpenColorIOConfiguration::WorkingColorSpaceName).Get());

					ConstCPUProcessorRcPtr InterchangeCPUProcessor = InterchangeProcessor->getOptimizedCPUProcessor(DestBitDepth, DestBitDepth, OPTIMIZATION_DEFAULT);
					InterchangeCPUProcessor->apply(*DestImageDesc);
				}

				return true;
			}
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& exception)
	{
		UE_LOG(LogOpenColorIO, Log, TEXT("Failed to transform image. Error message: %s"), StringCast<TCHAR>(exception.what()).Get());
	}
#endif
#endif // WITH_OCIO

	return false;
}
#endif // WITH_EDITOR

bool UOpenColorIOColorTransform::GetDisplayViewDirection(EOpenColorIOViewTransformDirection& OutDirection) const
{
	if (bIsDisplayViewType)
	{
		OutDirection = DisplayViewDirection;
	}

	return bIsDisplayViewType;
}

void UOpenColorIOColorTransform::AllColorTransformsCacheResourceShadersForRendering()
{
	for (TObjectIterator<UOpenColorIOColorTransform> It; It; ++It)
	{
		UOpenColorIOColorTransform* Transform = *It;

		Transform->CacheResourceShadersForRendering(false);
	}
}

bool UOpenColorIOColorTransform::GenerateColorTransformData(const FString& InSourceColorSpace, const FString& InDestinationColorSpace)
{
#if WITH_EDITOR && WITH_OCIO
	if (InSourceColorSpace.IsEmpty() || InDestinationColorSpace.IsEmpty())
	{
		return false;
	}

	SourceColorSpace = InSourceColorSpace;
	DestinationColorSpace = InDestinationColorSpace;
	bIsDisplayViewType = false;

	CacheResourceTextures();
	CacheResourceShadersForRendering(true);

	return true;
#endif //WITH_EDITOR
	return false;
}

bool UOpenColorIOColorTransform::GenerateColorTransformData(const FString& InSourceColorSpace, const FString& InDisplay, const FString& InView, EOpenColorIOViewTransformDirection InDirection)
{
#if WITH_EDITOR && WITH_OCIO
	if (InSourceColorSpace.IsEmpty() || InDisplay.IsEmpty() || InView.IsEmpty())
	{
		return false;
	}

	SourceColorSpace = InSourceColorSpace;
	DestinationColorSpace.Empty();
	Display = InDisplay;
	View = InView;
	bIsDisplayViewType = true;
	DisplayViewDirection = InDirection;

	CacheResourceTextures();
	CacheResourceShadersForRendering(true);

	return true;
#endif //WITH_EDITOR
	return false;
}

FString UOpenColorIOColorTransform::GetTransformFriendlyName()
{
	if (bIsDisplayViewType)
	{
		switch (DisplayViewDirection)
		{
		case EOpenColorIOViewTransformDirection::Forward:
			return SourceColorSpace + TEXT(" to ") + Display + TEXT(" - ") + View;
		case EOpenColorIOViewTransformDirection::Inverse:
			return Display + TEXT(" - ") + View + TEXT(" to ") + SourceColorSpace;
		default:
			checkNoEntry();
			return FString();
		}
	}
	else
	{
		return SourceColorSpace + TEXT(" to ") + DestinationColorSpace;
	}
}

bool UOpenColorIOColorTransform::UpdateShaderInfo(FString& OutShaderCodeHash, FString& OutShaderCode, FString& OutRawConfigHash)
{
#if WITH_EDITOR
#if WITH_OCIO
#if !PLATFORM_EXCEPTIONS_DISABLED
	try
#endif
	{
		OCIO_NAMESPACE::ConstProcessorRcPtr TransformProcessor = GetTransformProcessor(this);
		if (TransformProcessor)
		{
			OCIO_NAMESPACE::GpuShaderDescRcPtr ShaderDescription = OCIO_NAMESPACE::GpuShaderDesc::CreateShaderDesc();
			ShaderDescription->setLanguage(OCIO_NAMESPACE::GPU_LANGUAGE_HLSL_DX11);
			ShaderDescription->setFunctionName(StringCast<ANSICHAR>(OpenColorIOShader::OpenColorIOShaderFunctionName).Get());
			ShaderDescription->setResourcePrefix("Ocio");

			OCIO_NAMESPACE::ConstGPUProcessorRcPtr GPUProcessor = nullptr;
			const UOpenColorIOSettings* Settings = GetDefault<UOpenColorIOSettings>();
			check(Settings);

			if (Settings->bUseLegacyProcessor)
			{
				unsigned int EdgeLength = static_cast<unsigned int>(OpenColorIOShader::Lut3dEdgeLength);
				GPUProcessor = TransformProcessor->getOptimizedLegacyGPUProcessor(GetProcessorOptimization(), EdgeLength);
			}
			else
			{
				GPUProcessor = TransformProcessor->getOptimizedGPUProcessor(GetProcessorOptimization());
			}
			GPUProcessor->extractGpuShaderInfo(ShaderDescription);

			ensureMsgf(ShaderDescription->getNumDynamicProperties() == 0, TEXT("We do not currently support dynamic properties."));

			FString GLSLShaderCode = StringCast<TCHAR>(ShaderDescription->getShaderText()).Get();

			OutShaderCodeHash = StringCast<TCHAR>(ShaderDescription->getCacheID()).Get();
			OutShaderCode = StringCast<TCHAR>(ShaderDescription->getShaderText()).Get();
			OCIO_NAMESPACE::ConstConfigRcPtr CurrentConfig = ConfigurationOwner->GetNativeConfig_Internal()->Get();
			OutRawConfigHash = StringCast<TCHAR>(CurrentConfig->getCacheID()).Get();
			return true;
		}
		else
		{
			UE_LOG(LogOpenColorIO, Error, TEXT("Failed to fetch shader info for color transform %s. Configuration file [%s] was invalid."), *GetTransformFriendlyName(), *ConfigurationOwner->ConfigurationFile.FilePath);
		}
	}
#if !PLATFORM_EXCEPTIONS_DISABLED
	catch (OCIO_NAMESPACE::Exception& exception)
	{
		UE_LOG(LogOpenColorIO, Error, TEXT("Failed to fetch shader info for color transform %s. Error message: %s"), *GetTransformFriendlyName(), StringCast<TCHAR>(exception.what()).Get());
	}
#endif

	return false;
#else
	//Avoid triggering errors when building maps on build machine.
#if PLATFORM_WINDOWS
	if (!GIsBuildMachine)
	{
		UE_LOG(LogOpenColorIO, Error, TEXT("Can't update shader, OCIO library isn't present."));
	}
#endif //PLATFORM_WINDOWS
	return false;
#endif //WITH_OCIO
#else
	return true; //When not in editor, shaders have been cooked so we're not relying on the library data anymore.
#endif
}

TObjectPtr<UTexture> UOpenColorIOColorTransform::CreateTexture3DLUT(const FString& InProcessorIdentifier, const FName& InName, uint32 InLutLength, TextureFilter InFilter, const float* InSourceData)
{
	TObjectPtr<UVolumeTexture> OutTexture = nullptr;

#if WITH_EDITOR && WITH_OCIO
	check(InSourceData);
	const UOpenColorIOSettings* Settings = GetDefault<UOpenColorIOSettings>();

	/* Note here that while it is possible to create proper 32f textures using [UTexture]::CreateTransient and reparenting
	 * via Texture->Rename(nullptr, this), cooking would fail as it remains unsupported currently for those formats.
	 * (The same note applies to 1D LUT creation.) */

	OutTexture = NewObject<UVolumeTexture>(this, InName);

	//Initializes source data with the raw LUT. If it's found in DDC, the resulting platform data will be fetched from there. 
	//If not, the source data will be used to generate the platform data.
	OutTexture->MipGenSettings = TMGS_NoMipmaps;
	OutTexture->SRGB = 0;
	OutTexture->LODGroup = TEXTUREGROUP_ColorLookupTable;
	if (Settings->bUse32fLUT)
	{
		// 32f resources have to be explicitely requested using this compression setting.
		OutTexture->CompressionSettings = TextureCompressionSettings::TC_HDR_F32;
	}
	else
	{
		OutTexture->CompressionNone = true;
	}
	OutTexture->Filter = InFilter;
	OutTexture->AddressMode = TextureAddress::TA_Clamp;
	OutTexture->Source.Init(InLutLength, InLutLength, InLutLength, /*NumMips=*/ 1, TSF_RGBA32F, nullptr);

	FLinearColor* MipData = reinterpret_cast<FLinearColor*>(OutTexture->Source.LockMip(0));
	for (uint32 Z = 0; Z < InLutLength; ++Z)
	{
		for (uint32 Y = 0; Y < InLutLength; Y++)
		{
			FLinearColor* Row = &MipData[Y * InLutLength + Z * InLutLength * InLutLength];
			const float* Source = &InSourceData[Y * InLutLength * 3 + Z * InLutLength * InLutLength * 3];
			for (uint32 X = 0; X < InLutLength; X++)
			{
				FLinearColor& TargetColor = Row[X];
				TargetColor.R = Source[X * 3 + 0];
				TargetColor.G = Source[X * 3 + 1];
				TargetColor.B = Source[X * 3 + 2];
				TargetColor.A = 1.0f;
			}
		}
	}
	OutTexture->Source.UnlockMip(0);

	//Generate a Guid from the identifier received from the library and our DDC version.
	FGuid LutGuid;
	GetOpenColorIOLUTKeyGuid(InProcessorIdentifier, InName, LutGuid);
	OutTexture->Source.SetId(LutGuid, true);

	//Process our new texture to be usable in rendering pipeline.
	OutTexture->UpdateResource();
#endif

	return OutTexture;
}

TObjectPtr<UTexture> UOpenColorIOColorTransform::CreateTexture1DLUT(const FString& InProcessorIdentifier, const FName& InName, uint32 InTextureWidth, uint32 InTextureHeight, TextureFilter InFilter, bool bRedChannelOnly, const float* InSourceData)
{
	TObjectPtr<UTexture2D> OutTexture = nullptr;

#if WITH_EDITOR && WITH_OCIO
	check(InSourceData);
	const UOpenColorIOSettings* Settings = GetDefault<UOpenColorIOSettings>();

	OutTexture = NewObject<UTexture2D>(this, InName);
	OutTexture->MipGenSettings = TMGS_NoMipmaps;
	OutTexture->SRGB = 0;
	OutTexture->LODGroup = TEXTUREGROUP_ColorLookupTable;
	if (Settings->bUse32fLUT)
	{
		// 32f resources have to be explicitely requested using this compression setting.
		OutTexture->CompressionSettings = bRedChannelOnly ? TextureCompressionSettings::TC_SingleFloat : TextureCompressionSettings::TC_HDR_F32;
	}
	else
	{
		OutTexture->CompressionNone = true;
	}
	OutTexture->Filter = InFilter;
	OutTexture->AddressX = TextureAddress::TA_Clamp;
	OutTexture->AddressY = TextureAddress::TA_Clamp;

	if (bRedChannelOnly)
	{
		OutTexture->Source.Init(InTextureWidth, InTextureHeight, /*NumSlices=*/ 1, /*NumMips=*/ 1, TSF_R32F, reinterpret_cast<const uint8*>(InSourceData));
	}
	else
	{
		OutTexture->Source.Init(InTextureWidth, InTextureHeight, /*NumSlices=*/ 1, /*NumMips=*/ 1, TSF_RGBA32F, nullptr);

		FLinearColor* MipData = reinterpret_cast<FLinearColor*>(OutTexture->Source.LockMip(0));
		for (uint32 Y = 0; Y < InTextureHeight; Y++)
		{
			FLinearColor* Row = &MipData[Y * InTextureWidth];
			const float* Source = &InSourceData[Y * InTextureWidth * 3];
			for (uint32 X = 0; X < InTextureWidth; X++)
			{
				FLinearColor& TargetColor = Row[X];
				TargetColor.R = Source[X * 3 + 0];
				TargetColor.G = Source[X * 3 + 1];
				TargetColor.B = Source[X * 3 + 2];
				TargetColor.A = 1.0f;
			}
		}
		OutTexture->Source.UnlockMip(0);
	}

	//Generate a Guid from the identifier received from the library and our DDC version.
	FGuid LutGuid;
	GetOpenColorIOLUTKeyGuid(InProcessorIdentifier, InName, LutGuid);
	OutTexture->Source.SetId(LutGuid, true);

	//Process our new texture to be usable in rendering pipeline.
	OutTexture->UpdateResource();
#endif

	return OutTexture;
}

void UOpenColorIOColorTransform::FlushResourceShaderMaps()
{
	if (FApp::CanEverRender())
	{
		for (int32 Index = 0; Index < ERHIFeatureLevel::Num; Index++)
		{
			if (ColorTransformResources[Index])
			{
				ColorTransformResources[Index]->ReleaseShaderMap();
				ColorTransformResources[Index] = nullptr;
			}
		}
	}
}

void UOpenColorIOColorTransform::PostLoad()
{
	Super::PostLoad();

	if (FApp::CanEverRender())
	{
		ProcessSerializedShaderMaps(this, LoadedTransformResources, ColorTransformResources);
	}
	else
	{
		// Discard all loaded material resources
		for (FOpenColorIOTransformResource& Resource : LoadedTransformResources)
		{
			Resource.DiscardShaderMap();
		}
	}

	if (!ConfigurationOwner && GetOuter())
	{
		UE_LOG(LogOpenColorIO, Verbose, TEXT("ConfigurationOwner is null. Assigning Outer to ConfigurationOwner."));
		ConfigurationOwner = Cast<UOpenColorIOConfiguration>(GetOuter());
	}

	//To be able to fetch OCIO data, make sure our config owner has been postloaded.
	if (ConfigurationOwner)
	{
		ConfigurationOwner->ConditionalPostLoad();
		CacheResourceTextures();
		CacheResourceShadersForRendering(false);
	}
	else
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Outer is not an UOpenColorIOConfiguration. Outer class: %s, Outer name: %s. "), *GetOuter()->GetClass()->GetName(), *GetOuter()->GetName());
	}

	// Empty the list of loaded resources, we don't need it anymore
	LoadedTransformResources.Empty();

}

#if WITH_EDITORONLY_DATA
void UOpenColorIOColorTransform::DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses, const UClass* SpecificSubclass)
{
	Super::DeclareConstructClasses(OutConstructClasses, SpecificSubclass);
	OutConstructClasses.Add(FTopLevelAssetPath(UTexture2D::StaticClass()));
	OutConstructClasses.Add(FTopLevelAssetPath(UVolumeTexture::StaticClass()));
}
#endif

void UOpenColorIOColorTransform::BeginDestroy()
{
	Super::BeginDestroy();

	ReleaseFence.BeginFence();
}

bool UOpenColorIOColorTransform::IsReadyForFinishDestroy()
{
	bool bReady = Super::IsReadyForFinishDestroy();

	return bReady && ReleaseFence.IsFenceComplete();
}

void UOpenColorIOColorTransform::FinishDestroy()
{
	ReleaseResources();

	Super::FinishDestroy();
}

#if WITH_EDITOR

void UOpenColorIOColorTransform::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	TArray<FName> DesiredShaderFormats;
	TargetPlatform->GetAllTargetedShaderFormats(DesiredShaderFormats);

	TArray<FOpenColorIOTransformResource*>* CachedColorTransformResourceForPlatformPtr = &CachedColorTransformResourcesForCooking.FindOrAdd(TargetPlatform);

	if (DesiredShaderFormats.Num() > 0)
	{
		//Need to re-update shader data when cooking. They won't have been previously fetched.
		FString ShaderCodeHash;
		FString ShaderCode;
		FString RawConfigHash;
		if (UpdateShaderInfo(ShaderCodeHash, ShaderCode, RawConfigHash))
		{
			// Cache for all the shader formats that the cooking target requires
			for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
			{
				const EShaderPlatform LegacyShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);
				// Begin caching shaders for the target platform and store the FOpenColorIOTransformResource being compiled into CachedColorTransformResourcesForCooking
				CacheResourceShadersForCooking(LegacyShaderPlatform, TargetPlatform, ShaderCodeHash, ShaderCode, RawConfigHash, *CachedColorTransformResourceForPlatformPtr);
			}
		}
	}
}

bool UOpenColorIOColorTransform::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
{
	const TArray<FOpenColorIOTransformResource*>* CachedColorTransformResourcesForPlatform = CachedColorTransformResourcesForCooking.Find(TargetPlatform);

	if (CachedColorTransformResourcesForPlatform)
	{
		for (const FOpenColorIOTransformResource* const TransformResource : *CachedColorTransformResourcesForPlatform)
		{
			if (TransformResource->IsCompilationFinished() == false)
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

void UOpenColorIOColorTransform::ClearCachedCookedPlatformData(const ITargetPlatform *TargetPlatform)
{
	TArray<FOpenColorIOTransformResource*>* CachedColorTransformResourcesForPlatform = CachedColorTransformResourcesForCooking.Find(TargetPlatform);
	if (CachedColorTransformResourcesForPlatform != nullptr)
	{
		for (const FOpenColorIOTransformResource* const TransformResource : *CachedColorTransformResourcesForPlatform)
		{
			delete TransformResource;
		}
	}
	CachedColorTransformResourcesForCooking.Remove(TargetPlatform);
}

void UOpenColorIOColorTransform::ClearAllCachedCookedPlatformData()
{
	for (auto It : CachedColorTransformResourcesForCooking)
	{
		TArray<FOpenColorIOTransformResource*>& CachedColorTransformResourcesForPlatform = It.Value;
		for (int32 CachedResourceIndex = 0; CachedResourceIndex < CachedColorTransformResourcesForPlatform.Num(); CachedResourceIndex++)
		{
			delete CachedColorTransformResourcesForPlatform[CachedResourceIndex];
		}
	}

	CachedColorTransformResourcesForCooking.Empty();
}

#endif //WITH_EDITOR

void UOpenColorIOColorTransform::ReleaseResources()
{
	for (int32 FeatureLevelIndex = 0; FeatureLevelIndex < ERHIFeatureLevel::Num; FeatureLevelIndex++)
	{
		FOpenColorIOTransformResource*& CurrentResource = ColorTransformResources[FeatureLevelIndex];
		if (CurrentResource)
		{
			delete CurrentResource;
			CurrentResource = nullptr;
		}
	}

#if WITH_EDITOR
	if (!GExitPurge)
	{
		ClearAllCachedCookedPlatformData();
	}
#endif
}

