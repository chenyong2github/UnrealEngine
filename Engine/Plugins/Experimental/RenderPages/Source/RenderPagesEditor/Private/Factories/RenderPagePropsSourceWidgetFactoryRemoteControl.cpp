// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/RenderPagePropsSourceWidgetFactoryRemoteControl.h"
#include "UI/SRenderPagesPropsRemoteControl.h"
#include "RenderPage/RenderPagePropsSource.h"


TSharedPtr<UE::RenderPages::Private::SRenderPagesPropsBase> UE::RenderPages::Private::FRenderPagePropsSourceWidgetFactoryRemoteControl::CreateInstance(URenderPagePropsSourceBase* PropsSource, TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor)
{
	if (URenderPagePropsSourceRemoteControl* InPropsSource = Cast<URenderPagePropsSourceRemoteControl>(PropsSource))
	{
		return SNew(SRenderPagesPropsRemoteControl, BlueprintEditor, InPropsSource);
	}
	return nullptr;
}
