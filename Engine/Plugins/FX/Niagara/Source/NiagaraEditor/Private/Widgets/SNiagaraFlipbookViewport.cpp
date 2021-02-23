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
#include "EditorViewportCommands.h"
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
		// Only allow movement when in preview mode
		auto ViewModel = WeakViewModel.Pin();
		UNiagaraFlipbookSettings* FlipbookSettings = ViewModel ? ViewModel->GetFlipbookSettings() : nullptr;
		if ( bShowPreview && FlipbookSettings )
		{
			bool bForwardKeyState = false;
			bool bBackwardKeyState = false;
			bool bRightKeyState = false;
			bool bLeftKeyState = false;

			bool bUpKeyState = false;
			bool bDownKeyState = false;
			bool bZoomOutKeyState = false;
			bool bZoomInKeyState = false;

			bool bFocus = false;

			// Iterate through all key mappings to generate key state flags
			for (uint32 i = 0; i < static_cast<uint8>(EMultipleKeyBindingIndex::NumChords); ++i)
			{
				EMultipleKeyBindingIndex ChordIndex = static_cast<EMultipleKeyBindingIndex> (i);
				bForwardKeyState |= Viewport->KeyState(FViewportNavigationCommands::Get().Forward->GetActiveChord(ChordIndex)->Key);
				bBackwardKeyState |= Viewport->KeyState(FViewportNavigationCommands::Get().Backward->GetActiveChord(ChordIndex)->Key);
				bRightKeyState |= Viewport->KeyState(FViewportNavigationCommands::Get().Right->GetActiveChord(ChordIndex)->Key);
				bLeftKeyState |= Viewport->KeyState(FViewportNavigationCommands::Get().Left->GetActiveChord(ChordIndex)->Key);

				bUpKeyState |= Viewport->KeyState(FViewportNavigationCommands::Get().Up->GetActiveChord(ChordIndex)->Key);
				bDownKeyState |= Viewport->KeyState(FViewportNavigationCommands::Get().Down->GetActiveChord(ChordIndex)->Key);
				bZoomOutKeyState |= Viewport->KeyState(FViewportNavigationCommands::Get().FovZoomOut->GetActiveChord(ChordIndex)->Key);
				bZoomInKeyState |= Viewport->KeyState(FViewportNavigationCommands::Get().FovZoomIn->GetActiveChord(ChordIndex)->Key);

				bFocus |= Viewport->KeyState(FEditorViewportCommands::Get().FocusViewportToSelection->GetActiveChord(ChordIndex)->Key);
			}

			if ( FlipbookSettings->IsOrthographic() )
			{
				LocalMovement.X += bLeftKeyState ? KeyboardMoveSpeed : 0.0f;
				LocalMovement.X -= bRightKeyState ? KeyboardMoveSpeed : 0.0f;
				LocalMovement.Y += bBackwardKeyState ? KeyboardMoveSpeed : 0.0f;
				LocalMovement.Y -= bForwardKeyState ? KeyboardMoveSpeed : 0.0f;
			}
			else
			{
				//LocalMovement.X += bLeftKeyState ? KeyboardMoveSpeed : 0.0f;
				//LocalMovement.X -= bRightKeyState ? KeyboardMoveSpeed : 0.0f;
				//LocalMovement.Y += bUpKeyState ? KeyboardMoveSpeed : 0.0f;
				//LocalMovement.Y -= bDownKeyState ? KeyboardMoveSpeed : 0.0f;
				//LocalMovement.Z += bBackwardKeyState ? KeyboardMoveSpeed : 0.0f;
				//LocalMovement.Z -= bForwardKeyState ? KeyboardMoveSpeed : 0.0f;
			}

			LocalZoom += bZoomOutKeyState ? KeyboardMoveSpeed : 0.0f;
			LocalZoom -= bZoomInKeyState ? KeyboardMoveSpeed : 0.0f;

			// Focus
			if (bFocus)
			{
				FocusCamera();
			}
		}

		return true;
	}

	virtual bool InputAxis(FViewport* InViewport, int32 ControllerId, FKey Key, float Delta, float InDeltaTime, int32 NumSamples, bool bGamepad) override
	{
		// Viewport movement only enabled when preview is enabled otherwise there is no feedback
		auto ViewModel = WeakViewModel.Pin();
		UNiagaraFlipbookSettings* FlipbookSettings = ViewModel ? ViewModel->GetFlipbookSettings() : nullptr;
		if ( bShowPreview && FlipbookSettings && (Key == EKeys::MouseX || Key == EKeys::MouseY) )
		{
			if (FlipbookSettings->IsOrthographic())
			{
				if (InViewport->KeyState(EKeys::RightMouseButton))
				{
					// Zoom
					if (InViewport->KeyState(EKeys::LeftControl) || InViewport->KeyState(EKeys::RightControl))
					{
						LocalZoom += (Key == EKeys::MouseY) ? Delta : 0.0f;
					}
					// Aspect
					else if (InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt))
					{
						LocalAspect += (Key == EKeys::MouseY) ? Delta : 0.0f;
					}
					// Move
					else
					{
						LocalMovement.X += (Key == EKeys::MouseX) ? Delta : 0.0f;
						LocalMovement.Y += (Key == EKeys::MouseY) ? Delta : 0.0f;
					}
				}
			}
			else
			{
				// Zoom
				if (InViewport->KeyState(EKeys::RightMouseButton))
				{
					if (InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt))
					{
						LocalAspect += (Key == EKeys::MouseY) ? Delta : 0.0f;
					}
					else
					{
						LocalMovement.Z += (Key == EKeys::MouseY) ? Delta : 0.0f;
					}
				}
				// Middle button translate orbit location
				else if (InViewport->KeyState(EKeys::MiddleMouseButton))
				{
					LocalMovement.X += (Key == EKeys::MouseX) ? Delta : 0.0f;
					LocalMovement.Y += (Key == EKeys::MouseY) ? Delta : 0.0f;
				}
				// Rotation
				else if (InViewport->KeyState(EKeys::LeftMouseButton))
				{
					LocalRotation.X += (Key == EKeys::MouseX) ? Delta : 0.0f;
					LocalRotation.Y += (Key == EKeys::MouseY) ? Delta : 0.0f;
				}
			}
		}

		return true;
	}

	virtual void Tick(float DeltaSeconds) override
	{
		//FEditorViewportClient::Tick(DeltaSeconds);

		// Apply local movement
		auto ViewModel = WeakViewModel.Pin();
		UNiagaraFlipbookSettings* FlipbookSettings = ViewModel ? ViewModel->GetFlipbookSettings() : nullptr;
		if ( FlipbookSettings )
		{
			const FMatrix ViewMatrix = FlipbookSettings->GetViewMatrix().Inverse();
			const FVector XAxis = ViewMatrix.GetUnitAxis(EAxis::X);
			const FVector YAxis = ViewMatrix.GetUnitAxis(EAxis::Y);
			const FVector ZAxis = ViewMatrix.GetUnitAxis(EAxis::Z);

			FVector WorldMovement = FVector::ZeroVector;
			if (FlipbookSettings->IsOrthographic())
			{
				const FVector2D MoveSpeed = GetPreviewOrthoUnits();

				WorldMovement -= LocalMovement.X * MoveSpeed.X * XAxis;
				WorldMovement -= LocalMovement.Y * MoveSpeed.Y * YAxis;
				//WorldMovement += LocalMovement.Z * MoveSpeed * ZAxis;

				FlipbookSettings->CameraViewportLocation[(int)FlipbookSettings->CameraViewportMode] += WorldMovement;
			}
			else
			{
				FRotator& WorldRotation = FlipbookSettings->CameraViewportRotation[(int)FlipbookSettings->CameraViewportMode];
				WorldRotation.Yaw = FRotator::ClampAxis(WorldRotation.Yaw + LocalRotation.X);
				WorldRotation.Roll = FMath::Clamp(WorldRotation.Roll + LocalRotation.Y, 0.0f, 180.0f);

				const float MoveSpeed = PerspectiveMoveSpeed;
				WorldMovement -= LocalMovement.X * MoveSpeed * XAxis;
				WorldMovement -= LocalMovement.Y * MoveSpeed * YAxis;
				//WorldMovement -= LocalMovement.Z * MoveSpeed * ZAxis;
				FlipbookSettings->CameraViewportLocation[(int)FlipbookSettings->CameraViewportMode] += WorldMovement;

				FlipbookSettings->CameraOrbitDistance = FMath::Max(FlipbookSettings->CameraOrbitDistance + LocalMovement.Z, 0.01f);
			}

			if (!FMath::IsNearlyZero(LocalZoom))
			{
				if (FlipbookSettings->IsPerspective())
				{
					FlipbookSettings->CameraFOV = FMath::Clamp(FlipbookSettings->CameraFOV + LocalZoom, 0.001f, 179.0f);
				}
				else
				{
					FlipbookSettings->CameraOrthoWidth = FMath::Max(FlipbookSettings->CameraOrthoWidth + LocalZoom, 1.0f);
				}
			}

			if (!FMath::IsNearlyZero(LocalAspect))
			{
				if (FlipbookSettings->bUseCameraAspectRatio)
				{
					FlipbookSettings->CameraAspectRatio = FMath::Max(FlipbookSettings->CameraAspectRatio + (LocalAspect / 50.0f), 0.01f);
				}
			}
		}

		// Clear data
		LocalMovement = FVector::ZeroVector;
		LocalZoom = 0.0f;
		LocalAspect = 0.0f;
		LocalRotation = FVector::ZeroVector;
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
		const UNiagaraFlipbookSettings* FlipbookGeneratedSettings = ViewModel->GetFlipbookGeneratedSettings();
		const int32 PreviewTextureIndex = ViewModel->GetPreviewTextureIndex();

		// Determine view rects
		PreviewViewRect = FIntRect();
		FlipbookViewRect = FIntRect();
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
				if (FlipbookGeneratedSettings && FlipbookGeneratedSettings->OutputTextures.IsValidIndex(PreviewTextureIndex))
				{
					FlipbookViewRect = ConstrainRect(FIntRect(ViewRect.Max.X - ViewWidth, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y), FlipbookGeneratedSettings->OutputTextures[PreviewTextureIndex].FrameSize);
				}
				else
				{
					FlipbookViewRect = FIntRect(ViewRect.Max.X - ViewWidth, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
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
				PreviewComponent->SetSeekDelta(FlipbookSettings->GetSeekDelta());
				PreviewComponent->SeekToDesiredAge(WorldTime);
				PreviewComponent->TickComponent(FlipbookSettings->GetSeekDelta(), ELevelTick::LEVELTICK_All, nullptr);

				FNiagaraFlipbookRenderer FlipbookRenderer(PreviewComponent, WorldTime);
				FlipbookRenderer.RenderView(RealtimeRenderTarget, PreviewTextureIndex);

				const FVector2D HalfPixel(0.5f / float(OutputTexture.FrameSize.X), 0.5f / float(OutputTexture.FrameSize.Y));
				Canvas->DrawTile(
					PreviewViewRect.Min.X, PreviewViewRect.Min.Y, PreviewViewRect.Width(), PreviewViewRect.Height(),
					HalfPixel.X, HalfPixel.Y, 1.0f - HalfPixel.X, 1.0f - HalfPixel.Y,
					FLinearColor::White,
					RealtimeRenderTarget->Resource,
					false	//-TODO: Preview with alpha?
				);

				// Display info text
				if (bShowInfoText)
				{
					Canvas->DrawShadowedString(TextPosition.X, TextPosition.Y, TEXT("Live Preview"), DisplayFont, FLinearColor::White);
					TextPosition.Y += FontHeight;

					Canvas->DrawShadowedString(TextPosition.X + 5.0f, TextPosition.Y, *FString::Printf(TEXT("Texture(%d) FrameSize(%d x %d)"), PreviewTextureIndex, OutputTexture.FrameSize.X, OutputTexture.FrameSize.Y), DisplayFont, FLinearColor::White);
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

			const bool bFlipbookValid =
				(FlipbookGeneratedSettings != nullptr) &&
				FlipbookGeneratedSettings->OutputTextures.IsValidIndex(PreviewTextureIndex) &&
				(FlipbookGeneratedSettings->OutputTextures[PreviewTextureIndex].GeneratedTexture != nullptr);
			FVector2D TextPosition(FlipbookViewRect.Min.X + TextStartOffset.X, FlipbookViewRect.Min.Y + TextStartOffset.Y);

			if (bFlipbookValid)
			{
				const auto DisplayData = FlipbookGeneratedSettings->GetDisplayInfo(CurrentTime - FlipbookGeneratedSettings->StartSeconds, FlipbookGeneratedSettings->bPreviewLooping);
				const FNiagaraFlipbookTextureSettings& OutputTexture = FlipbookGeneratedSettings->OutputTextures[PreviewTextureIndex];

				const FIntPoint TextureSize = FIntPoint(FMath::Max(OutputTexture.GeneratedTexture->GetSizeX(), 1), FMath::Max(OutputTexture.GeneratedTexture->GetSizeY(), 1));
				const FIntPoint FramesPerDimension = FlipbookGeneratedSettings->FramesPerDimension;
				const FIntPoint FrameIndexA = FIntPoint(DisplayData.FrameIndexA % FramesPerDimension.X, DisplayData.FrameIndexA / FramesPerDimension.X);
				const FIntPoint FrameIndexB = FIntPoint(DisplayData.FrameIndexB % FramesPerDimension.X, DisplayData.FrameIndexB / FramesPerDimension.X);
				const FIntPoint FramePixelA = FIntPoint(FrameIndexA.X * OutputTexture.FrameSize.X, FrameIndexA.Y * OutputTexture.FrameSize.Y);
				const FIntPoint FramePixelB = FIntPoint(FrameIndexB.X * OutputTexture.FrameSize.X, FrameIndexB.Y * OutputTexture.FrameSize.Y);

				Canvas->DrawTile(
					FlipbookViewRect.Min.X, FlipbookViewRect.Min.Y, FlipbookViewRect.Width(), FlipbookViewRect.Height(),
					(float(FramePixelA.X) + 0.5f) / float(TextureSize.X), (float(FramePixelA.Y) + 0.5f) / float(TextureSize.Y), (float(FramePixelA.X + OutputTexture.FrameSize.X) - 0.5f) / float(TextureSize.X), (float(FramePixelA.Y + OutputTexture.FrameSize.Y) - 0.5f) / float(TextureSize.Y),
					FLinearColor::White,
					OutputTexture.GeneratedTexture->Resource,
					false	//-TODO: Preview with alpha?
				);

				// Display info text
				if (bShowInfoText)
				{
					Canvas->DrawShadowedString(TextPosition.X, TextPosition.Y, TEXT("Flipbook"), DisplayFont, FLinearColor::White);
					TextPosition.Y += FontHeight;

					Canvas->DrawShadowedString(TextPosition.X + 5.0f, TextPosition.Y, *FString::Printf(TEXT("Texture(%d) FrameSize(%d x %d) Frame(%d/%d)"), PreviewTextureIndex, OutputTexture.FrameSize.X, OutputTexture.FrameSize.Y, DisplayData.FrameIndexA, FlipbookGeneratedSettings->GetNumFrames()), DisplayFont, FLinearColor::White);
					TextPosition.Y += FontHeight;

					if (!FlipbookGeneratedSettings->Equals(*FlipbookSettings))
					{
						Canvas->DrawShadowedString(TextPosition.X + 5.0f, TextPosition.Y, TEXT("Warning: Out Of Date"), DisplayFont, FLinearColor::White);
						TextPosition.Y += FontHeight;
					}
				}
			}
			else
			{
				Canvas->DrawShadowedString(TextPosition.X, TextPosition.Y, TEXT("Flipbook Not Generated"), DisplayFont, FLinearColor::White);
				TextPosition.Y += FontHeight;
				}
		}
	}

	FIntRect ConstrainRect(const FIntRect& InRect, FIntPoint InSize) const
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

	void FocusCamera()
	{
		auto ViewModel = WeakViewModel.Pin();
		UNiagaraFlipbookSettings* FlipbookSettings = ViewModel ? ViewModel->GetFlipbookSettings() : nullptr;
		UNiagaraComponent* NiagaraComponent = ViewModel->GetPreviewComponent();
		if ( bShowPreview && FlipbookSettings && NiagaraComponent )
		{
			//-TODO: Should take aspect ratio into account here
			const FBoxSphereBounds ComponentBounds = NiagaraComponent->CalcBounds(NiagaraComponent->GetComponentTransform());
			if ( FlipbookSettings->IsOrthographic() )
			{
				FlipbookSettings->CameraViewportLocation[(int)FlipbookSettings->CameraViewportMode] = ComponentBounds.Origin;
				FlipbookSettings->CameraOrthoWidth = ComponentBounds.SphereRadius * 2.0f;
			}
			else
			{
				const float HalfFOVRadians = FMath::DegreesToRadians(FlipbookSettings->CameraFOV) * 0.5f;
				const float CameraDistance = ComponentBounds.SphereRadius / FMath::Tan(HalfFOVRadians);
				//const FVector CameraOffset = FlipbookSettings->GetViewMatrix().Inverse().GetUnitAxis(EAxis::Z) * CameraDistance;
				//FlipbookSettings->CameraViewportLocation[(int)ENiagaraFlipbookViewMode::Perspective] = ComponentBounds.Origin - CameraOffset;
				FlipbookSettings->CameraViewportLocation[(int)FlipbookSettings->CameraViewportMode] = ComponentBounds.Origin;
				FlipbookSettings->CameraOrbitDistance = CameraDistance;
			}
		}
	}

	FVector2D GetPreviewOrthoUnits() const
	{
		FVector2D OrthoUnits = FVector2D::ZeroVector;

		auto ViewModel = WeakViewModel.Pin();
		const UNiagaraFlipbookSettings* FlipbookSettings = ViewModel ? ViewModel->GetFlipbookSettings() : nullptr;
		if (FlipbookSettings && (PreviewViewRect.Area() > 0))
		{
			OrthoUnits = FlipbookSettings->GetOrthoSize(ViewModel->GetPreviewTextureIndex());
			OrthoUnits.X = OrthoUnits.X / float(PreviewViewRect.Width());
			OrthoUnits.Y = OrthoUnits.Y / float(PreviewViewRect.Height());
		}
		return OrthoUnits;
	}


	virtual UWorld* GetWorld() const override
	{
		auto ViewModel = WeakViewModel.Pin();
		return ViewModel ? ViewModel->GetPreviewComponent()->GetWorld() : nullptr;
	}

	UFont* GetFont() const { return GetStatsFont(); }

public:
	UTextureRenderTarget2D*						RealtimeRenderTarget = nullptr;

	FVector										LocalMovement = FVector::ZeroVector;
	float										LocalZoom = 0.0f;
	float										LocalAspect = 0.0f;
	FVector										LocalRotation = FVector::ZeroVector;
	float										KeyboardMoveSpeed = 5.0f;
	float										PerspectiveMoveSpeed = 2.0f;

	FIntRect									PreviewViewRect;
	FIntRect									FlipbookViewRect;

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
