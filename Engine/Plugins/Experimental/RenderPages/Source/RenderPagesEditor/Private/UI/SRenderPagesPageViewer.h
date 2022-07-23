// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"


class SBorder;
class URenderPage;
class URenderPagesMoviePipelineRenderJob;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	/**
	 * An enum containing the different page viewer modes that are currently available in the render pages plugin.
	 */
	enum class ERenderPagesPageViewerMode : uint8
	{
		Live,
		Preview,
		Rendered,
		None
	};

	/**
	 * The render page viewer, allows the user to see the expected render output, directly in the editor.
	 */
	class SRenderPagesPageViewer : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesPageViewer) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor);

	private:
		/** Creates a tab button for a viewer mode. */
		TSharedRef<SWidget> CreateViewerModeButton(const FText& ButtonText, const ERenderPagesPageViewerMode ButtonViewerMode);

	public:
		/** Refreshes the content of this widget. */
		void Refresh();

	private:
		void OnBatchRenderingStarted(URenderPagesMoviePipelineRenderJob* RenderJob) { Refresh(); }
		void OnBatchRenderingFinished(URenderPagesMoviePipelineRenderJob* RenderJob) { Refresh(); }

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** The widget that lists the viewers. */
		TSharedPtr<SBorder> WidgetContainer;

		/** The current viewer mode that should be shown in the UI. */
		ERenderPagesPageViewerMode ViewerMode;

		/** The last viewer mode that was shown in the UI. */
		ERenderPagesPageViewerMode CachedViewerMode;
	};
}
