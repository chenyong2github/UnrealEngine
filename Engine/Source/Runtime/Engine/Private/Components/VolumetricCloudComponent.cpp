// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/VolumetricCloudComponent.h"

#include "VolumetricCloudProxy.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Internationalization/Text.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstance.h"

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

#define LOCTEXT_NAMESPACE "VolumetricCloudComponent"



/*=============================================================================
	UVolumetricCloudComponent implementation.
=============================================================================*/

UVolumetricCloudComponent::UVolumetricCloudComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, LayerBottomAltitude(5.0f)
	, LayerHeight(10.0f)
	, TracingStartMaxDistance(350.0f)
	, TracingMaxDistance(50.0f)
	, PlanetRadius(6360.0f)					// Default to earth-like
	, GroundAlbedo(FColor(170, 170, 170))	// 170 => 0.4f linear
	, Material(nullptr)
	, bUsePerSampleAtmosphericLightTransmittance(false)
	, SkyLightCloudBottomOcclusion(0.5f)
	, ViewSampleCountScale(1.0f)
	, ReflectionSampleCountScale(1.0f)
	, ShadowViewSampleCountScale(1.0f)
	, ShadowReflectionSampleCountScale(1.0f)
	, ShadowTracingDistance(15.0f)
	, StopTracingTransmittanceThreshold(0.005f)
	, VolumetricCloudSceneProxy(nullptr)
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VolumetricCloudDefaultMaterialRef(TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
	Material = VolumetricCloudDefaultMaterialRef.Object;
}

UVolumetricCloudComponent::~UVolumetricCloudComponent()
{
}


void UVolumetricCloudComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
	// If one day we need to look up lightmass built data, lookup it up here using the guid from the correct MapBuildData.

	bool bHidden = false;
#if WITH_EDITORONLY_DATA
	bHidden = GetOwner() ? GetOwner()->bHiddenEdLevel : false;
#endif // WITH_EDITORONLY_DATA
	if (!ShouldComponentAddToScene())
	{
		bHidden = true;
	}

	if (GetVisibleFlag() && !bHidden &&
		ShouldComponentAddToScene() && ShouldRender() && IsRegistered() && (GetOuter() == NULL || !GetOuter()->HasAnyFlags(RF_ClassDefaultObject)))
	{
		// Create the scene proxy.
		VolumetricCloudSceneProxy = new FVolumetricCloudSceneProxy(this);
		GetWorld()->Scene->AddVolumetricCloud(VolumetricCloudSceneProxy);
	}

}

void UVolumetricCloudComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if (VolumetricCloudSceneProxy)
	{
		GetWorld()->Scene->RemoveVolumetricCloud(VolumetricCloudSceneProxy);

		FVolumetricCloudSceneProxy* SceneProxy = VolumetricCloudSceneProxy;
		ENQUEUE_RENDER_COMMAND(FDestroyVolumetricCloudSceneProxyCommand)(
			[SceneProxy](FRHICommandList& RHICmdList)
		{
			delete SceneProxy;
		});

		VolumetricCloudSceneProxy = nullptr;
	}
}

#if WITH_EDITOR

void UVolumetricCloudComponent::CheckForErrors()
{
	// Clouds with SkyAtmosphere?
}

void UVolumetricCloudComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // WITH_EDITOR

void UVolumetricCloudComponent::PostInterpChange(FProperty* PropertyThatChanged)
{
	// This is called when property is modified by InterpPropertyTracks
	Super::PostInterpChange(PropertyThatChanged);
}

void UVolumetricCloudComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
}


#define CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(MemberType, MemberName) void UVolumetricCloudComponent::Set##MemberName(MemberType NewValue)\
{\
	if (AreDynamicDataChangesAllowed() && MemberName != NewValue)\
	{\
		MemberName = NewValue;\
		MarkRenderStateDirty();\
	}\
}\

CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, LayerBottomAltitude);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, LayerHeight);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, TracingStartMaxDistance);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, TracingMaxDistance);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, PlanetRadius);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(FColor, GroundAlbedo);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(bool, bUsePerSampleAtmosphericLightTransmittance);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, SkyLightCloudBottomOcclusion);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, ViewSampleCountScale);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, ReflectionSampleCountScale);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, ShadowViewSampleCountScale);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, ShadowReflectionSampleCountScale);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, ShadowTracingDistance);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(float, StopTracingTransmittanceThreshold);
CLOUD_DECLARE_BLUEPRINT_SETFUNCTION(UMaterialInterface*, Material);

/*=============================================================================
	AVolumetricCloud implementation.
=============================================================================*/

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

AVolumetricCloud::AVolumetricCloud(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VolumetricCloudComponent = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("VolumetricCloudComponent"));
	RootComponent = VolumetricCloudComponent;

#if WITH_EDITORONLY_DATA

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> VolumetricCloudTextureObject;
			FName ID_VolumetricCloud;
			FText NAME_VolumetricCloud;
			FConstructorStatics()
				: VolumetricCloudTextureObject(TEXT("/Engine/EditorResources/S_VolumetricCloud"))
				, ID_VolumetricCloud(TEXT("Fog"))
				, NAME_VolumetricCloud(NSLOCTEXT("SpriteCategory", "Fog", "Fog"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		if (GetSpriteComponent())
		{
			GetSpriteComponent()->Sprite = ConstructorStatics.VolumetricCloudTextureObject.Get();
			GetSpriteComponent()->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
			GetSpriteComponent()->SpriteInfo.Category = ConstructorStatics.ID_VolumetricCloud;
			GetSpriteComponent()->SpriteInfo.DisplayName = ConstructorStatics.NAME_VolumetricCloud;
			GetSpriteComponent()->SetupAttachment(VolumetricCloudComponent);
		}
	}
#endif // WITH_EDITORONLY_DATA

	PrimaryActorTick.bCanEverTick = true;
	SetHidden(false);
}



#undef LOCTEXT_NAMESPACE


