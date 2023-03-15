// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolkitBuilder.h"

#include "IDetailsView.h"
#include "Widgets/SBoxPanel.h"
#include "SPrimaryButton.h"
#include "ToolbarRegistrationArgs.h"
#include "ToolElementRegistry.h"
#include "ToolkitStyle.h"
#include "Framework/Commands/UICommandList.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "ToolkitBuilder"

FToolElementRegistry FToolkitBuilder::ToolRegistry = FToolElementRegistry::Get();

bool FEditablePalette::IsInPalette(const FName CommandName) const
{
	return this->PaletteCommandNameArray.Contains(CommandName.ToString());
}

TArray<FString> FEditablePalette::GetPaletteCommandNames() const
{
	return PaletteCommandNameArray;
}

void FEditablePalette::AddCommandToPalette(const FString CommandNameString)
{
	PaletteCommandNameArray.Add(CommandNameString);

	SaveToConfig();
	
	OnPaletteEdited.ExecuteIfBound();
}

void FEditablePalette::RemoveCommandFromPalette(const FString CommandNameString)
{
	PaletteCommandNameArray.Remove(CommandNameString);

	SaveToConfig();
	
	OnPaletteEdited.ExecuteIfBound();
}

void FEditablePalette::SaveToConfig()
{
	if(GetConfigManager.IsBound())
	{
		IEditableToolPaletteConfigManager* ConfigManager = GetConfigManager.Execute();
		if(ConfigManager)
		{
			if(FEditableToolPaletteSettings* Config = ConfigManager->GetMutablePaletteConfig(EditablePaletteName))
			{
				Config->PaletteCommandNames = PaletteCommandNameArray;
				ConfigManager->SavePaletteConfig(EditablePaletteName);
			}
		}
	}
}

void FEditablePalette::LoadFromConfig()
{
	if(GetConfigManager.IsBound())
	{
		if(IEditableToolPaletteConfigManager* ConfigManager = GetConfigManager.Execute())
		{
			if(FEditableToolPaletteSettings* Config = ConfigManager->GetMutablePaletteConfig(EditablePaletteName))
			{
				PaletteCommandNameArray = Config->PaletteCommandNames;
			}
		}
	}
}

FEditablePalette::FEditablePalette(TSharedPtr<FUICommandInfo> InLoadToolPaletteAction,
	TSharedPtr<FUICommandInfo> InAddToPaletteAction,
	TSharedPtr<FUICommandInfo> InRemoveFromPaletteAction,
	FName InEditablePaletteName,
	FGetEditableToolPaletteConfigManager InGetConfigManager) :
	FToolPalette(InLoadToolPaletteAction, {}),
	AddToPaletteAction(InAddToPaletteAction),
	RemoveFromPaletteAction(InRemoveFromPaletteAction),
	EditablePaletteName(InEditablePaletteName),
	GetConfigManager(InGetConfigManager)
{
	LoadFromConfig();
}

FToolkitBuilder::FToolkitBuilder(
	FName InToolbarCustomizationName,
	TSharedPtr<FUICommandList> InToolkitCommandList,
	TSharedPtr<FToolkitSections> InToolkitSections) :
	FToolElementRegistrationArgs(EToolElement::Toolkit),
	ToolbarCustomizationName(InToolbarCustomizationName),
	ToolkitCommandList(InToolkitCommandList),
	ToolkitSections(InToolkitSections)
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

void FToolkitBuilder::GetCommandsForEditablePalette(TSharedRef<FEditablePalette> EditablePalette, TArray<TSharedPtr<const FUICommandInfo>>& OutCommands)
{
	TArray<FString> CommandNames = EditablePalette->GetPaletteCommandNames();

	for(const FString& CommandName : CommandNames)
	{
		if(TSharedPtr<const FUICommandInfo>* FoundCommand = PaletteCommandInfos.Find(CommandName))
		{
			if(*FoundCommand)
			{
				OutCommands.Add(*FoundCommand);
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("%s: Could not find Favorited Tool %s"), *ToolbarCustomizationName.ToString(), *CommandName);
			}
		}
	}
}

void FToolkitBuilder::AddPalette(TSharedPtr<FEditablePalette> Palette)
{
	Palette->OnPaletteEdited.BindSP(this, &FToolkitBuilder::OnEditablePaletteEdited, Palette.ToSharedRef());
	EditablePalettesArray.Add(Palette.ToSharedRef());
	AddPalette(StaticCastSharedRef<FToolPalette>(Palette.ToSharedRef()) );
}

void FToolkitBuilder::AddPalette(TSharedPtr<FToolPalette> Palette)
{
	for (TSharedRef<FButtonArgs> Button : Palette->PaletteActions)
	{
		PaletteCommandNameToButtonArgsMap.Add(Button->Command->GetCommandName().ToString(), Button);
		PaletteCommandInfos.Add(Button->Command->GetCommandName().ToString(), Button->Command);
	}
	LoadCommandNameToToolPaletteMap.Add(Palette->LoadToolPaletteAction->GetCommandName().ToString(), Palette);

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

void FToolkitBuilder::UpdateEditablePalette(TSharedRef<FEditablePalette> Palette)
{
	Palette->PaletteActions.Empty();

	TArray<FString> PaletteComandNameArray = Palette->GetPaletteCommandNames();

	for (const FString& Key : PaletteComandNameArray)
	{
		if (const TSharedPtr<FButtonArgs>* FoundButton = PaletteCommandNameToButtonArgsMap.Find(Key))
		{
			const TSharedRef<FButtonArgs> Button = (*FoundButton).ToSharedRef();
			Palette->PaletteActions.Add(Button);
		}
	}
}

void FToolkitBuilder::OnEditablePaletteEdited(TSharedRef<FEditablePalette> EditablePalette)
{
	UpdateEditablePalette(EditablePalette);

	// if the active Palette is the Palette to which we are toggling an action,
	// recreate it to load the new state after the toggle
	if ( ActivePalette == EditablePalette )
	{
		CreatePalette(EditablePalette);
	}
}

void FToolkitBuilder::UpdateWidget()
{
	for (const TSharedRef<FEditablePalette>& EditablePalette : EditablePalettesArray)
	{
		UpdateEditablePalette(EditablePalette);		
	}
	
}

void FToolkitBuilder::ToggleCommandInPalette(TSharedRef<FEditablePalette> Palette, FString CommandNameString)
{
	if (!Palette->IsInPalette(FName(CommandNameString)))
	{	
		Palette->AddCommandToPalette(CommandNameString);
	}
	else
	{
		Palette->RemoveCommandFromPalette(CommandNameString);		
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
		return;
/*
 *		@TODO: ~ enable this code for hiding category select on toggle		
 *		ActivePalette = nullptr;
		ResetToolPaletteWidget();
		return; */
	}
	
	CreatePalette(Palette);
}

void FToolkitBuilder::CreatePalette(TSharedPtr<FToolPalette> Palette)
{
	if (!Palette)
	{
		return;
	}
	
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
		PaletteButton->UserInterfaceActionType = (PaletteButton->UserInterfaceActionType != EUserInterfaceActionType::None) ?
														PaletteButton->UserInterfaceActionType :
														EUserInterfaceActionType::ToggleButton;
		PaletteButton->OnGetMenuContent.BindSP(SharedThis(this),
			&FToolkitBuilder::GetContextMenuContent,
			PaletteButton->Command->GetCommandName());
		PaletteToolbarBuilder->AddToolBarButton(PaletteButton.Get());
	}
	CreatePaletteWidget(*Palette.Get(), *Element.Get());
}

void FToolkitBuilder::CreatePaletteWidget(FToolPalette& Palette, FToolElement& Element)
{
	ToolPaletteWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		.FillHeight(1.0f)
		[

		SNew(SBorder)
		.Padding(Style.TitlePadding)
		.VAlign(VAlign_Center)
		.BorderImage(&Style.TitleBackgroundBrush)
		.HAlign(HAlign_Left)
		[

			SNew(STextBlock)
					.Justification(ETextJustify::Left)
					.Font(Style.TitleFont)
					.Text(Palette.LoadToolPaletteAction->GetLabel())
					.ColorAndOpacity(Style.TitleForegroundColor)
		]];
	
	ToolPaletteWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			Element.GenerateWidget()
		];
}


TSharedRef<SWidget> FToolkitBuilder::GetToolPaletteWidget() const
{
	return ToolPaletteWidget->AsShared();
}

TSharedRef<SWidget> FToolkitBuilder::GetContextMenuContent(const FName CommandName)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	for (const TSharedRef<FEditablePalette>& EditablePalette : EditablePalettesArray)
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
	Style = FToolkitStyle::Get().GetWidgetStyle<FToolkitWidgetStyle>("FToolkitWidgetStyle");
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

bool FToolkitBuilder::HasSelectedToolSet() const
{
	return HasActivePalette();
}

void FToolkitBuilder::SetActivePaletteOnLoad(const FUICommandInfo* Command)
{
	if (const TSharedPtr<FToolPalette>* LoadPalette = LoadCommandNameToToolPaletteMap.Find(Command->GetCommandName().ToString()))
	{
		CreatePalette(*LoadPalette);
	}
}

TSharedPtr<SWidget> FToolkitBuilder::GenerateWidget()
{
	if (!ToolkitWidgetHBox)
	{
		DefineWidget();
	}
	return ToolkitWidgetHBox.ToSharedRef();
}

void FToolkitBuilder::SetActiveToolDisplayName(FText InActiveToolDisplayName)
{
	ActiveToolDisplayName = InActiveToolDisplayName;
}

FText FToolkitBuilder::GetActiveToolDisplayName() const
{
	return ActiveToolDisplayName;
}

EVisibility FToolkitBuilder::GetActiveToolTitleVisibility() const
{
	return ActiveToolDisplayName.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
}

void FToolkitBuilder::DefineWidget()
{
	ToolkitWidgetVBox = SNew(SVerticalBox);
	ToolkitWidgetHBox = 
		SNew(SSplitter)
		+ SSplitter::Slot()
		.Resizable(false)
		.SizeRule(SSplitter::SizeToContent)
			[
				CreateToolbarWidget()
			]

		+ SSplitter::Slot()
		.SizeRule(SSplitter::FractionOfParent)
			[
				ToolkitWidgetVBox->AsShared()
			];

	if (ToolkitSections->ModeWarningArea)
	{
		ToolkitWidgetVBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(5)
		[
			ToolkitSections->ModeWarningArea->AsShared()
		];
	}
	
	ToolkitWidgetVBox->AddSlot()
	.AutoHeight()
	.HAlign(HAlign_Fill)
	.Padding(0)
		[
			GetToolPaletteWidget()
		];

	ToolkitWidgetVBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(0)
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.Padding(Style.ActiveToolTitleBorderPadding)
			.BorderImage(&Style.ToolDetailsBackgroundBrush)
			[
			SNew(SBorder)
			.Visibility(SharedThis(this), &FToolkitBuilder::GetActiveToolTitleVisibility)
			.BorderImage(&Style.TitleBackgroundBrush)
			.Padding(Style.ToolContextTextBlockPadding)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			[

				SNew(STextBlock)
						.Justification(ETextJustify::Left)
						.Font(Style.TitleFont)
						.Text(TAttribute<FText>(SharedThis(this), &FToolkitBuilder::GetActiveToolDisplayName))
						.ColorAndOpacity(Style.TitleForegroundColor)
			]
		]
	];
	
	if (ToolkitSections->ToolWarningArea)
	{
		ToolkitWidgetVBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Fill)
		.Padding(5)
		[
			ToolkitSections->ToolWarningArea->AsShared()
		];
	}

	if (ToolkitSections->DetailsView)
	{
		ToolkitWidgetVBox->AddSlot()
		.HAlign(HAlign_Fill)
		.FillHeight(1.f)
			[
			SNew(SBorder)
			.BorderImage(&Style.ToolDetailsBackgroundBrush)
			.Padding(8.f, 2.f, 0.f, 2.f)
				[
					ToolkitSections->DetailsView->AsShared()
				]
		
			];
	}
	if (ToolkitSections->Footer)
	{
		ToolkitWidgetVBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			.Padding(0)
		[
			ToolkitSections->Footer->AsShared()
		];
	}
}

#undef LOCTEXT_NAMESPACE