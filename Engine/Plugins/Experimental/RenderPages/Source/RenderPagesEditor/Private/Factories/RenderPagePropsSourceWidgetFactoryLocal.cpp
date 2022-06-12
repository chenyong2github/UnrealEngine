// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/RenderPagePropsSourceWidgetFactoryLocal.h"
#include "UI/SRenderPagesPropsLocal.h"
#include "RenderPage/RenderPagePropsSource.h"


TSharedPtr<UE::RenderPages::Private::SRenderPagesPropsBase> UE::RenderPages::Private::FRenderPagePropsSourceWidgetFactoryLocal::CreateInstance(URenderPagePropsSourceBase* PropsSource, TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor)
{
	if (URenderPagePropsSourceLocal* InPropsSource = Cast<URenderPagePropsSourceLocal>(PropsSource))
	{
		return SNew(SRenderPagesPropsLocal, BlueprintEditor, InPropsSource);
	}
	return nullptr;
}
