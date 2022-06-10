// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/SRenderPagesPropsBase.h"
#include "RenderPage/RenderPagePropsSource.h"


namespace UE::RenderPages
{
	class IRenderPageCollectionEditor;
}


namespace UE::RenderPages::Private
{
	/**
	 * The page props implementation for local properties.
	 */
	class SRenderPagesPropsLocal : public SRenderPagesPropsBase
	{
	public:
		SLATE_BEGIN_ARGS(SRenderPagesPropsLocal) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor, URenderPagePropsSourceLocal* InPropsSource);

	private:
		/** A reference to the BP Editor that owns this collection. */
		TWeakPtr<IRenderPageCollectionEditor> BlueprintEditorWeakPtr;

		/** The props source control. */
		TObjectPtr<URenderPagePropsSourceLocal> PropsSource;
	};
}
