
#include "SNiagaraPinTypeSelector.h"

#include "NiagaraEditorStyle.h"
#include "Widgets/Images/SLayeredImage.h"
#include "Widgets/SNiagaraParameterPanel.h"
#include "NiagaraEditorUtilities.h"

#define LOCTEXT_NAMESPACE "NiagaraPinTypeSelector"

void SNiagaraPinTypeSelector::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPin)
{
	ensure(InGraphPin);
	Pin = InGraphPin;

	ChildSlot
	[
		SAssignNew(SelectorButton, SComboButton)
		.ContentPadding(0.f)
		.HasDownArrow(false)
		.ButtonStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.Module.Pin.TypeSelector.Button")
		.ToolTipText(GetTooltipText())
		.OnGetMenuContent(this, &SNiagaraPinTypeSelector::GetMenuContent)
		.ButtonContent()
		[
			SNew(SImage)
			.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Module.Pin.TypeSelector"))
		]
	];
}

TSharedRef<SWidget> SNiagaraPinTypeSelector::GetMenuContent()
{
	TArray<TWeakObjectPtr<UNiagaraGraph>> Graphs;
	UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(Pin->GetOwningNode());
	Graphs.Add(NiagaraNode->GetNiagaraGraph());

	TSharedRef<SNiagaraAddParameterMenu2> MenuWidget = SNew(SNiagaraAddParameterMenu2, Graphs)
	.OnCollectCustomActions_Lambda([this](FGraphActionListBuilderBase& OutActions, bool& bOutCreateRemainingActions)
	{
		return FNiagaraEditorUtilities::CollectPinTypeChangeActions(OutActions, bOutCreateRemainingActions, Pin);
	})
	.OnAllowMakeType_UObject(NiagaraNode, &UNiagaraNode::AllowNiagaraTypeForPinTypeChange, Pin)
	.IsParameterRead(true);

	SelectorButton->SetMenuContentWidgetToFocus(MenuWidget->GetSearchBox());
	return MenuWidget;
}

FText SNiagaraPinTypeSelector::GetTooltipText() const
{
	return LOCTEXT("PinTypeSelectorTooltip", "Choose a different type for this pin");
}

#undef LOCTEXT_NAMESPACE