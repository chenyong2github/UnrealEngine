// Copyright Epic Games, Inc. All Rights Reserved.

//-TODO: Remove post from capture
//-TODO: Hook up loading a preview environment
//-TODO: Add custom post processes for capturing data?
//-TODO: Allow us to toggle preview on / off for flipbook / realtime

#include "ViewModels/NiagaraFlipbookViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "Widgets/SNiagaraFlipbookWidget.h"
#include "NiagaraFlipbookRenderer.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

#include "Niagara/Classes/NiagaraDataInterfaceRenderTarget2D.h"

#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "EngineModule.h"
#include "AssetToolsModule.h"
#include "LegacyScreenPercentageDriver.h"
#include "PackageTools.h"
#include "AdvancedPreviewScene.h"

#define LOCTEXT_NAMESPACE "NiagaraFlipbookViewModel"

FNiagaraFlipbookViewModel::FNiagaraFlipbookViewModel()
{
}

FNiagaraFlipbookViewModel::~FNiagaraFlipbookViewModel()
{
	if ( AdvancedPreviewScene && PreviewComponent )
	{
		AdvancedPreviewScene->RemoveComponent(PreviewComponent);
	}

	if ( PreviewComponent )
	{
		PreviewComponent->DestroyComponent();
	}
}

void FNiagaraFlipbookViewModel::Initialize(TWeakPtr<FNiagaraSystemViewModel> InWeakSystemViewModel)
{
	WeakSystemViewModel = InWeakSystemViewModel;

	{
		UNiagaraSystem* System = WeakSystemViewModel.Pin()->GetPreviewComponent()->GetAsset();

		PreviewComponent = NewObject<UNiagaraComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		PreviewComponent->CastShadow = 1;
		PreviewComponent->bCastDynamicShadow = 1;
		PreviewComponent->SetAllowScalability(false);
		PreviewComponent->SetAsset(System);
		PreviewComponent->SetForceSolo(true);
		PreviewComponent->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
		PreviewComponent->SetCanRenderWhileSeeking(false);
		PreviewComponent->Activate(true);

		AdvancedPreviewScene = MakeShareable(new FAdvancedPreviewScene(FPreviewScene::ConstructionValues()));
		AdvancedPreviewScene->SetFloorVisibility(false);
		AdvancedPreviewScene->AddComponent(PreviewComponent, PreviewComponent->GetRelativeTransform());
	}

	Widget = SNew(SNiagaraFlipbookWidget)
		.WeakViewModel(this->AsShared());
}

void FNiagaraFlipbookViewModel::RefreshView()
{
	//-TOOD:
}

void FNiagaraFlipbookViewModel::SetDisplayTimeFromNormalized(float NormalizeTime)
{
	if ( Widget )
	{
		if (const UNiagaraFlipbookSettings* GeneratedSettings = GetFlipbookGeneratedSettings())
		{
			const float RelativeTime = FMath::Clamp(NormalizeTime, 0.0f, 1.0f) * GeneratedSettings->DurationSeconds;
			Widget->SetPreviewRelativeTime(RelativeTime);
		}
		else if (UNiagaraFlipbookSettings* FlipbookSettings = GetFlipbookSettings())
		{
			const float RelativeTime = FMath::Clamp(NormalizeTime, 0.0f, 1.0f) * FlipbookSettings->DurationSeconds;
			Widget->SetPreviewRelativeTime(RelativeTime);
		}
	}
}

UNiagaraComponent* FNiagaraFlipbookViewModel::GetPreviewComponent() const
{
	return PreviewComponent;
}

UNiagaraFlipbookSettings* FNiagaraFlipbookViewModel::GetFlipbookSettings() const
{
	if (PreviewComponent)
	{
		UNiagaraSystem* Asset = PreviewComponent->GetAsset();
		return Asset ? Asset->GetFlipbookSettings() : nullptr;
	}

	return nullptr;
}

const UNiagaraFlipbookSettings* FNiagaraFlipbookViewModel::GetFlipbookGeneratedSettings() const
{
	if (PreviewComponent)
	{
		UNiagaraSystem* Asset = PreviewComponent->GetAsset();
		return Asset ? Asset->GetFlipbookGeneratedSettings() : nullptr;
	}

	return nullptr;
}

const FNiagaraFlipbookTextureSettings* FNiagaraFlipbookViewModel::GetPreviewTextureSettings() const
{
	if ( UNiagaraFlipbookSettings* Settings = GetFlipbookSettings() )
	{
		if ( Settings->OutputTextures.IsValidIndex(PreviewTextureIndex) )
		{
			return &Settings->OutputTextures[PreviewTextureIndex];
		}
	}
	return nullptr;
}

TSharedPtr<class SWidget> FNiagaraFlipbookViewModel::GetWidget()
{
	return Widget;
}

void FNiagaraFlipbookViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
}

void FNiagaraFlipbookViewModel::RenderFlipbook()
{
	UNiagaraFlipbookSettings* FlipbookSettings = GetFlipbookSettings();
	if ( (PreviewComponent == nullptr) || (FlipbookSettings == nullptr) )
	{
		return;
	}

	if ( FlipbookSettings->OutputTextures.Num() == 0 )
	{
		return;
	}

	UWorld* World = PreviewComponent->GetWorld();
	FSceneInterface* FlipbookScene = World->Scene;

	NiagaraEmitterInstanceBatcher* NiagaraBatcher = nullptr;
	if (World->Scene && World->Scene->GetFXSystem())
	{
		NiagaraBatcher = static_cast<NiagaraEmitterInstanceBatcher*>(World->Scene->GetFXSystem()->GetInterface(NiagaraEmitterInstanceBatcher::Name));
	}
	check(NiagaraBatcher);

	// Create output render targets & output flipbook data
	TArray<UTextureRenderTarget2D*, TInlineAllocator<4>> RenderTargets;
	TArray<TArray<FFloat16Color>> OutputFlipbooks;

	for ( const auto& OutputTexture : FlipbookSettings->OutputTextures )
	{
		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
		RenderTarget->AddToRoot();
		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->TargetGamma = 1.0f;
		RenderTarget->InitCustomFormat(OutputTexture.FrameSize.X, OutputTexture.FrameSize.Y, PF_FloatRGBA, false);

		RenderTargets.Add(RenderTarget);
		OutputFlipbooks.AddDefaulted_GetRef().SetNumZeroed(OutputTexture.TextureSize.X * OutputTexture.TextureSize.Y);
	}

	// Render frames
	PreviewComponent->ReinitializeSystem();

	const int32 TotalFrames = FlipbookSettings->FramesPerDimension.X * FlipbookSettings->FramesPerDimension.Y;
	const float FrameDeltaSeconds = FlipbookSettings->DurationSeconds / float(TotalFrames);

	FScopedSlowTask SlowTask(TotalFrames, LOCTEXT("RenderingFlipbook", "Rendering Flipbook..."));
	SlowTask.MakeDialog();

	for ( int32 iFrame=0; iFrame < TotalFrames; ++iFrame)
	{
		SlowTask.EnterProgressFrame(1);

		// Step component to time
		const float FrameTime = FlipbookSettings->StartSeconds + (float(iFrame) * FrameDeltaSeconds);
		PreviewComponent->SetSeekDelta(FlipbookSettings->GetSeekDelta());
		PreviewComponent->SeekToDesiredAge(FrameTime);
		PreviewComponent->TickComponent(FlipbookSettings->GetSeekDelta(), ELevelTick::LEVELTICK_All, nullptr);

		// We need any GPU sims to flush pending ticks
		if (NiagaraBatcher)
		{
			ENQUEUE_RENDER_COMMAND(NiagaraFlushBatcher)(
				[RT_NiagaraBatcher=NiagaraBatcher](FRHICommandListImmediate& RHICmdList)
				{
					RT_NiagaraBatcher->ProcessPendingTicksFlush(RHICmdList, true);
				}
			);
		}

		const float WorldTime = FApp::GetCurrentTime() - FlipbookSettings->StartSeconds - FlipbookSettings->DurationSeconds + FrameTime;

		// Render frame
		FNiagaraFlipbookRenderer FlipbookRenderer(PreviewComponent, FlipbookSettings, WorldTime);
		for (int32 iOutputTexture=0; iOutputTexture < FlipbookSettings->OutputTextures.Num(); ++iOutputTexture)
		{
			const FNiagaraFlipbookTextureSettings& OutputTexture = FlipbookSettings->OutputTextures[iOutputTexture];
			UTextureRenderTarget2D* RenderTarget = RenderTargets[iOutputTexture];
			FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

			FlipbookRenderer.RenderView(RenderTarget, iOutputTexture);

			TArray<FFloat16Color> OutSamples;
			RenderTargetResource->ReadFloat16Pixels(OutSamples);

			const FIntPoint ViewportOffset = FIntPoint(
				OutputTexture.FrameSize.X * (iFrame % FlipbookSettings->FramesPerDimension.X),
				OutputTexture.FrameSize.Y * (iFrame / FlipbookSettings->FramesPerDimension.X)
			);
			TArray<FFloat16Color>& OutputFlipbook = OutputFlipbooks[iOutputTexture];
			for ( int y=0; y < OutputTexture.FrameSize.Y; ++y )
			{
				FFloat16Color* SrcPixel = OutSamples.GetData() + (y * OutputTexture.FrameSize.X);
				FFloat16Color* DstPixel = OutputFlipbook.GetData() + ViewportOffset.X + ((y + ViewportOffset.Y) * OutputTexture.TextureSize.X);
				FMemory::Memcpy(DstPixel, SrcPixel, sizeof(FFloat16Color) * OutputTexture.FrameSize.X);
			}
		}
	}

	// Send data to generated textures
	for (int32 iOutputTexture = 0; iOutputTexture < FlipbookSettings->OutputTextures.Num(); ++iOutputTexture)
	{
		FNiagaraFlipbookTextureSettings& OutputTexture = FlipbookSettings->OutputTextures[iOutputTexture];

		// If we don't have a texture create one
		if (OutputTexture.GeneratedTexture == nullptr)
		{
			FString PackagePath = FPaths::GetPath(PreviewComponent->GetAsset()->GetPackage()->GetPathName());
			if (OutputTexture.OutputName.IsNone())
			{
				PackagePath = PackagePath / PreviewComponent->GetAsset()->GetName() + TEXT("_Flipbook") + FString::FromInt(iOutputTexture);
			}
			else
			{
				PackagePath = PackagePath / PreviewComponent->GetAsset()->GetName() + OutputTexture.OutputName.ToString();
			}

			IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			FString PackageName;
			FString AssetName;
			AssetTools.CreateUniqueAssetName(UPackageTools::SanitizePackageName(PackagePath), TEXT(""), PackageName, AssetName);

			OutputTexture.GeneratedTexture = NewObject<UTexture2D>(CreatePackage(*PackagePath), FName(*AssetName), RF_Standalone | RF_Public);
		}

		OutputTexture.GeneratedTexture->Source.Init(OutputTexture.TextureSize.X, OutputTexture.TextureSize.Y, 1, 1, TSF_RGBA16F, (const uint8*)(OutputFlipbooks[iOutputTexture].GetData()));
		OutputTexture.GeneratedTexture->AddressX = TextureAddress::TA_Clamp;
		OutputTexture.GeneratedTexture->AddressY = TextureAddress::TA_Clamp;
		OutputTexture.GeneratedTexture->UpdateResource();
		OutputTexture.GeneratedTexture->PostEditChange();
		OutputTexture.GeneratedTexture->MarkPackageDirty();
	}

	// Duplicate and set as generated data
	if (UNiagaraSystem* Asset = PreviewComponent->GetAsset())
	{
		Asset->SetFlipbookGeneratedSettings(DuplicateObject<UNiagaraFlipbookSettings>(FlipbookSettings, Asset));
	}

	// Clean up render targets
	for ( UTextureRenderTarget2D* RenderTarget : RenderTargets )
	{
		RenderTarget->RemoveFromRoot();
		RenderTarget = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
