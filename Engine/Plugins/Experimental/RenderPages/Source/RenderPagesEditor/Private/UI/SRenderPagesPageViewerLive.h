// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelSequence.h"
#include "LevelSequencePlayer.h"
#include "Widgets/SCompoundWidget.h"
#include "SEditorViewport.h"


class ALevelSequenceActor;
class SSlider;
class URenderPage;
class URenderPageCollection;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}

namespace UE::RenderPages::Private
{
	class SRenderPagesPageViewerFrameSlider;
}


namespace UE::RenderPages::Private
{
	/**
	 * The viewport client for the live page viewer widget.
	 */
	class FRenderPagesEditorViewportClient : public FEditorViewportClient
	{
	public:
		explicit FRenderPagesEditorViewportClient(FPreviewScene* PreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget = nullptr);

	public:
		//~ Begin FEditorViewportClient Interface
		virtual EMouseCursor::Type GetCursor(FViewport* InViewport, int32 X, int32 Y) override { return EMouseCursor::Default; }
		//~ End FEditorViewportClient Interface
	};


	/**
	 * The viewport for the live page viewer widget.
	 */
	class SRenderPagesEditorViewport : public SEditorViewport
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesEditorViewport) {}
		SLATE_END_ARGS()

		virtual void Tick(const FGeometry&, const double, const float) override;
		void Construct(const FArguments& InArgs);
		virtual ~SRenderPagesEditorViewport() override;

		bool ShowSequenceFrame(URenderPage* InPage, URenderPageCollection* InPageCollection, ULevelSequence* InSequence, const float InTime);

	protected:
		//~ Begin SEditorViewport Interface
		virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override { return ViewportClient.ToSharedRef(); }
		virtual void BindCommands() override {}
		virtual bool SupportsKeyboardFocus() const override { return false; }
		//~ End SEditorViewport Interface

		ULevelSequencePlayer* GetSequencePlayer();
		void DestroySequencePlayer();

	private:
		/** The viewport client. */
		TSharedPtr<FRenderPagesEditorViewportClient> ViewportClient;

		/** The world that the level sequence actor was spawned in. */
		UPROPERTY()
		TWeakObjectPtr<UWorld> LevelSequencePlayerWorld;

		/** The level sequence actor we spawned to play the sequence of any given page. */
		UPROPERTY()
		TObjectPtr<ALevelSequenceActor> LevelSequencePlayerActor;

		/** The level sequence player we created to play the sequence of any given page. */
		UPROPERTY()
		TObjectPtr<ULevelSequencePlayer> LevelSequencePlayer;

		/** The level sequence that we're currently playing. */
		UPROPERTY()
		TObjectPtr<ULevelSequence> LevelSequence;

		/** The time of the currently playing sequence. */
		float LevelSequenceTime;

		/** The page that's currently being shown. */
		UPROPERTY()
		TObjectPtr<URenderPage> Page;

		/** The collection of the page that's currently being shown. */
		UPROPERTY()
		TObjectPtr<URenderPageCollection> PageCollection;
	};


	/**
	 * A page viewer widget, allows the user to see a live render using a viewport.
	 */
	class SRenderPagesPageViewerLive : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesPageViewerLive) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor);

	private:
		void OnObjectModified(UObject* Object);
		void PagesDataChanged();
		void SelectedPageChanged();
		void FrameSliderValueChanged(const float NewValue);
		void UpdateViewport();
		void UpdateFrameSlider();

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** A reference to the job that's currently rendering. */
		TWeakObjectPtr<URenderPage> SelectedPageWeakPtr;

		/** The scene viewport widget */
		TSharedPtr<SRenderPagesEditorViewport> ViewportWidget;

		/** The widget that allows the user to select what frame they'd like to see. */
		TSharedPtr<SRenderPagesPageViewerFrameSlider> FrameSlider;
	};
}
