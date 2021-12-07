// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOColorTransform.h"

#include "Materials/MaterialInterface.h"
#include "Math/PackedVector.h"
#include "Modules/ModuleManager.h"
#include "OpenColorIOConfiguration.h"
#include "OpenColorIOModule.h"
#include "UObject/UObjectIterator.h"


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

void UOpenColorIOColorTransform::GetOpenColorIOLUTKeyGuid(const FString& InLutIdentifier, FGuid& OutLutGuid)
{
#if WITH_EDITOR
	FString DDCKey = FDerivedDataCacheInterface::BuildCacheKey(TEXT("OCIOLUT"), OPENCOLORIO_DERIVEDDATA_VER, *InLutIdentifier);

#if WITH_OCIO
	//Keep library version in the DDC key to invalidate it once we move to a new library
	DDCKey += TEXT("OCIOVersion");
	DDCKey += TEXT(OCIO_VERSION);
#endif //WITH_OCIO

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

bool UOpenColorIOColorTransform::Initialize(UOpenColorIOConfiguration* InOwner, const FString& InSourceColorSpace, const FString& InDestinationColorSpace)
{
	check(InOwner);
	ConfigurationOwner = InOwner;
	return GenerateColorTransformData(InSourceColorSpace, InDestinationColorSpace);
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
	NewResource->SetupResource((ERHIFeatureLevel::Type)TargetFeatureLevel, InShaderHash, InShaderCode, InRawConfigHash, GetTransformFriendlyName(), AssetPath);

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
			if (Lut3dTexture != nullptr)
			{
				Num3dLutsToSave = 1;
			}
		}

		Ar << Num3dLutsToSave;

		if (Num3dLutsToSave > 0)
		{
			Ar << Lut3dTexture;
		}
	}
	else if (Ar.IsLoading())
	{
		int32 NumLoaded3dLuts = 0;
		Ar << NumLoaded3dLuts;

		if (NumLoaded3dLuts > 0)
		{
			//Will only happen on cooked data
			Ar << Lut3dTexture;
		}
	}
}

void UOpenColorIOColorTransform::CacheResourceTextures()
{
	if (Lut3dTexture == nullptr)
	{
#if WITH_EDITOR && WITH_OCIO
		OCIO_NAMESPACE::ConstConfigRcPtr CurrentConfig = ConfigurationOwner->GetLoadedConfigurationFile();
		if (CurrentConfig)
		{
#if !PLATFORM_EXCEPTIONS_DISABLED
			try
#endif
			{
				OCIO_NAMESPACE::ConstProcessorRcPtr TransformProcessor = CurrentConfig->getProcessor(StringCast<ANSICHAR>(*SourceColorSpace).Get(), StringCast<ANSICHAR>(*DestinationColorSpace).Get());
				if (TransformProcessor)
				{
					OCIO_NAMESPACE::GpuShaderDescRcPtr ShaderDescription = OCIO_NAMESPACE::GpuShaderDesc::CreateShaderDesc();
					ShaderDescription->setLanguage(OCIO_NAMESPACE::GPU_LANGUAGE_HLSL_DX11);
					ShaderDescription->setFunctionName(StringCast<ANSICHAR>(OpenColorIOShader::OpenColorIOShaderFunctionName).Get());
					ShaderDescription->setResourcePrefix("Ocio");

					OCIO_NAMESPACE::ConstGPUProcessorRcPtr GPUProcessor = TransformProcessor->getOptimizedLegacyGPUProcessor(OCIO_NAMESPACE::OptimizationFlags::OPTIMIZATION_DEFAULT, OpenColorIOShader::Lut3dEdgeLength);
					GPUProcessor->extractGpuShaderInfo(ShaderDescription);

					FString Lut3dIdentifier = StringCast<TCHAR>(GPUProcessor->getCacheID()).Get();
					if (Lut3dIdentifier != TEXT("<NULL>") && ShaderDescription->getNum3DTextures() > 0 )
					{
						const char* TextureName = nullptr;
						const char* SamplerName = nullptr;
						unsigned int EdgeLength = static_cast<unsigned int>(OpenColorIOShader::Lut3dEdgeLength);
						OCIO_NAMESPACE::Interpolation Interpolation = OCIO_NAMESPACE::INTERP_BEST;
						ShaderDescription->get3DTexture(0, TextureName, SamplerName, EdgeLength, Interpolation);
						checkf(TextureName && *TextureName && SamplerName && *SamplerName && EdgeLength > 0, TEXT("Invalid OCIO texture or sampler."));

						const float* Lut3dData = 0x0;
						ShaderDescription->get3DTextureValues(0, Lut3dData);
						checkf(Lut3dData, TEXT("Failed to read OCIO 3d LUT data."));

						//In editor, it will use what's on DDC if there's something corresponding to the actual data or use that raw data
						//that OCIO library has on board. The texture will be serialized only when cooking.
						Update3dLutTexture(Lut3dIdentifier, Lut3dData);
					}
				}
				else
				{
					UE_LOG(LogOpenColorIO, Error, TEXT("Failed to cache 3dLUT for color transform %s. Transform processor was unusable."), *GetTransformFriendlyName());
				}
			}
#if !PLATFORM_EXCEPTIONS_DISABLED
			catch (OCIO_NAMESPACE::Exception& exception)
			{
				UE_LOG(LogOpenColorIO, Log, TEXT("Failed to cache 3dLUT for color transform %s. Error message: %s."), *GetTransformFriendlyName(), StringCast<TCHAR>(exception.what()).Get());
			}
#endif
		}
		else
		{
			UE_LOG(LogOpenColorIO, Error, TEXT("Failed to cache 3dLUT for color transform %s. Configuration file was invalid."), *GetTransformFriendlyName());
		}
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
				TransformResource->SetupResource(CacheFeatureLevel, ShaderCodeHash, ShaderCode, RawConfigHash, GetTransformFriendlyName(), AssetPath);

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

bool UOpenColorIOColorTransform::GetShaderAndLUTResouces(ERHIFeatureLevel::Type InFeatureLevel, FOpenColorIOTransformResource*& OutShaderResource, FTextureResource*& OutLUT3dResource)
{
	OutShaderResource = ColorTransformResources[InFeatureLevel];
	if (OutShaderResource)
	{
		//Some color transform will only require shader code with no LUT involved.
		if (Lut3dTexture != nullptr)
		{
			OutLUT3dResource = Lut3dTexture->GetResource();
		}

		return true;
	}
	else
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Shader resource was invalid for color transform %s. Were there errors during loading?"), *GetTransformFriendlyName());
		return false;
	}
}

bool UOpenColorIOColorTransform::IsTransform(const FString& InSourceColorSpace, const FString& InDestinationColorSpace) const
{
	return SourceColorSpace == InSourceColorSpace && DestinationColorSpace == InDestinationColorSpace;
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

	CacheResourceTextures();
	CacheResourceShadersForRendering(true);

	return true;
#endif //WITH_EDITOR
	return false;
}

FString UOpenColorIOColorTransform::GetTransformFriendlyName()
{
	return SourceColorSpace + TEXT(" to ") + DestinationColorSpace;
}

bool UOpenColorIOColorTransform::UpdateShaderInfo(FString& OutShaderCodeHash, FString& OutShaderCode, FString& OutRawConfigHash)
{
#if WITH_EDITOR
#if WITH_OCIO
	OCIO_NAMESPACE::ConstConfigRcPtr CurrentConfig = ConfigurationOwner->GetLoadedConfigurationFile();
	if (CurrentConfig)
	{
#if !PLATFORM_EXCEPTIONS_DISABLED
		try
#endif
		{
			OCIO_NAMESPACE::ConstProcessorRcPtr TransformProcessor = CurrentConfig->getProcessor(StringCast<ANSICHAR>(*SourceColorSpace).Get(), StringCast<ANSICHAR>(*DestinationColorSpace).Get());
			if (TransformProcessor)
			{
				OCIO_NAMESPACE::GpuShaderDescRcPtr ShaderDescription = OCIO_NAMESPACE::GpuShaderDesc::CreateShaderDesc();
				ShaderDescription->setLanguage(OCIO_NAMESPACE::GPU_LANGUAGE_HLSL_DX11);
				ShaderDescription->setFunctionName(StringCast<ANSICHAR>(OpenColorIOShader::OpenColorIOShaderFunctionName).Get());
				ShaderDescription->setResourcePrefix("Ocio");

				OCIO_NAMESPACE::ConstGPUProcessorRcPtr GPUProcessor = TransformProcessor->getOptimizedLegacyGPUProcessor(OCIO_NAMESPACE::OptimizationFlags::OPTIMIZATION_DEFAULT, OpenColorIOShader::Lut3dEdgeLength);
				GPUProcessor->extractGpuShaderInfo(ShaderDescription);

				FString GLSLShaderCode = StringCast<TCHAR>(ShaderDescription->getShaderText()).Get();

				OutShaderCodeHash = StringCast<TCHAR>(ShaderDescription->getCacheID()).Get();
				OutShaderCode = StringCast<TCHAR>(ShaderDescription->getShaderText()).Get();
				OutRawConfigHash = StringCast<TCHAR>(CurrentConfig->getCacheID()).Get();
				return true;
			}
			else
			{
				UE_LOG(LogOpenColorIO, Error, TEXT("Failed to fetch shader info for color transform %s. Transform processor was unusable."), *GetTransformFriendlyName());
			}
		}
#if !PLATFORM_EXCEPTIONS_DISABLED
		catch (OCIO_NAMESPACE::Exception& exception)
		{
			UE_LOG(LogOpenColorIO, Log, TEXT("Failed to fetch shader info for color transform %s. Error message: %s."), *GetTransformFriendlyName(), StringCast<TCHAR>(exception.what()).Get());
		}
#endif
	}
	else
	{
		UE_LOG(LogOpenColorIO, Error, TEXT("Failed to fetch shader info for color transform %s. Configuration file was invalid."), *GetTransformFriendlyName());
	}

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

void UOpenColorIOColorTransform::Update3dLutTexture(const FString& InLutIdentifier, const float* InSourceData)
{
#if WITH_EDITOR && WITH_OCIO
	check(InSourceData);

	Lut3dTexture = NewObject<UVolumeTexture>(this, NAME_None, RF_NoFlags);

	//Initializes source data with the raw LUT. If it's found in DDC, the resulting platform data will be fetched from there. 
	//If not, the source data will be used to generate the platform data.
	Lut3dTexture->MipGenSettings = TMGS_NoMipmaps;
	Lut3dTexture->CompressionNone = true;
	Lut3dTexture->Source.Init(OpenColorIOShader::Lut3dEdgeLength, OpenColorIOShader::Lut3dEdgeLength, OpenColorIOShader::Lut3dEdgeLength, /*NumMips=*/ 1, TSF_RGBA16F, nullptr);

	FFloat16Color* MipData = reinterpret_cast<FFloat16Color*>(Lut3dTexture->Source.LockMip(0));
	const uint32 LutLength = OpenColorIOShader::Lut3dEdgeLength;
	for (uint32 Z = 0; Z < LutLength; ++Z)
	{
		for (uint32 Y = 0; Y < LutLength; Y++)
		{
			FFloat16Color* Row = &MipData[Y * LutLength + Z * LutLength * LutLength];
			const float* Source = &InSourceData[Y * LutLength * 3 + Z * LutLength * LutLength * 3];
			for (uint32 X = 0; X < LutLength; X++)
			{
				Row[X] = FFloat16Color(FLinearColor(Source[X * 3 + 0], Source[X * 3 + 1], Source[X * 3 + 2]));
			}
		}
	}
	Lut3dTexture->Source.UnlockMip(0);

	//Generate a Guid from the identifier received from the library and our DDC version.
	FGuid LutGuid;
	GetOpenColorIOLUTKeyGuid(InLutIdentifier, LutGuid);
	Lut3dTexture->Source.SetId(LutGuid, true);

	//Process our new texture to be usable in rendering pipeline.
	Lut3dTexture->UpdateResource();
#endif
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
