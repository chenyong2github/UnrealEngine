// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MultiLineEditableText.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Font.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/SMultiLineEditableText.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UMultiLineEditableText

static FTextBlockStyle* DefaultMultiLineEditableTextStyle = nullptr;

UMultiLineEditableText::UMultiLineEditableText(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (DefaultMultiLineEditableTextStyle == nullptr)
	{
		// HACK: THIS SHOULD NOT COME FROM CORESTYLE AND SHOULD INSTEAD BE DEFINED BY ENGINE TEXTURES/PROJECT SETTINGS
		DefaultMultiLineEditableTextStyle = new FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));

		// Unlink UMG default colors from the editor settings colors.
		DefaultMultiLineEditableTextStyle->UnlinkColors();
	}

	WidgetStyle = *DefaultMultiLineEditableTextStyle;
	
	bIsReadOnly = false;
	SelectAllTextWhenFocused = false;
	ClearTextSelectionOnFocusLoss = true;
	RevertTextOnEscape = false;
	ClearKeyboardFocusOnCommit = true;
	AllowContextMenu = true;
	Clipping = EWidgetClipping::ClipToBounds;
	VirtualKeyboardDismissAction = EVirtualKeyboardDismissAction::TextChangeOnDismiss;
	AutoWrapText = true;

	if (!IsRunningDedicatedServer())
	{
		static ConstructorHelpers::FObjectFinder<UFont> RobotoFontObj(*UWidget::GetDefaultFontName());
		Font_DEPRECATED = FSlateFontInfo(RobotoFontObj.Object, 12, FName("Bold"));

		WidgetStyle.SetFont(Font_DEPRECATED);
	}
}

void UMultiLineEditableText::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyMultiLineEditableText.Reset();
}

TSharedRef<SWidget> UMultiLineEditableText::RebuildWidget()
{
	MyMultiLineEditableText = SNew(SMultiLineEditableText)
	.TextStyle(&WidgetStyle)
	.AllowContextMenu(AllowContextMenu)
	.IsReadOnly(bIsReadOnly)
//	.MinDesiredWidth(MinimumDesiredWidth)
//	.IsCaretMovedWhenGainFocus(IsCaretMovedWhenGainFocus)
	.SelectAllTextWhenFocused(SelectAllTextWhenFocused)
	.ClearTextSelectionOnFocusLoss(ClearTextSelectionOnFocusLoss)
	.RevertTextOnEscape(RevertTextOnEscape)
	.ClearKeyboardFocusOnCommit(ClearKeyboardFocusOnCommit)
//	.SelectAllTextOnCommit(SelectAllTextOnCommit)
//	.BackgroundImageSelected(BackgroundImageSelected ? TAttribute<const FSlateBrush*>(&BackgroundImageSelected->Brush) : TAttribute<const FSlateBrush*>())
//	.BackgroundImageSelectionTarget(BackgroundImageSelectionTarget ? TAttribute<const FSlateBrush*>(&BackgroundImageSelectionTarget->Brush) : TAttribute<const FSlateBrush*>())
//	.BackgroundImageComposing(BackgroundImageComposing ? TAttribute<const FSlateBrush*>(&BackgroundImageComposing->Brush) : TAttribute<const FSlateBrush*>())
//	.CaretImage(CaretImage ? TAttribute<const FSlateBrush*>(&CaretImage->Brush) : TAttribute<const FSlateBrush*>())
	.VirtualKeyboardOptions(VirtualKeyboardOptions)
	.VirtualKeyboardDismissAction(VirtualKeyboardDismissAction)
	.OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged, HandleOnTextChanged))
	.OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted, HandleOnTextCommitted))
	;
	
	return MyMultiLineEditableText.ToSharedRef();
}

void UMultiLineEditableText::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	TAttribute<FText> HintTextBinding = PROPERTY_BINDING(FText, HintText);

	MyMultiLineEditableText->SetTextStyle(&WidgetStyle);
	MyMultiLineEditableText->SetText(Text);
	MyMultiLineEditableText->SetHintText(HintTextBinding);
	MyMultiLineEditableText->SetAllowContextMenu(AllowContextMenu);
	MyMultiLineEditableText->SetIsReadOnly(bIsReadOnly);
	MyMultiLineEditableText->SetVirtualKeyboardDismissAction(VirtualKeyboardDismissAction);
	MyMultiLineEditableText->SetSelectAllTextWhenFocused(SelectAllTextWhenFocused);
	MyMultiLineEditableText->SetClearTextSelectionOnFocusLoss(ClearTextSelectionOnFocusLoss);
	MyMultiLineEditableText->SetRevertTextOnEscape(RevertTextOnEscape);
	MyMultiLineEditableText->SetClearKeyboardFocusOnCommit(ClearKeyboardFocusOnCommit);

//	MyMultiLineEditableText->SetColorAndOpacity(ColorAndOpacity);

	// TODO UMG Complete making all properties settable on SMultiLineEditableText

	Super::SynchronizeTextLayoutProperties(*MyMultiLineEditableText);
}


void UMultiLineEditableText::SetJustification(ETextJustify::Type InJustification)
{
	Super::SetJustification(InJustification);

	if (MyMultiLineEditableText.IsValid())
	{
		MyMultiLineEditableText->SetJustification(InJustification);
	}
}

FText UMultiLineEditableText::GetText() const
{
	if ( MyMultiLineEditableText.IsValid() )
	{
		return MyMultiLineEditableText->GetText();
	}

	return Text;
}

void UMultiLineEditableText::SetText(FText InText)
{
	Text = InText;
	if ( MyMultiLineEditableText.IsValid() )
	{
		MyMultiLineEditableText->SetText(Text);
	}
}

FText UMultiLineEditableText::GetHintText() const
{
	if (MyMultiLineEditableText.IsValid())
	{
		return MyMultiLineEditableText->GetHintText();
	}

	return HintText;
}

void UMultiLineEditableText::SetHintText(FText InHintText)
{
	HintText = InHintText;
	HintTextDelegate.Clear();
	if (MyMultiLineEditableText.IsValid())
	{
		TAttribute<FText> HintTextBinding = PROPERTY_BINDING(FText, HintText);
		MyMultiLineEditableText->SetHintText(HintTextBinding);
	}
}

void UMultiLineEditableText::SetIsReadOnly(bool bReadOnly)
{
	bIsReadOnly = bReadOnly;

	if ( MyMultiLineEditableText.IsValid() )
	{
		MyMultiLineEditableText->SetIsReadOnly(bIsReadOnly);
	}
}

void UMultiLineEditableText::SetWidgetStyle(const FTextBlockStyle& InWidgetStyle)
{
	WidgetStyle = InWidgetStyle;

	if (MyMultiLineEditableText.IsValid())
	{
		MyMultiLineEditableText->SetTextStyle(&WidgetStyle);
	}
}

void UMultiLineEditableText::HandleOnTextChanged(const FText& InText)
{
	OnTextChanged.Broadcast(InText);
}

void UMultiLineEditableText::HandleOnTextCommitted(const FText& InText, ETextCommit::Type CommitMethod)
{
	OnTextCommitted.Broadcast(InText, CommitMethod);
}

void UMultiLineEditableText::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerUE4Version() < VER_UE4_DEPRECATE_UMG_STYLE_OVERRIDES)
	{
		if (Font_DEPRECATED.HasValidFont())
		{
			WidgetStyle.Font = Font_DEPRECATED;
			Font_DEPRECATED = FSlateFontInfo();
		}
	}
}

#if WITH_EDITOR

const FText UMultiLineEditableText::GetPaletteCategory()
{
	return LOCTEXT("Input", "Input");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
