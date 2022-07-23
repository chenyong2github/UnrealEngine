// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/NotifyHook.h"


class IDetailsView;
class URenderPage;
class URenderPagesMoviePipelineRenderJob;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	/**
	 * A widget with which the user can modify the selected render page.
	 * Can only modify 1 render page at a time, this widget will show nothing when 0 or 2+ render pages are selected.
	 */
	class SRenderPagesPage : public SCompoundWidget, public FNotifyHook
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesPage) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor);

		// FNotifyHook interface
		virtual void NotifyPreChange(FEditPropertyChain* PropertyAboutToChange) override;
		virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged) override;
		// End of FNotifyHook interface

	private:
		/** Updates the details view. */
		void Refresh();

	private:
		void OnBatchRenderingStarted(URenderPagesMoviePipelineRenderJob* RenderJob) { Refresh(); }
		void OnBatchRenderingFinished(URenderPagesMoviePipelineRenderJob* RenderJob) { Refresh(); }

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** A reference to the details view. */
		TSharedPtr<IDetailsView> DetailsView;
	};
}
