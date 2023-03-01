// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTexture/SparseVolumeTextureViewerComponent.h"

#include "SparseVolumeTexture/SparseVolumeTextureViewerSceneProxy.h"
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

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

#define LOCTEXT_NAMESPACE "SparseVolumeTextureViewerComponent"

constexpr double SVTViewerDefaultVolumeExtent = 100.0;

/*=============================================================================
	USparseVolumeTextureViewerComponent implementation.
=============================================================================*/

USparseVolumeTextureViewerComponent::USparseVolumeTextureViewerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SparseVolumeTexturePreview(nullptr)
	, bAnimate(false)
	, AnimationFrame(0.0f)
	, ComponentToVisualize(0)
	, SparseVolumeTextureViewerSceneProxy(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	bTickInEditor = true;
}

USparseVolumeTextureViewerComponent::~USparseVolumeTextureViewerComponent()
{
}

#if WITH_EDITOR

void USparseVolumeTextureViewerComponent::CheckForErrors()
{
}

void USparseVolumeTextureViewerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!bAnimate && SparseVolumeTexturePreview)
	{
		FrameIndex = int32(AnimationFrame * float(SparseVolumeTexturePreview->GetNumFrames()));
	}
	MarkRenderStateDirty();

	SendRenderTransformCommand();
}

#endif // WITH_EDITOR

void USparseVolumeTextureViewerComponent::PostInterpChange(FProperty* PropertyThatChanged)
{
	// This is called when property is modified by InterpPropertyTracks
	Super::PostInterpChange(PropertyThatChanged);
}

void USparseVolumeTextureViewerComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
}

FBoxSphereBounds USparseVolumeTextureViewerComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBoxSphereBounds NormalizedBound;
	NormalizedBound.Origin = FVector(0.0f, 0.0f, 0.0f);

	if (SparseVolumeTexturePreview)
	{
		// We assume that the maximum bound will have a length of SVTViewerDefaultVolumeExtent (1 meter). 
		// Then the other dimensions are scaled relatively.
		// All this account for the size of volume with page table (they add padding).
		// TODO can we recover world size from OpenVDB meta data in meter?
		const FVector VolumeResolution = FVector(SparseVolumeTexturePreview->GetVolumeResolution());
		const double MaxDim = FMath::Max(FMath::Max(VolumeResolution.X, VolumeResolution.Y), VolumeResolution.Z);
		NormalizedBound.BoxExtent = VolumeResolution / MaxDim * SVTViewerDefaultVolumeExtent;
	}
	else
	{
		NormalizedBound.BoxExtent = FVector(SVTViewerDefaultVolumeExtent);
	}

	NormalizedBound.SphereRadius = NormalizedBound.BoxExtent.Size();
	return NormalizedBound.TransformBy(LocalToWorld);
}

void USparseVolumeTextureViewerComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
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
		SparseVolumeTextureViewerSceneProxy = new FSparseVolumeTextureViewerSceneProxy(this, FrameIndex);
		GetWorld()->Scene->AddSparseVolumeTextureViewer(SparseVolumeTextureViewerSceneProxy);
		SendRenderTransformCommand();
	}
}

void USparseVolumeTextureViewerComponent::DestroyRenderState_Concurrent()
{
	Super::DestroyRenderState_Concurrent();

	if (SparseVolumeTextureViewerSceneProxy)
	{
		GetWorld()->Scene->RemoveSparseVolumeTextureViewer(SparseVolumeTextureViewerSceneProxy);

		FSparseVolumeTextureViewerSceneProxy* SVTViewerSceneProxy = SparseVolumeTextureViewerSceneProxy;
		ENQUEUE_RENDER_COMMAND(FDestroySparseVolumeTextureViewerSceneProxyCommand)(
			[SVTViewerSceneProxy](FRHICommandList& RHICmdList)
			{
				delete SVTViewerSceneProxy;
			});

		SparseVolumeTextureViewerSceneProxy = nullptr;
	}
}

void USparseVolumeTextureViewerComponent::SendRenderTransform_Concurrent()
{
	Super::SendRenderTransform_Concurrent();

	SendRenderTransformCommand();
}

void USparseVolumeTextureViewerComponent::SendRenderTransformCommand()
{
	if (SparseVolumeTextureViewerSceneProxy)
	{
		FVector VolumeExtent = FVector(SVTViewerDefaultVolumeExtent);
		FVector VolumeResolution = FVector(SVTViewerDefaultVolumeExtent * 2.0);
		FUintVector4 PackedSVTUniforms0 = FUintVector4();
		FUintVector4 PackedSVTUniforms1 = FUintVector4();
		if (SparseVolumeTextureFrame)
		{
			VolumeResolution = FVector(SparseVolumeTextureFrame->GetVolumeResolution());
			const double MaxBoundsDim = FMath::Max(FMath::Max(VolumeResolution.X, VolumeResolution.Y), VolumeResolution.Z);
			VolumeExtent = VolumeResolution / MaxBoundsDim * SVTViewerDefaultVolumeExtent;
			SparseVolumeTextureFrame->GetPackedUniforms(PackedSVTUniforms0, PackedSVTUniforms1);
		}

		const FTransform ToWorldTransform = GetComponentTransform();
		const FVector Scale3D = ToWorldTransform.GetScale3D();
		// Keep max scaling since the DDA algorithm has trouble with non-uniform scale.
		// Note that using the other components in FScaleMatrix below is fine 
		// because they cancel out with the actual volume resolution, producing uniformly scaled voxels.
		const float MaxScaling = FMath::Max(Scale3D.X, FMath::Max(Scale3D.Y, Scale3D.Z));

		const FRotationMatrix WorldToLocalRotation = FRotationMatrix(FRotator(ToWorldTransform.GetRotation().Inverse()));
		const FMatrix44f ToLocalMatNoScale = FMatrix44f(WorldToLocalRotation);
		const FMatrix44f ToLocalMat = FMatrix44f(
			FTranslationMatrix(-ToWorldTransform.GetTranslation()) 
			* WorldToLocalRotation 
			* FScaleMatrix((VolumeExtent * MaxScaling).Reciprocal()));

		const FVector3f VolumeRes3f = FVector3f(VolumeResolution.X, VolumeResolution.Y, VolumeResolution.Z);


		FSparseVolumeTextureViewerSceneProxy* SVTViewerSceneProxy = SparseVolumeTextureViewerSceneProxy;
		ENQUEUE_RENDER_COMMAND(FUpdateSparseVolumeTextureViewerProxyTransformCommand)(
			[SVTViewerSceneProxy, ToLocalMat, ToLocalMatNoScale, CompIdx = (uint32)ComponentToVisualize, Ext = Extinction, Mip = MipLevel, PackedSVTUniforms0, PackedSVTUniforms1, VolumeRes3f]
		(FRHICommandList& RHICmdList)
			{
				SVTViewerSceneProxy->WorldToLocal = ToLocalMat;
				SVTViewerSceneProxy->WorldToLocalNoScale = ToLocalMatNoScale;
				SVTViewerSceneProxy->PackedSVTUniforms0 = PackedSVTUniforms0;
				SVTViewerSceneProxy->PackedSVTUniforms1 = PackedSVTUniforms1;
				SVTViewerSceneProxy->VolumeResolution = VolumeRes3f;
				SVTViewerSceneProxy->MipLevel = Mip;
				SVTViewerSceneProxy->ComponentToVisualize = CompIdx;
				SVTViewerSceneProxy->Extinction = Ext;
			});
	}
}

void USparseVolumeTextureViewerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (SparseVolumeTexturePreview)
	{
		if (bAnimate)
		{
			const int32 NumFrames = SparseVolumeTexturePreview->GetNumFrames();
			const float AnimationDuration = NumFrames / (FrameRate + UE_SMALL_NUMBER);
			AnimationTime = FMath::Fmod(AnimationTime + DeltaTime, (AnimationDuration + UE_SMALL_NUMBER));
			FrameIndex = int32(AnimationTime * FrameRate);
		}
		else
		{
			FrameIndex = int32(AnimationFrame * float(SparseVolumeTexturePreview->GetNumFrames()));
		}

		SparseVolumeTextureFrame = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexturePreview, FrameIndex, MipLevel);
	}
	else
	{
		SparseVolumeTextureFrame = nullptr;
	}

	MarkRenderStateDirty();
}

/*=============================================================================
	ASparseVolumeTextureViewer implementation.
=============================================================================*/

#if WITH_EDITOR
#include "ObjectEditorUtils.h"
#endif

ASparseVolumeTextureViewer::ASparseVolumeTextureViewer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SparseVolumeTextureViewerComponent = CreateDefaultSubobject<USparseVolumeTextureViewerComponent>(TEXT("SparseVolumeTextureViewerComponent"));
	RootComponent = SparseVolumeTextureViewerComponent;

#if WITH_EDITORONLY_DATA

	if (!IsRunningCommandlet())
	{
		// Structure to hold one-time initialization
		struct FConstructorStatics
		{
			ConstructorHelpers::FObjectFinderOptional<UTexture2D> SparseVolumeTextureViewerTextureObject;
			FName ID_SparseVolumeTextureViewer;
			FText NAME_SparseVolumeTextureViewer;
			FConstructorStatics()
				: SparseVolumeTextureViewerTextureObject(TEXT("/Engine/EditorResources/S_VolumetricCloud"))	// SVT_TODO set a specific icon
				, ID_SparseVolumeTextureViewer(TEXT("Fog"))
				, NAME_SparseVolumeTextureViewer(NSLOCTEXT("SpriteCategory", "Fog", "Fog"))
			{
			}
		};
		static FConstructorStatics ConstructorStatics;

		if (GetSpriteComponent())
		{
			GetSpriteComponent()->Sprite = ConstructorStatics.SparseVolumeTextureViewerTextureObject.Get();
			GetSpriteComponent()->SetRelativeScale3D(FVector(0.5f, 0.5f, 0.5f));
			GetSpriteComponent()->SpriteInfo.Category = ConstructorStatics.ID_SparseVolumeTextureViewer;
			GetSpriteComponent()->SpriteInfo.DisplayName = ConstructorStatics.NAME_SparseVolumeTextureViewer;
			GetSpriteComponent()->SetupAttachment(SparseVolumeTextureViewerComponent);
		}
	}
#endif // WITH_EDITORONLY_DATA

	PrimaryActorTick.bCanEverTick = true;
	SetHidden(false);
}

#undef LOCTEXT_NAMESPACE
