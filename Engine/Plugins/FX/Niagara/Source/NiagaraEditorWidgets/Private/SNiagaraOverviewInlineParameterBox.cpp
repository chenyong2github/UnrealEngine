// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewInlineParameterBox.h"

#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraStackEditorData.h"
#include "Styling/StyleColors.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"
#include "Widgets/SNiagaraParameterName.h"
#include "Widgets/SNiagaraPinTypeSelector.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SNiagaraOverviewInlineParameterBox"

void SNiagaraOverviewInlineParameterBox::Construct(const FArguments& InArgs, UNiagaraStackModuleItem& InStackModuleItem)
{
	ModuleItem = &InStackModuleItem;

	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SAssignNew(Container, SScrollBox)
		.Orientation(EOrientation::Orient_Horizontal)
		.ScrollBarThickness(FVector2D(2.f, 2.f))
		.ScrollBarVisibility(EVisibility::Collapsed)
		.ConsumeMouseWheel(EConsumeMouseWheel::Always)
	];

	ConstructChildren();
}

SNiagaraOverviewInlineParameterBox::~SNiagaraOverviewInlineParameterBox()
{
	for(FNiagaraLocalInputValueData& BoundInput : BoundFunctionInputs)
	{
		BoundInput.OnValueChanged.RemoveAll(this);
	}
}

FReply SNiagaraOverviewInlineParameterBox::NavigateToStack(FNiagaraLocalInputValueData LocalInputValueData)
{
	// even if we can't navigate, the button should consume the input
	if(!ModuleItem.IsValid())
	{
		return FReply::Handled();
	}
	
	ModuleItem->GetSystemViewModel()->GetSelectionViewModel()->UpdateSelectedEntries({ModuleItem.Get()}, {}, true);
	
	if(ensure(LocalInputValueData.StackSearchItems.Num() > 0))
	{
		ModuleItem->GetSystemViewModel()->GetSelectionViewModel()->GetSelectionStackViewModel()->SetSearchTextExternal(LocalInputValueData.StackSearchItems[0].Value);
		return FReply::Handled();
	}

	return FReply::Handled();
}

void SNiagaraOverviewInlineParameterBox::ConstructChildren()
{
	Container->ClearChildren();

	TArray<TSharedRef<SWidget>> ParameterWidgets = GenerateParameterWidgets();
	
	for (TSharedRef<SWidget> ParameterWidget : ParameterWidgets)
	{
		Container->AddSlot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(FMargin(2.f, 1.f))
		[
			ParameterWidget
		];
	}

	Container->SetVisibility(ParameterWidgets.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed);
}

TArray<TSharedRef<SWidget>> SNiagaraOverviewInlineParameterBox::GenerateParameterWidgets()
{
	TArray<TSharedRef<SWidget>> ParameterWidgets;

	// the stack module item should be valid at this point, but we check to make sure
	if(!ModuleItem.IsValid())
	{
		return ParameterWidgets;
	}
	
	UNiagaraScriptSource* ScriptSource = CastChecked<UNiagaraScriptSource>(ModuleItem->GetModuleNode().GetFunctionScriptSource());
	// in case the script source is no longer valid (i.e. the asset was deleted), we return immediately
	if(ScriptSource == nullptr)
	{
		return ParameterWidgets;
	}

	// we cache the sort order of the widgets we create to sort them after all widgets have been created
	TMap<TSharedRef<SWidget>, int32> SortOrder;

	// we go through all script variables of the source graph and check if a parameter is supposed to be displayed in the overview
	const TMap<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& AllMetaData = ScriptSource->NodeGraph->GetAllMetaData();
	for(const auto& ParameterMetaData : AllMetaData)
	{
		const UNiagaraScriptVariable* ScriptVariable = ParameterMetaData.Value;

		if(ScriptVariable == nullptr || !ScriptVariable->Metadata.bDisplayInOverviewStack)
		{
			continue;
		}

		TOptional<FNiagaraLocalInputValueData> LocalInputValueData = ModuleItem->GetLocalInputData(ParameterMetaData.Key, true);
		TOptional<FNiagaraDataInterfaceInput> InputDataInterface = ModuleItem->GetDataInterfaceForInput(ParameterMetaData.Key, true);

		// we only support inline display of local values and data interfaces currently
		// @TODO use the commented out line when DIs support this too.
		// if(!LocalInputValueData.IsSet() && !InputDataInterface.IsSet())
		if(!LocalInputValueData.IsSet())
		{
			continue;
		}

		TSharedPtr<SWidget> Widget;
		if(LocalInputValueData.IsSet())
		{
			Widget = GenerateParameterWidgetFromLocalValue(ScriptVariable, LocalInputValueData.GetValue());
		}
		else if(InputDataInterface.IsSet() && InputDataInterface->DataInterface.IsValid())
		{
			Widget = GenerateParameterWidgetFromDataInterface(ScriptVariable, InputDataInterface.GetValue());
		}		

		SortOrder.Add(Widget.ToSharedRef(), ScriptVariable->Metadata.InlineParameterSortPriority);
		ParameterWidgets.Add(Widget.ToSharedRef());
	}
	

	ParameterWidgets.Sort([&](TSharedRef<SWidget> WidgetA, TSharedRef<SWidget> WidgetB)
	{
		return SortOrder[WidgetA] < SortOrder[WidgetB];
	});
	
	return ParameterWidgets;
}

TSharedRef<SWidget> SNiagaraOverviewInlineParameterBox::GenerateParameterWidgetFromLocalValue(const UNiagaraScriptVariable* ScriptVariable, FNiagaraLocalInputValueData& LocalInputValueData)
{
	LocalInputValueData.OnValueChanged.RemoveAll(this);
	LocalInputValueData.OnValueChanged.AddSP(this, &SNiagaraOverviewInlineParameterBox::ConstructChildren);
	BoundFunctionInputs.Add(LocalInputValueData);
	FNiagaraTypeDefinition Type = ScriptVariable->Variable.GetType();
	FString Value = Type.ToStringCosmetic(LocalInputValueData.LocalData->GetStructMemory());
	const FLinearColor TypeColor = UEdGraphSchema_Niagara::GetTypeColor(Type);

	UTexture2D* Icon = nullptr;
	if (const UEnum* Enum = Type.GetEnum())
	{
		int32 EnumIndex = Enum->GetIndexByValue(*(int32*)LocalInputValueData.LocalData->GetStructMemory());
		if(ScriptVariable->Metadata.InlineParameterEnumOverrides.IsValidIndex(EnumIndex))
		{
			Value = ScriptVariable->Metadata.InlineParameterEnumOverrides[EnumIndex].OverrideName.ToString();
			Icon = ScriptVariable->Metadata.InlineParameterEnumOverrides[EnumIndex].IconOverride;
		}
	}

	TSharedPtr<SWidget> ParameterWidget;

	TSharedRef<SButton> ParameterWidgetButton = SNew(SButton)
	.Text(FText::FromString(Value))
	.ContentPadding(FMargin(-5, 0))
	.TextStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FTextBlockStyle>("NiagaraEditor.SystemOverview.InlineParameterText"))
	.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
	.OnClicked(this, &SNiagaraOverviewInlineParameterBox::NavigateToStack, LocalInputValueData);

	if(Icon != nullptr)
	{
		FSlateBrush& ImageBrush = ImageBrushes.Emplace_GetRef();
		ImageBrush.SetResourceObject(Icon);
		ImageBrush.ImageSize.X = 16;
		ImageBrush.ImageSize.Y = 16;
	
		TSharedRef<SImage> ParameterWidgetIcon= SNew(SImage).Image(&ImageBrush);
		ParameterWidgetButton->SetContent(ParameterWidgetIcon);
		//ParameterWidgetButton->SetBorderBackgroundColor(FStyleColors::Transparent);
		ParameterWidgetButton->SetButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"));
	}
	else
	{
		FLinearColor BorderColor = ScriptVariable->Metadata.bOverrideColor ? ScriptVariable->Metadata.InlineParameterColorOverride : TypeColor;
		ParameterWidgetButton->SetBorderBackgroundColor(BorderColor);				
	}			

	ParameterWidget = ParameterWidgetButton;

	FText			   IconToolTip = FText::GetEmpty();
	FSlateBrush const* IconBrush = FAppStyle::Get().GetBrush(TEXT("Kismet.AllClasses.VariableIcon"));
	FSlateColor        IconColor = FSlateColor(TypeColor);
	FString			   IconDocLink, IconDocExcerpt;
	FSlateBrush const* SecondaryIconBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));
	FSlateColor        SecondaryIconColor = IconColor;

	// we construct a tooltip widget that shows the parameter the value is associated with
	TSharedRef<SToolTip> TooltipWidget = SNew(SToolTip).Content()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(3.f)
		[
			SNew(SNiagaraParameterNameTextBlock)
			.IsReadOnly(true)
			.ParameterText(LOCTEXT("ParameterTooltipText", "Value for"))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SNiagaraIconWidget)
			.IconToolTip(IconToolTip)
			.IconBrush(IconBrush)
			.IconColor(IconColor)
			.DocLink(IconDocLink)
			.DocExcerpt(IconDocExcerpt)
			.SecondaryIconBrush(SecondaryIconBrush) 
			.SecondaryIconColor(SecondaryIconColor)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(3.f)
		[
			SNew(SNiagaraParameterNameTextBlock)
			.IsReadOnly(true)
			.ParameterText(FText::FromName(ScriptVariable->Variable.GetName()))
		]
	];

	ParameterWidget->SetToolTip(TooltipWidget);
	return ParameterWidget.ToSharedRef();
}

TSharedRef<SWidget> SNiagaraOverviewInlineParameterBox::GenerateParameterWidgetFromDataInterface(const UNiagaraScriptVariable* ScriptVariable, FNiagaraDataInterfaceInput& DataInterfaceInput)
{
	if(DataInterfaceInput.DataInterface.IsValid())
	{
		DataInterfaceInput.OnValueChanged.RemoveAll(this);
		DataInterfaceInput.OnValueChanged.AddSP(this, &SNiagaraOverviewInlineParameterBox::ConstructChildren);
		TArray<FNiagaraDataInterfaceError> Errors;
		TArray<FNiagaraDataInterfaceFeedback> Warnings;
		TArray<FNiagaraDataInterfaceFeedback> Infos;

		// these can be nullptr depending on context
		UNiagaraSystem* System = DataInterfaceInput.DataInterface->GetTypedOuter<UNiagaraSystem>();
		UNiagaraComponent* Component = DataInterfaceInput.DataInterface->GetTypedOuter<UNiagaraComponent>();

		//@TODO Finish this up.
		//DataInterfaceInput.DataInterface->GetInlineFeedback(System, Component, Errors, Warnings, Infos);

		TSharedPtr<SHorizontalBox> InfoBox = SNew(SHorizontalBox);
		for(FNiagaraDataInterfaceFeedback& Feedback : Infos)
		{
			InfoBox->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(Feedback.GetFeedbackSummaryText())
				.ToolTipText(Feedback.GetFeedbackText())
				.TextStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FTextBlockStyle>("NiagaraEditor.SystemOverview.InlineParameterText"))
			];			
		}

		return InfoBox.ToSharedRef();
	}

	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE
