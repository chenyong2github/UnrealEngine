// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"


class SBorder;
class URenderPage;
class URenderPagePropsSourceBase;
class URenderPagesMoviePipelineRenderJob;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	/**
	 * A widget with which the user can modify the props (like the remote control field values) of the selected render page.
	 * Can only modify the props of 1 render page at a time, this widget will show nothing when 0 or 2+ render pages are selected.
	 */
	class SRenderPagesProps : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesProps) {}
		SLATE_END_ARGS()

		virtual void Tick(const FGeometry&, const double, const float) override;
		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor);

		/** Refreshes the content of this widget. */
		void Refresh();

	private:
		void OnBatchRenderingStarted(URenderPagesMoviePipelineRenderJob* RenderJob) { Refresh(); }
		void OnBatchRenderingFinished(URenderPagesMoviePipelineRenderJob* RenderJob) { Refresh(); }

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** The widget that lists the property rows. */
		TSharedPtr<SBorder> WidgetContainer;

		/** The props source that's being shown in this widget. */
		TWeakObjectPtr<URenderPagePropsSourceBase> WidgetPropsSourceWeakPtr;
	};
}
