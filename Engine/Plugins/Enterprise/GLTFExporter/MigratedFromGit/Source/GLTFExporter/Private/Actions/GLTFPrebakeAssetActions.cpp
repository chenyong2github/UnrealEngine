// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Actions/GLTFPrebakeAssetActions.h"
#include "Actions/GLTFEditorStyle.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "GLTFPrebakeAssetActions"

FGLTFPrebakeAssetActions::FGLTFPrebakeAssetActions(const TSharedRef<IAssetTypeActions>& OriginalActions)
	: OriginalActions(OriginalActions)
{
}

bool FGLTFPrebakeAssetActions::HasActions(const TArray<UObject*>& InObjects) const
{
	return true;
}

void FGLTFPrebakeAssetActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	OriginalActions->GetActions(InObjects, Section);
	GetPrebakeActions(InObjects, Section);
}

void FGLTFPrebakeAssetActions::GetPrebakeActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	const TArray<FWeakObjectPtr> Objects(InObjects);

	Section.AddMenuEntry(
		"MenuEntry_Prebake",
		LOCTEXT("MenuEntry_Prebake", "Prebake glTF Export"),
		LOCTEXT("MenuEntry_PrebakeTooltip", "Creates a prebaked version of this material for glTF export."),
		FSlateIcon(FGLTFEditorStyle::Get().GetStyleSetName(), "Icon16x16"),
		FUIAction(FExecuteAction::CreateSP(this, &FGLTFPrebakeAssetActions::OnPrebake, Objects))
		);
}

void FGLTFPrebakeAssetActions::OnPrebake(TArray<FWeakObjectPtr> Objects) const
{
	for (const FWeakObjectPtr& Object : Objects)
	{
		if (UMaterialInterface* Material = Cast<UMaterialInterface>(Object.Get()))
		{
			// TODO
		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif
