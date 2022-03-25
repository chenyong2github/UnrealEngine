// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraBakerViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "Widgets/SNiagaraBakerWidget.h"
#include "NiagaraBakerRenderer.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

#include "Niagara/Classes/NiagaraDataInterfaceRenderTarget2D.h"

#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "AdvancedPreviewScene.h"
#include "AssetToolsModule.h"
#include "EngineModule.h"
#include "LegacyScreenPercentageDriver.h"
#include "PackageTools.h"
#include "ScopedTransaction.h"

#include "Factories/Texture2dFactoryNew.h"

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
		PreviewComponent->SetCanRenderWhileSeeking(true);
		PreviewComponent->SetMaxSimTime(0.0f);
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

TSharedPtr<class SWidget> FNiagaraBakerViewModel::GetWidget()
{
	return Widget;
}

void FNiagaraBakerViewModel::TogglePlaybackLooping()
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		const FScopedTransaction Transaction(LOCTEXT("ToggleLooping", "Toggle Looping"));
		BakerSettings->bPreviewLooping = !BakerSettings->bPreviewLooping;
		BakerSettings->Modify();
	}
}

bool FNiagaraBakerViewModel::IsPlaybackLooping() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->bPreviewLooping : false;
}

void FNiagaraBakerViewModel::SetCameraViewMode(ENiagaraBakerViewMode ViewMode)
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetCameraViewMode", "Set Camera View Mode"));
		BakerSettings->CameraViewportMode = ViewMode;
		BakerSettings->Modify();
	}
}

bool FNiagaraBakerViewModel::IsCameraViewMode(ENiagaraBakerViewMode ViewMode)
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->CameraViewportMode == ViewMode : false;
}

FText FNiagaraBakerViewModel::GetCurrentCameraModeText() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? GetCameraModeText(BakerSettings->CameraViewportMode) : FText::GetEmpty();
}

FName FNiagaraBakerViewModel::GetCurrentCameraModeIconName() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return GetCameraModeIconName(BakerSettings ? BakerSettings->CameraViewportMode : ENiagaraBakerViewMode::Perspective);
}

FSlateIcon FNiagaraBakerViewModel::GetCurrentCameraModeIcon() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return GetCameraModeIcon(BakerSettings ? BakerSettings->CameraViewportMode : ENiagaraBakerViewMode::Perspective);
}

FText FNiagaraBakerViewModel::GetCameraModeText(ENiagaraBakerViewMode Mode)
{
	switch (Mode)
	{
		case ENiagaraBakerViewMode::Perspective:	return LOCTEXT("Perspective", "Perspective");
		case ENiagaraBakerViewMode::OrthoFront:		return LOCTEXT("OrthoFront", "Front");
		case ENiagaraBakerViewMode::OrthoBack:		return LOCTEXT("OrthoBack", "Back");
		case ENiagaraBakerViewMode::OrthoLeft:		return LOCTEXT("OrthoLeft", "Left");
		case ENiagaraBakerViewMode::OrthoRight:		return LOCTEXT("OrthoRight", "Right");
		case ENiagaraBakerViewMode::OrthoTop:		return LOCTEXT("OrthoTop", "Top");
		case ENiagaraBakerViewMode::OrthoBottom:	return LOCTEXT("OrthoBottom", "Bottom");
		default:									return LOCTEXT("Error", "Error");
	}
}

FName FNiagaraBakerViewModel::GetCameraModeIconName(ENiagaraBakerViewMode Mode)
{
	static FName PerspectiveIcon("EditorViewport.Perspective");
	static FName TopIcon("EditorViewport.Top");
	static FName LeftIcon("EditorViewport.Left");
	static FName FrontIcon("EditorViewport.Front");
	static FName BottomIcon("EditorViewport.Bottom");
	static FName RightIcon("EditorViewport.Right");
	static FName BackIcon("EditorViewport.Back");

	switch (Mode)
	{
		case ENiagaraBakerViewMode::Perspective:	return PerspectiveIcon;
		case ENiagaraBakerViewMode::OrthoFront:		return FrontIcon;
		case ENiagaraBakerViewMode::OrthoBack:		return BackIcon;
		case ENiagaraBakerViewMode::OrthoLeft:		return LeftIcon;
		case ENiagaraBakerViewMode::OrthoRight:		return RightIcon;
		case ENiagaraBakerViewMode::OrthoTop:		return TopIcon;
		case ENiagaraBakerViewMode::OrthoBottom:	return BottomIcon;
		default:									return PerspectiveIcon;
	}
}

FSlateIcon FNiagaraBakerViewModel::GetCameraModeIcon(ENiagaraBakerViewMode Mode)
{
	return FSlateIcon(FEditorStyle::GetStyleSetName(), GetCameraModeIconName(Mode));
}

FVector FNiagaraBakerViewModel::GetCurrentCameraLocation() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->CameraViewportLocation[int(BakerSettings->CameraViewportMode)] : FVector::ZeroVector;
}

void FNiagaraBakerViewModel::SetCurrentCameraLocation(const FVector Value)
{
	if ( UNiagaraBakerSettings* BakerSettings = GetBakerSettings() )
	{
		BakerSettings->CameraViewportLocation[int(BakerSettings->CameraViewportMode)] = Value;
	}
}

FRotator FNiagaraBakerViewModel::GetCurrentCameraRotation() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->CameraViewportRotation[int(BakerSettings->CameraViewportMode)] : FRotator::ZeroRotator;
}

void FNiagaraBakerViewModel::SetCurrentCameraRotation(const FRotator Value) const
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		BakerSettings->CameraViewportRotation[int(BakerSettings->CameraViewportMode)] = Value;
	}
}

float FNiagaraBakerViewModel::GetCameraFOV() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->CameraFOV : 0.0f;
}

void FNiagaraBakerViewModel::SetCameraFOV(float InFOV)
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetCameraFOV", "Set Camera FOV"));
		BakerSettings->CameraFOV = InFOV;
		BakerSettings->Modify();
	}
}

float FNiagaraBakerViewModel::GetCameraOrbitDistance() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->CameraOrbitDistance : 0.0f;
}

void FNiagaraBakerViewModel::SetCameraOrbitDistance(float InOrbitDistance)
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetCameraOrbitDistance", "Set Camera Orbit Distance"));
		BakerSettings->CameraOrbitDistance = InOrbitDistance;
		BakerSettings->Modify();
	}
}

float FNiagaraBakerViewModel::GetCameraOrthoWidth() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->CameraOrthoWidth : 0.0f;
}

void FNiagaraBakerViewModel::SetCameraOrthoWidth(float InOrthoWidth)
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetCameraOrthoWidth", "Set Camera Ortho Width"));
		BakerSettings->CameraOrthoWidth = InOrthoWidth;
		BakerSettings->Modify();
	}
}

void FNiagaraBakerViewModel::ToggleCameraAspectRatioEnabled()
{
	if ( UNiagaraBakerSettings* BakerSettings = GetBakerSettings() )
	{
		const FScopedTransaction Transaction(LOCTEXT("ToggleUseCameraAspectRatio", "Toggle Use Camera Aspect Ratio"));
		BakerSettings->bUseCameraAspectRatio = !BakerSettings->bUseCameraAspectRatio;
		BakerSettings->Modify();
	}
}

bool FNiagaraBakerViewModel::IsCameraAspectRatioEnabled() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->bUseCameraAspectRatio : false;
}

float FNiagaraBakerViewModel::GetCameraAspectRatio() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings ? BakerSettings->CameraAspectRatio : 0.0f;
}

void FNiagaraBakerViewModel::SetCameraAspectRatio(float InAspectRatio)
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		const FScopedTransaction Transaction(LOCTEXT("SetCameraAspectRatio", "Set Camera Aspect Ratio"));
		BakerSettings->CameraAspectRatio = InAspectRatio;
		BakerSettings->Modify();
	}
}

void FNiagaraBakerViewModel::AddOutput()
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		const FScopedTransaction Transaction(LOCTEXT("AddOutput", "Add Output"));
		BakerSettings->OutputTextures.AddDefaulted();
		BakerSettings->Modify();

		CurrentOutputIndex = BakerSettings->OutputTextures.Num() - 1;
		OnCurrentOutputChanged.Broadcast();
	}
}

void FNiagaraBakerViewModel::RemoveCurrentOutput()
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		if (BakerSettings->OutputTextures.IsValidIndex(CurrentOutputIndex))
		{
			const FScopedTransaction Transaction(LOCTEXT("RemoveOutput", "Remove Output"));
			BakerSettings->OutputTextures.RemoveAt(CurrentOutputIndex);
			BakerSettings->Modify();
			CurrentOutputIndex = FMath::Max(0, CurrentOutputIndex - 1);
			OnCurrentOutputChanged.Broadcast();
		}
	}
}

bool FNiagaraBakerViewModel::CanRemoveCurrentOutput() const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	return BakerSettings && BakerSettings->OutputTextures.IsValidIndex(CurrentOutputIndex);
}

void FNiagaraBakerViewModel::SetCurrentOutputIndex(int32 OutputIndex)
{
	if (UNiagaraBakerSettings* BakerSettings = GetBakerSettings())
	{
		if (BakerSettings->OutputTextures.Num() > 0)
		{
			CurrentOutputIndex = FMath::Clamp(OutputIndex, 0, BakerSettings->OutputTextures.Num() - 1);
		}
		else
		{
			CurrentOutputIndex = 0;
		}
		OnCurrentOutputChanged.Broadcast();
	}
}

FText FNiagaraBakerViewModel::GetOutputText(int32 OutputIndex) const
{
	UNiagaraBakerSettings* BakerSettings = GetBakerSettings();
	if ( BakerSettings && BakerSettings->OutputTextures.IsValidIndex(OutputIndex) )
	{
		if ( BakerSettings->OutputTextures[OutputIndex].OutputName.IsNone() )
		{
			return FText::Format(LOCTEXT("OutputFormat", "Output {0}"), FText::AsNumber(OutputIndex));
		}
		else
		{
			return FText::FromName(BakerSettings->OutputTextures[OutputIndex].OutputName);
		}
	}
	return FText::GetEmpty();
}

FText FNiagaraBakerViewModel::GetCurrentOutputText() const
{
	return GetOutputText(CurrentOutputIndex);
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

	// Create output render targets & output Baker data
	TArray<UTextureRenderTarget2D*, TInlineAllocator<4>> RenderTargets;
	TArray<TArray<FFloat16Color>> OutputBakers;

	for ( const auto& OutputTexture : BakerSettings->OutputTextures )
	{
		if (OutputTexture.IsValidForBake())
		{
			UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
			RenderTarget->AddToRoot();
			RenderTarget->ClearColor = FLinearColor::Transparent;
			RenderTarget->TargetGamma = 1.0f;
			RenderTarget->InitCustomFormat(OutputTexture.FrameSize.X, OutputTexture.FrameSize.Y, PF_FloatRGBA, false);

			RenderTargets.Add(RenderTarget);
		}
		else
		{
			RenderTargets.Add(nullptr);
		}
		OutputBakers.AddDefaulted_GetRef().SetNumZeroed(OutputTexture.TextureSize.X * OutputTexture.TextureSize.Y);
	}

	// Render frames
	PreviewComponent->ReinitializeSystem();

	const int32 TotalFrames = BakerSettings->FramesPerDimension.X * BakerSettings->FramesPerDimension.Y;
	const float FrameDeltaSeconds = BakerSettings->DurationSeconds / float(TotalFrames);

	FScopedSlowTask SlowTask(TotalFrames, LOCTEXT("RenderingBaker", "Rendering Baker..."));
	SlowTask.MakeDialog();

	FNiagaraBakerRenderer BakerRenderer;

	for ( int32 iFrame=0; iFrame < TotalFrames; ++iFrame)
	{
		SlowTask.EnterProgressFrame(1);

		// Step component to time
		const float FrameTime = BakerSettings->StartSeconds + (float(iFrame) * FrameDeltaSeconds);
		PreviewComponent->SetSeekDelta(BakerSettings->GetSeekDelta());
		PreviewComponent->SeekToDesiredAge(FrameTime);
		PreviewComponent->TickComponent(BakerSettings->GetSeekDelta(), ELevelTick::LEVELTICK_All, nullptr);


		// Render frame
		for (int32 iOutputTexture=0; iOutputTexture < BakerSettings->OutputTextures.Num(); ++iOutputTexture)
		{
			const FNiagaraBakerTextureSettings& OutputTexture = BakerSettings->OutputTextures[iOutputTexture];
			UTextureRenderTarget2D* RenderTarget = RenderTargets[iOutputTexture];
			if (RenderTarget == nullptr)
			{
				continue;
			}
			FTextureRenderTargetResource* RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();

			BakerRenderer.RenderView(PreviewComponent, BakerSettings, FrameTime, RenderTarget, iOutputTexture);

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

	// Remove existing generated settings to avoid holding references to textures
	if (UNiagaraSystem* Asset = PreviewComponent->GetAsset())
	{
		Asset->SetBakerGeneratedSettings(nullptr);
	}

	// Send data to generated textures
	for (int32 iOutputTexture = 0; iOutputTexture < BakerSettings->OutputTextures.Num(); ++iOutputTexture)
	{
		FNiagaraBakerTextureSettings& OutputTexture = BakerSettings->OutputTextures[iOutputTexture];

		// If we don't already have a generated texture ask the user to create one
		if (OutputTexture.GeneratedTexture == nullptr)
		{
			IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			UTexture2DFactoryNew* Texture2DFactory = NewObject<UTexture2DFactoryNew>();

			FString PackagePath = FPaths::GetPath(PreviewComponent->GetAsset()->GetPackage()->GetPathName());
			FString AssetName = PreviewComponent->GetAsset()->GetName() + TEXT("_Baker") + FString::FromInt(iOutputTexture) + TEXT("Texture");
			OutputTexture.GeneratedTexture = Cast<UTexture2D>(AssetTools.CreateAssetWithDialog(AssetName, PackagePath, UTexture2D::StaticClass(), Texture2DFactory));
			if (OutputTexture.GeneratedTexture == nullptr)
			{
				// User skipped for some reason
				continue;
			}
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
		OutputTexture.GeneratedTexture->TemporarilyDisableStreaming();
	}

	// Duplicate and set as generated data
	if (UNiagaraSystem* Asset = PreviewComponent->GetAsset())
	{
		Asset->SetBakerGeneratedSettings(DuplicateObject<UNiagaraBakerSettings>(BakerSettings, Asset));
	}

	// Clean up render targets
	for ( UTextureRenderTarget2D* RenderTarget : RenderTargets )
	{
		if (RenderTarget)
		{
			RenderTarget->RemoveFromRoot();
			RenderTarget = nullptr;
		}
	}
}

#undef LOCTEXT_NAMESPACE
