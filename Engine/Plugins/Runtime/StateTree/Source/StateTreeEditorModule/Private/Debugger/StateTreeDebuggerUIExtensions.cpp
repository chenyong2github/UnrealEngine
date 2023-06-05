// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeDebuggerUIExtensions.h"
#include "Customizations/StateTreeEditorNodeUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "StateTreeConditionBase.h"
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
TSharedRef<SWidget> CreateStateWidget(TSharedPtr<IPropertyHandle> EnabledProperty)
{
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
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.MenuContent()
			[
				DebuggingActions.MakeWidget()
			]
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FStateTreeEditorStyle::Get().GetBrush("StateTreeEditor.DebugOptions"))
			]
		];

	return HeaderContentWidget;
}

TSharedRef<SWidget> CreateEditorNodeWidget(TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	using namespace EditorNodeUtils;
	
	bool bIsTask = false;
	bool bIsCondition = false;
	if (const FStructProperty* TmpStructProperty = CastField<FStructProperty>(InPropertyHandle->GetProperty()))
	{
		if (TmpStructProperty->Struct->IsChildOf(TBaseStructure<FStateTreeEditorNode>::Get()))
		{
			const UScriptStruct* ScriptStruct = nullptr;
			if (const FStateTreeEditorNode* Node = GetCommonNode(InPropertyHandle))
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
				FExecuteAction::CreateLambda([InPropertyHandle] { OnTaskEnableToggled(InPropertyHandle); }),
				FCanExecuteAction(),
				FGetActionCheckState::CreateLambda([InPropertyHandle]
					{
						return IsTaskDisabled(InPropertyHandle) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
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
				FExecuteAction::CreateLambda([InPropertyHandle]
					{
						OnConditionEvaluationModeChanged(InPropertyHandle, EStateTreeConditionEvaluationMode::Evaluated);
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([InPropertyHandle]
					{
						return GetConditionEvaluationMode(InPropertyHandle) == EStateTreeConditionEvaluationMode::Evaluated;
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
				FExecuteAction::CreateLambda([InPropertyHandle]
					{
						OnConditionEvaluationModeChanged(InPropertyHandle, EStateTreeConditionEvaluationMode::ForcedTrue);
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([InPropertyHandle]
					{
						return GetConditionEvaluationMode(InPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedTrue;
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
				FExecuteAction::CreateLambda([InPropertyHandle]
					{
						OnConditionEvaluationModeChanged(InPropertyHandle, EStateTreeConditionEvaluationMode::ForcedFalse);
					}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([InPropertyHandle]
					{
						return GetConditionEvaluationMode(InPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedFalse;
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
				.Visibility_Lambda([InPropertyHandle]
					{
						return IsTaskDisabled(InPropertyHandle) ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SNew(STextBlock)
					.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
					.ColorAndOpacity(FStyleColors::Foreground)
					.Text(LOCTEXT("LabelDisabled", "DISABLED"))
					.ToolTipText(LOCTEXT("DisabledTaskTooltip", "This task has been disabled."))
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
				.BorderBackgroundColor_Lambda([InPropertyHandle]
					{
						return GetConditionEvaluationMode(InPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedTrue
								   ? FStyleColors::AccentGreen.GetSpecifiedColor()
								   : FStyleColors::AccentRed.GetSpecifiedColor();
					})
				.Visibility_Lambda([InPropertyHandle]
					{
						return GetConditionEvaluationMode(InPropertyHandle) != EStateTreeConditionEvaluationMode::Evaluated ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(STextBlock)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
						.Text(LOCTEXT("ConditionForcedTrue", "TRUE"))
						.ToolTipText(LOCTEXT("ForcedTrueConditionTooltip", "This condition is not evaluated and result forced to 'true'."))
						.Visibility_Lambda([InPropertyHandle]
							{
								return GetConditionEvaluationMode(InPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedTrue ? EVisibility::Visible : EVisibility::Collapsed;
							})
					]
					+ SOverlay::Slot()
					[
						SNew(STextBlock)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
						.Text(LOCTEXT("ConditionForcedFalse", "FALSE"))
						.ToolTipText(LOCTEXT("ForcedFalseConditionTooltip", "This condition is not evaluated and result forced to 'false'."))
						.Visibility_Lambda([InPropertyHandle]
							{
								return GetConditionEvaluationMode(InPropertyHandle) == EStateTreeConditionEvaluationMode::ForcedFalse ? EVisibility::Visible : EVisibility::Collapsed;
							})
					]
				]
			]
		]
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
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.MenuContent()
			[
				DebuggingActions.MakeWidget()
			]
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FStateTreeEditorStyle::Get().GetBrush("StateTreeEditor.DebugOptions"))
			]
		];
}

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

} // namespace UE::StateTreeEditor::DebuggerExtensions

#undef LOCTEXT_NAMESPACE
