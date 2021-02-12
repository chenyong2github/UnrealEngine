// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraFlipbookViewport.h"
#include "ViewModels/NiagaraFlipbookViewModel.h"
#include "SNiagaraFlipbookViewportToolbar.h"
#include "NiagaraFlipbookRenderer.h"
#include "NiagaraComponent.h"

#include "Engine/Font.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "CanvasTypes.h"
#include "EngineModule.h"
#include "LegacyScreenPercentageDriver.h"
#include "ImageUtils.h"
#include "SEditorViewportToolBarMenu.h"

#define LOCTEXT_NAMESPACE "NiagaraFlipbookViewport"

//////////////////////////////////////////////////////////////////////////

class FNiagaraFlipbookViewportClient : public FEditorViewportClient
{
public:
	FNiagaraFlipbookViewportClient(const TSharedRef<SNiagaraFlipbookViewport>& InOwnerViewport)
		: FEditorViewportClient(nullptr, nullptr, StaticCastSharedRef<SEditorViewport>(InOwnerViewport))
	{
		SetViewportType(ELevelViewportType::LVT_OrthoXZ);
	}

	virtual bool InputKey(FViewport* InViewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad) override
	{
		return false;
	}

	virtual void CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y) override
	{
	}

	FIntRect ConstrainRect(const FIntRect& InRect, FIntPoint InSize)
	{
		const float Scale = FMath::Min(float(InRect.Width()) /  float(InSize.X), float(InRect.Height()) / float(InSize.Y));
		const FIntPoint ScaledSize(FMath::FloorToInt(float(InSize.X) * Scale), FMath::FloorToInt(float(InSize.Y) * Scale));
		FIntPoint RectMin(
			InRect.Min.X + ((InRect.Width()  - ScaledSize.X) >> 1),
			InRect.Min.Y + ((InRect.Height() - ScaledSize.Y) >> 1)
		);

		return FIntRect(RectMin, RectMin + ScaledSize);
	}

	void ClearViewArea(FCanvas* Canvas, const FIntRect& InRect)
	{
		UTexture2D* Texture = nullptr;
		FLinearColor Color = ClearColor;
		FVector2D EndUV(1.0f, 1.0f);

		if (bShowCheckerBoard)
		{
			Texture = GetCheckerboardTexture();
			Color = FLinearColor::White;
			EndUV.X = float(InRect.Width()) / float(FMath::Max(Texture->GetSizeX(), 1));
			EndUV.Y = float(InRect.Height()) / float(FMath::Max(Texture->GetSizeY(), 1));
		}
		Canvas->DrawTile(InRect.Min.X, InRect.Min.Y, InRect.Width(), InRect.Height(), 0.0f, 0.0f, EndUV.X, EndUV.Y, Color, Texture ? Texture->Resource : nullptr, false);
	}

	virtual void Tick(float DeltaSeconds) override
	{
	}

	/** FViewportClient interface */
	virtual void Draw(FViewport* InViewport, FCanvas* Canvas) override
	{
		Canvas->Clear(FLinearColor::Transparent);

		auto ViewModel = WeakViewModel.Pin();
		if (ViewModel == nullptr)
		{
			return;
		}

		//if (ViewRect.Width() <= 1 || ViewRect.Height() < = 1)
		//{
		//	return;
		//}

		UFont* DisplayFont = GetFont();
		const float FontHeight = DisplayFont->GetMaxCharHeight() + 1.0f;
		const FVector2D TextStartOffset(5.0f, 30.0f);

		const UNiagaraFlipbookSettings* FlipbookSettings = ViewModel->GetFlipbookSettings();
		const FNiagaraFlipbookTextureSettings* PreviewTextureSettings = ViewModel->GetPreviewTexture();
		const int32 PreviewTextureIndex = ViewModel->GetPreviewTextureIndex();

		// Determine view rects
		FIntRect PreviewViewRect;
		FIntRect FlipbookViewRect;
		{
			const FIntRect ViewRect = Canvas->GetViewRect();
			const int32 Border = 3;
			const int32 ViewWidth = bShowPreview && bShowFlipbook ? (ViewRect.Width() >> 1) - Border : ViewRect.Width();

			if (bShowPreview)
			{
				if (FlipbookSettings->OutputTextures.IsValidIndex(PreviewTextureIndex))
				{
					PreviewViewRect = ConstrainRect(FIntRect(ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Min.X + ViewWidth, ViewRect.Max.Y), FlipbookSettings->OutputTextures[PreviewTextureIndex].FrameSize);
				}
				else
				{
					PreviewViewRect = FIntRect(ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Min.X + ViewWidth, ViewRect.Max.Y);
				}
			}

			if ( bShowFlipbook )
			{
				if (FlipbookSettings->OutputTextures.IsValidIndex(PreviewTextureIndex))
				{
					FlipbookViewRect = ConstrainRect(FIntRect(ViewRect.Max.X - ViewWidth, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y), FlipbookSettings->OutputTextures[PreviewTextureIndex].FrameSize);
				}
				else
				{
					FlipbookViewRect = FIntRect(ViewRect.Max.X - ViewWidth, ViewRect.Min.Y, ViewRect.Min.X, ViewRect.Max.Y);
				}
			}
		}

		// Render World Preview
		if ( bShowPreview )
		{
			ClearViewArea(Canvas, PreviewViewRect);

			const float WorldTime = CurrentTime;
			FVector2D TextPosition(PreviewViewRect.Min.X + TextStartOffset.X, PreviewViewRect.Min.Y + TextStartOffset.Y);
			if (FlipbookSettings->OutputTextures.IsValidIndex(PreviewTextureIndex))
			{
				// Ensure render target is the correct size
				const FNiagaraFlipbookTextureSettings& OutputTexture = FlipbookSettings->OutputTextures[PreviewTextureIndex];
				if ( !RealtimeRenderTarget || RealtimeRenderTarget->SizeX != OutputTexture.FrameSize.X || RealtimeRenderTarget->SizeY != OutputTexture.FrameSize.Y )
				{
					if (RealtimeRenderTarget == nullptr)
					{
						RealtimeRenderTarget = NewObject<UTextureRenderTarget2D>();
					}
					RealtimeRenderTarget->ClearColor = FLinearColor::Transparent;
					RealtimeRenderTarget->TargetGamma = 1.0f;
					RealtimeRenderTarget->InitCustomFormat(OutputTexture.FrameSize.X, OutputTexture.FrameSize.Y, PF_FloatRGBA, false);
				}

				// Seek to correct time and render
				UNiagaraComponent* PreviewComponent = ViewModel->GetPreviewComponent();
				PreviewComponent->SeekToDesiredAge(WorldTime);
				PreviewComponent->TickComponent(1.0f / 60.0f, ELevelTick::LEVELTICK_All, nullptr);

				FNiagaraFlipbookRenderer FlipbookRenderer(PreviewComponent, WorldTime);
				FlipbookRenderer.RenderView(RealtimeRenderTarget, PreviewTextureIndex);

				Canvas->DrawTile(
					PreviewViewRect.Min.X, PreviewViewRect.Min.Y, PreviewViewRect.Width(), PreviewViewRect.Height(),
					0.0f, 0.0f, 1.0f, 1.0f,
					FLinearColor::White,
					RealtimeRenderTarget->Resource,
					false	//-TODO: Preview with alpha?
				);

				// Display info text
				if (bShowInfoText)
				{
					Canvas->DrawShadowedString(TextPosition.X, TextPosition.Y, TEXT("Live Preview"), DisplayFont, FLinearColor::White);
					TextPosition.Y += FontHeight;
				}
			}
			else
			{
				Canvas->DrawShadowedString(TextPosition.X, TextPosition.Y, TEXT("Live Preview"), DisplayFont, FLinearColor::White);
				TextPosition.Y += FontHeight;

				Canvas->DrawShadowedString(TextPosition.X + 5.0f, TextPosition.Y, TEXT("No Output Texture"), DisplayFont, FLinearColor::White);
			}
		}

		// Render Flipbook
		if ( bShowFlipbook)
		{
			ClearViewArea(Canvas, FlipbookViewRect);

			const bool bFlipbookValid = PreviewTextureSettings != nullptr;
			const auto DisplayData = ViewModel->GetDisplayDataFromAbsoluteTime(CurrentTime);
			FVector2D TextPosition(FlipbookViewRect.Min.X + TextStartOffset.X, FlipbookViewRect.Min.Y + TextStartOffset.Y);

			if (bFlipbookValid)
			{
				const FIntPoint TextureSize = FIntPoint(FMath::Max(PreviewTextureSettings->GeneratedTexture->GetSizeX(), 1), FMath::Max(PreviewTextureSettings->GeneratedTexture->GetSizeY(), 1));
				const FIntPoint FramesPerDimension = ViewModel->GetGeneratedFramesPerDimension();
				const FIntPoint FrameIndex2D = FIntPoint(DisplayData.FrameIndex % FramesPerDimension.X, DisplayData.FrameIndex / FramesPerDimension.X);
				const FIntPoint FramePixel = FIntPoint(FrameIndex2D.X * PreviewTextureSettings->FrameSize.X, FrameIndex2D.Y * PreviewTextureSettings->FrameSize.Y);

				Canvas->DrawTile(
					FlipbookViewRect.Min.X, FlipbookViewRect.Min.Y, FlipbookViewRect.Width(), FlipbookViewRect.Height(),
					float(FramePixel.X) / float(TextureSize.X), float(FramePixel.Y) / float(TextureSize.Y), float(FramePixel.X + PreviewTextureSettings->FrameSize.X) / float(TextureSize.X), float(FramePixel.Y + PreviewTextureSettings->FrameSize.Y) / float(TextureSize.Y),
					FLinearColor::White,
					PreviewTextureSettings->GeneratedTexture->Resource,
					false	//-TODO: Preview with alpha?
				);

				// Display info text
				if (bShowInfoText)
				{
					Canvas->DrawShadowedString(TextPosition.X, TextPosition.Y, TEXT("Flipbook"), DisplayFont, FLinearColor::White);
					TextPosition.Y += FontHeight;

					Canvas->DrawShadowedString(TextPosition.X + 5.0f, TextPosition.Y, *FString::Printf(TEXT("Texture(%d) FrameSize(%d x %d) Frame(%d/%d)"), PreviewTextureIndex, PreviewTextureSettings->FrameSize.X, PreviewTextureSettings->FrameSize.Y, DisplayData.FrameIndex, DisplayData.NumFrames), DisplayFont, FLinearColor::White);
					TextPosition.Y += FontHeight;
				}
			}
			else
			{
				Canvas->DrawShadowedString(TextPosition.X, TextPosition.Y, TEXT("Flipbook"), DisplayFont, FLinearColor::White);
				TextPosition.Y += FontHeight;

				Canvas->DrawShadowedString(TextPosition.X + 5.0f, TextPosition.Y, TEXT("Texture Not Generated"), DisplayFont, FLinearColor::White);
			}
		}
	}

	
	UTexture2D* GetCheckerboardTexture()
	{
		if (CheckerboardTexture == nullptr)
		{
			CheckerboardTexture = FImageUtils::CreateCheckerboardTexture(CheckerboardColorOne, CheckerboardColorTwo, CheckerSize);
		}
		return CheckerboardTexture;
	}

	void DestroyCheckerboardTexture()
	{
		if (CheckerboardTexture)
		{
			if (CheckerboardTexture->Resource)
			{
				CheckerboardTexture->ReleaseResource();
			}
			CheckerboardTexture->MarkPendingKill();
			CheckerboardTexture = nullptr;
		}
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		FEditorViewportClient::AddReferencedObjects(Collector);

		Collector.AddReferencedObject(CheckerboardTexture);
		Collector.AddReferencedObject(RealtimeRenderTarget);
	}

	virtual UWorld* GetWorld() const override { return nullptr; }

	UFont* GetFont() const { return GetStatsFont(); }

public:
	UTextureRenderTarget2D*						RealtimeRenderTarget = nullptr;

	bool										bShowCheckerBoard = true;
	UTexture2D*									CheckerboardTexture = nullptr;
	FColor										CheckerboardColorOne = FColor(128, 128, 128);
	FColor										CheckerboardColorTwo = FColor(64, 64, 64);
	int32										CheckerSize = 32;

	FLinearColor								ClearColor = FColor::Black;

	bool										bShowInfoText = true;
	bool										bShowPreview = true;
	bool										bShowFlipbook = true;

	TWeakPtr<FNiagaraFlipbookViewModel>			WeakViewModel;
	float										CurrentTime = 0.0f;
	float										DeltaTime = 0.0f;
};

//////////////////////////////////////////////////////////////////////////

void SNiagaraFlipbookViewport::Construct(const FArguments& InArgs)
{
	WeakViewModel = InArgs._WeakViewModel;

	SEditorViewport::FArguments ParentArgs;
	//ParentArgs.EnableGammaCorrection(false);
	//ParentArgs.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute());
	//ParentArgs.ShowEffectWhenDisabled(false);
	//ParentArgs.EnableBlending(true);
	SEditorViewport::Construct(ParentArgs);
}

TSharedRef<FEditorViewportClient> SNiagaraFlipbookViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShareable(new FNiagaraFlipbookViewportClient(SharedThis(this)));
	ViewportClient->WeakViewModel = WeakViewModel;

	return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SNiagaraFlipbookViewport::MakeViewportToolbar()
{
	return
		SNew(SNiagaraFlipbookViewportToolbar)
		.WeakViewModel(WeakViewModel)
		.WeakViewport(SharedThis(this));
	;
}

void SNiagaraFlipbookViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	ViewportClient->bNeedsRedraw = true;

	if (FSlateThrottleManager::Get().IsAllowingExpensiveTasks())
	{
		ViewportClient->Tick(InDeltaTime);
		GEditor->UpdateSingleViewportClient(ViewportClient.Get(), /*bInAllowNonRealtimeViewportToDraw=*/ true, /*bLinkedOrthoMovement=*/ false);
	}
}

TSharedRef<SEditorViewport> SNiagaraFlipbookViewport::GetViewportWidget()
{
	return SharedThis(this);
}

TSharedPtr<FExtender> SNiagaraFlipbookViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}

void SNiagaraFlipbookViewport::OnFloatingButtonClicked()
{
}

bool SNiagaraFlipbookViewport::IsInfoTextEnabled() const
{
	return ViewportClient->bShowInfoText;
}

void SNiagaraFlipbookViewport::SetInfoTextEnabled(bool bEnabled)
{
	ViewportClient->bShowInfoText = bEnabled;
}

bool SNiagaraFlipbookViewport::IsPreviewViewEnabled() const
{
	return ViewportClient->bShowPreview;
}

void SNiagaraFlipbookViewport::SetPreviewViewEnabled(bool bEnabled)
{
	ViewportClient->bShowPreview = bEnabled;
}

bool SNiagaraFlipbookViewport::IsFlipbookViewEnabled() const
{
	return ViewportClient->bShowFlipbook;
}

void SNiagaraFlipbookViewport::SetFlipbookViewEnabled(bool bEnabled)
{
	ViewportClient->bShowFlipbook = bEnabled;
}

void SNiagaraFlipbookViewport::RefreshView(const float CurrentTime, const float DeltaTime)
{
	ViewportClient->CurrentTime = CurrentTime;
	ViewportClient->DeltaTime = DeltaTime;
}

ENiagaraFlipbookViewMode SNiagaraFlipbookViewport::GetCameraMode() const
{
	if ( auto ViewModel = WeakViewModel.Pin() )
	{
		const UNiagaraFlipbookSettings* FlipbookSettings = ViewModel->GetFlipbookSettings();
		return FlipbookSettings->CameraViewportMode;
	}
	// Should never happen
	checkf(false, TEXT("No ViewModel present"));
	return ENiagaraFlipbookViewMode::Perspective;
}

void SNiagaraFlipbookViewport::SetCameraMode(ENiagaraFlipbookViewMode Mode)
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		UNiagaraFlipbookSettings* FlipbookSettings = ViewModel->GetFlipbookSettings();
		FlipbookSettings->CameraViewportMode = Mode;
	}
}

bool SNiagaraFlipbookViewport::IsCameraMode(ENiagaraFlipbookViewMode Mode) const
{
	if (auto ViewModel = WeakViewModel.Pin())
	{
		const UNiagaraFlipbookSettings* FlipbookSettings = ViewModel->GetFlipbookSettings();
		return FlipbookSettings->CameraViewportMode == Mode;
	}
	return false;
}

FText SNiagaraFlipbookViewport::GetCameraModeText(ENiagaraFlipbookViewMode Mode) const
{
	switch (Mode)
	{
		case ENiagaraFlipbookViewMode::Perspective:		return LOCTEXT("Perspective", "Perspective");
		case ENiagaraFlipbookViewMode::OrthoFront:		return LOCTEXT("OrthoFront", "Front");
		case ENiagaraFlipbookViewMode::OrthoBack:		return LOCTEXT("OrthoBack", "Back");
		case ENiagaraFlipbookViewMode::OrthoLeft:		return LOCTEXT("OrthoLeft", "Left");
		case ENiagaraFlipbookViewMode::OrthoRight:		return LOCTEXT("OrthoRight", "Right");
		case ENiagaraFlipbookViewMode::OrthoTop:		return LOCTEXT("OrthoTop", "Top");
		case ENiagaraFlipbookViewMode::OrthoBottom:		return LOCTEXT("OrthoBottom", "Bottom");
		default:										return LOCTEXT("Error", "Error");
	}
}

FName SNiagaraFlipbookViewport::GetCameraModeIconName(ENiagaraFlipbookViewMode Mode) const
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
		case ENiagaraFlipbookViewMode::Perspective:		return PerspectiveIcon;
		case ENiagaraFlipbookViewMode::OrthoFront:		return FrontIcon;
		case ENiagaraFlipbookViewMode::OrthoBack:		return BackIcon;
		case ENiagaraFlipbookViewMode::OrthoLeft:		return LeftIcon;
		case ENiagaraFlipbookViewMode::OrthoRight:		return RightIcon;
		case ENiagaraFlipbookViewMode::OrthoTop:		return TopIcon;
		case ENiagaraFlipbookViewMode::OrthoBottom:		return BottomIcon;
		default:										return PerspectiveIcon;
	}
}

const struct FSlateBrush* SNiagaraFlipbookViewport::GetActiveCameraModeIcon() const
{
	return FEditorStyle::GetBrush(GetCameraModeIconName(GetCameraMode()));
}

#undef LOCTEXT_NAMESPACE
