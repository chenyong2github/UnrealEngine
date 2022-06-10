// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/IRenderPagePropsSourceWidgetFactory.h"


namespace UE::RenderPages::Private
{
	/**
	 * The factory that creates props source instances for the props type "remote control".
	 */
	class RENDERPAGESEDITOR_API FRenderPagePropsSourceWidgetFactoryRemoteControl final : public IRenderPagePropsSourceWidgetFactory
	{
	public:
		virtual TSharedPtr<SRenderPagesPropsBase> CreateInstance(URenderPagePropsSourceBase* PropsSource, TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor) override;
	};
}
