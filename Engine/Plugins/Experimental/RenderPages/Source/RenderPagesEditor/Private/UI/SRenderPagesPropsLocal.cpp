// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesPropsLocal.h"
#include "RenderPage/RenderPageCollection.h"
#include "IRenderPageCollectionEditor.h"
#include "SlateOptMacros.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPropsLocal"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPropsLocal::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor, URenderPagePropsSourceLocal* InPropsSource)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;
	PropsSource = InPropsSource;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION


#undef LOCTEXT_NAMESPACE
