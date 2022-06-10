// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintModes/RenderPagesApplicationModeBase.h"
#include "BlueprintModes/RenderPagesApplicationModes.h"
#include "IRenderPageCollectionEditor.h"


UE::RenderPages::Private::FRenderPagesApplicationModeBase::FRenderPagesApplicationModeBase(TSharedPtr<IRenderPageCollectionEditor> InRenderPagesEditor, FName InModeName)
	: FBlueprintEditorApplicationMode(InRenderPagesEditor, InModeName, FRenderPagesApplicationModes::GetLocalizedMode, false, false)
	, BlueprintEditorWeakPtr(InRenderPagesEditor)
{}

URenderPagesBlueprint* UE::RenderPages::Private::FRenderPagesApplicationModeBase::GetBlueprint() const
{
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		return BlueprintEditor->GetRenderPagesBlueprint();
	}
	return nullptr;
}

TSharedPtr<UE::RenderPages::IRenderPageCollectionEditor> UE::RenderPages::Private::FRenderPagesApplicationModeBase::GetBlueprintEditor() const
{
	return BlueprintEditorWeakPtr.Pin();
}
