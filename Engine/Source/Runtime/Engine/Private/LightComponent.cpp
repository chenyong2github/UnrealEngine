// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LightComponent.cpp: LightComponent implementation.
=============================================================================*/

#include "EnginePrivate.h"
#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif
#include "MessageLog.h"
#include "UObjectToken.h"
#include "ComponentInstanceDataCache.h"
#include "TargetPlatform.h"
#include "ComponentReregisterContext.h"
#include "Components/PointLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"

void FStaticShadowDepthMap::InitRHI()
{
	if (ShadowMapSizeX > 0 && ShadowMapSizeY > 0 && GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM4)
	{
		FRHIResourceCreateInfo CreateInfo;
		FTexture2DRHIRef Texture2DRHI = RHICreateTexture2D(ShadowMapSizeX, ShadowMapSizeY, PF_R16F, 1, 1, 0, CreateInfo);
		TextureRHI = Texture2DRHI;

		uint32 DestStride = 0;
		uint8* TextureData = (uint8*)RHILockTexture2D(Texture2DRHI, 0, RLM_WriteOnly, DestStride, false);
		uint32 RowSize = ShadowMapSizeX * GPixelFormats[PF_R16F].BlockBytes;

		for (int32 Y = 0; Y < ShadowMapSizeY; Y++)
		{
			FMemory::Memcpy(TextureData + DestStride * Y, ((uint8*)DepthSamples.GetData()) + RowSize * Y, RowSize);
		}

		RHIUnlockTexture2D(Texture2DRHI, 0, false);
	}
}

void FStaticShadowDepthMap::Empty()
{
	DEC_DWORD_STAT_BY(STAT_PrecomputedShadowDepthMapMemory, DepthSamples.GetAllocatedSize());

	ShadowMapSizeX = 0;
	ShadowMapSizeY = 0;
	DepthSamples.Empty();
}

void FStaticShadowDepthMap::InitializeAfterImport()
{
	INC_DWORD_STAT_BY(STAT_PrecomputedShadowDepthMapMemory, DepthSamples.GetAllocatedSize());
	BeginInitResource(this);
}

FArchive& operator<<(FArchive& Ar, FStaticShadowDepthMap& ShadowMap)
{
	Ar << ShadowMap.WorldToLight;
	Ar << ShadowMap.ShadowMapSizeX;
	Ar << ShadowMap.ShadowMapSizeY;
	Ar << ShadowMap.DepthSamples;

	if (Ar.IsLoading())
	{
		INC_DWORD_STAT_BY(STAT_PrecomputedShadowDepthMapMemory, ShadowMap.DepthSamples.GetAllocatedSize());
	}

	return Ar;
}

void ULightComponentBase::SetCastShadows(bool bNewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& CastShadows != bNewValue)
	{
		CastShadows = bNewValue;
		MarkRenderStateDirty();
	}
}

void ULightComponentBase::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.UE4Ver() < VER_UE4_INVERSE_SQUARED_LIGHTS_DEFAULT)
	{
		Intensity = Brightness_DEPRECATED;
	}
}

/**
 * Called after duplication & serialization and before PostLoad. Used to e.g. make sure GUIDs remains globally unique.
 */
void ULightComponentBase::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (!bDuplicateForPIE)
	{
		// Create new guids for light.
		UpdateLightGUIDs();
	}
}


#if WITH_EDITOR
/**
 * Called after importing property values for this object (paste, duplicate or .t3d import)
 * Allow the object to perform any cleanup for properties which shouldn't be duplicated or
 * are unsupported by the script serialization
 */
void ULightComponentBase::PostEditImport()
{
	Super::PostEditImport();
	// Create new guids for light.
	UpdateLightGUIDs();
}

void ULightComponentBase::UpdateLightSpriteTexture()
{
	if (SpriteComponent != NULL)
	{
		SpriteComponent->SetSprite(GetEditorSprite());

		float SpriteScale = GetEditorSpriteScale();
		SpriteComponent->SetRelativeScale3D(FVector(SpriteScale));
	}
}

void ULightComponentBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Update sprite 
	UpdateLightSpriteTexture();
}

#endif

/**
 * Validates light GUIDs and resets as appropriate.
 */
void ULightComponentBase::ValidateLightGUIDs()
{
	// Validate light guids.
	if( !LightGuid.IsValid() )
	{
		LightGuid = FGuid::NewGuid();
	}
}

void ULightComponentBase::UpdateLightGUIDs()
{
	LightGuid = FGuid::NewGuid();
}

bool ULightComponentBase::HasStaticLighting() const
{
	AActor* Owner = GetOwner();

	return Owner && (Mobility == EComponentMobility::Static);
}

bool ULightComponentBase::HasStaticShadowing() const
{
	AActor* Owner = GetOwner();

	return Owner && (Mobility != EComponentMobility::Movable);
}

#if WITH_EDITOR
void ULightComponentBase::OnRegister()
{
	Super::OnRegister();

	if (SpriteComponent)
	{
		SpriteComponent->SpriteInfo.Category = TEXT("Lighting");
		SpriteComponent->SpriteInfo.DisplayName = NSLOCTEXT("SpriteCategory", "Lighting", "Lighting");

		UpdateLightSpriteTexture();
	}
}
#endif

bool ULightComponentBase::ShouldCollideWhenPlacing() const
{
	return true;
}

FBoxSphereBounds ULightComponentBase::GetPlacementExtent() const
{
	FBoxSphereBounds NewBounds;
	NewBounds.Origin = FVector::ZeroVector;
	NewBounds.BoxExtent = FVector(25.0f, 25.0f, 25.0f);
	NewBounds.SphereRadius = 12.5f;
	return NewBounds;
}


FLightSceneProxy::FLightSceneProxy(const ULightComponent* InLightComponent)
	: LightComponent(InLightComponent)
	, IndirectLightingScale(InLightComponent->IndirectLightingIntensity)
	, ShadowBias(InLightComponent->ShadowBias)
	, ShadowSharpen(InLightComponent->ShadowSharpen)
	, MinRoughness(InLightComponent->MinRoughness)
	, LightGuid(InLightComponent->LightGuid)
	, ShadowMapChannel(InLightComponent->ShadowMapChannel)
	, PreviewShadowMapChannel(InLightComponent->PreviewShadowMapChannel)
	, IESTexture(0)
	, bStaticLighting(InLightComponent->HasStaticLighting())
	, bStaticShadowing(InLightComponent->HasStaticShadowing())
	, bCastDynamicShadow(InLightComponent->CastShadows && InLightComponent->CastDynamicShadows)
	, bCastStaticShadow(InLightComponent->CastShadows && InLightComponent->CastStaticShadows)
	, bCastTranslucentShadows(InLightComponent->CastTranslucentShadows)
	, bAffectTranslucentLighting(InLightComponent->bAffectTranslucentLighting)
	, bUsedAsAtmosphereSunLight(InLightComponent->IsUsedAsAtmosphereSunLight())
	, bAffectDynamicIndirectLighting(InLightComponent->bAffectDynamicIndirectLighting)
	, bHasReflectiveShadowMap(InLightComponent->bAffectDynamicIndirectLighting && InLightComponent->GetLightType() == LightType_Directional)
	, bUseRayTracedDistanceFieldShadows(InLightComponent->bUseRayTracedDistanceFieldShadows)
	, RayStartOffsetDepthScale(InLightComponent->RayStartOffsetDepthScale)
	, LightType(InLightComponent->GetLightType())	
	, ComponentName(InLightComponent->GetOwner() ? InLightComponent->GetOwner()->GetFName() : InLightComponent->GetFName())
	, LevelName(InLightComponent->GetOutermost()->GetFName())
	, StatId(InLightComponent->GetStatID(true))
	, FarShadowDistance(0)
	, FarShadowCascadeCount(0)
{
	// Brightness in Lumens
	float LightBrightness = InLightComponent->ComputeLightBrightness();

	if(LightComponent->IESTexture)
	{
		UTextureLightProfile* IESTextureObject = Cast<UTextureLightProfile>(LightComponent->IESTexture);

		if(IESTextureObject)
		{
			IESTexture = IESTextureObject;
		}
	}
	Color = FLinearColor(InLightComponent->LightColor) * LightBrightness;

	if(LightComponent->LightFunctionMaterial &&
		LightComponent->LightFunctionMaterial->GetMaterial()->MaterialDomain == MD_LightFunction )
	{
		LightFunctionMaterial = LightComponent->LightFunctionMaterial->GetRenderProxy(false);
	}
	else
	{
		LightFunctionMaterial = NULL;
	}

	LightFunctionScale = LightComponent->LightFunctionScale;
	LightFunctionFadeDistance = LightComponent->LightFunctionFadeDistance;
	LightFunctionDisabledBrightness = LightComponent->DisabledBrightness;

	StaticShadowDepthMap = &LightComponent->StaticShadowDepthMap;
}

bool FLightSceneProxy::ShouldCreatePerObjectShadowsForDynamicObjects() const
{
	// Only create per-object shadows for Stationary lights, which use static shadowing from the world and therefore need a way to integrate dynamic objects
	return HasStaticShadowing() && !HasStaticLighting();
}

void FLightSceneProxy::SetTransform(const FMatrix& InLightToWorld,const FVector4& InPosition)
{
	LightToWorld = InLightToWorld;
	WorldToLight = InLightToWorld.InverseFast();
	Position = InPosition;
}

void FLightSceneProxy::SetColor(const FLinearColor& InColor)
{
	Color = InColor;
}

void FLightSceneProxy::ApplyWorldOffset(FVector InOffset)
{
	FMatrix NewLightToWorld = LightToWorld.ConcatTranslation(InOffset);
	FVector4 NewPosition = Position + InOffset;
	SetTransform(NewLightToWorld, NewPosition);
}

ULightComponentBase::ULightComponentBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Brightness_DEPRECATED = 3.1415926535897932f;
	Intensity = 3.1415926535897932f;
	LightColor = FColor::White;
	bAffectsWorld = true;
	CastShadows = true;
	CastStaticShadows = true;
	CastDynamicShadows = true;
	bPrecomputedLightingIsValid = true;
#if WITH_EDITORONLY_DATA
	bVisualizeComponent = true;
#endif
}

/**
 * Updates/ resets light GUIDs.
 */
ULightComponent::ULightComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ShadowMapChannel = INDEX_NONE;
	PreviewShadowMapChannel = INDEX_NONE;
	IndirectLightingIntensity = 1.0f;
	ShadowBias = 0.5f;
	ShadowSharpen = 0.0f;
	bUseIESBrightness = false;
	IESBrightnessScale = 1.0f;
	IESTexture = NULL;

	bAffectTranslucentLighting = true;
	LightFunctionScale = FVector(1024.0f, 1024.0f, 1024.0f);

	LightFunctionFadeDistance = 100000.0f;
	DisabledBrightness = 0.5f;
	MinRoughness = 0.08f;

	bEnableLightShaftBloom = false;
	BloomScale = .2f;
	BloomThreshold = 0;
	BloomTint = FColor::White;

	RayStartOffsetDepthScale = .01f;
}


void ULightComponent::UpdateLightGUIDs()
{
	Super::UpdateLightGUIDs();
	ShadowMapChannel = INDEX_NONE;
}

bool ULightComponent::AffectsPrimitive(const UPrimitiveComponent* Primitive) const
{
	// Check whether the light affects the primitive's bounding volume.
	return AffectsBounds(Primitive->Bounds);
}

bool ULightComponent::AffectsBounds(const FBoxSphereBounds& Bounds) const
{
	return true;
}

bool ULightComponent::IsShadowCast(UPrimitiveComponent* Primitive) const
{
	if(Primitive->HasStaticLighting())
	{
		return CastShadows && CastStaticShadows;
	}
	else
	{
		return CastShadows && CastDynamicShadows;
	}
}

float ULightComponent::ComputeLightBrightness() const
{
	float LightBrightness = Intensity;

	if(IESTexture)
	{
		UTextureLightProfile* IESTextureObject = Cast<UTextureLightProfile>(IESTexture);

		if(IESTextureObject)
		{
			if(bUseIESBrightness)
			{
				LightBrightness = IESTextureObject->Brightness * IESBrightnessScale;
			}

			LightBrightness *= IESTextureObject->TextureMultiplier;
		}
	}

	return LightBrightness;
}

void ULightComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.UE4Ver() >= VER_UE4_STATIC_SHADOW_DEPTH_MAPS)
	{
		if (Ar.IsCooking() && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::HighQualityLightmaps))
		{
			// Toss lighting data only needed for high quality lightmaps
			FStaticShadowDepthMap EmptyDepthMap;
			Ar << EmptyDepthMap;
		}
		else
		{
			Ar << StaticShadowDepthMap;
		}
	}
}

/**
 * Called after this UObject has been serialized
 */
void ULightComponent::PostLoad()
{
	Super::PostLoad();

	if (LightFunctionMaterial && HasStaticLighting())
	{
		// Light functions can only be used on dynamic lights
		LightFunctionMaterial = NULL;
	}

	PreviewShadowMapChannel = ShadowMapChannel;
	Intensity = FMath::Max(0.0f, Intensity);

	if (GetLinkerUE4Version() < VER_UE4_LIGHTCOMPONENT_USE_IES_TEXTURE_MULTIPLIER_ON_NON_IES_BRIGHTNESS)
	{
		if(IESTexture)
		{
			UTextureLightProfile* IESTextureObject = Cast<UTextureLightProfile>(IESTexture);

			if(IESTextureObject)
			{
				Intensity /= IESTextureObject->TextureMultiplier; // Previous version didn't apply IES texture multiplier, so cancel out
				IESBrightnessScale = FMath::Pow(IESBrightnessScale, 2.2f); // Previous version applied 2.2 gamma to brightness scale
				IESBrightnessScale /= IESTextureObject->TextureMultiplier; // Previous version didn't apply IES texture multiplier, so cancel out
			}
		}
	}

	if (HasStaticShadowing() && !HasStaticLighting())
	{
		BeginInitResource(&StaticShadowDepthMap);
	}
}

#if WITH_EDITOR
void ULightComponent::PreEditUndo()
{
	// Directly call UActorComponent::PreEditChange to avoid ULightComponent::PreEditChange being called for transactions.
	UActorComponent::PreEditChange(NULL);
}

void ULightComponent::PostEditUndo()
{
	// Directly call UActorComponent::PostEditChange to avoid ULightComponent::PostEditChange being called for transactions.
	UActorComponent::PostEditChange();
}

bool ULightComponent::CanEditChange(const UProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightFunctionMaterial)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightFunctionScale)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightFunctionFadeDistance)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, DisabledBrightness)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, IESTexture)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, bUseIESBrightness)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, IESBrightnessScale))
		{
			if (Mobility == EComponentMobility::Static)
			{
				return false;
			}
		}
		
		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, bUseRayTracedDistanceFieldShadows)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, RayStartOffsetDepthScale))
		{
			static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));
			bool bCanEdit = CastShadows && CastDynamicShadows && Mobility != EComponentMobility::Static && CVar->GetValueOnGameThread() != 0;
			return bCanEdit;
		}

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightFunctionScale)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightFunctionFadeDistance)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, DisabledBrightness))
		{
			return LightFunctionMaterial != NULL;
		}

		if (PropertyName == TEXT("LightmassSettings"))
		{
			return Mobility != EComponentMobility::Movable;
		}

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, BloomScale)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, BloomThreshold)
			|| PropertyName == GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, BloomTint))
		{
			return bEnableLightShaftBloom;
		}
	}

	return Super::CanEditChange(InProperty);
}

void ULightComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FString PropertyName = PropertyThatChanged ? PropertyThatChanged->GetName() : TEXT("");

	Intensity = FMath::Max(0.0f, Intensity);

	if (HasStaticLighting())
	{
		// Lightmapped lights must not have light functions
		LightFunctionMaterial = NULL;
	}

	// Unbuild lighting because a property changed
	// Exclude properties that don't affect built lighting
	//@todo - make this inclusive instead of exclusive?
	if( PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, CastTranslucentShadows) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, CastDynamicShadows) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, bAffectTranslucentLighting) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, MinRoughness) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightFunctionMaterial) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightFunctionScale) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightFunctionFadeDistance) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, DisabledBrightness) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, ShadowBias) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, ShadowSharpen) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, bEnableLightShaftBloom) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, BloomScale) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, BloomThreshold) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, BloomTint) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, bUseRayTracedDistanceFieldShadows) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, RayStartOffsetDepthScale) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, bVisible) &&
		// Point light properties that shouldn't unbuild lighting
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UPointLightComponent, SourceRadius) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UPointLightComponent, SourceLength) &&
		// Directional light properties that shouldn't unbuild lighting
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, DynamicShadowDistanceMovableLight) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, DynamicShadowDistanceStationaryLight) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, DynamicShadowCascades) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, FarShadowDistance) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, FarShadowCascadeCount) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, CascadeDistributionExponent) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, CascadeTransitionFraction) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, ShadowDistanceFadeoutFraction) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, bUseInsetShadowsForMovableObjects) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, DistanceFieldShadowDistance) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, bEnableLightShaftOcclusion) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, OcclusionMaskDarkness) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, OcclusionDepthRange) &&
		PropertyName != GET_MEMBER_NAME_STRING_CHECKED(UDirectionalLightComponent, LightShaftOverrideDirection) &&
		// Properties that should only unbuild lighting for a Static light (can be changed dynamically on a Stationary light)
		(PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, Intensity) || Mobility == EComponentMobility::Static) &&
		(PropertyName != GET_MEMBER_NAME_STRING_CHECKED(ULightComponent, LightColor) || Mobility == EComponentMobility::Static))
	{
		InvalidateLightingCache();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULightComponent::UpdateLightSpriteTexture()
{
	if( SpriteComponent != NULL )
	{
		if (HasStaticShadowing() &&
			!HasStaticLighting() &&
			bAffectsWorld &&
			CastShadows &&
			CastStaticShadows &&
			PreviewShadowMapChannel == INDEX_NONE)
		{
			UTexture2D* SpriteTexture = NULL;
			SpriteTexture = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorResources/LightIcons/S_LightError.S_LightError"));
			SpriteComponent->SetSprite(SpriteTexture);
			SpriteComponent->SetRelativeScale3D(FVector(0.5f));
		}
		else
		{
			Super::UpdateLightSpriteTexture();
		}
	}
}

#endif // WITH_EDITOR

void ULightComponent::BeginDestroy()
{
	Super::BeginDestroy();

	if (HasStaticShadowing() && !HasStaticLighting())
	{
		BeginReleaseResource(&StaticShadowDepthMap);
		StaticShadowDepthMap.Empty();
	}

	// Use a fence to keep track of when the rendering thread executes the release command
	DestroyFence.BeginFence();
}

bool ULightComponent::IsReadyForFinishDestroy()
{
	// Don't allow the light component to be destroyed until its rendering resources have been released
	return Super::IsReadyForFinishDestroy() && DestroyFence.IsFenceComplete();
}

void ULightComponent::OnRegister()
{
	Super::OnRegister();

	// Update GUIDs on attachment if they are not valid.
	ValidateLightGUIDs();
}

void ULightComponent::CreateRenderState_Concurrent()
{
	Super::CreateRenderState_Concurrent();

	bool bHidden = false;
#if WITH_EDITORONLY_DATA
	bHidden = GetOwner() ? GetOwner()->bHiddenEdLevel : false;
#endif // WITH_EDITORONLY_DATA

	if(!ShouldComponentAddToScene())
	{
		bHidden = true;
	}

	if (bAffectsWorld)
	{
		if (bVisible && !bHidden)
		{
			// Add the light to the scene.
			World->Scene->AddLight(this);
		}
		// Add invisible stationary lights to the scene in the editor
		// Even invisible stationary lights consume a shadowmap channel so they must be included in the stationary light overlap preview
		else if (GIsEditor 
			&& !World->IsGameWorld()
			&& CastShadows 
			&& CastStaticShadows 
			&& HasStaticShadowing()
			&& !HasStaticLighting())
		{
			World->Scene->AddInvisibleLight(this);
		}
	}
}

void ULightComponent::SendRenderTransform_Concurrent()
{
	// Update the scene info's transform for this light.
	World->Scene->UpdateLightTransform(this);
	Super::SendRenderTransform_Concurrent();
}

void ULightComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();
	World->Scene->RemoveLight(this);
}

/** Set brightness of the light */
void ULightComponent::SetIntensity(float NewIntensity)
{
	// Can't set brightness on a static light
	if (AreDynamicDataChangesAllowed()
		&& Intensity != NewIntensity)
	{
		Intensity = NewIntensity;

		// Use lightweight color and brightness update 
		if( World && World->Scene )
		{
			//@todo - remove from scene if brightness or color becomes 0
			World->Scene->UpdateLightColorAndBrightness( this );
		}
	}
}

/** Set color of the light */
void ULightComponent::SetLightColor(FLinearColor NewLightColor)
{
	FColor NewColor(NewLightColor);

	// Can't set color on a static light
	if (AreDynamicDataChangesAllowed()
		&& LightColor != NewColor)
	{
		LightColor	= NewColor;

		// Use lightweight color and brightness update 
		if( World && World->Scene )
		{
			//@todo - remove from scene if brightness or color becomes 0
			World->Scene->UpdateLightColorAndBrightness( this );
		}
	}
}

void ULightComponent::SetLightFunctionMaterial(UMaterialInterface* NewLightFunctionMaterial)
{
	// Can't set light function on a static light
	if (AreDynamicDataChangesAllowed()
		&& NewLightFunctionMaterial != LightFunctionMaterial)
	{
		LightFunctionMaterial = NewLightFunctionMaterial;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetLightFunctionScale(FVector NewLightFunctionScale)
{
	if (AreDynamicDataChangesAllowed()
		&& NewLightFunctionScale != LightFunctionScale)
	{
		LightFunctionScale = NewLightFunctionScale;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetLightFunctionFadeDistance(float NewLightFunctionFadeDistance)
{
	if (AreDynamicDataChangesAllowed()
		&& NewLightFunctionFadeDistance != LightFunctionFadeDistance)
	{
		LightFunctionFadeDistance = NewLightFunctionFadeDistance;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetAffectDynamicIndirectLighting(bool bNewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& bAffectDynamicIndirectLighting != bNewValue)
	{
		bAffectDynamicIndirectLighting = bNewValue;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetAffectTranslucentLighting(bool bNewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& bAffectTranslucentLighting != bNewValue)
	{
		bAffectTranslucentLighting = bNewValue;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetEnableLightShaftBloom(bool bNewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& bEnableLightShaftBloom != bNewValue)
	{
		bEnableLightShaftBloom = bNewValue;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetBloomScale(float NewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& BloomScale != NewValue)
	{
		BloomScale = NewValue;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetBloomThreshold(float NewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& BloomThreshold != NewValue)
	{
		BloomThreshold = NewValue;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetBloomTint(FColor NewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& BloomTint != NewValue)
	{
		BloomTint = NewValue;
		MarkRenderStateDirty();
	}
}

void ULightComponent::SetIESTexture(UTextureLightProfile* NewValue)
{
	if (AreDynamicDataChangesAllowed()
		&& IESTexture != NewValue)
	{
		IESTexture = NewValue;
		MarkRenderStateDirty();
	}
}

// GetDirection
FVector ULightComponent::GetDirection() const 
{ 
	return ComponentToWorld.GetUnitAxis( EAxis::X );
}

void ULightComponent::UpdateColorAndBrightness()
{
	if( World && World->Scene )
	{
		World->Scene->UpdateLightColorAndBrightness( this );
	}
}

//
//	ULightComponent::InvalidateLightingCache
//

void ULightComponent::InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly)
{
	InvalidateLightingCacheInner(true);

	UWorld* World = GetWorld();
	if (GIsEditor
		&& World != NULL
		&& HasStaticShadowing()
		&& !HasStaticLighting())
	{
		ReassignStationaryLightChannels(World, false);
	}
}

/** Invalidates the light's cached lighting with the option to recreate the light Guids. */
void ULightComponent::InvalidateLightingCacheInner(bool bRecreateLightGuids)
{
	// Save the light state for transactions.
	Modify();

	// Detach the component from the scene for the duration of this function.
	FComponentReregisterContext ReregisterContext(this);

	// Block until the RT processes the unregister before modifying variables that it may need to access
	FlushRenderingCommands();

	StaticShadowDepthMap.Empty();
	BeginReleaseResource(&StaticShadowDepthMap);

	bPrecomputedLightingIsValid = false;

	if (bRecreateLightGuids)
	{
		// Create new guids for light.
		UpdateLightGUIDs();
	}
	else
	{
		ValidateLightGUIDs();
		ShadowMapChannel = INDEX_NONE;
	}
	
	// Send to render thread
	MarkRenderStateDirty();
}

/** Used to store lightmap data during RerunConstructionScripts */
class FPrecomputedLightInstanceData : public FSceneComponentInstanceData
{
public:
	FPrecomputedLightInstanceData(const ULightComponent* SourceComponent)
		: FSceneComponentInstanceData(SourceComponent)
		, Transform(SourceComponent->ComponentToWorld)
		, LightGuid(SourceComponent->LightGuid)
		, ShadowMapChannel(SourceComponent->ShadowMapChannel)
		, PreviewShadowMapChannel(SourceComponent->PreviewShadowMapChannel)
		, bPrecomputedLightingIsValid(SourceComponent->bPrecomputedLightingIsValid)
	{}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		FSceneComponentInstanceData::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<ULightComponent>(Component)->ApplyComponentInstanceData(this);
	}

	FTransform Transform;
	FGuid LightGuid;
	int32 ShadowMapChannel;
	int32 PreviewShadowMapChannel;
	bool bPrecomputedLightingIsValid;
};

FName ULightComponent::GetComponentInstanceDataType() const
{
	static const FName PrecomputedLightInstanceDataTypeName(TEXT("PrecomputedLightInstanceData"));
	return PrecomputedLightInstanceDataTypeName;
}

FActorComponentInstanceData* ULightComponent::GetComponentInstanceData() const
{
	// Allocate new struct for holding light map data
	return new FPrecomputedLightInstanceData(this);
}

void ULightComponent::ApplyComponentInstanceData(FPrecomputedLightInstanceData* LightMapData)
{
	check(LightMapData);

	if (!LightMapData->Transform.Equals(ComponentToWorld))
	{
		return;
	}

	LightGuid = LightMapData->LightGuid;
	ShadowMapChannel = LightMapData->ShadowMapChannel;
	PreviewShadowMapChannel = LightMapData->PreviewShadowMapChannel;
	bPrecomputedLightingIsValid = LightMapData->bPrecomputedLightingIsValid;

	MarkRenderStateDirty();

#if WITH_EDITOR
	// Update the icon with the new state of PreviewShadowMapChannel
	UpdateLightSpriteTexture();
#endif
}

int32 ULightComponent::GetNumMaterials() const
{
	return 1;
}

UMaterialInterface* ULightComponent::GetMaterial(int32 ElementIndex) const
{
	if (ElementIndex == 0)
	{
		return LightFunctionMaterial;
	}
	else
	{
		return NULL;
	}
}

void ULightComponent::SetMaterial(int32 ElementIndex, UMaterialInterface* InMaterial)
{
	if (ElementIndex == 0)
	{
		LightFunctionMaterial = InMaterial;
		MarkRenderStateDirty();
	}
}

/** 
* This is called when property is modified by InterpPropertyTracks
*
* @param PropertyThatChanged	Property that changed
*/
void ULightComponent::PostInterpChange(UProperty* PropertyThatChanged)
{
	static FName LightColorName(TEXT("LightColor"));
	static FName IntensityName(TEXT("Intensity"));
	static FName BrightnessName(TEXT("Brightness"));
	static FName IndirectLightingIntensityName(TEXT("IndirectLightingIntensity"));

	FName PropertyName = PropertyThatChanged->GetFName();
	if (PropertyName == LightColorName
		|| PropertyName == IntensityName
		|| PropertyName == BrightnessName
		|| PropertyName == IndirectLightingIntensityName)
	{
		// Old brightness tracks will animate the deprecated value
		if (PropertyName == BrightnessName)
		{
			Intensity = Brightness_DEPRECATED;
		}

		UpdateColorAndBrightness();
	}
	else
	{
		Super::PostInterpChange(PropertyThatChanged);
	}
}

/** Stores a light and a channel it has been assigned to. */
struct FLightAndChannel
{
	ULightComponent* Light;
	int32 Channel;

	FLightAndChannel(ULightComponent* InLight) :
		Light(InLight),
		Channel(INDEX_NONE)
	{}
};

struct FCompareLightsByArrayCount
{
	FORCEINLINE bool operator()( const TArray<FLightAndChannel*>& A, const TArray<FLightAndChannel*>& B ) const 
	{ 
		// Sort by descending array count
		return B.Num() < A.Num(); 
	}
};

void ULightComponent::ReassignStationaryLightChannels(UWorld* TargetWorld, bool bAssignForLightingBuild)
{
	TMap<FLightAndChannel*, TArray<FLightAndChannel*> > LightToOverlapMap;

	// Build an array of all static shadowing lights that need to be assigned
	for(TObjectIterator<ULightComponent> LightIt(RF_ClassDefaultObject|RF_PendingKill); LightIt; ++LightIt)
	{
		ULightComponent* const LightComponent = *LightIt;

		const bool bLightIsInWorld = LightComponent->GetOwner() && TargetWorld->ContainsActor(LightComponent->GetOwner()) && !LightComponent->GetOwner()->IsPendingKill();

		if (bLightIsInWorld 
			// Only operate on stationary light components (static shadowing only)
			&& LightComponent->HasStaticShadowing()
			&& !LightComponent->HasStaticLighting())
		{
			if (LightComponent->bAffectsWorld
				&& LightComponent->CastShadows 
				&& LightComponent->CastStaticShadows)
			{
				LightToOverlapMap.Add(new FLightAndChannel(LightComponent), TArray<FLightAndChannel*>());
			}
			else
			{
				// Reset the preview channel of stationary light components that shouldn't get a channel
				// This is necessary to handle a light being newly disabled
				LightComponent->PreviewShadowMapChannel = INDEX_NONE;

#if WITH_EDITOR
				LightComponent->UpdateLightSpriteTexture();
#endif
			}
		}
	}

	// Build an array of overlapping lights
	for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator It(LightToOverlapMap); It; ++It)
	{
		ULightComponent* CurrentLight = It.Key()->Light;
		TArray<FLightAndChannel*>& OverlappingLights = It.Value();

		if (bAssignForLightingBuild)
		{
			// This should have happened during lighting invalidation at the beginning of the build anyway
			CurrentLight->ShadowMapChannel = INDEX_NONE;
		}

		for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator OtherIt(LightToOverlapMap); OtherIt; ++OtherIt)
		{
			ULightComponent* OtherLight = OtherIt.Key()->Light;

			if (CurrentLight != OtherLight 
				// Testing both directions because the spotlight <-> spotlight test is just cone vs bounding sphere
				//@todo - more accurate spotlight <-> spotlight intersection
				&& CurrentLight->AffectsBounds(FBoxSphereBounds(OtherLight->GetBoundingSphere()))
				&& OtherLight->AffectsBounds(FBoxSphereBounds(CurrentLight->GetBoundingSphere())))
			{
				OverlappingLights.Add(OtherIt.Key());
			}
		}
	}
		
	// Sort lights with the most overlapping lights first
	LightToOverlapMap.ValueSort(FCompareLightsByArrayCount());

	TMap<FLightAndChannel*, TArray<FLightAndChannel*> > SortedLightToOverlapMap;

	// Add directional lights to the beginning so they always get channels
	for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator It(LightToOverlapMap); It; ++It)
	{
		FLightAndChannel* CurrentLight = It.Key();

		if (CurrentLight->Light->GetLightType() == LightType_Directional)
		{
			SortedLightToOverlapMap.Add(It.Key(), It.Value());
		}
	}

	// Add everything else, which has been sorted by descending overlaps
	for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator It(LightToOverlapMap); It; ++It)
	{
		FLightAndChannel* CurrentLight = It.Key();

		if (CurrentLight->Light->GetLightType() != LightType_Directional)
		{
			SortedLightToOverlapMap.Add(It.Key(), It.Value());
		}
	}

	// Go through lights and assign shadowmap channels
	//@todo - retry with different ordering heuristics when it fails
	for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator It(SortedLightToOverlapMap); It; ++It)
	{
		bool bChannelUsed[4] = {0};
		FLightAndChannel* CurrentLight = It.Key();
		const TArray<FLightAndChannel*>& OverlappingLights = It.Value();

		// Mark which channels have already been assigned to overlapping lights
		for (int32 OverlappingIndex = 0; OverlappingIndex < OverlappingLights.Num(); OverlappingIndex++)
		{
			FLightAndChannel* OverlappingLight = OverlappingLights[OverlappingIndex];

			if (OverlappingLight->Channel != INDEX_NONE)
			{
				bChannelUsed[OverlappingLight->Channel] = true;
			}
		}

		// Use the lowest free channel
		for (int32 ChannelIndex = 0; ChannelIndex < ARRAY_COUNT(bChannelUsed); ChannelIndex++)
		{
			if (!bChannelUsed[ChannelIndex])
			{
				CurrentLight->Channel = ChannelIndex;
				break;
			}
		}
	}

	// Go through the assigned lights and update their render state and icon
	for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator It(SortedLightToOverlapMap); It; ++It)
	{
		FLightAndChannel* CurrentLight = It.Key();

		if (CurrentLight->Light->PreviewShadowMapChannel != CurrentLight->Channel)
		{
			CurrentLight->Light->PreviewShadowMapChannel = CurrentLight->Channel;
			CurrentLight->Light->MarkRenderStateDirty();
		}

#if WITH_EDITOR
		CurrentLight->Light->UpdateLightSpriteTexture();
#endif

		if (bAssignForLightingBuild)
		{
			CurrentLight->Light->ShadowMapChannel = CurrentLight->Channel;

			if (CurrentLight->Light->ShadowMapChannel == INDEX_NONE)
			{
				FMessageLog("LightingResults").Error()
					->AddToken(FUObjectToken::Create(CurrentLight->Light->GetOwner()))
					->AddToken(FTextToken::Create( NSLOCTEXT("Lightmass", "LightmassError_FailedToAllocateShadowmapChannel", "Severe performance loss: Failed to allocate shadowmap channel for stationary light due to overlap - light will fall back to dynamic shadows!") ) );
			}
		}

		delete CurrentLight;
	}
}

static void ToggleLight(const TArray<FString>& Args)
{
	for (TObjectIterator<ULightComponent> It; It; ++It)
	{
		ULightComponent* Light = *It;
		if (Light->Mobility != EComponentMobility::Static)
		{
			FString LightName = (Light->GetOwner() ? Light->GetOwner()->GetFName() : Light->GetFName()).ToString();
			for (int32 ArgIndex = 0; ArgIndex < Args.Num(); ++ArgIndex)
			{
				const FString& ToggleName = Args[ArgIndex];
				if (LightName.Contains(ToggleName) )
				{
					Light->ToggleVisibility(/*bPropagateToChildren=*/ false);
					UE_LOG(LogConsoleResponse,Display,TEXT("Now%svisible: %s"),
						Light->IsVisible() ? TEXT("") : TEXT(" not "),
						*Light->GetFullName()
						);
					break;
				}
			}
		}
	}
}

static FAutoConsoleCommand ToggleLightCmd(
	TEXT("ToggleLight"),
	TEXT("Toggles all lights whose name contains the specified string"),
	FConsoleCommandWithArgsDelegate::CreateStatic(ToggleLight)
	);
