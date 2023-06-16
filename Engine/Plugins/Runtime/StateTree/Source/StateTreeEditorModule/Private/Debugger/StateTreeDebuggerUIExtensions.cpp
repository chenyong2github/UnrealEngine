// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeDebuggerUIExtensions.h"
#include "Customizations/StateTreeEditorNodeUtils.h"
#include "DetailLayoutBuilder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorStyle.h"
#include "StateTreeTaskBase.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SOverlay.h"


#define LOCTEXT_NAMESPACE "StateTreeEditor"

namespace UE::StateTreeEditor::DebuggerExtensions
{

void OnConditionEvaluationModeChanged(TSharedPtr<IPropertyHandle> StructProperty, const EStateTreeConditionEvaluationMode Mode)
{
	EditorNodeUtils::ModifyNodeInTransaction(LOCTEXT("OnEvaluationModeChanged", "Condition Evaluation Mode Changed"),
		StructProperty,
		[Mode](IPropertyHandle& StructProperty)
		{
			if (FStateTreeEditorNode* Node = EditorNodeUtils::GetMutableCommonNode(StructProperty))
			{
				if (FStateTreeConditionBase* ConditionBase = Node->Node.GetMutablePtr<FStateTreeConditionBase>())
				{
					ConditionBase->EvaluationMode = Mode;
				}
			}
		});
}

void OnTaskEnableToggled(TSharedPtr<IPropertyHandle> StructProperty)
{
	EditorNodeUtils::ModifyNodeInTransaction(LOCTEXT("OnTaskEnableToggled", "Toggled Task Enabled"),
		StructProperty,
		[](IPropertyHandle& StructProperty)
		{
			if (FStateTreeEditorNode* Node = EditorNodeUtils::GetMutableCommonNode(StructProperty))
			{
				if (FStateTreeTaskBase* TaskBase = Node->Node.GetMutablePtr<FStateTreeTaskBase>())
				{
					TaskBase->bTaskEnabled = !TaskBase->bTaskEnabled;
				}
			}
		});
}

void OnStateEnableToggled(TSharedPtr<IPropertyHandle> EnabledProperty)
{
	FScopedTransaction Transaction(LOCTEXT("OnStateEnableToggled", "Toggled State Enabled"));

	bool bEnabled = false;
	const FPropertyAccess::Result Result = EnabledProperty->GetValue(bEnabled);

	if (Result == FPropertyAccess::MultipleValues || bEnabled == false)
	{
		EnabledProperty->SetValue(true);
	}
	else
	{
		EnabledProperty->SetValue(false);
	}
}

bool HasStateBreakpoint(const TArray<TWeakObjectPtr<>>& StatesBeingCustomized, const UStateTreeEditorData* EditorData, const EStateTreeBreakpointType Type)
{
#if WITH_STATETREE_DEBUGGER
	if (EditorData == nullptr)
	{
		return false;
	}
	for (const TWeakObjectPtr<>& WeakStateObject : StatesBeingCustomized)
	{
		if (const UStateTreeState* State = static_cast<const UStateTreeState*>(WeakStateObject.Get()))
		{
			if (EditorData->HasBreakpoint(State->ID, Type))
			{
				return true;
			}
		}
	}
#endif // WITH_STATETREE_DEBUGGER
	return false;
}

void OnStateBreakpointToggled(const TArray<TWeakObjectPtr<>>& States, UStateTreeEditorData* EditorData, const EStateTreeBreakpointType Type)
{
#if WITH_STATETREE_DEBUGGER
	if (EditorData == nullptr || States.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ToggleStateBreakpoint", "Toggle State Breakpoint"));
	EditorData->Modify();

	for (const TWeakObjectPtr<>& WeakStateObject : States)
	{
		if (const UStateTreeState* State = static_cast<const UStateTreeState*>(WeakStateObject.Get()))
		{
			const bool bRemoved = EditorData->RemoveBreakpoint(State->ID, Type);
			if (bRemoved == false)
			{
				EditorData->AddBreakpoint(State->ID, Type);
			}
		}
	}
#endif // WITH_STATETREE_DEBUGGER
}

bool HasTaskBreakpoint(TSharedPtr<IPropertyHandle> StructProperty, const UStateTreeEditorData* EditorData, const EStateTreeBreakpointType Type)
{
#if WITH_STATETREE_DEBUGGER
	const FStateTreeEditorNode* Node = EditorNodeUtils::GetCommonNode(StructProperty);
	if (EditorData != nullptr && Node != nullptr)
	{
		return EditorData->HasBreakpoint(Node->ID, Type);
	}
#endif // WITH_STATETREE_DEBUGGER
	return false;
}

void OnTaskBreakpointToggled(TSharedPtr<IPropertyHandle> StructProperty, UStateTreeEditorData* EditorData, const EStateTreeBreakpointType Type)
{
#if WITH_STATETREE_DEBUGGER
	const FStateTreeEditorNode* Node = EditorNodeUtils::GetMutableCommonNode(StructProperty);
	if (EditorData != nullptr && Node != nullptr)
	{
		const FScopedTransaction Transaction(LOCTEXT("ToggleTaskBreakpoint", "Toggle Task Breakpoint"));
		EditorData->Modify();
		
		const bool bRemoved = EditorData->RemoveBreakpoint(Node->ID, Type);
		if (bRemoved == false)
		{
			EditorData->AddBreakpoint(Node->ID, Type);
		}
	}
#endif // WITH_STATETREE_DEBUGGER
}

TSharedRef<SWidget> CreateStateWidget(IDetailLayoutBuilder& DetailBuilder, UStateTreeEditorData* TreeData)
{
	TWeakObjectPtr<UStateTreeEditorData> WeakTreeData = TreeData;
	const TSharedPtr<IPropertyHandle> EnabledProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, bEnabled));
	TArray<TWeakObjectPtr<UObject>> StatesBeingCustomized = DetailBuilder.GetSelectedObjects();
	
	FMenuBuilder DebuggingActions(/*CloseWindowAfterMenuSelection*/ true, /*CommandList*/nullptr);

	DebuggingActions.BeginSection(FName("Options"), LOCTEXT("StateOptions", "State Debug Options"));
	DebuggingActions.AddMenuEntry
	(
		LOCTEXT("ToggleStateEnabled", "State Enabled"),
		LOCTEXT("ToggleStateEnabled_ToolTip", "Enables or disables selected state(s). StateTree must be recompiled to take effect."),
		FSlateIcon(),
		FUIAction
		(
			FExecuteAction::CreateLambda([EnabledProperty] { OnStateEnableToggled(EnabledProperty); }),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda([EnabledProperty]
				{
					bool bEnabled = false;
					const FPropertyAccess::Result Result = EnabledProperty->GetValue(bEnabled);
					if (Result == FPropertyAccess::MultipleValues)
					{
						return ECheckBoxState::Undetermined;
					}

					return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
		),
		NAME_None,
		EUserInterfaceActionType::Check
	);
	DebuggingActions.AddSeparator();
	DebuggingActions.AddMenuEntry
	(
		LOCTEXT("ToggleOnEnterStateBreakpoint", "Break on Enter"),
		LOCTEXT("ToggleOnEnterStateBreakpoint_ToolTip", "Enables or disables breakpoint when entering selected state(s)."),
		FSlateIcon(),
		FUIAction
		(
			FExecuteAction::CreateLambda([StatesBeingCustomized, WeakTreeData] { OnStateBreakpointToggled(StatesBeingCustomized, WeakTreeData.Get(), EStateTreeBreakpointType::OnEnter); }),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda([StatesBeingCustomized, WeakTreeData]
				{
					return HasStateBreakpoint(StatesBeingCustomized, WeakTreeData.Get(), EStateTreeBreakpointType::OnEnter) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
		),
		NAME_None,
		EUserInterfaceActionType::Check
	);
	DebuggingActions.AddMenuEntry
	(
		LOCTEXT("ToggleOnExitStateBreakpoint", "Break on Exit"),
		LOCTEXT("ToggleOnExitStateBreakpoint_ToolTip", "Enables or disables breakpoint when exiting selected state(s)."),
		FSlateIcon(),
		FUIAction
		(
			FExecuteAction::CreateLambda([StatesBeingCustomized, WeakTreeData] { OnStateBreakpointToggled(StatesBeingCustomized, WeakTreeData.Get(), EStateTreeBreakpointType::OnExit); }),
			FCanExecuteAction(),
			FGetActionCheckState::CreateLambda([StatesBeingCustomized, WeakTreeData]
				{
					return HasStateBreakpoint(StatesBeingCustomized, WeakTreeData.Get(), EStateTreeBreakpointType::OnExit) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
		),
		NAME_None,
		EUserInterfaceActionType::Check
	);
	DebuggingActions.EndSection();

	const TSharedRef<SHorizontalBox> HeaderContentWidget = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::OnDemand)
			
			// Disabled Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(6, 1))
				.BorderImage(FStateTreeEditorStyle::Get().GetBrush("StateTree.Node.Label"))
				.Visibility_Lambda([Property=EnabledProperty]
					{
						bool bEnabled = false;
						Property->GetValue(bEnabled);
						return bEnabled ? EVisibility::Collapsed : EVisibility::Visible;
					})
				[
					SNew(STextBlock)
					.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
					.ColorAndOpacity(FStyleColors::Foreground)
					.Text(LOCTEXT("LabelDisabled", "DISABLED"))
					.ToolTipText(LOCTEXT("DisabledStateTooltip", "This state has been disabled."))
				]
			]
			
			// Break on enter Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(6, 2))
				.BorderImage(FStateTreeEditorStyle::Get().GetBrush("StateTree.Node.Label"))
				.Visibility_Lambda([StatesBeingCustomized, WeakTreeData]
					{
						return HasStateBreakpoint(StatesBeingCustomized, WeakTreeData.Get(), EStateTreeBreakpointType::OnEnter) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SNew(STextBlock)
					.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
					.ColorAndOpacity(FStyleColors::Foreground)
					.Text(LOCTEXT("LabelOnEnterBreakpoint", "BREAK ON ENTER"))
					.ToolTipText(LOCTEXT("BreakOnEnterTaskTooltip", "Break when entering this task."))
				]
			]

			// Break on exit Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(6, 2))
				.BorderImage(FStateTreeEditorStyle::Get().GetBrush("StateTree.Node.Label"))
				.Visibility_Lambda([StatesBeingCustomized, WeakTreeData]
					{
						return HasStateBreakpoint(StatesBeingCustomized, WeakTreeData.Get(), EStateTreeBreakpointType::OnExit) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SNew(STextBlock)
					.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
					.ColorAndOpacity(FStyleColors::Foreground)
					.Text(LOCTEXT("LabelOnExitBreakpoint", "BREAK ON EXIT"))
					.ToolTipText(LOCTEXT("BreakOnExitTaskTooltip", "Break when exiting this task."))
				]
			]
			
		]
		// Debug Options
		+ SHorizontalBox::Slot()
		.Padding(0)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("DebugOptions", "Debug Options"))
			.HasDownArrow(false)
			.ContentPadding(0)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.MenuContent()
			[
				DebuggingActions.MakeWidget()
			]
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FStateTreeEditorStyle::Get().GetBrush("StateTreeEditor.DebugOptions"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];

	return HeaderContentWidget;
}

TSharedRef<SWidget> CreateEditorNodeWidget(const TSharedPtr<IPropertyHandle>& StructPropertyHandle, UStateTreeEditorData* TreeData)
{
	using namespace EditorNodeUtils;

	TWeakObjectPtr<UStateTreeEditorData> WeakTreeData = TreeData;
	
	bool bIsTask = false;
	bool bIsCondition = false;
	if (const FStructProperty* TmpStructProperty = CastField<FStructProperty>(StructPropertyHandle->GetProperty()))
	{
		if (TmpStructProperty->Struct->IsChildOf(TBaseStructure<FStateTreeEditorNode>::Get()))
		{
			const UScriptStruct* ScriptStruct = nullptr;
			if (const FStateTreeEditorNode* Node = GetCommonNode(StructPropertyHandle))
			{
				ScriptStruct = Node->Node.GetScriptStruct();
			}

			if (ScriptStruct != nullptr)
			{
				bIsTask = ScriptStruct->IsChildOf(TBaseStructure<FStateTreeTaskBase>::Get());
				bIsCondition = ScriptStruct->IsChildOf(TBaseStructure<FStateTreeConditionBase>::Get());
			}
		}
	}

	// In case there is no common Editor node or not associated to a condition or a task we don't need to add any widget.	
	if (bIsCondition == false && bIsTask == false)
	{
		return SNullWidget::NullWidget;
	}

	FMenuBuilder DebuggingActions(/*CloseWindowAfterMenuSelection*/ true, /*CommandList*/nullptr);

	if (bIsTask)
	{
		DebuggingActions.BeginSection(FName("Options"), LOCTEXT("TaskOptions", "Task Debug Options"));
		DebuggingActions.AddMenuEntry
		(
			LOCTEXT("ToggleTaskEnabled", "Task Enabled"),
			LOCTEXT("ToggleTaskEnabled_ToolTip", "Enables or disables selected task(s). StateTree must be recompiled to take effect."),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([StructPropertyHandle] { OnTaskEnableToggled(StructPropertyHandle); }),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([StructPropertyHandle]
					{
						return IsTaskDisabled(StructPropertyHandle) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
					})
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);
		DebuggingActions.AddSeparator();
		DebuggingActions.AddMenuEntry
		(
			LOCTEXT("ToggleOnEnterTaskBreakpoint", "Break on Enter"),
			LOCTEXT("ToggleOnEnterTaskBreakpoint_ToolTip", "Enables or disables breakpoint when entering selected task(s)."),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([StructPropertyHandle, WeakTreeData] { OnTaskBreakpointToggled(StructPropertyHandle, WeakTreeData.Get(), EStateTreeBreakpointType::OnEnter); }),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([StructPropertyHandle, WeakTreeData]
					{
						return HasTaskBreakpoint(StructPropertyHandle, WeakTreeData.Get(), EStateTreeBreakpointType::OnEnter) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);
		DebuggingActions.AddMenuEntry
		(
			LOCTEXT("ToggleOnExitTaskBreakpoint", "Break on Exit"),
			LOCTEXT("ToggleOnExitTaskBreakpoint_ToolTip", "Enables or disables breakpoint when exiting selected task(s)."),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([StructPropertyHandle, WeakTreeData] { OnTaskBreakpointToggled(StructPropertyHandle, WeakTreeData.Get(), EStateTreeBreakpointType::OnExit); }),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([StructPropertyHandle, WeakTreeData]
					{
						return HasTaskBreakpoint(StructPropertyHandle, WeakTreeData.Get(), EStateTreeBreakpointType::OnExit) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);
		DebuggingActions.EndSection();
	}

	if (bIsCondition)
	{
		DebuggingActions.BeginSection(FName("Options"), LOCTEXT("ConditionOptions", "Condition Debug Options"));
		DebuggingActions.AddMenuEntry
		(
			LOCTEXT("Evaluate", "Evaluate"),
			LOCTEXT("Evaluate_ToolTip", "Condition result is evaluated (normal behavior)"),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([StructPropertyHandle]
					{
						OnConditionEvaluationModeChanged(StructPropertyHandle, EStateTreeConditionEvaluationMode::Evaluated);
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([StructPropertyHandle]
					{
						return GetConditionEvaluationMode(StructPropertyHandle) == EStateTreeConditionEvaluationMode::Evaluated;
					})
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
		DebuggingActions.AddMenuEntry
		(
			LOCTEXT("ForceTrue", "Force True"),
			LOCTEXT("ForceTrue_ToolTip", "Result is forced to 'true' (condition is not evaluated)"),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([StructPropertyHandle]
					{
						OnConditionEvaluationModeChanged(StructPropertyHandle, EStateTreeConditionEvaluationMode::ForcedTrue);
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([StructPropertyHandle]
					{
						return GetConditionEvaluationMode(StructPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedTrue;
					})
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
		DebuggingActions.AddMenuEntry
		(
			LOCTEXT("ForceFalse", "Force False"),
			LOCTEXT("ForceFalse_ToolTip", "Result is forced to 'false' (condition is not evaluated)"),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([StructPropertyHandle]
					{
						OnConditionEvaluationModeChanged(StructPropertyHandle, EStateTreeConditionEvaluationMode::ForcedFalse);
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([StructPropertyHandle]
					{
						return GetConditionEvaluationMode(StructPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedFalse;
					})
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		DebuggingActions.EndSection();
	}


	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Right)
		[
			SNew(SHorizontalBox)
			.Clipping(EWidgetClipping::OnDemand)

			// Disabled Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(6, 2))
				.BorderImage(FStateTreeEditorStyle::Get().GetBrush("StateTree.Node.Label"))
				.Visibility_Lambda([StructPropertyHandle]
					{
						return IsTaskDisabled(StructPropertyHandle) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SNew(STextBlock)
					.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
					.ColorAndOpacity(FStyleColors::Foreground)
					.Text(LOCTEXT("LabelDisabled", "DISABLED"))
					.ToolTipText(LOCTEXT("DisabledTaskTooltip", "This task has been disabled."))
				]
			]

			// Break on enter Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(6, 2))
				.BorderImage(FStateTreeEditorStyle::Get().GetBrush("StateTree.Node.Label"))
				.Visibility_Lambda([StructPropertyHandle, WeakTreeData]
					{
						return HasTaskBreakpoint(StructPropertyHandle, WeakTreeData.Get(), EStateTreeBreakpointType::OnEnter) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SNew(STextBlock)
					.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
					.ColorAndOpacity(FStyleColors::Foreground)
					.Text(LOCTEXT("LabelOnEnterBreakpoint", "BREAK ON ENTER"))
					.ToolTipText(LOCTEXT("BreakOnEnterTaskTooltip", "Break when entering this task."))
				]
			]

			// Break on exit Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(6, 2))
				.BorderImage(FStateTreeEditorStyle::Get().GetBrush("StateTree.Node.Label"))
				.Visibility_Lambda([StructPropertyHandle, WeakTreeData]
					{
						return HasTaskBreakpoint(StructPropertyHandle, WeakTreeData.Get(), EStateTreeBreakpointType::OnExit) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SNew(STextBlock)
					.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
					.ColorAndOpacity(FStyleColors::Foreground)
					.Text(LOCTEXT("LabelOnExitBreakpoint", "BREAK ON EXIT"))
					.ToolTipText(LOCTEXT("BreakOnExitTaskTooltip", "Break when exiting this task."))
				]
			]

			// Force True / Force False Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(6, 2))
				.BorderImage(FStateTreeEditorStyle::Get().GetBrush("StateTree.Node.Label"))
				.BorderBackgroundColor_Lambda([StructPropertyHandle]
					{
						return GetConditionEvaluationMode(StructPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedTrue
								   ? FStyleColors::AccentGreen.GetSpecifiedColor()
								   : FStyleColors::AccentRed.GetSpecifiedColor();
					})
				.Visibility_Lambda([StructPropertyHandle]
					{
						return GetConditionEvaluationMode(StructPropertyHandle) != EStateTreeConditionEvaluationMode::Evaluated ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(STextBlock)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
						.Text(LOCTEXT("ConditionForcedTrue", "TRUE"))
						.ToolTipText(LOCTEXT("ForcedTrueConditionTooltip", "This condition is not evaluated and result forced to 'true'."))
						.Visibility_Lambda([StructPropertyHandle]
							{
								return GetConditionEvaluationMode(StructPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedTrue ? EVisibility::Visible : EVisibility::Collapsed;
							})
					]
					+ SOverlay::Slot()
					[
						SNew(STextBlock)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
						.Text(LOCTEXT("ConditionForcedFalse", "FALSE"))
						.ToolTipText(LOCTEXT("ForcedFalseConditionTooltip", "This condition is not evaluated and result forced to 'false'."))
						.Visibility_Lambda([StructPropertyHandle]
							{
								return GetConditionEvaluationMode(StructPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedFalse ? EVisibility::Visible : EVisibility::Collapsed;
							})
					]
				]
			]
		]
		+ SHorizontalBox::Slot()
		.Padding(0,0,6,0)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("DebugOptions", "Debug Options"))
			.HasDownArrow(false)
			.ContentPadding(0)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.MenuContent()
			[
				DebuggingActions.MakeWidget()
			]
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FStateTreeEditorStyle::Get().GetBrush("StateTreeEditor.DebugOptions"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
}

} // namespace UE::StateTreeEditor::DebuggerExtensions

#undef LOCTEXT_NAMESPACE
