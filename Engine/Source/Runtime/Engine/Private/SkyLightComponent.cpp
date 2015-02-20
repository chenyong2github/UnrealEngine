// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyLightComponent.cpp: SkyLightComponent implementation.
=============================================================================*/

#include "EnginePrivate.h"
#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif
#include "Engine/SkyLight.h"
#include "MessageLog.h"
#include "UObjectToken.h"
#include "Net/UnrealNetwork.h"
#include "MapErrors.h"
#include "ComponentInstanceDataCache.h"
#include "ShaderCompiler.h"
#include "Components/SkyLightComponent.h"

#define LOCTEXT_NAMESPACE "SkyLightComponent"

void FSkyTextureCubeResource::InitRHI()
{
	if (GetFeatureLevel() >= ERHIFeatureLevel::SM4)
	{
		FRHIResourceCreateInfo CreateInfo;
		TextureCubeRHI = RHICreateTextureCube(Size, Format, NumMips, 0, CreateInfo);
		TextureRHI = TextureCubeRHI;

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer
		(
			SF_Trilinear,
			AM_Clamp,
			AM_Clamp,
			AM_Clamp
		);
		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
	}
}

void FSkyTextureCubeResource::Release()
{
	check( IsInGameThread() );
	checkSlow(NumRefs > 0);
	if(--NumRefs == 0)
	{
		BeginReleaseResource(this);
		// Have to defer actual deletion until above rendering command has been processed, we will use the deferred cleanup interface for that
		BeginCleanup(this);
	}
}

void UWorld::UpdateAllSkyCaptures()
{
	TArray<USkyLightComponent*> UpdatedComponents;

	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		USkyLightComponent* CaptureComponent = *It;

		if (ContainsActor(CaptureComponent->GetOwner()) && !CaptureComponent->IsPendingKill())
		{
			// Purge cached derived data and force an update
			CaptureComponent->SetCaptureIsDirty();
			UpdatedComponents.Add(CaptureComponent);
		}
	}

	USkyLightComponent::UpdateSkyCaptureContents(this);
}

FSkyLightSceneProxy::FSkyLightSceneProxy(const USkyLightComponent* InLightComponent)
	: LightComponent(InLightComponent)
	, ProcessedTexture(InLightComponent->ProcessedSkyTexture)
	, SkyDistanceThreshold(InLightComponent->SkyDistanceThreshold)
	, bCastShadows(InLightComponent->CastShadows)
	, bWantsStaticShadowing(InLightComponent->Mobility == EComponentMobility::Stationary)
	, bPrecomputedLightingIsValid(InLightComponent->bPrecomputedLightingIsValid)
	, bHasStaticLighting(InLightComponent->HasStaticLighting())
	, LightColor(FLinearColor(InLightComponent->LightColor) * InLightComponent->Intensity)
	, IrradianceEnvironmentMap(InLightComponent->IrradianceEnvironmentMap)
	, IndirectLightingIntensity(InLightComponent->IndirectLightingIntensity)
	, OcclusionMaxDistance(InLightComponent->OcclusionMaxDistance)
	, Contrast(InLightComponent->Contrast)
	, MinOcclusion(InLightComponent->MinOcclusion)
	, OcclusionTint(InLightComponent->OcclusionTint)
{
}

USkyLightComponent::USkyLightComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
		static ConstructorHelpers::FObjectFinder<UTexture2D> StaticTexture(TEXT("/Engine/EditorResources/LightIcons/SkyLight"));
		StaticEditorTexture = StaticTexture.Object;
		StaticEditorTextureScale = 1.0f;
		DynamicEditorTexture = StaticTexture.Object;
		DynamicEditorTextureScale = 1.0f;
	}
#endif

	Brightness_DEPRECATED = 1;
	Intensity = 1;
	IndirectLightingIntensity = 1.0f;
	SkyDistanceThreshold = 150000;
	Mobility = EComponentMobility::Stationary;
	bCaptureDirty = false;
	bLowerHemisphereIsBlack = true;
	bSavedConstructionScriptValuesValid = true;
	bHasEverCaptured = false;
	OcclusionMaxDistance = 1000;
	MinOcclusion = 0;
	OcclusionTint = FColor::Black;
}

FSkyLightSceneProxy* USkyLightComponent::CreateSceneProxy() const
{
	if (ProcessedSkyTexture)
	{
		return new FSkyLightSceneProxy(this);
	}
	
	return NULL;
}

void USkyLightComponent::SetCaptureIsDirty()
{ 
	if (bVisible && bAffectsWorld)
	{
		SkyCapturesToUpdate.AddUnique(this);
		bCaptureDirty = true; 

		// Mark saved values as invalid, in case a sky recapture is requested in a construction script between a save / restore of sky capture state
		bSavedConstructionScriptValuesValid = false;
	}
}

TArray<USkyLightComponent*> USkyLightComponent::SkyCapturesToUpdate;

void USkyLightComponent::CreateRenderState_Concurrent()
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

	const bool bIsValid = SourceType != SLS_SpecifiedCubemap || Cubemap != NULL;

	if (bAffectsWorld && bVisible && !bHidden && bIsValid)
	{
		// Create the light's scene proxy.
		SceneProxy = CreateSceneProxy();

		if (SceneProxy)
		{
			// Add the light to the scene.
			World->Scene->SetSkyLight(SceneProxy);
		}
	}
}

extern ENGINE_API int32 GReflectionCaptureSize;

void USkyLightComponent::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Enqueue an update by default, so that newly placed components will get an update
		// PostLoad will undo this for components loaded from disk
		bCaptureDirty = true;
		SkyCapturesToUpdate.AddUnique(this);
	}

	Super::PostInitProperties();
}

void USkyLightComponent::PostLoad()
{
	Super::PostLoad();

	// All components are queued for update on creation by default, remove if needed
	if (!bVisible || HasAnyFlags(RF_ClassDefaultObject))
	{
		SkyCapturesToUpdate.Remove(this);
		bCaptureDirty = false;
	}
}

/** 
 * Fast path for updating light properties that doesn't require a re-register,
 * Which would otherwise cause the scene's static draw lists to be recreated.
 */
void USkyLightComponent::UpdateLimitedRenderingStateFast()
{
	if (SceneProxy)
	{
		ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
			FFastUpdateSkyLightCommand,
			FSkyLightSceneProxy*,LightSceneProxy,SceneProxy,
			FLinearColor,LightColor,FLinearColor(LightColor) * Intensity,
			float,IndirectLightingIntensity,IndirectLightingIntensity,
		{
			LightSceneProxy->LightColor = LightColor;
			LightSceneProxy->IndirectLightingIntensity = IndirectLightingIntensity;
		});
	}
}

/** 
* This is called when property is modified by InterpPropertyTracks
*
* @param PropertyThatChanged	Property that changed
*/
void USkyLightComponent::PostInterpChange(UProperty* PropertyThatChanged)
{
	static FName LightColorName(TEXT("LightColor"));
	static FName IntensityName(TEXT("Intensity"));
	static FName IndirectLightingIntensityName(TEXT("IndirectLightingIntensity"));

	FName PropertyName = PropertyThatChanged->GetFName();
	if (PropertyName == LightColorName
		|| PropertyName == IntensityName
		|| PropertyName == IndirectLightingIntensityName)
	{
		UpdateLimitedRenderingStateFast();
	}
	else
	{
		Super::PostInterpChange(PropertyThatChanged);
	}
}

void USkyLightComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if (SceneProxy)
	{
		World->Scene->DisableSkyLight(SceneProxy);

		ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER(
			FDestroySkyLightCommand,
			FSkyLightSceneProxy*,LightSceneProxy,SceneProxy,
		{
			delete LightSceneProxy;
		});

		SceneProxy = NULL;
	}
}

#if WITH_EDITOR
void USkyLightComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	SetCaptureIsDirty();
}

bool USkyLightComponent::CanEditChange(const UProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (FCString::Strcmp(*PropertyName, TEXT("Cubemap")) == 0)
		{
			return SourceType == SLS_SpecifiedCubemap;
		}

		if (FCString::Strcmp(*PropertyName, TEXT("Contrast")) == 0
			|| FCString::Strcmp(*PropertyName, TEXT("OcclusionMaxDistance")) == 0
			|| FCString::Strcmp(*PropertyName, TEXT("MinOcclusion")) == 0)
		{
			static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));
			return Mobility == EComponentMobility::Movable && CastShadows && CVar->GetValueOnGameThread() != 0;
		}
	}

	return Super::CanEditChange(InProperty);
}

void USkyLightComponent::CheckForErrors()
{
	AActor* Owner = GetOwner();

	if (Owner && bVisible && bAffectsWorld)
	{
		UWorld* ThisWorld = Owner->GetWorld();
		bool bMultipleFound = false;

		if (ThisWorld)
		{
			for (TObjectIterator<USkyLightComponent> ComponentIt; ComponentIt; ++ComponentIt)
			{
				USkyLightComponent* Component = *ComponentIt;

				if (Component != this 
					&& !Component->HasAnyFlags(RF_PendingKill)
					&& Component->bVisible
					&& Component->bAffectsWorld
					&& Component->GetOwner() 
					&& ThisWorld->ContainsActor(Component->GetOwner())
					&& !Component->GetOwner()->IsPendingKill())
				{
					bMultipleFound = true;
					break;
				}
			}
		}

		if (bMultipleFound)
		{
			FMessageLog("MapCheck").Error()
				->AddToken(FUObjectToken::Create(Owner))
				->AddToken(FTextToken::Create(LOCTEXT( "MapCheck_Message_MultipleSkyLights", "Multiple sky lights are active, only one can be enabled per world." )))
				->AddToken(FMapErrorToken::Create(FMapErrors::MultipleSkyLights));
		}
	}
}

#endif // WITH_EDITOR

void USkyLightComponent::BeginDestroy()
{
	// Deregister the component from the update queue
	if (bCaptureDirty)
	{
		SkyCapturesToUpdate.Remove(this);
	}

	// Release reference
	ProcessedSkyTexture = NULL;

	// Begin a fence to track the progress of the above BeginReleaseResource being completed on the RT
	ReleaseResourcesFence.BeginFence();

	Super::BeginDestroy();
}

bool USkyLightComponent::IsReadyForFinishDestroy()
{
	// Wait until the fence is complete before allowing destruction
	return Super::IsReadyForFinishDestroy() && ReleaseResourcesFence.IsFenceComplete();
}

/** Used to store lightmap data during RerunConstructionScripts */
class FPrecomputedSkyLightInstanceData : public FSceneComponentInstanceData
{
public:
	FPrecomputedSkyLightInstanceData(const USkyLightComponent* SourceComponent)
		: FSceneComponentInstanceData(SourceComponent)
	{}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		FSceneComponentInstanceData::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<USkyLightComponent>(Component)->ApplyComponentInstanceData(this);
	}

	FGuid LightGuid;
	bool bPrecomputedLightingIsValid;
	// This has to be refcounted to keep it alive during the handoff without doing a deep copy
	TRefCountPtr<FSkyTextureCubeResource> ProcessedSkyTexture;
	FSHVectorRGB3 IrradianceEnvironmentMap;
};

// Init type name static
FName USkyLightComponent::GetComponentInstanceDataType() const
{
	static const FName PrecomputedSkyLightInstanceDataTypeName(TEXT("PrecomputedSkyLightInstanceData"));
	return PrecomputedSkyLightInstanceDataTypeName;
}

FActorComponentInstanceData* USkyLightComponent::GetComponentInstanceData() const
{
	FPrecomputedSkyLightInstanceData* InstanceData = new FPrecomputedSkyLightInstanceData(this);
	InstanceData->LightGuid = LightGuid;
	InstanceData->bPrecomputedLightingIsValid = bPrecomputedLightingIsValid;
	InstanceData->ProcessedSkyTexture = ProcessedSkyTexture;
	InstanceData->IrradianceEnvironmentMap = IrradianceEnvironmentMap;

	return InstanceData;
}

void USkyLightComponent::ApplyComponentInstanceData(FPrecomputedSkyLightInstanceData* LightMapData)
{
	check(LightMapData);

	LightGuid = LightMapData->LightGuid;
	bPrecomputedLightingIsValid = LightMapData->bPrecomputedLightingIsValid;
	ProcessedSkyTexture = LightMapData->ProcessedSkyTexture;
	IrradianceEnvironmentMap = LightMapData->IrradianceEnvironmentMap;

	if (ProcessedSkyTexture && bSavedConstructionScriptValuesValid)
	{
		// We have valid capture state, remove the queued update
		bCaptureDirty = false;
		SkyCapturesToUpdate.Remove(this);
	}

	MarkRenderStateDirty();
}

void USkyLightComponent::UpdateSkyCaptureContents(UWorld* WorldToUpdate)
{
	if (WorldToUpdate->Scene)
	{
		const bool bIsCompilingShaders = GShaderCompilingManager != NULL && GShaderCompilingManager->IsCompiling();

		// Iterate backwards so we can remove elements without changing the index
		for (int32 CaptureIndex = SkyCapturesToUpdate.Num() - 1; CaptureIndex >= 0; CaptureIndex--)
		{
			USkyLightComponent* CaptureComponent = SkyCapturesToUpdate[CaptureIndex];

			if ((!CaptureComponent->GetOwner() || WorldToUpdate->ContainsActor(CaptureComponent->GetOwner()))
				// Only process sky capture requests once async shader compiling completes, otherwise we will capture the scene with temporary shaders
				&& (!bIsCompilingShaders || CaptureComponent->SourceType == SLS_SpecifiedCubemap))
			{
				// Only capture valid sky light components
				if (CaptureComponent->SourceType != SLS_SpecifiedCubemap || CaptureComponent->Cubemap)
				{
					// Allocate the needed texture on first capture
					if (!CaptureComponent->ProcessedSkyTexture)
					{
						CaptureComponent->ProcessedSkyTexture = new FSkyTextureCubeResource();
						CaptureComponent->ProcessedSkyTexture->SetupParameters(GReflectionCaptureSize, FMath::CeilLogTwo(GReflectionCaptureSize) + 1, PF_FloatRGBA);
						BeginInitResource(CaptureComponent->ProcessedSkyTexture);
						CaptureComponent->MarkRenderStateDirty();
					}

					WorldToUpdate->Scene->UpdateSkyCaptureContents(CaptureComponent, false, CaptureComponent->ProcessedSkyTexture, CaptureComponent->IrradianceEnvironmentMap);

					CaptureComponent->bHasEverCaptured = true;
					CaptureComponent->MarkRenderStateDirty();
				}

				// Only remove queued update requests if we processed it for the right world
				SkyCapturesToUpdate.RemoveAt(CaptureIndex);
			}
		}
	}
}

void USkyLightComponent::CaptureEmissiveIrradianceEnvironmentMap(FSHVectorRGB3& OutIrradianceMap) const
{
	OutIrradianceMap = FSHVectorRGB3();

	if (GetScene() && (SourceType != SLS_SpecifiedCubemap || Cubemap))
	{
		// Capture emissive scene lighting only for the lighting build
		// This is necessary to avoid a feedback loop with the last lighting build results
		GetScene()->UpdateSkyCaptureContents(this, true, NULL, OutIrradianceMap);
	}
}

/** Set brightness of the light */
void USkyLightComponent::SetIntensity(float NewIntensity)
{
	// Can't set brightness on a static light
	if (AreDynamicDataChangesAllowed()
		&& Intensity != NewIntensity)
	{
		Intensity = NewIntensity;
		UpdateLimitedRenderingStateFast();
	}
}

/** Set color of the light */
void USkyLightComponent::SetLightColor(FLinearColor NewLightColor)
{
	FColor NewColor(NewLightColor);

	// Can't set color on a static light
	if (AreDynamicDataChangesAllowed()
		&& LightColor != NewColor)
	{
		LightColor = NewColor;
		UpdateLimitedRenderingStateFast();
	}
}

void USkyLightComponent::SetCubemap(UTextureCube* NewCubemap)
{
	// Can't set color on a static light
	if (AreDynamicDataChangesAllowed()
		&& Cubemap != NewCubemap)
	{
		Cubemap = NewCubemap;
		MarkRenderStateDirty();
		SetCaptureIsDirty();
	}
}

void USkyLightComponent::SetOcclusionTint(const FColor& InTint)
{
	// Can't set on a static light
	if (AreDynamicDataChangesAllowed()
		&& OcclusionTint != InTint)
	{
		OcclusionTint = InTint;
		MarkRenderStateDirty();
	}
}

void USkyLightComponent::SetMinOcclusion(float InMinOcclusion)
{
	// Can't set on a static light
	if (AreDynamicDataChangesAllowed()
		&& MinOcclusion != InMinOcclusion)
	{
		MinOcclusion = InMinOcclusion;
		MarkRenderStateDirty();
	}
}

void USkyLightComponent::SetVisibility(bool bNewVisibility, bool bPropagateToChildren)
{
	const bool bOldWasVisible = bVisible;

	Super::SetVisibility(bNewVisibility, bPropagateToChildren);

	if (bVisible && !bOldWasVisible && !bHasEverCaptured)
	{
		// Capture if we are being enabled for the first time
		SetCaptureIsDirty();
	}
}

void USkyLightComponent::RecaptureSky()
{
	SetCaptureIsDirty();
}

ASkyLight::ASkyLight(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LightComponent = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLightComponent0"));
	RootComponent = LightComponent;

#if WITH_EDITORONLY_DATA
	if (!IsRunningCommandlet())
	{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SkyLightTextureObject;
		FName ID_Sky;
		FText NAME_Sky;

		FConstructorStatics()
				: SkyLightTextureObject(TEXT("/Engine/EditorResources/LightIcons/SkyLight"))
				, ID_Sky(TEXT("Sky"))
			, NAME_Sky(NSLOCTEXT( "SpriteCategory", "Sky", "Sky" ))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

		if (GetSpriteComponent())
		{
			GetSpriteComponent()->Sprite = ConstructorStatics.SkyLightTextureObject.Get();
			GetSpriteComponent()->SpriteInfo.Category = ConstructorStatics.ID_Sky;
			GetSpriteComponent()->SpriteInfo.DisplayName = ConstructorStatics.NAME_Sky;
			GetSpriteComponent()->AttachParent = LightComponent;
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void ASkyLight::GetLifetimeReplicatedProps( TArray< FLifetimeProperty > & OutLifetimeProps ) const
{
	Super::GetLifetimeReplicatedProps( OutLifetimeProps );

	DOREPLIFETIME( ASkyLight, bEnabled );
}

void ASkyLight::OnRep_bEnabled()
{
	LightComponent->SetVisibility(bEnabled);
}

#undef LOCTEXT_NAMESPACE

/** Returns LightComponent subobject **/
USkyLightComponent* ASkyLight::GetLightComponent() const { return LightComponent; }
