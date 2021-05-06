// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/DisplayClusterPreviewComponent.h"
#include "Components/DisplayClusterCameraComponent.h"

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "DisplayClusterRootActor.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterStrings.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "RHI.h"
#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"

#include "IDisplayClusterProjection.h"
#include "Render/Viewport/IDisplayClusterViewport.h"

#include "UObject/ConstructorHelpers.h"


UDisplayClusterPreviewComponent::UDisplayClusterPreviewComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	static ConstructorHelpers::FObjectFinder<UMaterial> PreviewMaterialObj(TEXT("/nDisplay/Materials/Preview/M_ProjPolicyPreview"));

	check(PreviewMaterialObj.Object);

	bWantsInitializeComponent = true;

	PreviewMaterial = PreviewMaterialObj.Object;
	PreviewMesh = nullptr;
#endif
}

#if WITH_EDITOR

const uint32 UDisplayClusterPreviewComponent::MaxRenderTargetDimension = 2048;

void UDisplayClusterPreviewComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	InitializeInternals();
}

void UDisplayClusterPreviewComponent::DestroyComponent(bool bPromoteChildren)
{
	if (PreviewMesh)
	{
		bUseMeshUsePreviewMaterialInstance = false;
		PreviewMesh->SetMaterial(0, OriginalMaterial);
	}

	RemovePreviewTexture();

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


bool UDisplayClusterPreviewComponent::UpdatePreviewMesh()
{
	check(IsInGameThread());

	// And search for new mesh reference
	IDisplayClusterViewport* Viewport = GetCurrentViewport();
	if (Viewport != nullptr && Viewport->GetProjectionPolicy().IsValid())
	{
		if (Viewport->GetProjectionPolicy()->HasPreviewMesh())
		{
			// create warp mesh or update changes
			if (PreviewMesh == nullptr || Viewport->GetProjectionPolicy()->IsConfigurationChanged(&WarpMeshSavedProjectionPolicy))
			{
				// Handle delete prev mesh component
				if (PreviewMesh)
				{
					if (bUseMeshUsePreviewMaterialInstance && OriginalMaterial != nullptr)
					{
						// Restore
						bUseMeshUsePreviewMaterialInstance = false;
						PreviewMesh->SetMaterial(0, OriginalMaterial);
					}

					// Forget old mesh ptr
					PreviewMesh = nullptr;
					OriginalMaterial = nullptr;
				}

				bUseMeshUsePreviewMaterialInstance = false;
				PreviewMesh = Viewport->GetProjectionPolicy()->GetOrCreatePreviewMeshComponent(Viewport);

				if (PreviewMesh)
				{
					// Save original mesh material
					UMaterialInterface* MatInterface = PreviewMesh->GetMaterial(0);
					if (MatInterface)
					{
						OriginalMaterial = MatInterface->GetMaterial();
					}
					
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
	}

	return false;
}

void UDisplayClusterPreviewComponent::UpdatePreviewResources()
{
	if (PreviewMesh && PreviewMesh->GetName().Find(TEXT("TRASH_")) != INDEX_NONE)
	{
		// Screen components are regenerated from construction scripts, but preview components are added in dynamically. This preview component may end up
		// pointing to invalid data on reconstruction.
		// TODO: See if we can remove this hack
		// 
		//!
		PreviewMesh = nullptr;
	}

	if (GetWorld())
	{
		UpdatePreviewRenderTarget();
		UpdatePreviewMesh();
	}

	if (PreviewMesh)
	{
		if (ViewportConfig && ViewportConfig->bIsEnabled)
		{
			// Render preview on mesh:
			if (bUseMeshUsePreviewMaterialInstance == false)
			{
				PreviewMesh->SetMaterial(0, PreviewMaterialInstance);
				bUseMeshUsePreviewMaterialInstance = true;
			}
		}
		else
		{
			// Restore original material
			if (bUseMeshUsePreviewMaterialInstance == true)
			{
				PreviewMesh->SetMaterial(0, OriginalMaterial);
				bUseMeshUsePreviewMaterialInstance = false;
			}
		}
	}
}

void UDisplayClusterPreviewComponent::InitializeInternals()
{
	if (!PreviewMaterialInstance)
	{
		PreviewMaterialInstance = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
	}
}

void UDisplayClusterPreviewComponent::UpdatePreviewRenderTarget()
{
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

			if (!PreviewMaterialInstance)
			{
				PreviewMaterialInstance = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
			}

			if (PreviewMaterialInstance && RenderTarget)
			{
				PreviewMaterialInstance->SetTextureParameterValue(TEXT("Preview"), RenderTarget);
			}
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
			FTextureRenderTarget2DResource* TexResource = (FTextureRenderTarget2DResource*)RenderTarget->Resource;
			if (TexResource)
			{
				FCanvas Canvas(TexResource, NULL, 0, 0, 0, GMaxRHIFeatureLevel);
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

			//! Debug purpose
			// The int casts above can sometimes cause the OutSize to have a zero in one or both its components, which will cause crashes when
			// creating the render target on the preview component. Clamp OutSize so that it always has a size of at least 1 in each coordinate
			static const int32 MaxTextureSize = 1 << (GMaxTextureMipCount - 1);
			check(OutSize.X <= MaxTextureSize);
			check(OutSize.Y <= MaxTextureSize);
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
	return (Viewport != nullptr) && Viewport->GetProjectionPolicy().IsValid();//! && Viewport->GetProjectionPolicy()->HasPreviewMesh();
}

void UDisplayClusterPreviewComponent::RemovePreviewTexture()
{
#if WITH_EDITOR
	//! FIXme Add/remove texture for UE resource collection

	//! @todo: add correct RenderTexture delete
	//! 
	PreviewTexture = nullptr;
#endif
}

bool UDisplayClusterPreviewComponent::UpdatePreviewTexture()
{
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
				RemovePreviewTexture();
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
			RemovePreviewTexture();
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
		void* TextureData = PreviewTexture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		const int32 TextureDataSize = SurfData.Num() * 4;
		FMemory::Memcpy(TextureData, SurfData.GetData(), TextureDataSize);
		PreviewTexture->PlatformData->Mips[0].BulkData.Unlock();
		PreviewTexture->UpdateResource();
	}

	if (PreviewTexture->GetOuter() != this)
	{
		PreviewTexture->Rename(nullptr, this, REN_DoNotDirty | REN_ForceNoResetLoaders | REN_DontCreateRedirectors);
	}

	return true;
}

void UDisplayClusterPreviewComponent::HandleRenderTargetTextureDefferedUpdate()
{
	check(IsInGameThread());

	//! @todo: integrate to configurator logic
	//! deffered update flag
	bIsRenderTargetSurfaceChanged = true;
}

UTexture2D* UDisplayClusterPreviewComponent::GetOrCreateRenderTexture2D()
{
	if (!IsPreviewAvailable())
	{
		RemovePreviewTexture();
	}
	else
	if (RenderTarget && bIsRenderTargetSurfaceChanged)
	{
		UpdatePreviewTexture();
	}

	return PreviewTexture;
}

#endif
