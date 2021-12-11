// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewInlineParameterBox.h"

#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraEditorModule.h"
#include "Styling/StyleColors.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "Widgets/SNiagaraParameterName.h"
#include "Widgets/SNiagaraPinTypeSelector.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWrapBox.h"
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
	for(TWeakObjectPtr<UNiagaraStackFunctionInput> BoundInput : BoundFunctionInputs)
	{
		if(BoundInput.IsValid())
		{
			BoundInput->OnValueChanged().RemoveAll(this);
		}		
	}
	
	BoundFunctionInputs.Empty();
}

int32 SNiagaraOverviewInlineParameterBox::GetNumChildWidgets()
{
	return NumParameterWidgets;
}

FReply SNiagaraOverviewInlineParameterBox::NavigateToStack(TWeakObjectPtr<UNiagaraStackFunctionInput> FunctionInput)
{
	// even if we can't navigate, the button should consume the input
	if(!ModuleItem.IsValid() || !FunctionInput.IsValid() || FunctionInput->IsFinalized())
	{
		return FReply::Handled();
	}
	
	ModuleItem->GetSystemViewModel()->GetSelectionViewModel()->UpdateSelectedEntries({ModuleItem.Get()}, {}, true);

	TArray<UNiagaraStackEntry::FStackSearchItem> SearchItems;
	FunctionInput->GetSearchItems(SearchItems);
	if(ensure(SearchItems.Num() > 0))
	{
		ModuleItem->GetSystemViewModel()->GetSelectionViewModel()->GetSelectionStackViewModel()->SetSearchTextExternal(SearchItems[0].Value);
		return FReply::Handled();
	}

	return FReply::Handled();
}

void SNiagaraOverviewInlineParameterBox::ConstructChildren()
{
	Container->ClearChildren();
	NumParameterWidgets = 0;

	for(TWeakObjectPtr<UNiagaraStackFunctionInput> BoundInput : BoundFunctionInputs)
	{
		if(BoundInput.IsValid())
		{
			BoundInput->OnValueChanged().RemoveAll(this);
		}
	}
	BoundFunctionInputs.Empty();
	
	TArray<TSharedRef<SWidget>> ParameterWidgets = GenerateParameterWidgets();
	
	for (TSharedRef<SWidget> ParameterWidget : ParameterWidgets)
	{
		Container->AddSlot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.Padding(FMargin(2.f, 0.f))
		[
			ParameterWidget
		];

		NumParameterWidgets++;
	}

	SetVisibility(ParameterWidgets.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed);
	Container->SetEnabled(ModuleItem->GetIsEnabled());
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
	
	TArray<UNiagaraStackFunctionInput*> FunctionInputs = ModuleItem->GetInlineParameterInputs();
	for(UNiagaraStackFunctionInput* FunctionInput : FunctionInputs)
	{
		FunctionInput->OnValueChanged().RemoveAll(this);
		FunctionInput->OnValueChanged().AddSP(this, &SNiagaraOverviewInlineParameterBox::ConstructChildren);
		BoundFunctionInputs.Add(FunctionInput);
		
		TSharedPtr<SWidget> Widget;
		if(FunctionInput->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Local)
		{
			Widget = GenerateParameterWidgetFromLocalValue(FunctionInput);
		}
		// @todo currently data mode inputs won't be retrieved by GetInlineParameterInputs
		else if(FunctionInput->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Data)
		{
			Widget = GenerateParameterWidgetFromDataInterface(FunctionInput);
		}

		if(Widget == SNullWidget::NullWidget)
		{
			continue;
		}

		SortOrder.Add(Widget.ToSharedRef(), FunctionInput->GetInputMetaData()->InlineParameterSortPriority);
		ParameterWidgets.Add(Widget.ToSharedRef());
	}
	
	ParameterWidgets.Sort([&](TSharedRef<SWidget> WidgetA, TSharedRef<SWidget> WidgetB)
	{
		return SortOrder[WidgetA] < SortOrder[WidgetB];
	});
	
	return ParameterWidgets;
}

TSharedRef<SWidget> SNiagaraOverviewInlineParameterBox::GenerateParameterWidgetFromLocalValue(UNiagaraStackFunctionInput* FunctionInput)
{
	bool bGenerateProperWidget = true;
	
	FNiagaraTypeDefinition Type = FunctionInput->GetInputType();
	TOptional<FNiagaraVariableMetaData> InputMetaData = FunctionInput->GetInputMetaData();
	TSharedPtr<INiagaraEditorTypeUtilities> TypeUtilities = FNiagaraEditorModule::Get().GetTypeUtilities(Type);

	// we construct a variable with the data from our local struct memory because type utilities don't work on raw data
	FNiagaraVariable Variable(Type, NAME_None);
	Variable.SetData(FunctionInput->GetLocalValueStruct()->GetStructMemory());
	
	// by default we display type information. In some cases we have overrides, but even then we want to keep the type information as we continue using it in the tooltips
	const FText ValueText = TypeUtilities->GetStackDisplayText(Variable);
	FText DisplayedText = ValueText;
	const FLinearColor TypeColor = UEdGraphSchema_Niagara::GetTypeColor(Type);
	FLinearColor DisplayedColor = TypeColor;

	UTexture2D* Icon = nullptr;
	if (const UEnum* Enum = Type.GetEnum())
	{
		int32 EnumIndex = Enum->GetIndexByValue(*(int32*)FunctionInput->GetLocalValueStruct()->GetStructMemory());
		if(InputMetaData->InlineParameterEnumOverrides.IsValidIndex(EnumIndex))
		{
			const FNiagaraEnumParameterMetaData& EnumParameterMetaData = InputMetaData->InlineParameterEnumOverrides[EnumIndex];

			// we only want to use the override name if it has been set
			DisplayedText = !EnumParameterMetaData.OverrideName.IsNone() ? FText::FromName(EnumParameterMetaData.OverrideName) : DisplayedText;			
			Icon = EnumParameterMetaData.IconOverride;
			DisplayedColor = EnumParameterMetaData.bUseColorOverride ? EnumParameterMetaData.ColorOverride : DisplayedColor;
		}
	}
	else if(Type == FNiagaraTypeDefinition::GetBoolDef() && InputMetaData->bEnableBoolOverride)
	{
		const FNiagaraBoolParameterMetaData& BoolParameterMetaData = InputMetaData->InlineParameterBoolOverride;
		DisplayedColor = InputMetaData->bOverrideColor ? InputMetaData->InlineParameterColorOverride : DisplayedColor;

		int32 Val = *(int32*)FunctionInput->GetLocalValueStruct()->GetStructMemory();
		bool bParameterValue = Val == 0 ? false : true;

		if(bParameterValue == true)
		{
			DisplayedText = !BoolParameterMetaData.OverrideNameTrue.IsNone() ? FText::FromName(BoolParameterMetaData.OverrideNameTrue) : DisplayedText;			
			Icon = BoolParameterMetaData.IconOverrideTrue;
		}
		else if(bParameterValue == false)
		{
			DisplayedText = !BoolParameterMetaData.OverrideNameFalse.IsNone() ? FText::FromName(BoolParameterMetaData.OverrideNameFalse) : DisplayedText;			
			Icon = BoolParameterMetaData.IconOverrideFalse;
		}
		
		if(BoolParameterMetaData.DisplayMode != ENiagaraBoolDisplayMode::DisplayAlways)
		{
			// in case we only want to display the variable in one case, we make sure to test against the correct value
			bool bDisplayBool = BoolParameterMetaData.DisplayMode == ENiagaraBoolDisplayMode::DisplayIfTrue ? true : false;  
			if(bDisplayBool != bParameterValue)
			{
				bGenerateProperWidget = false;
			}			
		}		
	}
	else if(Type == FNiagaraTypeDefinition::GetColorDef())
	{
		DisplayedText = FText::FromName(FunctionInput->GetInputParameterHandle().GetName());
		DisplayedColor = *(FLinearColor*)FunctionInput->GetLocalValueStruct()->GetStructMemory();
	}

	TSharedPtr<SWidget> ParameterWidget;

	// we might have cases in which we don't want to display a parameter's data, such as if we only want to display a bool parameter when it is set to True
	if(bGenerateProperWidget)
	{
		TWeakObjectPtr<UNiagaraStackFunctionInput> FunctionInputWeak = FunctionInput;
		TSharedRef<SButton> ParameterWidgetButton = SNew(SButton)
		.Text(DisplayedText)
		.TextStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FTextBlockStyle>("NiagaraEditor.SystemOverview.InlineParameterText"))
		.ButtonStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FButtonStyle>("NiagaraEditor.SystemOverview.InlineParameterButton"))
		.ContentPadding(FMargin(0.f))
		.OnClicked(this, &SNiagaraOverviewInlineParameterBox::NavigateToStack, FunctionInputWeak);

		// if we have an icon available, we use it instead of any text. Border color is irrelevant here.
		if(Icon != nullptr)
		{
			FSlateBrush& ImageBrush = ImageBrushes.Emplace_GetRef();
			ImageBrush.SetResourceObject(Icon);
			ImageBrush.ImageSize.X = 16;
			ImageBrush.ImageSize.Y = 16;
	
			TSharedRef<SImage> ParameterWidgetIcon= SNew(SImage).Image(&ImageBrush);
			ParameterWidgetButton->SetContent(ParameterWidgetIcon);
			ParameterWidgetButton->SetButtonStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FButtonStyle>("NiagaraEditor.SystemOverview.InlineParameterButton.Transparent"));
		}
		// otherwise, we use text and set the border to its specified color (type, override, instance override color)
		else
		{
			// if we are using a custom color, we set the button style to use a white base in order to get rid of a tint
			if(DisplayedColor != TypeColor)
			{
				ParameterWidgetButton->SetButtonStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FButtonStyle>("NiagaraEditor.SystemOverview.InlineParameterButton.NoTint"));
			}
			
			ParameterWidgetButton->SetBorderBackgroundColor(DisplayedColor);
		}			

		ParameterWidget = ParameterWidgetButton;
	}
	else
	{
		return SNullWidget::NullWidget;
	}
	

	FText			   IconToolTip = FText::GetEmpty();
	FSlateBrush const* IconBrush = FAppStyle::Get().GetBrush(TEXT("Kismet.AllClasses.VariableIcon"));
	FSlateColor        IconColor = FSlateColor(TypeColor);
	FString			   IconDocLink, IconDocExcerpt;
	FSlateBrush const* SecondaryIconBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));
	FSlateColor        SecondaryIconColor = IconColor;

	// we construct a tooltip widget that shows the parameter the value is associated with
	TSharedRef<SToolTip> TooltipWidget = SNew(SToolTip).Content()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
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
				.ParameterText(FText::FromName(FunctionInput->GetInputParameterHandle().GetParameterHandleString()))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(3.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ParameterTooltipText", "Value: "))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(3.f)
			[
				SNew(STextBlock)
				.Text(ValueText)
			]
		]
	];

	ParameterWidget->SetToolTip(TooltipWidget);
	return ParameterWidget.ToSharedRef();
}

TSharedRef<SWidget> SNiagaraOverviewInlineParameterBox::GenerateParameterWidgetFromDataInterface(UNiagaraStackFunctionInput* FunctionInput)
{
	// if(DataInterfaceInput.DataInterface.IsValid())
	// {
	// 	DataInterfaceInput.OnValueChanged.RemoveAll(this);
	// 	DataInterfaceInput.OnValueChanged.AddSP(this, &SNiagaraOverviewInlineParameterBox::ConstructChildren);
	// 	TArray<FNiagaraDataInterfaceError> Errors;
	// 	TArray<FNiagaraDataInterfaceFeedback> Warnings;
	// 	TArray<FNiagaraDataInterfaceFeedback> Infos;
	//
	// 	// these can be nullptr depending on context
	// 	UNiagaraSystem* System = DataInterfaceInput.DataInterface->GetTypedOuter<UNiagaraSystem>();
	// 	UNiagaraComponent* Component = DataInterfaceInput.DataInterface->GetTypedOuter<UNiagaraComponent>();
	//
	// 	//@TODO Finish this up.
	// 	//DataInterfaceInput.DataInterface->GetInlineFeedback(System, Component, Errors, Warnings, Infos);
	//
	// 	TSharedPtr<SHorizontalBox> InfoBox = SNew(SHorizontalBox);
	// 	for(FNiagaraDataInterfaceFeedback& Feedback : Infos)
	// 	{
	// 		InfoBox->AddSlot()
	// 		.AutoWidth()
	// 		[
	// 			SNew(STextBlock)
	// 			.Text(Feedback.GetFeedbackSummaryText())
	// 			.ToolTipText(Feedback.GetFeedbackText())
	// 			.TextStyle(&FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FTextBlockStyle>("NiagaraEditor.SystemOverview.InlineParameterText"))
	// 		];			
	// 	}
	//
	// 	return InfoBox.ToSharedRef();
	//}

	return SNullWidget::NullWidget;
}

#undef LOCTEXT_NAMESPACE
