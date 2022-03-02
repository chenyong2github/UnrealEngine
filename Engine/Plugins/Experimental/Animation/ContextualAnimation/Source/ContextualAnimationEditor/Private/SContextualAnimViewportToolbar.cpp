// Copyright Epic Games, Inc. All Rights Reserved.

#include "SContextualAnimViewportToolbar.h"
#include "SContextualAnimViewport.h"
#include "PreviewProfileController.h"
#include "ContextualAnimAssetEditorCommands.h"

#define LOCTEXT_NAMESPACE "ContextualAnimViewportToolBar"

void SContextualAnimViewportToolBar::Construct(const FArguments& InArgs, TSharedPtr<SContextualAnimViewport> InViewport)
{
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments().AddRealtimeButton(false).PreviewProfileController(MakeShared<FPreviewProfileController>()), InViewport);
}

TSharedRef<SWidget> SContextualAnimViewportToolBar::GenerateShowMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();

	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder ShowMenuBuilder(bInShouldCloseWindowAfterMenuSelection, ViewportRef->GetCommandList());
	{
		ShowMenuBuilder.AddSubMenu(
			LOCTEXT("ShowMenu_IKTargetsDrawSubMenu", "IK Targets"),
			LOCTEXT("ShowMenu_IKTargetsDrawSubMenuToolTip", "IK Targets Drawing Options"),
			FNewMenuDelegate::CreateLambda([](FMenuBuilder& SubMenuBuilder)
				{
					const FContextualAnimAssetEditorCommands& Commands = FContextualAnimAssetEditorCommands::Get();

					SubMenuBuilder.BeginSection("IKTargets", LOCTEXT("ShowMenu_IKTargetsLabel", "IK Targets"));
					{
						SubMenuBuilder.AddMenuEntry(Commands.ShowIKTargetsDrawSelected);
						SubMenuBuilder.AddMenuEntry(Commands.ShowIKTargetsDrawAll);
						SubMenuBuilder.AddMenuEntry(Commands.ShowIKTargetsDrawNone);
					}
					SubMenuBuilder.EndSection();
				})
		);
	}

	return ShowMenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE