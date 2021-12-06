// Copyright Epic Games, Inc. All Rights Reserved.

#include "DetailsCustomizations/UVSelectToolCustomizations.h"
#include <Framework/MultiBox/MultiBoxBuilder.h>

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "UVEditorCommands.h"
#include "UVSelectTool.h"

#define LOCTEXT_NAMESPACE "UVSelectToolCustomizations"

TSharedRef<IDetailCustomization> FUVSelectToolActionPropertySetDetails::MakeInstance()
{
	return MakeShareable(new FUVSelectToolActionPropertySetDetails);
}


void FUVSelectToolActionPropertySetDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Replace the actions category with a toolbar of action buttons that look like the tool buttons.
	// When we move selection into the mode, we will move these to the same toolbar as the tools.
	DetailBuilder.HideCategory("Actions");

	// We need the tool so we can route the commands from the new toolbar to it.
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (!ensure(ObjectsBeingCustomized.Num() > 0))
	{
		return;
	}
	USelectToolActionPropertySet* ActionsObject = Cast<USelectToolActionPropertySet>(ObjectsBeingCustomized[0]);
	if (!ensure(ActionsObject))
	{
		return;
	}
	UUVSelectTool* Tool = ActionsObject->ParentTool.Get();
	if (!ensure(Tool))
	{
		return;
	}

	// We have to use some command list since we're creating the toolbar in this FUICommandInfo-based way.
	// We can't use the actual one that the tool uses because there's not a way to get to it through the
	// tool. This means that these can't yet be bound to hotkeys, but that's ok because they will be moving
	// out to mode level at some point anyway, and at that point we will have more flexibility.
	TSharedPtr<FUICommandList> CommandList = MakeShared<FUICommandList>();
	const FUVEditorCommands& CommandInfos = FUVEditorCommands::Get();

	auto MapAction = [CommandList, Tool](TSharedPtr<const FUICommandInfo> CommandInfo, ESelectToolAction Action)
	{
		CommandList->MapAction(
			CommandInfo,
			FExecuteAction::CreateLambda([Tool, Action]() {
				Tool->RequestAction(Action);
			}),
			FCanExecuteAction(),
			FGetActionCheckState(),
			FIsActionButtonVisible(),
			EUIActionRepeatMode::RepeatDisabled
		);
	};
	MapAction(CommandInfos.SewAction, ESelectToolAction::Sew);
	MapAction(CommandInfos.SplitAction, ESelectToolAction::Split);
	MapAction(CommandInfos.IslandConformalUnwrapAction, ESelectToolAction::IslandConformalUnwrap);

	// Finally build the actual toolbar
	FUniformToolBarBuilder ToolbarBuilder(CommandList, FMultiBoxCustomization(NAME_None));
	ToolbarBuilder.AddToolBarButton(CommandInfos.SewAction);
	ToolbarBuilder.AddToolBarButton(CommandInfos.SplitAction);
	ToolbarBuilder.AddToolBarButton(CommandInfos.IslandConformalUnwrapAction);

	DetailBuilder.EditCategory("EditActions")
		.AddCustomRow(LOCTEXT("ActionsSectionFilterString", "Edit Actions"), false)
		[
			ToolbarBuilder.MakeWidget()
		];
}

#undef LOCTEXT_NAMESPACE