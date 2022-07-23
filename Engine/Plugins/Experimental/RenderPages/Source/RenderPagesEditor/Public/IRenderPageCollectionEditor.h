// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditor.h"


class URenderPagesMoviePipelineRenderJob;
class URenderPage;
class URenderPageCollection;
class URenderPagesBlueprint;

namespace UE::RenderPages::Private
{
	class FRenderPagesBlueprintEditorToolbar;
}


namespace UE::RenderPages
{
	/**
	 * The render page editor interface.
	 */
	class IRenderPageCollectionEditor : public FBlueprintEditor
	{
	public:
		/** @return The render pages blueprint currently being edited in this editor. */
		virtual class URenderPagesBlueprint* GetRenderPagesBlueprint() const = 0;

		/** @return The render pages collection instance. */
		virtual URenderPageCollection* GetInstance() const = 0;

		/** @return The render pages toolbar. */
		virtual TSharedPtr<Private::FRenderPagesBlueprintEditorToolbar> GetRenderPagesToolbarBuilder() = 0;

		/** Returns whether it is currently rendering or playing (so changes in the level and such should be ignored). */
		virtual bool IsCurrentlyRenderingOrPlaying() const { return IsBatchRendering() || IsPreviewRendering() || IsValid(GEditor->PlayWorld); }

		/** Returns whether it can currently render (like a preview render or a batch render) or not. */
		virtual bool CanCurrentlyRender() const { return !IsCurrentlyRenderingOrPlaying(); }

		/** Returns whether it is currently batch rendering or not. */
		virtual bool IsBatchRendering() const = 0;

		/** Returns the current batch rendering job, or null if it's not currently batch rendering. */
		virtual URenderPagesMoviePipelineRenderJob* GetBatchRenderJob() const = 0;

		/** Returns whether it is currently preview rendering or not. */
		virtual bool IsPreviewRendering() const = 0;

		/** Returns the current preview rendering job, or null if it's not currently rendering a preview. */
		virtual URenderPagesMoviePipelineRenderJob* GetPreviewRenderJob() const = 0;

		/** Sets the current preview rendering job, set it to null if it's not currently rendering a preview. */
		virtual void SetPreviewRenderJob(URenderPagesMoviePipelineRenderJob* Job) = 0;

		/** Marks the editing asset as modified. */
		virtual void MarkAsModified() = 0;

		/** Get the currently selected render pages. */
		virtual TArray<URenderPage*> GetSelectedRenderPages() const = 0;

		/** Set the selected render pages. */
		virtual void SetSelectedRenderPages(const TArray<URenderPage*>& RenderPages) = 0;

		DECLARE_MULTICAST_DELEGATE(FOnRenderPagesChanged);
		virtual FOnRenderPagesChanged& OnRenderPagesChanged() { return OnRenderPagesChangedDelegate; }

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnRenderPageCreated, URenderPage*);
		virtual FOnRenderPageCreated& OnRenderPageCreated() { return OnRenderPageCreatedDelegate; }

		DECLARE_MULTICAST_DELEGATE(FOnRenderPagesSelectionChanged);
		virtual FOnRenderPagesSelectionChanged& OnRenderPagesSelectionChanged() { return OnRenderPagesSelectionChangedDelegate; }

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnRenderPagesBatchRenderingStarted, URenderPagesMoviePipelineRenderJob*);
		virtual FOnRenderPagesBatchRenderingStarted& OnRenderPagesBatchRenderingStarted() { return OnRenderPagesBatchRenderingStartedDelegate; }

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnRenderPagesBatchRenderingFinished, URenderPagesMoviePipelineRenderJob*);
		virtual FOnRenderPagesBatchRenderingFinished& OnRenderPagesBatchRenderingFinished() { return OnRenderPagesBatchRenderingFinishedDelegate; }

	private:
		/** The delegate for when render pages in the collection changed. */
		FOnRenderPagesChanged OnRenderPagesChangedDelegate;

		/** The delegate for when a render page is created. */
		FOnRenderPageCreated OnRenderPageCreatedDelegate;

		/** The delegate for when the selection of render pages changed. */
		FOnRenderPagesSelectionChanged OnRenderPagesSelectionChangedDelegate;

		/** The delegate for when batch rendering of pages started. */
		FOnRenderPagesBatchRenderingStarted OnRenderPagesBatchRenderingStartedDelegate;

		/** The delegate for when batch rendering of pages ended, successful or not. */
		FOnRenderPagesBatchRenderingFinished OnRenderPagesBatchRenderingFinishedDelegate;
	};
}
