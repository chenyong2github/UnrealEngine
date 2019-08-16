// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/SViewport.h"
#include "MatineeViewportClient.h"
#include "Matinee.h"

#include "Slate/SceneViewport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "EngineAnalytics.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "IDocumentation.h"
#include "Framework/Text/SlateHyperlinkRun.h"

static void OnDocLinkClicked(const FSlateHyperlinkRun::FMetadata& Metadata)
{
	const FString* Url = Metadata.Find(TEXT("href"));
	if (Url)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			TArray<FAnalyticsEventAttribute> EventAttributes;
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("DocLink"), *Url));

			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Matinee.DeprecationWarning.DocLinkClicked"), EventAttributes);
		}

		IDocumentation::Get()->Open(*Url, FDocumentationSourceInfo(TEXT("editor")));
	}
}

class SMatineeDeprecationMessage : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMatineeDeprecationMessage)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0.4f, 0.f, 0.f, 1.f))
			.BorderImage(FEditorStyle::GetBrush(TEXT("WhiteBrush")))
			.Visibility(this, &SMatineeDeprecationMessage::GetWarningMessageVisibility)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SRichTextBlock)
					.Text(NSLOCTEXT("Matinee", "MatineeLastVersionSupported", "As of 4.23, Matinee is no longer supported by UE4 and will be removed from the engine in the near future. Once removed, you will <NormalText.Important>no longer be able to run a Matinee or open Matinee Editor</>.\n"
									"Please use the <a id=\"udn\" href=\"/Engine/Sequencer/HowTo/MatineeConversionTool\" style=\"Hyperlink\">Matinee to Sequencer Conversion Tool</> to convert any files to Sequencer as soon as possible."))
					.AutoWrapText(true)
					.DecoratorStyleSet(&FEditorStyle::Get())
					+ SRichTextBlock::HyperlinkDecorator(TEXT("udn"), FSlateHyperlinkRun::FOnClick::CreateStatic(&OnDocLinkClicked))

				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
					.Text(NSLOCTEXT("Matinee", "DismissMatineeSupportWarning", "Dismiss"))
					.OnClicked(this, &SMatineeDeprecationMessage::DismissWarningForever)
				]
			]
		];
	}

	FReply DismissWarningForever() 
	{
		if (GConfig)
		{
			GConfig->SetBool(TEXT("Matinee"), TEXT("HasDismissedDeprecationWarning"), true, GEditorIni);

			if (FEngineAnalytics::IsAvailable())
			{
				FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Matinee.DeprecationWarning.Dimissed"));
			}
		}

		return FReply::Handled();
	}

	EVisibility GetWarningMessageVisibility() const
	{
		if (GConfig)
		{
			bool bDismissed = false;
			GConfig->GetBool(TEXT("Matinee"), TEXT("HasDismissedDeprecationWarning"), bDismissed, GEditorIni);

			return bDismissed ? EVisibility::Collapsed : EVisibility::Visible;
		}

		return EVisibility::Collapsed;
	}
};


/*-----------------------------------------------------------------------------
 SMatineeVCHolder
 -----------------------------------------------------------------------------*/

void SMatineeViewport::Construct(const FArguments& InArgs, TWeakPtr<FMatinee> InMatinee)
{
	this->ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SAssignNew(ViewportWidget, SViewport)
				.EnableGammaCorrection(false)
				.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
				.ShowEffectWhenDisabled(false)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(ScrollBar_Vert, SScrollBar)
				.AlwaysShowScrollbar(true)
				.OnUserScrolled(this, &SMatineeViewport::OnScroll)
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SMatineeDeprecationMessage)
		]
	];

	InterpEdVC = MakeShareable(new FMatineeViewportClient(InMatinee.Pin().Get()));

	InterpEdVC->bSetListenerPosition = false;

	InterpEdVC->VisibilityDelegate.BindSP(this, &SMatineeViewport::IsVisible);

	InterpEdVC->SetRealtime(true);

	Viewport = MakeShareable(new FSceneViewport(InterpEdVC.Get(), ViewportWidget));
	InterpEdVC->Viewport = Viewport.Get();

	// The viewport widget needs an interface so it knows what should render
	ViewportWidget->SetViewportInterface( Viewport.ToSharedRef() );
	
	// Setup the initial metrics for the scroll bar
	AdjustScrollBar();

}

SMatineeViewport::~SMatineeViewport()
{
	if (InterpEdVC.IsValid())
	{
		InterpEdVC->Viewport = NULL;
	}
}

/**
 * Returns the Mouse Position in the viewport
 */
FIntPoint SMatineeViewport::GetMousePos()
{
	FIntPoint Pos;
	if (Viewport.IsValid())
	{
		Viewport->GetMousePos(Pos);
	}

	return Pos;
}

/**
 * Updates the scroll bar for the current state of the window's size and content layout.  This should be called
 *  when either the window size changes or the vertical size of the content contained in the window changes.
 */
void SMatineeViewport::AdjustScrollBar()
{
	if( InterpEdVC.IsValid() && ScrollBar_Vert.IsValid() )
	{

		// Grab the height of the client window
		const uint32 ViewportHeight = InterpEdVC->Viewport->GetSizeXY().Y;

		if (ViewportHeight > 0)
		{
			// Compute scroll bar layout metrics
			uint32 ContentHeight = InterpEdVC->ComputeGroupListContentHeight();
			uint32 ContentBoxHeight =	InterpEdVC->ComputeGroupListBoxHeight( ViewportHeight );


			// The current scroll bar position
			float ScrollBarPos = -((float)InterpEdVC->ThumbPos_Vert) / ((float)ContentHeight);

			// The thumb size is the number of 'scrollbar units' currently visible
			float NewScrollBarThumbSize = ((float)ContentBoxHeight) / ((float)ContentHeight);   //ContentBoxHeight;

			if (NewScrollBarThumbSize > 1.f)
			{
				InterpEdVC->ThumbPos_Vert = 0;
				NewScrollBarThumbSize = 1.f;
			}

			ScrollBarThumbSize = NewScrollBarThumbSize;
			OnScroll(ScrollBarPos);
		}
	}
}

void SMatineeViewport::OnScroll( float InScrollOffsetFraction )
{
	const float LowerLimit = 1.0f - ScrollBarThumbSize;
	if (InScrollOffsetFraction > LowerLimit)
	{
		InScrollOffsetFraction = LowerLimit;
	}

	if( InterpEdVC.IsValid() && ScrollBar_Vert.IsValid() )
	{
		// Compute scroll bar layout metrics
		uint32 ContentHeight = InterpEdVC->ComputeGroupListContentHeight();

		InterpEdVC->ThumbPos_Vert = -InScrollOffsetFraction * ContentHeight;
		ScrollBar_Vert->SetState(InScrollOffsetFraction, ScrollBarThumbSize);

		// Force it to draw so the view change is seen
		InterpEdVC->Viewport->Invalidate();
		InterpEdVC->Viewport->Draw();
	}
}

bool SMatineeViewport::IsVisible() const
{
	return InterpEdVC.IsValid() && (!InterpEdVC->ParentTab.IsValid() || InterpEdVC->ParentTab.Pin()->IsForeground());
}

void FMatinee::OnToggleDirectorTimeline()
{
	if (DirectorTrackWindow.IsValid() && DirectorTrackWindow->InterpEdVC.IsValid())
	{
		DirectorTrackWindow->InterpEdVC->bWantTimeline = !DirectorTrackWindow->InterpEdVC->bWantTimeline;
		DirectorTrackWindow->InterpEdVC->Viewport->Invalidate();
		DirectorTrackWindow->InterpEdVC->Viewport->Draw();

		// Save to ini when it changes.
		GConfig->SetBool(TEXT("Matinee"), TEXT("DirectorTimelineEnabled"), DirectorTrackWindow->InterpEdVC->bWantTimeline, GEditorPerProjectIni);
	}
}

bool FMatinee::IsDirectorTimelineToggled()
{
	if (DirectorTrackWindow.IsValid() && DirectorTrackWindow->InterpEdVC.IsValid())
	{
		return DirectorTrackWindow->InterpEdVC->bWantTimeline;
	}

	return false;
}

void FMatinee::OnToggleCurveEditor()
{
	if ( CurveEdTab.IsValid() )
	{
		SetCurveTabVisibility(false);
	}
	else
	{
		SetCurveTabVisibility(true);
	}
}

bool FMatinee::IsCurveEditorToggled()
{
	if ( CurveEdTab.IsValid() )
	{
		return true;
	}

	return false;
}

void FMatinee::BuildTrackWindow()
{	
	TWeakPtr<FMatinee> MatineePtr = SharedThis(this);

	TrackWindow = SNew(SMatineeViewport, MatineePtr);
	DirectorTrackWindow = SNew(SMatineeViewport, MatineePtr)
		.Visibility(this, &FMatinee::GetDirectorTrackWindowVisibility);

	// Setup track window defaults
	TrackWindow->InterpEdVC->bIsDirectorTrackWindow = false;
	TrackWindow->InterpEdVC->bWantTimeline = true;

	DirectorTrackWindow->InterpEdVC->bIsDirectorTrackWindow = true;
	DirectorTrackWindow->InterpEdVC->bWantTimeline = true;
}
