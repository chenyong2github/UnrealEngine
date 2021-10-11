// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MultiLineEditableTextBox.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Font.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Styling/UMGCoreStyle.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UMultiLineEditableTextBox

static FEditableTextBoxStyle* DefaultMultiLineEditableTextBoxStyle = nullptr;
static FTextBlockStyle* DefaultMultiLineEditableTextBoxTextStyle = nullptr;

#if WITH_EDITOR
static FEditableTextBoxStyle* EditorMultiLineEditableTextBoxStyle = nullptr;
static FTextBlockStyle* EditorMultiLineEditableTextBoxTextStyle = nullptr;
#endif 

UMultiLineEditableTextBox::UMultiLineEditableTextBox(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ForegroundColor_DEPRECATED = FLinearColor::Black;
	BackgroundColor_DEPRECATED = FLinearColor::White;
	ReadOnlyForegroundColor_DEPRECATED = FLinearColor::Black;

	if (DefaultMultiLineEditableTextBoxStyle == nullptr)
	{
		DefaultMultiLineEditableTextBoxStyle = new FEditableTextBoxStyle(FUMGCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"));

		// Unlink UMG default colors.
		DefaultMultiLineEditableTextBoxStyle->UnlinkColors();
	}

	if (DefaultMultiLineEditableTextBoxTextStyle == nullptr)
	{
		DefaultMultiLineEditableTextBoxTextStyle = new FTextBlockStyle(FUMGCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));

		// Unlink UMG default colors.
		DefaultMultiLineEditableTextBoxTextStyle->UnlinkColors();
	}
	
	WidgetStyle = *DefaultMultiLineEditableTextBoxStyle;
	TextStyle = *DefaultMultiLineEditableTextBoxTextStyle;

#if WITH_EDITOR 
	if (EditorMultiLineEditableTextBoxStyle == nullptr)
	{
		EditorMultiLineEditableTextBoxStyle = new FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"));

		// Unlink UMG Editor colors from the editor settings colors.
		EditorMultiLineEditableTextBoxStyle->UnlinkColors();
	}

	if (EditorMultiLineEditableTextBoxTextStyle == nullptr)
	{
		EditorMultiLineEditableTextBoxTextStyle = new FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"));

		// Unlink UMG Editor colors from the editor settings colors.
		EditorMultiLineEditableTextBoxTextStyle->UnlinkColors();
	}
	
	if (IsEditorWidget())
	{
		WidgetStyle = *EditorMultiLineEditableTextBoxStyle;
		TextStyle = *EditorMultiLineEditableTextBoxTextStyle;

		// The CDO isn't an editor widget and thus won't use the editor style, call post edit change to mark difference from CDO
		PostEditChange();
	}
#endif // WITH_EDITOR

	bIsReadOnly = false;
	AllowContextMenu = true;
	VirtualKeyboardDismissAction = EVirtualKeyboardDismissAction::TextChangeOnDismiss;
	AutoWrapText = true;

	if (!IsRunningDedicatedServer())
	{
		static ConstructorHelpers::FObjectFinder<UFont> RobotoFontObj(*UWidget::GetDefaultFontName());
		Font_DEPRECATED = FSlateFontInfo(RobotoFontObj.Object, 12, FName("Bold"));
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

void UMultiLineEditableTextBox::SetForegroundColor(FLinearColor color)
{
	if(MyEditableTextBlock.IsValid())
	{
		MyEditableTextBlock->SetForegroundColor(color);
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

	if ( GetLinkerUEVersion() < VER_UE4_DEPRECATE_UMG_STYLE_ASSETS )
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

	if (GetLinkerUEVersion() < VER_UE4_DEPRECATE_UMG_STYLE_OVERRIDES)
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
