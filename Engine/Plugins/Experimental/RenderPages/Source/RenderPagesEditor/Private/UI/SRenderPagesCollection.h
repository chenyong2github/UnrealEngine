// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/NotifyHook.h"


class URenderPageCollection;
class IDetailsView;

namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	/**
	 * A widget with which the user can modify the render pages collection.
	 * Doesn't contain any UI elements to modify the pages the collection contains.
	 */
	class SRenderPagesCollection : public SCompoundWidget, public FNotifyHook
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesCollection) {}
		SLATE_END_ARGS()

		virtual void Tick(const FGeometry&, const double, const float) override;
		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor);

		//~ Begin FNotifyHook Interface
		virtual void NotifyPreChange(FEditPropertyChain* PropertyAboutToChange) override;
		virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged) override;
		//~ End FNotifyHook Interface

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** A reference to the details view. */
		TSharedPtr<IDetailsView> DetailsView;

		/** The render page collection that's being edited in the details view. */
		TWeakObjectPtr<URenderPageCollection> DetailsViewRenderPageCollectionWeakPtr;
	};
}
