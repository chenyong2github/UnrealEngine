// Copyright Epic Games, Inc. All Rights Reserved.

//-TODO: Remove post from capture
//-TODO: Hook up loading a preview environment
//-TODO: Add custom post processes for capturing data?
//-TODO: Allow us to toggle preview on / off for baker / realtime

#include "ViewModels/NiagaraBakerViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "Widgets/SNiagaraBakerWidget.h"
#include "NiagaraBakerRenderer.h"
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

#define LOCTEXT_NAMESPACE "NiagaraBakerViewModel"

FNiagaraBakerViewModel::FNiagaraBakerViewModel()
{
}

FNiagaraBakerViewModel::~FNiagaraBakerViewModel()
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

void FNiagaraBakerViewModel::Initialize(TWeakPtr<FNiagaraSystemViewModel> InWeakSystemViewModel)
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

	Widget = SNew(SNiagaraBakerWidget)
		.WeakViewModel(this->AsShared());
}

void FNiagaraBakerViewModel::RefreshView()
{
	//-TOOD:
}

void FNiagaraBakerViewModel::SetDisplayTimeFromNormalized(float NormalizeTime)
{
	if ( Widget )
	{
		if (const UNiagaraBakerSettings* GeneratedSettings = GetBakerGeneratedSettings())
		{
			const float RelativeTime = FMath::Clamp(NormalizeTime, 0.0f, 1.0f) * GeneratedSettings->DurationSeconds;
			Widget->SetPreviewRelativeTime(RelativeTime);
		}
		else if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
		{
			const float RelativeTime = FMath::Clamp(NormalizeTime, 0.0f, 1.0f) * BakerSettings->DurationSeconds;
			Widget->SetPreviewRelativeTime(RelativeTime);
		}
	}
}

UNiagaraComponent* FNiagaraBakerViewModel::GetPreviewComponent() const
{
	return PreviewComponent;
}

UNiagaraBakerSettings* FNiagaraBakerViewModel::GetBakerSettings() const
{
	if (PreviewComponent)
	{
		UNiagaraSystem* Asset = PreviewComponent->GetAsset();
		return Asset ? Asset->GetBakerSettings() : nullptr;
	}

	return nullptr;
}

const UNiagaraBakerSettings* FNiagaraBakerViewModel::GetBakerGeneratedSettings() const
{
	if (PreviewComponent)
	{
		UNiagaraSystem* Asset = PreviewComponent->GetAsset();
		return Asset ? Asset->GetBakerGeneratedSettings() : nullptr;
	}

	return nullptr;
}

const FNiagaraBakerTextureSettings* FNiagaraBakerViewModel::GetPreviewTextureSettings() const
{
	if ( UNiagaraBakerSettings* Settings = GetBakerSettings() )
	{
		if ( Settings->OutputTextures.IsValidIndex(PreviewTextureIndex) )
		{
			return &Settings->OutputTextures[PreviewTextureIndex];
		}
	}
	return nullptr;
}

TSharedPtr<class SWidget> FNiagaraBakerViewModel::GetWidget()
{
	return Widget;
}

void FNiagaraBakerViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
}

void FNiagaraBakerViewModel::RenderBaker()
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	if ( (PreviewComponent == nullptr) || (BakerSettings == nullptr) )
	{
		return;
	}

	if ( BakerSettings->OutputTextures.Num() == 0 )
	{
		return;
	}

	UWorld* World = PreviewComponent->GetWorld();
	FSceneInterface* BakerScene = World->Scene;

	NiagaraEmitterInstanceBatcher* NiagaraBatcher = nullptr;
	if (World->Scene && World->Scene->GetFXSystem())
	{
		NiagaraBatcher = static_cast<NiagaraEmitterInstanceBatcher*>(World->Scene->GetFXSystem()->GetInterface(NiagaraEmitterInstanceBatcher::Name));
	}
	check(NiagaraBatcher);

	// Create output render targets & output Baker data
	TArray<UTextureRenderTarget2D*, TInlineAllocator<4>> RenderTargets;
	TArray<TArray<FFloat16Color>> OutputBakers;

	for ( const auto& OutputTexture : BakerSettings->OutputTextures )
	{
		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
		RenderTarget->AddToRoot();
		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->TargetGamma = 1.0f;
		RenderTarget->InitCustomFormat(OutputTexture.FrameSize.X, OutputTexture.FrameSize.Y, PF_FloatRGBA, false);

		RenderTargets.Add(RenderTarget);
		OutputBakers.AddDefaulted_GetRef().SetNumZeroed(OutputTexture.TextureSize.X * OutputTexture.TextureSize.Y);
	}

	// Render frames
	PreviewComponent->ReinitializeSystem();

	const int32 TotalFrames = BakerSettings->FramesPerDimension.X * BakerSettings->FramesPerDimension.Y;
	const float FrameDeltaSeconds = BakerSettings->DurationSeconds / float(TotalFrames);

	FScopedSlowTask SlowTask(TotalFrames, LOCTEXT("RenderingBaker", "Rendering Baker..."));
	SlowTask.MakeDialog();

	for ( int32 iFrame=0; iFrame < TotalFrames; ++iFrame)
	{
		SlowTask.EnterProgressFrame(1);

		// Step component to time
		const float FrameTime = BakerSettings->StartSeconds + (float(iFrame) * FrameDeltaSeconds);
		PreviewComponent->SetSeekDelta(BakerSettings->GetSeekDelta());
		PreviewComponent->SeekToDesiredAge(FrameTime);
		PreviewComponent->TickComponent(BakerSettings->GetSeekDelta(), ELevelTick::LEVELTICK_All, nullptr);

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

		const float WorldTime = FApp::GetCurrentTime() - BakerSettings->StartSeconds - BakerSettings->DurationSeconds + FrameTime;

		// Render frame
		FNiagaraBakerRenderer BakerRenderer(PreviewComponent, BakerSettings, WorldTime);
		for (int32 iOutputTexture=0; iOutputTexture < BakerSettings->OutputTextures.Num(); ++iOutputTexture)
		{
			const FNiagaraBakerTextureSettings& OutputTexture = BakerSettings->OutputTextures[iOutputTexture];
			UTextureRenderTarget2D* RenderTarget = RenderTargets[iOutputTexture];
			FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

			BakerRenderer.RenderView(RenderTarget, iOutputTexture);

			TArray<FFloat16Color> OutSamples;
			RenderTargetResource->ReadFloat16Pixels(OutSamples);

			const FIntPoint ViewportOffset = FIntPoint(
				OutputTexture.FrameSize.X * (iFrame % BakerSettings->FramesPerDimension.X),
				OutputTexture.FrameSize.Y * (iFrame / BakerSettings->FramesPerDimension.X)
			);
			TArray<FFloat16Color>& OutputBaker = OutputBakers[iOutputTexture];
			for ( int y=0; y < OutputTexture.FrameSize.Y; ++y )
			{
				FFloat16Color* SrcPixel = OutSamples.GetData() + (y * OutputTexture.FrameSize.X);
				FFloat16Color* DstPixel = OutputBaker.GetData() + ViewportOffset.X + ((y + ViewportOffset.Y) * OutputTexture.TextureSize.X);
				FMemory::Memcpy(DstPixel, SrcPixel, sizeof(FFloat16Color) * OutputTexture.FrameSize.X);
			}
		}
	}

	// Send data to generated textures
	for (int32 iOutputTexture = 0; iOutputTexture < BakerSettings->OutputTextures.Num(); ++iOutputTexture)
	{
		FNiagaraBakerTextureSettings& OutputTexture = BakerSettings->OutputTextures[iOutputTexture];

		// If we don't have a texture create one
		if (OutputTexture.GeneratedTexture == nullptr)
		{
			FString PackagePath = FPaths::GetPath(PreviewComponent->GetAsset()->GetPackage()->GetPathName());
			if (OutputTexture.OutputName.IsNone())
			{
				PackagePath = PackagePath / PreviewComponent->GetAsset()->GetName() + TEXT("_Baker") + FString::FromInt(iOutputTexture);
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

		const bool bIsPoT = FMath::IsPowerOfTwo(OutputTexture.TextureSize.X) && FMath::IsPowerOfTwo(OutputTexture.TextureSize.Y);

		OutputTexture.GeneratedTexture->Source.Init(OutputTexture.TextureSize.X, OutputTexture.TextureSize.Y, 1, 1, TSF_RGBA16F, (const uint8*)(OutputBakers[iOutputTexture].GetData()));
		OutputTexture.GeneratedTexture->PowerOfTwoMode = ETexturePowerOfTwoSetting::None;
		OutputTexture.GeneratedTexture->MipGenSettings = bIsPoT ? TMGS_FromTextureGroup : TMGS_NoMipmaps;
		OutputTexture.GeneratedTexture->AddressX = TextureAddress::TA_Clamp;
		OutputTexture.GeneratedTexture->AddressY = TextureAddress::TA_Clamp;
		OutputTexture.GeneratedTexture->UpdateResource();
		OutputTexture.GeneratedTexture->PostEditChange();
		OutputTexture.GeneratedTexture->MarkPackageDirty();
	}

	// Duplicate and set as generated data
	if (UNiagaraSystem* Asset = PreviewComponent->GetAsset())
	{
		Asset->SetBakerGeneratedSettings(DuplicateObject<UNiagaraBakerSettings>(BakerSettings, Asset));
	}

	// Clean up render targets
	for ( UTextureRenderTarget2D* RenderTarget : RenderTargets )
	{
		RenderTarget->RemoveFromRoot();
		RenderTarget = nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
