// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelExposedField.h"

#include "Algo/Transform.h"
#include "EditorFontGlyphs.h"
#include "RCPanelWidgetRegistry.h"
#include "IDetailTreeNode.h"
#include "IRemoteControlUIModule.h"
#include "Layout/Visibility.h"
#include "RemoteControlField.h"
#include "RemoteControlPanelStyle.h"
#include "RemoteControlPreset.h"
#include "RemoteControlSettings.h"
#include "Styling/AppStyle.h"
#include "ScopedTransaction.h"
#include "SRCPanelTreeNode.h"
#include "Styling/SlateBrush.h"
#include "UObject/Object.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "RemoteControlPanel"

namespace ExposedFieldUtils
{
	TSharedRef<SWidget> CreateNodeValueWidget(const TSharedPtr<IDetailTreeNode>& Node)
	{
		FNodeWidgets NodeWidgets = Node->CreateNodeWidgets();

		TSharedRef<SHorizontalBox> FieldWidget = SNew(SHorizontalBox);

		if (NodeWidgets.ValueWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					NodeWidgets.ValueWidget.ToSharedRef()
				];
		}
		else if (NodeWidgets.WholeRowWidget)
		{
			FieldWidget->AddSlot()
				.Padding(FMargin(3.0f, 2.0f))
				.AutoWidth()
				[
					NodeWidgets.WholeRowWidget.ToSharedRef()
				];
		}

		return FieldWidget;
	}
}

TSharedPtr<SRCPanelTreeNode> SRCPanelExposedField::MakeInstance(const FGenerateWidgetArgs& Args)
{
	return SNew(SRCPanelExposedField, StaticCastSharedPtr<FRemoteControlField>(Args.Entity), Args.ColumnSizeData, Args.WidgetRegistry).Preset(Args.Preset).EditMode(Args.bIsInEditMode);
}

void SRCPanelExposedField::Construct(const FArguments& InArgs, TWeakPtr<FRemoteControlField> InField, FRCColumnSizeData InColumnSizeData, TWeakPtr<FRCPanelWidgetRegistry> InWidgetRegistry)
{
	WeakField = MoveTemp(InField);

	ColumnSizeData = MoveTemp(InColumnSizeData);
	WidgetRegistry = MoveTemp(InWidgetRegistry);
	
	if (TSharedPtr<FRemoteControlField> FieldPtr = WeakField.Pin())
	{
		Initialize(FieldPtr->GetId(), InArgs._Preset.Get(), InArgs._EditMode);
	
		CachedLabel = FieldPtr->GetLabel();
		EntityId = FieldPtr->GetId();
		
		if (FieldPtr->FieldType == EExposedFieldType::Property)
		{
			ConstructPropertyWidget();
		}
		else
		{
			ConstructFunctionWidget();
		}
	}
}

void SRCPanelExposedField::GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const
{
	OutChildren.Append(ChildWidgets);
}

SRCPanelTreeNode::ENodeType SRCPanelExposedField::GetRCType() const
{
	return SRCPanelTreeNode::Field;
}

FName SRCPanelExposedField::GetFieldLabel() const
{
	return CachedLabel;
}

EExposedFieldType SRCPanelExposedField::GetFieldType() const
{
	if (TSharedPtr<FRemoteControlField> Field = WeakField.Pin())
	{
		return Field->FieldType;
	}
	
	return EExposedFieldType::Invalid;
}

void SRCPanelExposedField::SetIsHovered(bool bInIsHovered)
{
	bIsHovered = bInIsHovered;
}

void SRCPanelExposedField::Refresh()
{
	if (TSharedPtr<FRemoteControlField> Field = WeakField.Pin())
	{
		CachedLabel = Field->GetLabel();
		
		if (Field->FieldType == EExposedFieldType::Property)
		{
			ConstructPropertyWidget();
		}
		else if (Field->FieldType == EExposedFieldType::Function)
		{
			ConstructFunctionWidget();
		}
	}
}

void SRCPanelExposedField::GetBoundObjects(TSet<UObject*>& OutBoundObjects) const
{
	if (TSharedPtr<FRemoteControlField> Field = WeakField.Pin())
	{
		OutBoundObjects.Append(Field->GetBoundObjects());
	}
}

TSharedRef<SWidget> SRCPanelExposedField::ConstructWidget()
{
	if (TSharedPtr<FRemoteControlField> Field = WeakField.Pin())
	{
		// For the moment, just use the first object.
		TArray<UObject*> Objects = Field->GetBoundObjects();
		if (GetFieldType() == EExposedFieldType::Property && Objects.Num() > 0)
		{
			if (TSharedPtr<FRCPanelWidgetRegistry> Registry = WidgetRegistry.Pin())
			{
				if (TSharedPtr<IDetailTreeNode> Node = Registry->GetObjectTreeNode(Objects[0], Field->FieldPathInfo.ToPathPropertyString(), ERCFindNodeMethod::Path))
				{
					TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
					Node->GetChildren(ChildNodes);
					ChildWidgets.Reset(ChildNodes.Num());

					for (const TSharedRef<IDetailTreeNode>& ChildNode : ChildNodes)
					{
						ChildWidgets.Add(SNew(SRCPanelFieldChildNode, ChildNode, ColumnSizeData));
					}

					return MakeFieldWidget(ExposedFieldUtils::CreateNodeValueWidget(MoveTemp(Node)));
				}
			}
		}

		return MakeFieldWidget(CreateInvalidWidget());
	}

	return MakeFieldWidget(SNullWidget::NullWidget);
}

TSharedRef<SWidget> SRCPanelExposedField::MakeFieldWidget(const TSharedRef<SWidget>& InWidget)
{
	FText WarningMessage;

	if (GetDefault<URemoteControlSettings>()->bDisplayInEditorOnlyWarnings)
	{
		bool bIsEditorOnly = false,
            bIsEditableInPackaged = true,
            bIsCallableInPackaged = true;
		
		if (const TSharedPtr<FRemoteControlField> RCField = GetRemoteControlField().Pin())
		{
			bIsEditorOnly = RCField->IsEditorOnly();
			if (RCField->FieldType == EExposedFieldType::Property)
			{
				bIsEditableInPackaged = StaticCastSharedPtr<FRemoteControlProperty>(RCField)->IsEditableInPackaged();
			}
			else
			{
				bIsCallableInPackaged = StaticCastSharedPtr<FRemoteControlFunction>(RCField)->IsCallableInPackaged();
			}
		}

		FTextBuilder Builder;
		if (bIsEditorOnly)
		{
			Builder.AppendLine(LOCTEXT("EditorOnlyWarning", "This field will be unavailable in packaged projects."));
		}

		if (!bIsEditableInPackaged)
		{
			Builder.AppendLine(LOCTEXT("NotEditableInPackagedWarning", "This property will not be editable in packaged projects."));
		}

		if (!bIsCallableInPackaged)
		{
			Builder.AppendLine(LOCTEXT("NotCallableInPackagedWarning", "This function will not be callable in packaged projects."));
		}
		
		WarningMessage = Builder.ToText();
	}
	
	return CreateEntityWidget(InWidget, WarningMessage);
}

void SRCPanelExposedField::ConstructPropertyWidget()
{
	ChildSlot.AttachWidget(ConstructWidget());
}

void SRCPanelExposedField::ConstructFunctionWidget()
{
	TSharedPtr<SRCPanelExposedField> ExposedFieldWidget;

	URemoteControlPreset* RCPreset = Preset.Get();
	if (!RCPreset)
	{
		return;
	}

	if (const TSharedPtr<FRemoteControlFunction> RCFunction = RCPreset->GetExposedEntity<FRemoteControlFunction>(EntityId).Pin())
	{
		if (RCFunction->GetFunction() && RCFunction->GetBoundObjects().Num())
		{
			if (TSharedPtr<FRCPanelWidgetRegistry> Registry = WidgetRegistry.Pin())
			{
				Registry->Refresh(RCFunction->FunctionArguments);

				TArray<TSharedPtr<SRCPanelFieldChildNode>> ChildNodes;
				for (TFieldIterator<FProperty> It(RCFunction->GetFunction()); It; ++It)
				{
					const bool Param = It->HasAnyPropertyFlags(CPF_Parm);
					const bool OutParam = It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ConstParm);
					const bool ReturnParam = It->HasAnyPropertyFlags(CPF_ReturnParm);

					if (!Param || OutParam || ReturnParam)
					{
						continue;
					}

					if (TSharedPtr<IDetailTreeNode> PropertyNode = Registry->GetStructTreeNode(RCFunction->FunctionArguments, It->GetFName().ToString(), ERCFindNodeMethod::Name))
					{
						ChildNodes.Add(SNew(SRCPanelFieldChildNode, PropertyNode.ToSharedRef(), ColumnSizeData));
					}
				}

				ChildSlot.AttachWidget(MakeFieldWidget(ConstructCallFunctionButton()));
				ChildWidgets = ChildNodes;
			}
			

			return;
		}
	}

	ChildSlot.AttachWidget(ConstructWidget());
}

TSharedRef<SWidget> SRCPanelExposedField::ConstructCallFunctionButton(bool bIsEnabled)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Left)
		.Padding(2.0f)
		.AutoHeight()
		[
			SNew(SButton)
			.IsEnabled(bIsEnabled)
			.VAlign(VAlign_Center)
			.OnClicked_Raw(this, &SRCPanelExposedField::OnClickFunctionButton)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CallFunctionLabel", "Call Function"))
			]
		];
}

FReply SRCPanelExposedField::OnClickFunctionButton()
{
	FScopedTransaction FunctionTransaction(LOCTEXT("CallExposedFunction", "Called a function through preset."));
	FEditorScriptExecutionGuard ScriptGuard;

	if (URemoteControlPreset* RCPreset = Preset.Get())
	{
		if (TSharedPtr<FRemoteControlFunction> Function = RCPreset->GetExposedEntity<FRemoteControlFunction>(EntityId).Pin())
		{
			for (UObject* Object : Function->GetBoundObjects())
			{
				if (Function->FunctionArguments && Function->FunctionArguments->IsValid())
				{
					Object->Modify();
					Object->ProcessEvent(Function->GetFunction(), Function->FunctionArguments->GetStructMemory());
				}
				else
				{
					ensureAlwaysMsgf(false, TEXT("Function default arguments could not be resolved."));
				}
			}
		}
	}

	return FReply::Handled();
}

void SRCPanelFieldChildNode::Construct(const FArguments& InArgs, const TSharedRef<IDetailTreeNode>& InNode, FRCColumnSizeData InColumnSizeData)
{
	TArray<TSharedRef<IDetailTreeNode>> ChildNodes;
	InNode->GetChildren(ChildNodes);

	Algo::Transform(ChildNodes, ChildrenNodes, [InColumnSizeData](const TSharedRef<IDetailTreeNode>& ChildNode) { return SNew(SRCPanelFieldChildNode, ChildNode, InColumnSizeData); });

	ColumnSizeData = InColumnSizeData;

	FNodeWidgets Widgets = InNode->CreateNodeWidgets();
	FMakeNodeWidgetArgs Args;
	Args.NameWidget = Widgets.NameWidget;
	Args.ValueWidget = ExposedFieldUtils::CreateNodeValueWidget(InNode);

	ChildSlot
	[
		MakeNodeWidget(Args)
	];
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanel*/
