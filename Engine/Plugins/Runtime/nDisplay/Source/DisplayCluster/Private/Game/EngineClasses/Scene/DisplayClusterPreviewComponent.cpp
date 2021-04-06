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

#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"

#include "IDisplayClusterProjection.h"

#include "UObject/ConstructorHelpers.h"


UDisplayClusterPreviewComponent::UDisplayClusterPreviewComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	RenderTarget = nullptr;
	RenderTexture = nullptr;
	ViewportConfig = nullptr;
	TextureSize = FIntPoint(512, 512);
	TextureGamma = 2.0f;
	RefreshPeriod = 0;
	bApplyWarpBlend = true;
	OriginalMaterial = nullptr;
	PreviewMaterial = nullptr;
	PreviewMaterialInstance = nullptr;

	PrimaryComponentTick.bCanEverTick = !IsTemplate();
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetRefreshPeriod(RefreshPeriod);
	bAutoActivate = true;
	bTickInEditor = true;
	bWantsInitializeComponent = true;

	static ConstructorHelpers::FObjectFinder<UMaterial> PreviewMaterialObj(TEXT("/nDisplay/Materials/Preview/M_ProjPolicyPreview"));
	check(PreviewMaterialObj.Object);
	PreviewMaterial = PreviewMaterialObj.Object;
#endif
}


#if WITH_EDITOR
void UDisplayClusterPreviewComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	InitializeInternals();
	UpdateProjectionPolicy();
}

void UDisplayClusterPreviewComponent::BuildPreview()
{
	if (ProjectionPolicyInstance.IsValid())
	{
		if (ProjectionPolicyInstance->HasMeshPreview())
		{
			PreviewMesh = ProjectionPolicyInstance->BuildMeshPreview(ProjectionPolicyParameters);
			if (PreviewMesh)
			{
				UMaterialInterface* MatInterface = PreviewMesh->GetMaterial(0);
				if (MatInterface)
				{
					OriginalMaterial = MatInterface->GetMaterial();
				}

				PreviewMesh->SetMaterial(0, PreviewMaterialInstance);
			}
		}
	}
}

void UDisplayClusterPreviewComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	// Projection policy
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDisplayClusterPreviewComponent, ProjectionPolicy))
	{
		UpdateProjectionPolicy();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDisplayClusterPreviewComponent, RefreshPeriod))
	{
		SetRefreshPeriod(RefreshPeriod);
	}

	// PostEditChangeProperty will end up calling the owning actor's RerunConstructionScripts() and bIsEditingProperty let's us handle that.
	bIsEditingProperty = true;
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bIsEditingProperty = false;
}

void UDisplayClusterPreviewComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (ProjectionPolicyInstance.IsValid() && ProjectionPolicyInstance->HasPreviewRendering() && RootActor)
	{
		if (PreviewMesh && PreviewMesh->GetName().Find(TEXT("TRASH_")) != INDEX_NONE)
		{
			// Screen components are regenerated from construction scripts, but preview components are added in dynamically. This preview component may end up
			// pointing to invalid data on reconstruction.
			// TODO: See if we can remove this hack
			RenderTexture = nullptr;
			BuildPreview();
		}
		
		if (ViewportConfig)
		{
			UDisplayClusterCameraComponent* Camera = RootActor->GetCameraById(ViewportConfig->Camera);
			if (!Camera)
			{
				Camera = RootActor->GetCameraById(RootActor->GetPreviewDefaultCamera());
				if (!Camera)
				{
					Camera = RootActor->GetDefaultCamera();
				}
			}

			if (Camera && RenderTarget)
			{
				UpdateRenderTarget();
				ProjectionPolicyInstance->RenderFrame(Camera, ProjectionPolicyParameters, RenderTarget->GameThread_GetRenderTargetResource(), FIntRect(FIntPoint(0, 0), FIntPoint(RenderTarget->SizeX, RenderTarget->SizeY)), bApplyWarpBlend);
			}
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UDisplayClusterPreviewComponent::DestroyComponent(bool bPromoteChildren)
{
	if (PreviewMesh)
	{
		PreviewMesh->SetMaterial(0, OriginalMaterial);
	}

	ProjectionPolicyInstance.Reset();
	RenderTexture = nullptr;

	Super::DestroyComponent(bPromoteChildren);
}

void UDisplayClusterPreviewComponent::OnUnregister()
{
	Super::OnUnregister();
	ProjectionPolicyInstance.Reset();
}

void UDisplayClusterPreviewComponent::SetConfigData(ADisplayClusterRootActor* InRootActor, UDisplayClusterConfigurationViewport* InViewportConfig)
{
	ViewportConfig = InViewportConfig;
	RootActor = InRootActor;

	if (ViewportConfig)
	{
		// This could be triggered just from editing a property on this preview and the preview may have an updated PolicyType that isn't the same as the underlying data.
		SetProjectionPolicy(bIsEditingProperty ? ProjectionPolicy : ViewportConfig->ProjectionPolicy.Type);
	}

	BuildPreview();
}

void UDisplayClusterPreviewComponent::SetProjectionPolicy(const FString& ProjPolicy)
{
	ProjectionPolicy = ProjPolicy;
	UpdateProjectionPolicy();
}

void UDisplayClusterPreviewComponent::InitializeInternals()
{
	// Store all available projection policy types
	IDisplayCluster::Get().GetRenderMgr()->GetRegisteredProjectionPolicies(ProjectionPolicies);

	static const TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
	static const EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread()));

	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this);
		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->TargetGamma = TextureGamma;
		RenderTarget->InitCustomFormat(TextureSize.X, TextureSize.Y, SceneTargetFormat, false);
	}

	if (!PreviewMaterialInstance)
	{
		PreviewMaterialInstance = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
	}

	if (PreviewMaterialInstance && RenderTarget)
	{
		PreviewMaterialInstance->SetTextureParameterValue(TEXT("Preview"), RenderTarget);
	}
}

void UDisplayClusterPreviewComponent::UpdateProjectionPolicy()
{
	// Release current instance
	ProjectionPolicyInstance.Reset();
	ProjectionPolicyParameters = nullptr;

	// Get new policy instance
	if (ProjectionPolicies.Contains(ProjectionPolicy))
	{
		TSharedPtr<IDisplayClusterProjectionPolicyFactory> Factory = IDisplayClusterProjection::Get().GetProjectionFactory(ProjectionPolicy);
		if (Factory.IsValid())
		{
			const FString RHIName = GDynamicRHI->GetName();
			ProjectionPolicyInstance = Factory->Create(ProjectionPolicy, RHIName, FString(), TMap<FString, FString>());
		}
	}

	// Initialize policy specific parameters object
	if (ProjectionPolicyInstance.IsValid())
	{
		ProjectionPolicyParameters = ProjectionPolicyInstance->CreateParametersObject(this);
		if (ProjectionPolicyParameters)
		{
			if (ViewportConfig)
			{
				ProjectionPolicyParameters->Parse(RootActor, ViewportConfig->ProjectionPolicy);
			}
			ProjectionPolicyInstance->InitializePreview(ProjectionPolicyParameters);
		}
	}
}

void UDisplayClusterPreviewComponent::UpdateRenderTarget()
{
	if (!RenderTarget)
	{
		return;
	}
	RenderTarget->TargetGamma = TextureGamma;
	RenderTarget->ResizeTarget(TextureSize.X, TextureSize.Y);
}

bool UDisplayClusterPreviewComponent::IsPreviewAvailable() const
{
	return ProjectionPolicyInstance.IsValid() ? ProjectionPolicyInstance->HasPreviewRendering() : false;
}

UTexture2D* UDisplayClusterPreviewComponent::GetOrCreateRenderTexture2D()
{
	if (!IsPreviewAvailable())
	{
		RenderTexture = nullptr;
		return nullptr;
	}
	
	if (RenderTarget)
	{

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
					RenderTexture = nullptr;
					return nullptr;
				}
			}
		}

		UTexture2D* Texture = RenderTexture ? RenderTexture : UTexture2D::CreateTransient(RenderTarget->SizeX, RenderTarget->SizeY, PF_B8G8R8A8);
		{
			Texture->MipGenSettings = TMGS_NoMipmaps;
			Texture->SRGB = RenderTarget->SRGB;

			void* TextureData = Texture->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			const int32 TextureDataSize = SurfData.Num() * 4;
			FMemory::Memcpy(TextureData, SurfData.GetData(), TextureDataSize);
			Texture->PlatformData->Mips[0].BulkData.Unlock();
			Texture->UpdateResource();
		}

		if (Texture->GetOuter() != this)
		{
			Texture->Rename(nullptr, this, REN_DoNotDirty | REN_ForceNoResetLoaders | REN_DontCreateRedirectors);
		}

		RenderTexture = Texture;
	}

	return RenderTexture;
}
#endif
