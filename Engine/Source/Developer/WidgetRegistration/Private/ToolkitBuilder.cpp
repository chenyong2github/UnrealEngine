

#include "ToolkitBuilder.h"

#include "Widgets/SBoxPanel.h"
#include "SPrimaryButton.h"
#include "ToolbarRegistrationArgs.h"
#include "ToolElementRegistry.h"
#include "Framework/Commands/UICommandList.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "ToolkitBuilder"

FToolElementRegistry FToolkitBuilder::ToolRegistry = FToolElementRegistry::Get();

bool FEditablePalette::IsInPalette(const FName CommandName) const
{
	return this->PaletteCommandNameArray.Contains(CommandName.ToString());
}

FEditablePalette::FEditablePalette(TSharedPtr<FUICommandInfo> InLoadToolPaletteAction,
	TArray<FString>& InPaletteCommandNameArray,
	TSharedPtr<FUICommandInfo> InAddToPaletteAction,
	TSharedPtr<FUICommandInfo> InRemoveFromPaletteAction) :
	FToolPalette(InLoadToolPaletteAction, {}),
	AddToPaletteAction(InAddToPaletteAction),
	RemoveFromPaletteAction(InRemoveFromPaletteAction),
	PaletteCommandNameArray(InPaletteCommandNameArray)
{
}

FToolkitBuilder::FToolkitBuilder(
	FName InToolbarCustomizationName,
	TSharedPtr<FUICommandList> InToolkitCommandList) :
	FToolElementRegistrationArgs(EToolElement::Toolkit),
	ToolbarCustomizationName(InToolbarCustomizationName),
	ToolkitCommandList(InToolkitCommandList)
{
	ResetWidget();
}

TSharedPtr<FToolBarBuilder> FToolkitBuilder::GetLoadPaletteToolbar()
{
	return LoadPaletteToolBarBuilder;
}

TSharedRef<SWidget> FToolkitBuilder::CreateToolbarWidget() const
{
	return ToolRegistry.GenerateWidget(VerticalToolbarElement.ToSharedRef());
}

void FToolkitBuilder::AddPalette(TSharedPtr<FEditablePalette> Palette)
{
	EditablePalettesArray.Add(Palette.ToSharedRef());
	AddPalette(StaticCastSharedRef<FToolPalette>(Palette.ToSharedRef()) );
}


void FToolkitBuilder::AddPalette(TSharedPtr<FToolPalette> Palette)
{
	for (TSharedRef<FButtonArgs> Button : Palette->PaletteActions)
	{
		PaletteCommandNameToButtonArgsMap.Add(Button->Command->GetCommandName().ToString(), Button);
	}

	LoadToolPaletteCommandList->MapAction(
				Palette->LoadToolPaletteAction,
				FExecuteAction::CreateSP(SharedThis(this),
				&FToolkitBuilder::TogglePalette,
				Palette),
				FCanExecuteAction::CreateLambda([] { return true; }),
				FGetActionCheckState::CreateSP(SharedThis(this),
					&FToolkitBuilder::IsActiveToolPalette,
					Palette->LoadToolPaletteAction->GetCommandName())
									 	);
	LoadPaletteToolBarBuilder->AddToolBarButton(Palette->LoadToolPaletteAction);
}

ECheckBoxState FToolkitBuilder::IsActiveToolPalette(FName CommandName) const
{
	return (ActivePalette &&
			ActivePalette->LoadToolPaletteAction->GetCommandName() == CommandName) ?
				ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FToolkitBuilder::UpdateEditablePalette(FEditablePalette& Palette)
{
	Palette.PaletteActions.Empty();

	for (const FString& Key : Palette.PaletteCommandNameArray)
	{
		if (const TSharedPtr<FButtonArgs>* FoundButton = PaletteCommandNameToButtonArgsMap.Find(Key))
		{
			const TSharedRef<FButtonArgs> Button = (*FoundButton).ToSharedRef();
			Palette.PaletteActions.Add(Button);
		}
	}
}

void FToolkitBuilder::UpdateWidget()
{
	for (const TSharedRef<FEditablePalette> EditablePalette : EditablePalettesArray)
	{
		UpdateEditablePalette(*EditablePalette);		
	}
	
}

void FToolkitBuilder::ToggleCommandInPalette(TSharedRef<FEditablePalette> Palette, FString CommandNameString)
{
	if (!Palette->PaletteCommandNameArray.Contains(CommandNameString))
	{	
		Palette->PaletteCommandNameArray.Add(CommandNameString);
	}
	else
	{
		Palette->PaletteCommandNameArray.Remove(CommandNameString);		
	}

	UpdateEditablePalette(*Palette);

	// if the active Palette is the Palette to which we are toggling an action,
	// recreate it to load the new state after the toggle
	if ( ActivePalette == Palette )
	{
		CreatePalette(Palette);
	}
}

bool FToolkitBuilder::HasActivePalette() const
{
	return ActivePalette != nullptr;
}


void FToolkitBuilder::TogglePalette(TSharedPtr<FToolPalette> Palette)
{
	const FName CommandName = Palette->LoadToolPaletteAction->GetCommandName();
	
	if (ActivePalette && ActivePalette->LoadToolPaletteAction->GetCommandName() == CommandName)
	{
		ActivePalette = nullptr;
		ResetToolPaletteWidget();
	    OnToolEnded.ExecuteIfBound();
		return;
	}
	
	CreatePalette(Palette);
}

void FToolkitBuilder::CreatePalette(TSharedPtr<FToolPalette> Palette)
{
	const FName CommandName = Palette->LoadToolPaletteAction->GetCommandName();
	ActivePalette = Palette;
	ResetToolPaletteWidget();
	const TSharedPtr<FSlimHorizontalUniformToolBarBuilder> PaletteToolbarBuilder =
		MakeShareable(new FSlimHorizontalUniformToolBarBuilder(ToolkitCommandList,
			FMultiBoxCustomization(ToolbarCustomizationName)));

	const TSharedPtr<FToolbarRegistrationArgs> RegistrationArgs = MakeShareable<FToolbarRegistrationArgs>(
		new FToolbarRegistrationArgs(PaletteToolbarBuilder.ToSharedRef()));
	FToolElementRegistrationKey Key = FToolElementRegistrationKey(CommandName, EToolElement::Toolbar);
	TSharedPtr<FToolElement> Element = ToolRegistry.GetToolElementSP(Key);
	
	if (!Element)
	{
		Element = MakeShareable(
				new FToolElement(Palette->LoadToolPaletteAction->GetCommandName(),
				RegistrationArgs.ToSharedRef()));
		ToolRegistry.RegisterElement(Element.ToSharedRef());
	}
	
	Element->SetRegistrationArgs(RegistrationArgs.ToSharedRef());
	
	LoadCommandNameToPaletteToolbarBuilderMap.Add(Palette->LoadToolPaletteAction->GetCommandName(), PaletteToolbarBuilder.ToSharedRef());
	
	PaletteToolbarBuilder->SetStyle(&FAppStyle::Get(), "SlimPaletteToolBar");

	for (TSharedRef<FButtonArgs> PaletteButton : Palette->PaletteActions)
	{
		PaletteButton->CommandList = ToolkitCommandList;
		PaletteButton->UserInterfaceActionType = EUserInterfaceActionType::ToggleButton;
		PaletteButton->OnGetMenuContent.BindSP(SharedThis(this),
			&FToolkitBuilder::GetContextMenuContent,
			PaletteButton->Command->GetCommandName());
		PaletteToolbarBuilder->AddToolBarButton(PaletteButton.Get());
	}

	ToolPaletteWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		.FillHeight(1.0f)
		[

		SNew(SBorder)
		.Padding(FAppStyle::GetMargin("SlimPaletteToolBarStyle.TitleBar.Padding"))
		.VAlign(VAlign_Center)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.HAlign(HAlign_Left)
		[

			SNew(STextBlock)
					.Justification(ETextJustify::Left)
					.AutoWrapText(true)
					.Text(Palette->LoadToolPaletteAction->GetLabel())
					.ColorAndOpacity(FStyleColors::Foreground)
		]];
	
	ToolPaletteWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			Element->GenerateWidget()
		];

	OnToolEnded.ExecuteIfBound();
}

TSharedRef<SWidget> FToolkitBuilder::GetToolPaletteWidget() const
{
	return ToolPaletteWidget->AsShared();
}

TSharedRef<SWidget> FToolkitBuilder::GetContextMenuContent(const FName CommandName)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (const TSharedRef<FEditablePalette> EditablePalette : EditablePalettesArray)
	{
		const FUIAction ItemAction(FExecuteAction::CreateSP(
			SharedThis(this), 
			&FToolkitBuilder::ToggleCommandInPalette, 
			EditablePalette, CommandName.ToString()));
		const FText LoadPaletteActionCommandLabelFText = EditablePalette->LoadToolPaletteAction->GetLabel();
		const FText& ItemText = EditablePalette->IsInPalette(CommandName) ? 
			FText::Format(LOCTEXT("RemoveFromPalette", "Remove from {0}"), LoadPaletteActionCommandLabelFText) : 
			FText::Format(LOCTEXT("AddToPalette", "Add to {0}"), LoadPaletteActionCommandLabelFText);

		MenuBuilder.AddMenuEntry(ItemText, ItemText, FSlateIcon(), ItemAction);
		
	}

	return MenuBuilder.MakeWidget();
}

void FToolkitBuilder::ResetWidget()
{
	LoadToolPaletteCommandList = MakeShareable(new FUICommandList);
	LoadPaletteToolBarBuilder = MakeShared<FVerticalToolBarBuilder>(LoadToolPaletteCommandList, FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
	ToolPaletteWidget = SNew(SVerticalBox);

	LoadCommandNameToPaletteToolbarBuilderMap.Reset();
	EditablePalettesArray.Reset();

	FToolElementRegistrationKey Key = FToolElementRegistrationKey(ToolbarCustomizationName, EToolElement::Toolbar);
	VerticalToolbarElement = ToolRegistry.GetToolElementSP(Key);
	const TSharedRef<FToolbarRegistrationArgs> VerticalToolbarRegistrationArgs = MakeShareable<FToolbarRegistrationArgs>(
		new FToolbarRegistrationArgs(LoadPaletteToolBarBuilder.ToSharedRef()));
	
	if (!VerticalToolbarElement)
	{
		VerticalToolbarElement = MakeShareable(new FToolElement
			(ToolbarCustomizationName,
			VerticalToolbarRegistrationArgs));
		ToolRegistry.RegisterElement(VerticalToolbarElement.ToSharedRef());
	}

	VerticalToolbarElement->SetRegistrationArgs(VerticalToolbarRegistrationArgs);	
}

void FToolkitBuilder::ResetToolPaletteWidget()
{
	if (ToolPaletteWidget)
	{
		ToolPaletteWidget->ClearChildren();
		return;
	}
	ToolPaletteWidget = SNew(SVerticalBox);
}

#undef LOCTEXT_NAMESPACE