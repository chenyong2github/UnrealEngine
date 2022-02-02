// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterPreviewComponent.h"
#include "Components/DisplayClusterCameraComponent.h"

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "RHI.h"
#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "CanvasTypes.h"

#include "IDisplayClusterProjection.h"
#include "Render/Viewport/IDisplayClusterViewport.h"

#include "UObject/ConstructorHelpers.h"


UDisplayClusterPreviewComponent::UDisplayClusterPreviewComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	static ConstructorHelpers::FObjectFinder<UMaterial> PreviewMaterialObj(TEXT("/nDisplay/Materials/Preview/M_ProjPolicyPreview"));
	check(PreviewMaterialObj.Object);

	PreviewMaterial = PreviewMaterialObj.Object;

	bWantsInitializeComponent = true;
#endif
}

#if WITH_EDITOR
void UDisplayClusterPreviewComponent::OnComponentCreated()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UDisplayClusterPreviewComponent::OnComponentCreated"), STAT_OnComponentCreated, STATGROUP_NDisplay);
	
	Super::OnComponentCreated();

	InitializePreviewMaterial();
}

void UDisplayClusterPreviewComponent::DestroyComponent(bool bPromoteChildren)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UDisplayClusterPreviewComponent::DestroyComponent"), STAT_DestroyComponent, STATGROUP_NDisplay);
	
	ReleasePreviewMesh();
	ReleasePreviewMaterial();

	ReleasePreviewTexture();
	ReleasePreviewRenderTarget();

	Super::DestroyComponent(bPromoteChildren);
}

IDisplayClusterViewport* UDisplayClusterPreviewComponent::GetCurrentViewport() const
{
	if (RootActor != nullptr)
	{
		return RootActor->FindPreviewViewport(ViewportId);
	}

	return nullptr;
}

bool UDisplayClusterPreviewComponent::InitializePreviewComponent(ADisplayClusterRootActor* InRootActor, const FString& InViewportId, UDisplayClusterConfigurationViewport* InViewportConfig)
{
	RootActor = InRootActor;
	ViewportId = InViewportId;
	ViewportConfig = InViewportConfig;

	return true;
}

void UDisplayClusterPreviewComponent::UpdatePreviewMeshMaterial(bool bRestoreOriginalMaterial)
{
	if (bRestoreOriginalMaterial && !bIsRootActorPreviewMesh)
	{
		// Forged created meshes, dont restore
		ReleasePreviewMesh();
	}

	if (PreviewMesh)
	{
		bool bViewportPreviewEnabled = (ViewportConfig && RootActor && RootActor->bPreviewEnable);

		if (bRestoreOriginalMaterial || !bViewportPreviewEnabled)
		{
			// Restore
			if (OriginalMaterial)
			{
				PreviewMesh->SetMaterial(0, OriginalMaterial);
				OriginalMaterial = nullptr;
			}
		}
		else
		{
			// Save original material
			if (OriginalMaterial == nullptr)
			{
				// Save original mesh material
				UMaterialInterface* MatInterface = PreviewMesh->GetMaterial(0);
				if (MatInterface)
				{
					OriginalMaterial = MatInterface->GetMaterial();
				}
			}

			// Set preview material
			if (PreviewMaterialInstance)
			{
				PreviewMesh->SetMaterial(0, PreviewMaterialInstance);
			}
		}
	}
}

bool UDisplayClusterPreviewComponent::UpdatePreviewMesh(bool bRestoreOriginalMaterial)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UDisplayClusterPreviewComponent::UpdatePreviewMesh"), STAT_UpdatePreviewMesh, STATGROUP_NDisplay);
	
	check(IsInGameThread());

	if (PreviewMesh && PreviewMesh->GetName().Find(TEXT("TRASH_")) != INDEX_NONE)
	{
		// Screen components are regenerated from construction scripts, but preview components are added in dynamically. This preview component may end up
		// pointing to invalid data on reconstruction.
		// TODO: See if we can remove this hack
		ReleasePreviewMesh();
	}

	// And search for new mesh reference
	IDisplayClusterViewport* Viewport = GetCurrentViewport();
	if (Viewport != nullptr && Viewport->GetProjectionPolicy().IsValid())
	{
		if (Viewport->GetProjectionPolicy()->HasPreviewMesh())
		{
			// create warp mesh or update changes
			if (Viewport->GetProjectionPolicy()->IsConfigurationChanged(&WarpMeshSavedProjectionPolicy))
			{
				UpdatePreviewMeshMaterial(true);
				ReleasePreviewMesh();
			}

			if (PreviewMesh == nullptr)
			{
				// Get new mesh ptr
				PreviewMesh = Viewport->GetProjectionPolicy()->GetOrCreatePreviewMeshComponent(Viewport, bIsRootActorPreviewMesh);

				UpdatePreviewMeshMaterial(bRestoreOriginalMaterial);
					
					if (!ensure(ViewportConfig))
					{
						// Can be null during a reimport.
						// @TODO reimport: See if we can avoid this during reimport.
						return false;
					}

					// Update saved proj policy parameters
					WarpMeshSavedProjectionPolicy = ViewportConfig->ProjectionPolicy;
					return true;
				}
			}
		}
	else
	{
		UpdatePreviewMeshMaterial(true);
	}

	return false;
}

void UDisplayClusterPreviewComponent::ReleasePreviewMesh()
{
	// Forget old mesh with material
	PreviewMesh = nullptr;
	OriginalMaterial = nullptr;
}

void UDisplayClusterPreviewComponent::UpdatePreviewResources()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UDisplayClusterPreviewComponent::UpdatePreviewResources"), STAT_UpdatePreviewResources, STATGROUP_NDisplay);
	
	if (GetWorld())
	{
		UpdatePreviewRenderTarget();
		UpdatePreviewMesh();
	}

	UpdatePreviewMeshMaterial();
}

void UDisplayClusterPreviewComponent::UpdatePreviewMaterial()
{
	if (PreviewMaterialInstance != nullptr)
	{
		PreviewMaterialInstance->SetTextureParameterValue(TEXT("Preview"), RenderTarget);
	}
}

void UDisplayClusterPreviewComponent::InitializePreviewMaterial()
{
	if (PreviewMaterial != nullptr && PreviewMaterialInstance == nullptr)
	{
		PreviewMaterialInstance = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
	}
}

void UDisplayClusterPreviewComponent::ReleasePreviewMaterial()
{
	if (PreviewMaterialInstance != nullptr)
	{
		PreviewMaterialInstance->SetTextureParameterValue(TEXT("Preview"), nullptr);
		PreviewMaterialInstance = nullptr;
	}
}

void UDisplayClusterPreviewComponent::ReleasePreviewRenderTarget()
{
	if (RenderTarget != nullptr)
	{
		RenderTarget = nullptr;
	}
}

void UDisplayClusterPreviewComponent::UpdatePreviewRenderTarget()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UDisplayClusterPreviewComponent::UpdatePreviewRenderTarget"), STAT_UpdatePreviewRenderTarget, STATGROUP_NDisplay);
	
	FIntPoint TextureSize(1,1);
	float     TextureGamma = 1.f;

	if (GetPreviewTextureSettings(TextureSize, TextureGamma))
	{
		// Create new RTT
		if (RenderTarget == nullptr)
		{
			RenderTarget = NewObject<UTextureRenderTarget2D>(this);
			RenderTarget->ClearColor = FLinearColor::Black;
			RenderTarget->TargetGamma = TextureGamma;

			static const TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
			static const EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread()));

			RenderTarget->InitCustomFormat(TextureSize.X, TextureSize.Y, SceneTargetFormat, false);

			InitializePreviewMaterial();
			UpdatePreviewMaterial();
		}
		// Update exist RTT resource:
		else
		{
			RenderTarget->ResizeTarget(TextureSize.X, TextureSize.Y);
			RenderTarget->TargetGamma = TextureGamma;
		}
	}
	else
	{
		//@todo: disable this viewport
		if (RenderTarget)
		{
			// clear preview RTT to black in this case
			FTextureRenderTarget2DResource* TexResource = (FTextureRenderTarget2DResource*)RenderTarget->GetResource();
			if (TexResource)
			{
				FCanvas Canvas(TexResource, NULL, FGameTime(), GMaxRHIFeatureLevel);
				Canvas.Clear(FLinearColor::Black);
			}
		}
	}
}

bool UDisplayClusterPreviewComponent::GetPreviewTextureSettings(FIntPoint& OutSize, float& OutGamma) const
{
	IDisplayClusterViewport* Viewport = GetCurrentViewport();
	if (Viewport != nullptr)
	{
		// The viewport size is already capped for RenderSettings
		const TArray<FDisplayClusterViewport_Context>& Contexts = Viewport->GetContexts();
		if (Contexts.Num() > 0)
		{
			OutSize = Contexts[0].FrameTargetRect.Size();

			check(OutSize.X > 0);
			check(OutSize.Y > 0);

			//! Get gamma from current FViewport
			OutGamma = 2.2f;

			return true;
		}
	}

	return false;
}

bool UDisplayClusterPreviewComponent::IsPreviewAvailable() const
{
	IDisplayClusterViewport* Viewport = GetCurrentViewport();
	return (Viewport != nullptr) && Viewport->GetProjectionPolicy().IsValid();
}

void UDisplayClusterPreviewComponent::ReleasePreviewTexture()
{
	if (PreviewTexture != nullptr)
	{
		PreviewTexture = nullptr;
	}
}

bool UDisplayClusterPreviewComponent::UpdatePreviewTexture()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UDisplayClusterPreviewComponent::UpdatePreviewTexture"), STAT_UpdatePreviewTexture, STATGROUP_NDisplay);
	
	check(RenderTarget);

	TArray<FColor> SurfData;
	FRenderTarget* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	RenderTargetResource->ReadPixels(SurfData);
	{
		// Check for invalid data.. could happen if a viewport was unbound from a screen/mesh/ect but still has a policy assigned.
		const FColor EmptyColor(ForceInitToZero);
		if (SurfData.Num() > 0 && SurfData[0] == EmptyColor)
		{
			// Quick test on first element shows we might have an invalid texture.
			const TSet<FColor> TestEmpty(SurfData);
			if (TestEmpty.Num() == 1 && *TestEmpty.CreateConstIterator() == EmptyColor)
			{
				// Check rest of the texture -- Texture is blank
				ReleasePreviewTexture();
				return false;
			}
		}
	}

	FIntPoint DstSize = RenderTargetResource->GetSizeXY();
	bool SRGB = RenderTarget->SRGB;

	// If source rendertarget texture changed
	if (PreviewTexture != nullptr)
	{
		if (PreviewTexture->GetSizeX() != DstSize.X || PreviewTexture->GetSizeY() != DstSize.Y || PreviewTexture->SRGB != SRGB)
		{
			// Size changed, re-create
			ReleasePreviewTexture();
		}
	}

	if (PreviewTexture == nullptr)
	{
		// Create new resource
		PreviewTexture = UTexture2D::CreateTransient(DstSize.X, DstSize.Y, PF_B8G8R8A8);
		if (PreviewTexture == nullptr)
		{
			return false;
		}

		PreviewTexture->MipGenSettings = TMGS_NoMipmaps;
		PreviewTexture->SRGB = RenderTarget->SRGB;
	}

	// Transfer data
	{
		void* TextureData = PreviewTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		const int32 TextureDataSize = SurfData.Num() * 4;
		FMemory::Memcpy(TextureData, SurfData.GetData(), TextureDataSize);
		PreviewTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
		PreviewTexture->UpdateResource();
	}

	if (PreviewTexture->GetOuter() != this)
	{
		PreviewTexture->Rename(nullptr, this, REN_DoNotDirty | REN_ForceNoResetLoaders | REN_DontCreateRedirectors);
	}

	return true;
}

void UDisplayClusterPreviewComponent::HandleRenderTargetTextureDeferredUpdate()
{
	check(IsInGameThread());

	RenderTargetSurfaceChangedCnt = 2;
}

UTexture2D* UDisplayClusterPreviewComponent::GetOrCreateRenderTexture2D()
{
	if (!IsPreviewAvailable())
	{
		ReleasePreviewTexture();
	}
	else
	if (RenderTarget && RenderTargetSurfaceChangedCnt)
	{
		if (--RenderTargetSurfaceChangedCnt == 0)
		{
			UpdatePreviewTexture();
		}
	}

	return PreviewTexture;
}

#endif
