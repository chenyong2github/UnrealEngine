// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FNiagaraFlipbookViewModel;
class SNiagaraFlipbookViewport;
class SNiagaraFlipbookTimelineWidget;
class IDetailsView;

class SNiagaraFlipbookWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraFlipbookWidget) {}
		SLATE_ARGUMENT(TWeakPtr<FNiagaraFlipbookViewModel>, WeakViewModel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	FReply OnCapture();

	TSharedRef<SWidget> MakeTextureSelectionWidget() const;
	FText GetSelectedTextureAsText() const;

	class UNiagaraFlipbookSettings* GetFlipbookSettings() const;

	void SetPreviewRelativeTime(float RelativeTime);

	FReply OnTransportBackwardEnd();
	FReply OnTransportBackwardStep();
	FReply OnTransportForwardPlay();
	FReply OnTransportForwardStep();
	FReply OnTransportForwardEnd();

private:
	TWeakPtr<FNiagaraFlipbookViewModel>			WeakViewModel;
	TSharedPtr<SNiagaraFlipbookViewport>		ViewportWidget;
	TSharedPtr<SNiagaraFlipbookTimelineWidget>	TimelineWidget;
	TSharedPtr<IDetailsView>					FlipbookSettingsDetails;
	TSharedPtr<SWidget>							TransportControls;

	bool										bIsPlaying = false;
	float										PreviewRelativeTime = 0.0f;
};
