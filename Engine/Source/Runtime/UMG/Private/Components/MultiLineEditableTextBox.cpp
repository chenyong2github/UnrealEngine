// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Components/MultiLineEditableTextBox.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Font.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UMultiLineEditableTextBox

static FEditableTextBoxStyle* DefaultMultiLineEditableTextBoxStyle = nullptr;
static FTextBlockStyle* DefaultMultiLineEditableTextBoxTextStyle = nullptr;

UMultiLineEditableTextBox::UMultiLineEditableTextBox(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ForegroundColor_DEPRECATED = FLinearColor::Black;
	BackgroundColor_DEPRECATED = FLinearColor::White;
	ReadOnlyForegroundColor_DEPRECATED = FLinearColor::Black;

	if (DefaultMultiLineEditableTextBoxStyle == nullptr)
	{
		// HACK: THIS SHOULD NOT COME FROM CORESTYLE AND SHOULD INSTEAD BE DEFINED BY ENGINE TEXTURES/PROJECT SETTINGS
		DefaultMultiLineEditableTextBoxStyle = new FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"));

		// Unlink UMG default colors from the editor settings colors.
		DefaultMultiLineEditableTextBoxStyle->UnlinkColors();
	}

	if (DefaultMultiLineEditableTextBoxTextStyle == nullptr)
	{
		// HACK: THIS SHOULD NOT COME FROM CORESTYLE AND SHOULD INSTEAD BE DEFINED BY ENGINE TEXTURES/PROJECT SETTINGS
		DefaultMultiLineEditableTextBoxTextStyle = new FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));

		// Unlink UMG default colors from the editor settings colors.
		DefaultMultiLineEditableTextBoxTextStyle->UnlinkColors();
	}
	
	WidgetStyle = *DefaultMultiLineEditableTextBoxStyle;
	TextStyle = *DefaultMultiLineEditableTextBoxTextStyle;

	bIsReadOnly = false;
	AllowContextMenu = true;
	VirtualKeyboardDismissAction = EVirtualKeyboardDismissAction::TextChangeOnDismiss;
	AutoWrapText = true;

	if (!IsRunningDedicatedServer())
	{
		static ConstructorHelpers::FObjectFinder<UFont> RobotoFontObj(*UWidget::GetDefaultFontName());
		Font_DEPRECATED = FSlateFontInfo(RobotoFontObj.Object, 12, FName("Bold"));

		WidgetStyle.SetFont(Font_DEPRECATED);
		WidgetStyle.SetForegroundColor(ForegroundColor_DEPRECATED);
		WidgetStyle.SetBackgroundColor(BackgroundColor_DEPRECATED);
		WidgetStyle.SetReadOnlyForegroundColor(ReadOnlyForegroundColor_DEPRECATED);
	}
}

void UMultiLineEditableTextBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyEditableTextBlock.Reset();
}

TSharedRef<SWidget> UMultiLineEditableTextBox::RebuildWidget()
{
	MyEditableTextBlock = SNew(SMultiLineEditableTextBox)
		.Style(&WidgetStyle)
		.TextStyle(&TextStyle)
		.AllowContextMenu(AllowContextMenu)
		.IsReadOnly(bIsReadOnly)
//		.MinDesiredWidth(MinimumDesiredWidth)
//		.Padding(Padding)
//		.IsCaretMovedWhenGainFocus(IsCaretMovedWhenGainFocus)
//		.SelectAllTextWhenFocused(SelectAllTextWhenFocused)
//		.RevertTextOnEscape(RevertTextOnEscape)
//		.ClearKeyboardFocusOnCommit(ClearKeyboardFocusOnCommit)
//		.SelectAllTextOnCommit(SelectAllTextOnCommit)
		.VirtualKeyboardOptions(VirtualKeyboardOptions)
		.VirtualKeyboardDismissAction(VirtualKeyboardDismissAction)
		.OnTextChanged(BIND_UOBJECT_DELEGATE(FOnTextChanged, HandleOnTextChanged))
		.OnTextCommitted(BIND_UOBJECT_DELEGATE(FOnTextCommitted, HandleOnTextCommitted))
		;

	return MyEditableTextBlock.ToSharedRef();
}

void UMultiLineEditableTextBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	TAttribute<FText> HintTextBinding = PROPERTY_BINDING(FText, HintText);

	MyEditableTextBlock->SetStyle(&WidgetStyle);
	MyEditableTextBlock->SetText(Text);
	MyEditableTextBlock->SetHintText(HintTextBinding);
	MyEditableTextBlock->SetAllowContextMenu(AllowContextMenu);
	MyEditableTextBlock->SetIsReadOnly(bIsReadOnly);
	MyEditableTextBlock->SetVirtualKeyboardDismissAction(VirtualKeyboardDismissAction);

//	MyEditableTextBlock->SetIsPassword(IsPassword);
//	MyEditableTextBlock->SetColorAndOpacity(ColorAndOpacity);

	// TODO UMG Complete making all properties settable on SMultiLineEditableTextBox

	Super::SynchronizeTextLayoutProperties(*MyEditableTextBlock);
}

void UMultiLineEditableTextBox::SetJustification(ETextJustify::Type InJustification)
{
	Super::SetJustification(InJustification);

	if (MyEditableTextBlock.IsValid())
	{
		MyEditableTextBlock->SetJustification(InJustification);
	}
}

FText UMultiLineEditableTextBox::GetText() const
{
	if ( MyEditableTextBlock.IsValid() )
	{
		return MyEditableTextBlock->GetText();
	}

	return Text;
}

void UMultiLineEditableTextBox::SetText(FText InText)
{
	Text = InText;
	if ( MyEditableTextBlock.IsValid() )
	{
		MyEditableTextBlock->SetText(Text);
	}
}

FText UMultiLineEditableTextBox::GetHintText() const
{
	if (MyEditableTextBlock.IsValid())
	{
		return MyEditableTextBlock->GetHintText();
	}

	return HintText;
}

void UMultiLineEditableTextBox::SetHintText(FText InHintText)
{
	HintText = InHintText;
	HintTextDelegate.Clear();
	if ( MyEditableTextBlock.IsValid() )
	{
		TAttribute<FText> HintTextBinding = PROPERTY_BINDING(FText, HintText);
		MyEditableTextBlock->SetHintText(HintTextBinding);
	}
}

void UMultiLineEditableTextBox::SetError(FText InError)
{
	if ( MyEditableTextBlock.IsValid() )
	{
		MyEditableTextBlock->SetError(InError);
	}
}

void UMultiLineEditableTextBox::SetIsReadOnly(bool bReadOnly)
{
	bIsReadOnly = bReadOnly;

	if ( MyEditableTextBlock.IsValid() )
	{
		MyEditableTextBlock->SetIsReadOnly(bIsReadOnly);
	}
}

void UMultiLineEditableTextBox::SetTextStyle(const FTextBlockStyle& InTextStyle)
{
	TextStyle = InTextStyle;

	if (MyEditableTextBlock.IsValid())
	{
		MyEditableTextBlock->SetTextStyle(&TextStyle);
	}
}

void UMultiLineEditableTextBox::HandleOnTextChanged(const FText& InText)
{
	OnTextChanged.Broadcast(InText);
}

void UMultiLineEditableTextBox::HandleOnTextCommitted(const FText& InText, ETextCommit::Type CommitMethod)
{
	OnTextCommitted.Broadcast(InText, CommitMethod);
}

void UMultiLineEditableTextBox::PostLoad()
{
	Super::PostLoad();

	if ( GetLinkerUE4Version() < VER_UE4_DEPRECATE_UMG_STYLE_ASSETS )
	{
		if ( Style_DEPRECATED != nullptr )
		{
			const FEditableTextBoxStyle* StylePtr = Style_DEPRECATED->GetStyle<FEditableTextBoxStyle>();
			if ( StylePtr != nullptr )
			{
				WidgetStyle = *StylePtr;
			}

			Style_DEPRECATED = nullptr;
		}
	}

	if (GetLinkerUE4Version() < VER_UE4_DEPRECATE_UMG_STYLE_OVERRIDES)
	{
		if (Font_DEPRECATED.HasValidFont())
		{
			WidgetStyle.Font = Font_DEPRECATED;
			Font_DEPRECATED = FSlateFontInfo();
		}

		if (ForegroundColor_DEPRECATED != FLinearColor::Black)
		{
			WidgetStyle.ForegroundColor = ForegroundColor_DEPRECATED;
			ForegroundColor_DEPRECATED = FLinearColor::Black;
		}

		if (BackgroundColor_DEPRECATED != FLinearColor::White)
		{
			WidgetStyle.BackgroundColor = BackgroundColor_DEPRECATED;
			BackgroundColor_DEPRECATED = FLinearColor::White;
		}

		if (ReadOnlyForegroundColor_DEPRECATED != FLinearColor::Black)
		{
			WidgetStyle.ReadOnlyForegroundColor = ReadOnlyForegroundColor_DEPRECATED;
			ReadOnlyForegroundColor_DEPRECATED = FLinearColor::Black;
		}
	}
}

#if WITH_EDITOR

const FText UMultiLineEditableTextBox::GetPaletteCategory()
{
	return LOCTEXT("Input", "Input");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
