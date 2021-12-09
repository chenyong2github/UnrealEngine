// Copyright Epic Games, Inc. All Rights Reserved.

#include "SOptimusEditorGraphExplorerItem.h"

#include "OptimusActionStack.h"
#include "OptimusEditor.h"
#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphSchema.h"
#include "OptimusEditorGraphSchemaActions.h"
#include "OptimusNameValidator.h"

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusDeformer.h"
#include "OptimusNodeGraph.h"
#include "OptimusResourceDescription.h"
#include "OptimusVariableDescription.h"
#include "SOptimusDataTypeSelector.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"


class SResourceDataTypeSelectorHelper :
	public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SResourceDataTypeSelectorHelper ) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UOptimusResourceDescription* InResource, TAttribute<bool> bInIsReadOnly)	
	{
		WeakResource = InResource;

		ChildSlot
		[
			SNew(SOptimusDataTypeSelector)
			.CurrentDataType(this, &SResourceDataTypeSelectorHelper::OnGetDataType)
			.UsageMask(EOptimusDataTypeUsageFlags::Resource)
			.ViewType(SOptimusDataTypeSelector::EViewType::IconOnly)
			.bViewOnly(bInIsReadOnly.Get())			// FIXME: May be dynamic.
			.OnDataTypeChanged(this, &SResourceDataTypeSelectorHelper::OnDataTypeChanged)
		];
	}

private:
	FOptimusDataTypeHandle OnGetDataType() const
	{
		UOptimusResourceDescription *Resource = WeakResource.Get();
		if (Resource)
		{
			return Resource->DataType.Resolve();
		}
		else
		{
			return FOptimusDataTypeHandle();
		}
	}

	void OnDataTypeChanged(FOptimusDataTypeHandle InDataType)
	{
		// FIXME: Call command.
	}

	TWeakObjectPtr<UOptimusResourceDescription> WeakResource;
};


class SVariableDataTypeSelectorHelper : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVariableDataTypeSelectorHelper) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UOptimusVariableDescription* InVariable, TAttribute<bool> bInIsReadOnly)
	{
		WeakVariable = InVariable;

		ChildSlot
		    [SNew(SOptimusDataTypeSelector)
		            .CurrentDataType(this, &SVariableDataTypeSelectorHelper::OnGetDataType)
		            .UsageMask(EOptimusDataTypeUsageFlags::Variable)
		            .ViewType(SOptimusDataTypeSelector::EViewType::IconOnly)
		            .bViewOnly(bInIsReadOnly.Get()) // FIXME: May be dynamic.
		            .OnDataTypeChanged(this, &SVariableDataTypeSelectorHelper::OnDataTypeChanged)];
	}

private:
	FOptimusDataTypeHandle OnGetDataType() const
	{
		UOptimusVariableDescription* Variable = WeakVariable.Get();
		if (Variable)
		{
			return Variable->DataType.Resolve();
		}
		else
		{
			return FOptimusDataTypeHandle();
		}
	}

	void OnDataTypeChanged(FOptimusDataTypeHandle InDataType)
	{
		// FIXME: Call command.
	}

	TWeakObjectPtr<UOptimusVariableDescription> WeakVariable;
};



void SOptimusEditorGraphEplorerItem::Construct(
	const FArguments& InArgs, 
	FCreateWidgetForActionData* const InCreateData, 
	TWeakPtr<FOptimusEditor> InOptimusEditor
	)
{
	TSharedPtr<FEdGraphSchemaAction> GraphAction = InCreateData->Action;
	ActionPtr = InCreateData->Action;
	OptimusEditor = InOptimusEditor;

	TWeakPtr<FEdGraphSchemaAction> WeakGraphAction = GraphAction;
	const bool bIsReadOnlyCreate = InCreateData->bIsReadOnly;
	auto IsReadOnlyLambda = [WeakGraphAction, InOptimusEditor, bIsReadOnlyCreate]()
	{
		if (WeakGraphAction.IsValid() && InOptimusEditor.IsValid())
		{
		}

		return bIsReadOnlyCreate;
	};
	TAttribute<bool> bIsReadOnly = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda(IsReadOnlyLambda));

	FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			CreateIconWidget(InCreateData, bIsReadOnly)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(/* horizontal */ 3.0f, /* vertical */ 0.0f)
		[
			CreateTextSlotWidget(InCreateData, bIsReadOnly )
		]		
	];
}


TSharedRef<SWidget> SOptimusEditorGraphEplorerItem::CreateIconWidget(FCreateWidgetForActionData* const InCreateData, TAttribute<bool> InbIsReadOnly)
{
	TSharedPtr<FEdGraphSchemaAction> Action = InCreateData->Action;
	TSharedPtr<FOptimusEditor> Editor = OptimusEditor.Pin();
	
	TSharedPtr<SWidget> IconWidget;

	if (ensure(Action) && ensure(Editor))
	{
		IOptimusPathResolver* PathResolver = Editor->GetDeformerInterface<IOptimusPathResolver>();
		if (Action->GetTypeId() == FOptimusSchemaAction_Graph::StaticGetTypeId())
		{
			FOptimusSchemaAction_Graph* GraphAction = static_cast<FOptimusSchemaAction_Graph*>(Action.Get());
			UOptimusNodeGraph* NodeGraph = PathResolver->ResolveGraphPath(GraphAction->GraphPath);
			if (ensure(NodeGraph))
			{
				IconWidget = SNew(SImage)
					.Image(UOptimusEditorGraph::GetGraphTypeIcon(NodeGraph));
			}
		}
		else if (Action->GetTypeId() == FOptimusSchemaAction_Resource::StaticGetTypeId())
		{
			FOptimusSchemaAction_Resource* ResourceAction = static_cast<FOptimusSchemaAction_Resource*>(Action.Get());
			UOptimusResourceDescription* Resource = PathResolver->ResolveResource(ResourceAction->ResourceName);
			if (ensure(Resource))
			{
				
				IconWidget = SNew(SResourceDataTypeSelectorHelper, Resource, InbIsReadOnly);
			}
		}
		else if (Action->GetTypeId() == FOptimusSchemaAction_Variable::StaticGetTypeId())
		{
			FOptimusSchemaAction_Variable* VariableAction = static_cast<FOptimusSchemaAction_Variable*>(Action.Get());
			UOptimusVariableDescription* Variable = PathResolver->ResolveVariable(VariableAction->VariableName);
			if (ensure(Variable))
			{
				IconWidget = SNew(SVariableDataTypeSelectorHelper, Variable, InbIsReadOnly);
			}
		}
	}

	if (IconWidget.IsValid())
	{
		return IconWidget.ToSharedRef();
	}
	else
	{
		return SNullWidget::NullWidget;
	}
}

TSharedRef<SWidget> SOptimusEditorGraphEplorerItem::CreateTextSlotWidget(
	FCreateWidgetForActionData* const InCreateData, 
	TAttribute<bool> InbIsReadOnly
	)
{
	FOnVerifyTextChanged OnVerifyTextChanged;
	FOnTextCommitted OnTextCommitted;

	if (false /* Check for specific action rename options */)
	{
		
	}
	else
	{
		OnVerifyTextChanged.BindSP(this, &SOptimusEditorGraphEplorerItem::OnNameTextVerifyChanged);
		OnTextCommitted.BindSP(this, &SOptimusEditorGraphEplorerItem::OnNameTextCommitted);
	}

	if (InCreateData->bHandleMouseButtonDown)
	{
		MouseButtonDownDelegate = InCreateData->MouseButtonDownDelegate;
	}

	// FIXME: Tooltips

	TSharedPtr<SInlineEditableTextBlock> EditableTextElement = SNew(SInlineEditableTextBlock)
	    .Text(this, &SOptimusEditorGraphEplorerItem::GetDisplayText)
	    .HighlightText(InCreateData->HighlightText)
	    // .ToolTip(ToolTipWidget)
	    .OnVerifyTextChanged(OnVerifyTextChanged)
	    .OnTextCommitted(OnTextCommitted)
	    .IsSelected(InCreateData->IsRowSelectedDelegate)
	    .IsReadOnly(InbIsReadOnly);

	InlineRenameWidget = EditableTextElement.ToSharedRef();

	InCreateData->OnRenameRequest->BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);

	return InlineRenameWidget.ToSharedRef();
}


FText SOptimusEditorGraphEplorerItem::GetDisplayText() const
{
	const UOptimusEditorGraphSchema* Schema = GetDefault<UOptimusEditorGraphSchema>();
	if (MenuDescriptionCache.IsOutOfDate(Schema))
	{
		TSharedPtr< FEdGraphSchemaAction > GraphAction = ActionPtr.Pin();

		MenuDescriptionCache.SetCachedText(ActionPtr.Pin()->GetMenuDescription(), Schema);
	}

	return MenuDescriptionCache;
}


bool SOptimusEditorGraphEplorerItem::OnNameTextVerifyChanged(
	const FText& InNewText, 
	FText& OutErrorMessage
	)
{
	TSharedPtr<FEdGraphSchemaAction> Action = ActionPtr.Pin();
	TSharedPtr<FOptimusEditor> Editor = OptimusEditor.Pin();

	if (ensure(Action) && ensure(Editor))
	{
		FString NameStr = InNewText.ToString();

		FName OriginalName;
		const UObject* NamespaceObject = nullptr;
		const UClass* NamespaceClass = nullptr;

		if (Action->GetTypeId() == FOptimusSchemaAction_Graph::StaticGetTypeId())
		{
			const FOptimusSchemaAction_Graph *GraphAction = static_cast<FOptimusSchemaAction_Graph *>(Action.Get());
			UOptimusNodeGraph* NodeGraph = Editor->GetDeformerInterface<IOptimusPathResolver>()->ResolveGraphPath(GraphAction->GraphPath);
			if (ensure(NodeGraph))
			{
				OriginalName = NodeGraph->GetFName();
				NamespaceObject = Cast<UObject>(NodeGraph->GetCollectionOwner());
				NamespaceClass = UOptimusNodeGraph::StaticClass();
			}
		}
		else if (Action->GetTypeId() == FOptimusSchemaAction_Resource::StaticGetTypeId())
		{
			const FOptimusSchemaAction_Resource* ResourceAction = static_cast<FOptimusSchemaAction_Resource*>(Action.Get());
			OriginalName = ResourceAction->ResourceName;
			NamespaceObject = Editor->GetDeformer();
			NamespaceClass = UOptimusResourceDescription::StaticClass();
		}
		else if (Action->GetTypeId() == FOptimusSchemaAction_Variable::StaticGetTypeId())
		{
			const FOptimusSchemaAction_Variable* VariableAction = static_cast<FOptimusSchemaAction_Variable*>(Action.Get());
			OriginalName = VariableAction->VariableName;
			NamespaceObject = Editor->GetDeformer();
			NamespaceClass = UOptimusVariableDescription::StaticClass();
		}

		TSharedPtr<INameValidatorInterface> NameValidator = MakeShareable(new FOptimusNameValidator(NamespaceObject, NamespaceClass, OriginalName));

		EValidatorResult ValidatorResult = NameValidator->IsValid(NameStr);
		switch (ValidatorResult)
		{
		case EValidatorResult::Ok:
		case EValidatorResult::ExistingName:
			// These are fine, don't need to surface to the user, the rename can 'proceed' even if the name is the existing one
			break;
		default:
			OutErrorMessage = INameValidatorInterface::GetErrorText(NameStr, ValidatorResult);
			break;
		}

		return OutErrorMessage.IsEmpty();
	}
	else
	{
		return false;
	}
}


void SOptimusEditorGraphEplorerItem::OnNameTextCommitted(
	const FText& InNewText, 
	ETextCommit::Type InTextCommit
	)
{
	TSharedPtr<FEdGraphSchemaAction> Action = ActionPtr.Pin();
	TSharedPtr<FOptimusEditor> Editor = OptimusEditor.Pin();
	
	if (ensure(Action) && ensure(Editor))
	{
		FString NameStr = InNewText.ToString();

		if (Action->GetTypeId() == FOptimusSchemaAction_Graph::StaticGetTypeId())
		{
			FOptimusSchemaAction_Graph* GraphAction = static_cast<FOptimusSchemaAction_Graph*>(Action.Get());
			UOptimusNodeGraph* NodeGraph = Editor->GetDeformerInterface<IOptimusPathResolver>()->ResolveGraphPath(GraphAction->GraphPath);

			if (ensure(NodeGraph))
			{
				NodeGraph->GetCollectionOwner()->RenameGraph(NodeGraph, NameStr);
			}
		}
		else if (Action->GetTypeId() == FOptimusSchemaAction_Resource::StaticGetTypeId())
		{
			FOptimusSchemaAction_Resource* ResourceAction = static_cast<FOptimusSchemaAction_Resource*>(Action.Get());
			UOptimusResourceDescription* Resource = Editor->GetDeformer()->ResolveResource(ResourceAction->ResourceName);
			if (ensure(Resource))
			{
				Editor->GetDeformer()->RenameResource(Resource, FName(NameStr));
			}
		}
		else if (Action->GetTypeId() == FOptimusSchemaAction_Variable::StaticGetTypeId())
		{
			FOptimusSchemaAction_Variable* VariableAction = static_cast<FOptimusSchemaAction_Variable*>(Action.Get());
			UOptimusVariableDescription* Variable = Editor->GetDeformer()->ResolveVariable(VariableAction->VariableName);
			if (ensure(Variable))
			{
				Editor->GetDeformer()->RenameVariable(Variable, FName(NameStr));
			}
		}
	}
}
