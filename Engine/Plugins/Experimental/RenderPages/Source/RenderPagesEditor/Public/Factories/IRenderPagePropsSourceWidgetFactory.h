// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IRenderPageCollectionEditor.h"


class URenderPagePropsSourceBase;

namespace UE::RenderPages::Private
{
	class SRenderPagesPropsBase;
}


namespace UE::RenderPages
{
	/**
	 * The props source widget factory interface.
	 * Implementations should create a widget for the given props source.
	 */
	class RENDERPAGESEDITOR_API IRenderPagePropsSourceWidgetFactory
	{
	public:
		virtual ~IRenderPagePropsSourceWidgetFactory() = default;
		virtual TSharedPtr<Private::SRenderPagesPropsBase> CreateInstance(URenderPagePropsSourceBase* PropsSource, TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor) { return nullptr; }
	};
}
