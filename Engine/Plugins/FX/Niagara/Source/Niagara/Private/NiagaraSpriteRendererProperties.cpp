// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraRenderer.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraConstants.h"
#include "NiagaraRendererSprites.h"
#include "NiagaraBoundsCalculatorHelper.h"
#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "DerivedDataCacheInterface.h"
#endif
#define LOCTEXT_NAMESPACE "UNiagaraSpriteRendererProperties"

TArray<TWeakObjectPtr<UNiagaraSpriteRendererProperties>> UNiagaraSpriteRendererProperties::SpriteRendererPropertiesToDeferredInit;

#if ENABLE_COOK_STATS
class NiagaraCutoutCookStats
{
public:
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats;
};

FCookStats::FDDCResourceUsageStats NiagaraCutoutCookStats::UsageStats;
FCookStatsManager::FAutoRegisterCallback NiagaraCutoutCookStats::RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
{
	UsageStats.LogStats(AddStat, TEXT("NiagaraCutout.Usage"), TEXT(""));
});
#endif // ENABLE_COOK_STATS

UNiagaraSpriteRendererProperties::UNiagaraSpriteRendererProperties()
	: Alignment(ENiagaraSpriteAlignment::Unaligned)
	, FacingMode(ENiagaraSpriteFacingMode::FaceCamera)
	, CustomFacingVectorMask(ForceInitToZero)
	, PivotInUVSpace(0.5f, 0.5f)
	, SortMode(ENiagaraSortMode::None)
	, SubImageSize(1.0f, 1.0f)
	, bSubImageBlend(false)
	, bRemoveHMDRollInVR(false)
	, bSortOnlyWhenTranslucent(true)
	, MinFacingCameraBlendDistance(0.0f)
	, MaxFacingCameraBlendDistance(0.0f)
#if WITH_EDITORONLY_DATA
	, BoundingMode(BVC_EightVertices)
	, AlphaThreshold(0.1f)
#endif // WITH_EDITORONLY_DATA
{
	FNiagaraTypeDefinition MaterialDef(UMaterialInterface::StaticClass());
	MaterialUserParamBinding.Parameter.SetType(MaterialDef);
}

FNiagaraRenderer* UNiagaraSpriteRendererProperties::CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter)
{
	FNiagaraRenderer* NewRenderer = new FNiagaraRendererSprites(FeatureLevel, this, Emitter);	
	NewRenderer->Initialize(this, Emitter);
	return NewRenderer;
}

FNiagaraBoundsCalculator* UNiagaraSpriteRendererProperties::CreateBoundsCalculator()
{
	return new FNiagaraBoundsCalculatorHelper<true, false, false>();
}

void UNiagaraSpriteRendererProperties::GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const
{
	bool bSet = false;
	if (InEmitter != nullptr && MaterialUserParamBinding.Parameter.IsValid() && InEmitter->FindBinding(MaterialUserParamBinding, OutMaterials))
	{
		bSet = true;
	}

	if (!bSet)
	{
		OutMaterials.Add(Material);
	}
}

void UNiagaraSpriteRendererProperties::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA

	if (MaterialUserParamBinding.Parameter.GetType().GetClass() != UMaterialInterface::StaticClass())
	{
		FNiagaraTypeDefinition MaterialDef(UMaterialInterface::StaticClass());
		MaterialUserParamBinding.Parameter.SetType(MaterialDef);
	}

	if (!FPlatformProperties::RequiresCookedData())
	{
		if (CutoutTexture)
		{	// Here we don't call UpdateCutoutTexture() to avoid issue with the material postload.
			CutoutTexture->ConditionalPostLoad();
		}
		CacheDerivedData();
	}
#endif // WITH_EDITORONLY_DATA
}

void UNiagaraSpriteRendererProperties::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// We can end up hitting PostInitProperties before the Niagara Module has initialized bindings this needs, mark this object for deferred init and early out.
		if (FModuleManager::Get().IsModuleLoaded("Niagara") == false)
		{
			SpriteRendererPropertiesToDeferredInit.Add(this);
			return;
		}
		InitBindings();
	}
}

void UNiagaraSpriteRendererProperties::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	Ar.UsingCustomVersion(FNiagaraCustomVersion::GUID);
	const int32 NiagaraVersion = Ar.CustomVer(FNiagaraCustomVersion::GUID);

	if (Ar.IsLoading() && (NiagaraVersion < FNiagaraCustomVersion::DisableSortingByDefault))
	{
		SortMode = ENiagaraSortMode::ViewDistance;
	}

	Super::Serialize(Record);

	bool bIsCookedForEditor = false;
#if WITH_EDITORONLY_DATA
	bIsCookedForEditor = GetOutermost()->bIsCookedForEditor;
#endif // WITH_EDITORONLY_DATA

	FArchive& UnderlyingArchive = Record.GetUnderlyingArchive();
	if (UnderlyingArchive.IsCooking() || (FPlatformProperties::RequiresCookedData() && UnderlyingArchive.IsLoading()) || bIsCookedForEditor)
	{
		DerivedData.Serialize(Record.EnterField(SA_FIELD_NAME(TEXT("DerivedData"))));
	}
}

/** The bindings depend on variables that are created during the NiagaraModule startup. However, the CDO's are build prior to this being initialized, so we defer setting these values until later.*/
void UNiagaraSpriteRendererProperties::InitCDOPropertiesAfterModuleStartup()
{
	UNiagaraSpriteRendererProperties* CDO = CastChecked<UNiagaraSpriteRendererProperties>(UNiagaraSpriteRendererProperties::StaticClass()->GetDefaultObject());
	CDO->InitBindings();

	for (TWeakObjectPtr<UNiagaraSpriteRendererProperties>& WeakSpriteRendererProperties : SpriteRendererPropertiesToDeferredInit)
	{
		if (WeakSpriteRendererProperties.Get())
		{
			WeakSpriteRendererProperties->InitBindings();
		}
	}
}

void UNiagaraSpriteRendererProperties::InitBindings()
{
	if (PositionBinding.BoundVariable.GetName() == NAME_None)
	{
		PositionBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_POSITION);
		ColorBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_COLOR);
		VelocityBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_VELOCITY);
		SpriteRotationBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SPRITE_ROTATION);
		SpriteSizeBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SPRITE_SIZE);
		SpriteFacingBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SPRITE_FACING);
		SpriteAlignmentBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SPRITE_ALIGNMENT);
		SubImageIndexBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_SUB_IMAGE_INDEX);
		DynamicMaterialBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM);
		DynamicMaterial1Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_1);
		DynamicMaterial2Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_2);
		DynamicMaterial3Binding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_3);
		CameraOffsetBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_CAMERA_OFFSET);
		UVScaleBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_UV_SCALE);
		MaterialRandomBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_MATERIAL_RANDOM);
		NormalizedAgeBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		
		//Default custom sorting to age
		CustomSortingBinding = FNiagaraConstants::GetAttributeDefaultBinding(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
	}
}

#if WITH_EDITORONLY_DATA

void UNiagaraSpriteRendererProperties::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) 
{
	SubImageSize.X = FMath::Max<float>(SubImageSize.X, 1.f);
	SubImageSize.Y = FMath::Max<float>(SubImageSize.Y, 1.f);

	// DerivedData.BoundingGeometry in case we cleared the CutoutTexture
	if (bUseMaterialCutoutTexture || CutoutTexture || DerivedData.BoundingGeometry.Num())
	{
		const bool bUpdateCutoutDDC = 
			PropertyChangedEvent.GetPropertyName() == TEXT("bUseMaterialCutoutTexture") ||
			PropertyChangedEvent.GetPropertyName() == TEXT("CutoutTexture") ||
			PropertyChangedEvent.GetPropertyName() == TEXT("BoundingMode") ||
			PropertyChangedEvent.GetPropertyName() == TEXT("OpacitySourceMode") ||
			PropertyChangedEvent.GetPropertyName() == TEXT("AlphaThreshold") ||
			(bUseMaterialCutoutTexture && PropertyChangedEvent.GetPropertyName() == TEXT("Material"));

		if (bUpdateCutoutDDC)
		{
			UpdateCutoutTexture();
			CacheDerivedData();
		}
	}
	
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

const TArray<FNiagaraVariable>& UNiagaraSpriteRendererProperties::GetRequiredAttributes()
{
	static TArray<FNiagaraVariable> Attrs;

	if (Attrs.Num() == 0)
	{
	}

	return Attrs;
}

const TArray<FNiagaraVariable>& UNiagaraSpriteRendererProperties::GetOptionalAttributes()
{
	static TArray<FNiagaraVariable> Attrs;

	if (Attrs.Num() == 0)
	{
		Attrs.Add(SYS_PARAM_PARTICLES_POSITION);
		Attrs.Add(SYS_PARAM_PARTICLES_VELOCITY);
		Attrs.Add(SYS_PARAM_PARTICLES_COLOR);
		Attrs.Add(SYS_PARAM_PARTICLES_SPRITE_ROTATION);
		Attrs.Add(SYS_PARAM_PARTICLES_NORMALIZED_AGE);
		Attrs.Add(SYS_PARAM_PARTICLES_SPRITE_SIZE);
		Attrs.Add(SYS_PARAM_PARTICLES_SPRITE_FACING);
		Attrs.Add(SYS_PARAM_PARTICLES_SPRITE_ALIGNMENT);
		Attrs.Add(SYS_PARAM_PARTICLES_SUB_IMAGE_INDEX);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_1);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_2);
		Attrs.Add(SYS_PARAM_PARTICLES_DYNAMIC_MATERIAL_PARAM_3);
		Attrs.Add(SYS_PARAM_PARTICLES_CAMERA_OFFSET);
		Attrs.Add(SYS_PARAM_PARTICLES_UV_SCALE);
		Attrs.Add(SYS_PARAM_PARTICLES_MATERIAL_RANDOM);
	}

	return Attrs;
}

bool UNiagaraSpriteRendererProperties::IsMaterialValidForRenderer(UMaterial* InMaterial, FText& InvalidMessage)
{
	if (InMaterial->bUsedWithNiagaraSprites == false)
	{
		InvalidMessage = NSLOCTEXT("NiagaraSpriteRendererProperties", "InvalidMaterialMessage", "The material isn't marked as \"Used with particle sprites\"");
		return false;
	}
	return true;
}

void UNiagaraSpriteRendererProperties::FixMaterial(UMaterial* InMaterial)
{
	InMaterial->Modify();
	InMaterial->bUsedWithNiagaraSprites = true;
	InMaterial->ForceRecompileForRendering();
}

void UNiagaraSpriteRendererProperties::UpdateCutoutTexture()
{
	if (bUseMaterialCutoutTexture)
	{
		CutoutTexture = nullptr;
		if (Material)
		{
			// Try to find an opacity mask texture to default to, if not try to find an opacity texture
			TArray<UTexture*> OpacityMaskTextures;
			Material->GetTexturesInPropertyChain(MP_OpacityMask, OpacityMaskTextures, nullptr, nullptr);
			if (OpacityMaskTextures.Num())
			{
				CutoutTexture = (UTexture2D*)OpacityMaskTextures[0];
			}
			else
			{
				TArray<UTexture*> OpacityTextures;
				Material->GetTexturesInPropertyChain(MP_Opacity, OpacityTextures, nullptr, nullptr);
				if (OpacityTextures.Num())
				{
					CutoutTexture = (UTexture2D*)OpacityTextures[0];
				}
			}
		}
	}
}

void UNiagaraSpriteRendererProperties::CacheDerivedData()
{
	if (CutoutTexture)
	{
		const FString KeyString = FSubUVDerivedData::GetDDCKeyString(CutoutTexture->Source.GetId(), (int32)SubImageSize.X, (int32)SubImageSize.Y, (int32)BoundingMode, AlphaThreshold, (int32)OpacitySourceMode);
		TArray<uint8> Data;

		COOK_STAT(auto Timer = NiagaraCutoutCookStats::UsageStats.TimeSyncWork());
		if (GetDerivedDataCacheRef().GetSynchronous(*KeyString, Data))
		{
			COOK_STAT(Timer.AddHit(Data.Num()));
			DerivedData.BoundingGeometry.Empty(Data.Num() / sizeof(FVector2D));
			DerivedData.BoundingGeometry.AddUninitialized(Data.Num() / sizeof(FVector2D));
			FPlatformMemory::Memcpy(DerivedData.BoundingGeometry.GetData(), Data.GetData(), Data.Num() * Data.GetTypeSize());
		}
		else
		{
			DerivedData.Build(CutoutTexture, (int32)SubImageSize.X, (int32)SubImageSize.Y, BoundingMode, AlphaThreshold, OpacitySourceMode);

			Data.Empty(DerivedData.BoundingGeometry.Num() * sizeof(FVector2D));
			Data.AddUninitialized(DerivedData.BoundingGeometry.Num() * sizeof(FVector2D));
			FPlatformMemory::Memcpy(Data.GetData(), DerivedData.BoundingGeometry.GetData(), DerivedData.BoundingGeometry.Num() * DerivedData.BoundingGeometry.GetTypeSize());
			GetDerivedDataCacheRef().Put(*KeyString, Data);
			COOK_STAT(Timer.AddMiss(Data.Num()));
		}
	}
	else
	{
		DerivedData.BoundingGeometry.Empty();
	}
}

#endif // WITH_EDITORONLY_DATA


int32 UNiagaraSpriteRendererProperties::GetNumCutoutVertexPerSubimage() const
{
	if (DerivedData.BoundingGeometry.Num())
	{
		const int32 NumSubImages = FMath::Max<int32>(1, (int32)SubImageSize.X * (int32)SubImageSize.Y);
		const int32 NumCutoutVertexPerSubImage = DerivedData.BoundingGeometry.Num() / NumSubImages;

		// Based on BVC_FourVertices || BVC_EightVertices
		ensure(NumCutoutVertexPerSubImage == 4 || NumCutoutVertexPerSubImage == 8);

		return NumCutoutVertexPerSubImage;
	}
	else
	{
		return 0;
	}
}

uint32 UNiagaraSpriteRendererProperties::GetNumIndicesPerInstance() const
{
	// This is a based on cutout vertices making a triangle strip.
	if (GetNumCutoutVertexPerSubimage() == 8)
	{
		return 18;
	}
	else
	{
		return 6;
	}
}

#undef LOCTEXT_NAMESPACE
