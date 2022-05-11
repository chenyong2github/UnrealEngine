// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"


namespace UE::PoseSearch
{
	class FDatabasePreviewScene;
	class FDatabaseViewportClient;
	class FDatabaseEditorToolkit;
	class SPoseSearchDatabaseViewportToolBar;

	struct FDatabasePreviewRequiredArgs
	{
		FDatabasePreviewRequiredArgs(
			const TSharedRef<FDatabaseEditorToolkit>& InAssetEditorToolkit,
			const TSharedRef<FDatabasePreviewScene>& InPreviewScene)
			: AssetEditorToolkit(InAssetEditorToolkit)
			, PreviewScene(InPreviewScene)
		{
		}

		TSharedRef<FDatabaseEditorToolkit> AssetEditorToolkit;

		TSharedRef<FDatabasePreviewScene> PreviewScene;
	};

	class SDatabaseViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
	{
	public:
		SLATE_BEGIN_ARGS(SDatabaseViewport) {}
		SLATE_END_ARGS();

		void Construct(const FArguments& InArgs, const FDatabasePreviewRequiredArgs& InRequiredArgs);
		virtual ~SDatabaseViewport() {}

		// ~ICommonEditorViewportToolbarInfoProvider interface
		virtual TSharedRef<class SEditorViewport> GetViewportWidget() override;
		virtual TSharedPtr<FExtender> GetExtenders() const override;
		virtual void OnFloatingButtonClicked() override;
		// ~End of ICommonEditorViewportToolbarInfoProvider interface

	protected:

		// ~SEditorViewport interface
		virtual void BindCommands() override;
		virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
		virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
		// ~End of SEditorViewport interface

		/** The viewport toolbar */
		TSharedPtr<SPoseSearchDatabaseViewportToolBar> ViewportToolbar;

		/** Viewport client */
		TSharedPtr<FDatabaseViewportClient> ViewportClient;

		/** The preview scene that we are viewing */
		TWeakPtr<FDatabasePreviewScene> PreviewScenePtr;

		/** Asset editor toolkit we are embedded in */
		TWeakPtr<FDatabaseEditorToolkit> AssetEditorToolkitPtr;
	};

	class SDatabasePreview : public SCompoundWidget
	{
	public:
		DECLARE_DELEGATE_TwoParams(FOnScrubPositionChanged, double, bool)
		DECLARE_DELEGATE(FOnButtonClickedEvent)

		SLATE_BEGIN_ARGS(SDatabasePreview) {}
			SLATE_ATTRIBUTE(double, SliderScrubTime);
			SLATE_ATTRIBUTE(TRange<double>, SliderViewRange);
			SLATE_EVENT(FOnScrubPositionChanged, OnSliderScrubPositionChanged);
			SLATE_EVENT(FOnButtonClickedEvent, OnBackwardEnd);
			SLATE_EVENT(FOnButtonClickedEvent, OnBackwardStep);
			SLATE_EVENT(FOnButtonClickedEvent, OnBackward);
			SLATE_EVENT(FOnButtonClickedEvent, OnPause);
			SLATE_EVENT(FOnButtonClickedEvent, OnForward);
			SLATE_EVENT(FOnButtonClickedEvent, OnForwardStep);
			SLATE_EVENT(FOnButtonClickedEvent, OnForwardEnd);
		SLATE_END_ARGS();

		void Construct(const FArguments& InArgs, const FDatabasePreviewRequiredArgs& InRequiredArgs);

	protected:
		TAttribute<double> SliderScrubTimeAttribute;
		TAttribute<TRange<double>> SliderViewRange = TRange<double>(0.0, 1.0);
		FOnScrubPositionChanged OnSliderScrubPositionChanged;
		FOnButtonClickedEvent OnBackwardEnd;
		FOnButtonClickedEvent OnBackwardStep;
		FOnButtonClickedEvent OnBackward;
		FOnButtonClickedEvent OnPause;
		FOnButtonClickedEvent OnForward;
		FOnButtonClickedEvent OnForwardStep;
		FOnButtonClickedEvent OnForwardEnd;
	};
}
