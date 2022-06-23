// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesPageViewerPreview.h"
#include "UI/Components/SRenderPagesPageViewerFrameSlider.h"
#include "RenderPage/RenderPageCollection.h"
#include "IRenderPageCollectionEditor.h"
#include "IRenderPagesModule.h"
#include "MoviePipelineOutputSetting.h"
#include "RenderPage/RenderPageManager.h"
#include "SlateOptMacros.h"
#include "Styles/RenderPagesEditorStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPageViewerPreview"


bool UE::RenderPages::Private::SRenderPagesPageViewerPreview::bHasRenderedSinceAppStart = false;


void UE::RenderPages::Private::SRenderPagesPageViewerPreview::Tick(const FGeometry&, const double, const float)
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (BlueprintEditor->CanCurrentlyRender() && !CurrentJob)
		{
			UpdateRerenderButton();
			UpdateFrameSlider();

			if (FramesUntilRenderNewPreview > 0)
			{
				FramesUntilRenderNewPreview--;
				if (FramesUntilRenderNewPreview <= 0)
				{
					InternalRenderNewPreview();
				}
			}
		}
	}
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;
	SelectedPageWeakPtr = nullptr;
	CurrentJob = nullptr;
	FramesUntilRenderNewPreview = 0;
	ImageBrushEmpty = FSlateBrush();
	ImageBrushEmpty.DrawAs = ESlateBrushDrawType::Type::NoDrawType;
	ImageBrush = FSlateBrush();
	ImageTexture = nullptr;
	LastUpdateImageTextureSelectedPageWeakPtr = nullptr;

	SAssignNew(Image, SImage)
		.Image(&ImageBrushEmpty);

	SAssignNew(ImageBackground, SImage)
		.Image(&ImageBrushEmpty);

	SAssignNew(FrameSlider, SRenderPagesPageViewerFrameSlider)
		.Visibility(EVisibility::Hidden)
		.OnValueChanged(this, &SRenderPagesPageViewerPreview::FrameSliderValueChanged)
		.OnCaptureEnd(this, &SRenderPagesPageViewerPreview::FrameSliderValueChangedEnd);

	SelectedPageChanged();

	InBlueprintEditor->OnRenderPagesChanged().AddSP(this, &SRenderPagesPageViewerPreview::PagesDataChanged);
	InBlueprintEditor->OnRenderPagesSelectionChanged().AddSP(this, &SRenderPagesPageViewerPreview::SelectedPageChanged);
	FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SRenderPagesPageViewerPreview::OnObjectModified);

	ChildSlot
	[
		SNew(SVerticalBox)
		.Visibility_Lambda([this]() -> EVisibility { return (!IsPreviewWidget() && CurrentJob) ? EVisibility::Hidden : EVisibility::Visible; })

		// image
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f)
		[
			SAssignNew(RerenderButton, SButton)
			.ContentPadding(0.0f)
			.ButtonStyle(FRenderPagesEditorStyle::Get(), TEXT("Invisible"))
			.IsFocusable(false)
			.OnClicked(this, &SRenderPagesPageViewerPreview::OnClicked)
			[
				SNew(SScaleBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Stretch(EStretch::ScaleToFit)
				.StretchDirection(EStretchDirection::Both)
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						ImageBackground.ToSharedRef()
					]

					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						Image.ToSharedRef()
					]
				]
			]
		]

		// slider
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f)
		[
			FrameSlider.ToSharedRef()
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION


FReply UE::RenderPages::Private::SRenderPagesPageViewerPreview::OnClicked()
{
	if (!IsPreviewWidget())
	{
		RenderNewPreview();
	}

	return FReply::Handled();
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::OnObjectModified(UObject* Object)
{
	if (SelectedPageWeakPtr.IsValid() && (Object == SelectedPageWeakPtr.Get()))
	{
		// page changed
		SelectedPageChanged();
	}
	else if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (Object == BlueprintEditor->GetInstance())
		{
			// page collection changed
			PagesDataChanged();
		}
	}
}


void UE::RenderPages::Private::SRenderPagesPageViewerPreview::PagesDataChanged()
{
	if (IsPreviewWidget())
	{
		UpdateImageTexture();
		RenderNewPreview();
	}
	else
	{
		UpdateImageTexture();
	}
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::SelectedPageChanged()
{
	SelectedPageWeakPtr = nullptr;
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (const TArray<URenderPage*> SelectedPages = BlueprintEditor->GetSelectedRenderPages(); (SelectedPages.Num() == 1))
		{
			SelectedPageWeakPtr = SelectedPages[0];
		}
	}

	if (IsPreviewWidget())
	{
		UpdateImageTexture();
		RenderNewPreview();
	}
	else
	{
		UpdateImageTexture();
	}
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::FrameSliderValueChanged(const float NewValue)
{
	if (!IsPreviewWidget())
	{
		UpdateImageTexture(false);
	}
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::FrameSliderValueChangedEnd()
{
	if (IsPreviewWidget())
	{
		RenderNewPreview();
	}
	else
	{
		UpdateImageTexture();
	}
}


void UE::RenderPages::Private::SRenderPagesPageViewerPreview::RenderNewPreview()
{
	FramesUntilRenderNewPreview = 1;
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::InternalRenderNewPreview()
{
	InternalRenderNewPreviewOfPage(SelectedPageWeakPtr.Get());
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::InternalRenderNewPreviewOfPage(URenderPage* Page)
{
	if (GetTickSpaceGeometry().Size.X <= 0)
	{
		// don't render, try again next frame
		RenderNewPreview();
		return;
	}

	if (!IsValid(Page))
	{
		SetImageTexture(nullptr);
		return;
	}

	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		double WidgetWidth = FMath::Max(120.0, FMath::Min(GetTickSpaceGeometry().Size.X, GetTickSpaceGeometry().Size.Y * Page->GetOutputAspectRatio()));
		double RenderResolution = WidgetWidth * (IsPreviewWidget() ? 1.25 : 0.75);// pixels in width

		FRenderPageManagerRenderPreviewFrameArgs JobArgs;
		if (IsPreviewWidget())
		{
			TOptional<int32> SelectedFrame = (!FrameSlider.IsValid() ? TOptional<int32>() : FrameSlider->GetSelectedFrame(Page));
			if (!SelectedFrame.IsSet())
			{
				SetImageTexture(nullptr);
				return;
			}

			JobArgs.bHeadless = bHasRenderedSinceAppStart;
			JobArgs.Frame = SelectedFrame.Get(0);
		}
		JobArgs.PageCollection = BlueprintEditor->GetInstance();
		JobArgs.Page = Page;
		JobArgs.Resolution = FIntPoint(FMath::RoundToInt32(RenderResolution), FMath::RoundToInt32(RenderResolution / Page->GetOutputAspectRatio()));

		const TSharedPtr<SWidget> BaseThis = AsShared();
		const TSharedPtr<SRenderPagesPageViewerPreview> This = StaticCastSharedPtr<SRenderPagesPageViewerPreview>(BaseThis);
		JobArgs.Callback = FRenderPageManagerRenderPreviewFrameArgsCallback::CreateLambda([This, BlueprintEditor](const bool bSuccess)
		{
			if (This.IsValid())
			{
				This->RenderNewPreviewCallback(bSuccess);
			}
			else if (BlueprintEditor.IsValid())
			{
				BlueprintEditor->SetPreviewRenderJob(nullptr);
			}
		});

		if (URenderPagesMoviePipelineRenderJob* NewJob = IRenderPagesModule::Get().GetManager().RenderPreviewFrame(JobArgs))
		{
			CurrentJob = NewJob;
			BlueprintEditor->SetPreviewRenderJob(CurrentJob);
			return;
		}
	}
	SetImageTexture(nullptr);
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::RenderNewPreviewCallback(const bool bSuccess)
{
	bHasRenderedSinceAppStart = true;
	UpdateImageTexture();

	CurrentJob = nullptr;
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		BlueprintEditor->SetPreviewRenderJob(CurrentJob);
	}
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::UpdateImageTexture(const bool bForce)
{
	if (FrameSlider.IsValid())
	{
		if (URenderPage* SelectedPage = SelectedPageWeakPtr.Get(); IsValid(SelectedPage))
		{
			if (!bForce && LastUpdateImageTextureSelectedPageWeakPtr.IsValid() && (LastUpdateImageTextureSelectedPageWeakPtr == SelectedPage))
			{
				if (IsPreviewWidget() || (LastUpdateImageTextureFrame == FrameSlider->GetSelectedFrame(SelectedPage)))
				{
					return; // nothing changed
				}
			}
			LastUpdateImageTextureSelectedPageWeakPtr = SelectedPage;

			if (IsPreviewWidget())
			{
				if (!FrameSlider->GetSelectedFrame(SelectedPage).IsSet())
				{
					SetImageTexture(nullptr);
					return;
				}
				SetImageTexture(IRenderPagesModule::Get().GetManager().GetSingleRenderedPreviewFrame(SelectedPage));
				return;
			}

			TOptional<int32> Frame = FrameSlider->GetSelectedFrame(SelectedPage);
			LastUpdateImageTextureFrame = Frame;
			if (!Frame.IsSet())
			{
				SetImageTexture(nullptr);
				return;
			}
			SetImageTexture(IRenderPagesModule::Get().GetManager().GetRenderedPreviewFrame(SelectedPage, Frame.Get(0)));
			return;
		}
	}
	SetImageTexture(nullptr);
	LastUpdateImageTextureSelectedPageWeakPtr = nullptr;
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::SetImageTexture(UTexture2D* Texture)
{
	{// cleanup >>
		if (Image.IsValid())
		{
			Image->SetImage(&ImageBrushEmpty);
			ImageBackground->SetImage(&ImageBrushEmpty);
		}
		ImageBrush.SetResourceObject(nullptr);
		ImageBrush.SetImageSize(FVector2D(0, 0));

		if (IsValid(ImageTexture))
		{
			ImageTexture->RemoveFromRoot();
		}
		ImageTexture = nullptr;
	}// cleanup <<

	{// set new texture >>
		if (IsValid(Texture) && Texture->GetResource())
		{
			ImageTexture = Texture;
			ImageTexture->AddToRoot();

			if (Image.IsValid())
			{
				static constexpr double PreviewTabAspectRatio = 1.96875;

				double ImageWidth = ImageTexture->GetResource()->GetSizeX();
				double ImageHeight = ImageTexture->GetResource()->GetSizeY();
				const double ImageAspectRatio = ImageWidth / ImageHeight;
				if (ImageAspectRatio > PreviewTabAspectRatio)
				{
					ImageWidth = 1280.0;
					ImageHeight = ImageWidth / ImageAspectRatio;
				}
				else
				{
					ImageHeight = 1280.0 / PreviewTabAspectRatio;
					ImageWidth = ImageHeight * ImageAspectRatio;
				}

				ImageBrush = FSlateBrush();
				ImageBrush.DrawAs = ESlateBrushDrawType::Type::Image;
				ImageBrush.ImageType = ESlateBrushImageType::Type::FullColor;
				ImageBrush.SetResourceObject(ImageTexture);
				ImageBrush.SetImageSize(FVector2D(ImageWidth, ImageHeight));
				Image->SetImage(&ImageBrush);
				ImageBackground->SetImage(FCoreStyle::Get().GetBrush("Checkerboard"));
			}
		}
	}// set new texture <<
}


void UE::RenderPages::Private::SRenderPagesPageViewerPreview::UpdateRerenderButton()
{
	if (!RerenderButton.IsValid())
	{
		return;
	}
	const bool bIsUsable = (!IsPreviewWidget() && SelectedPageWeakPtr.IsValid());

	RerenderButton->SetButtonStyle(&FRenderPagesEditorStyle::Get().GetWidgetStyle<FButtonStyle>(bIsUsable ? TEXT("HoverHintOnly") : TEXT("Invisible")));
	RerenderButton->SetCursor(bIsUsable ? EMouseCursor::Type::Hand : EMouseCursor::Type::Default);
}

void UE::RenderPages::Private::SRenderPagesPageViewerPreview::UpdateFrameSlider()
{
	if (!FrameSlider.IsValid())
	{
		return;
	}
	FrameSlider->SetVisibility(EVisibility::Hidden);

	if (URenderPage* SelectedPage = SelectedPageWeakPtr.Get(); IsValid(SelectedPage))
	{
		if (IsPreviewWidget() && !FrameSlider->GetSelectedSequenceFrame(SelectedPage).IsSet())
		{
			return;
		}

		if (!FrameSlider->SetFramesText(SelectedPage))
		{
			return;
		}

		FrameSlider->SetVisibility(EVisibility::Visible);
	}
}


#undef LOCTEXT_NAMESPACE
