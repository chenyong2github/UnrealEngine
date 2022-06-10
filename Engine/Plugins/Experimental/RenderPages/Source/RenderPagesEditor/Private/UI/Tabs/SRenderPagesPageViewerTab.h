// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"


namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	/**
	 * The page viewer tab.
	 */
	class SRenderPagesPageViewerTab : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesPageViewerTab) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor);
	};
}
