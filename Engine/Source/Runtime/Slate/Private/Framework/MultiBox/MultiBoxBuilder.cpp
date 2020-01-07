// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/MultiBox/SHeadingBlock.h"
#include "Framework/MultiBox/SMenuEntryBlock.h"
#include "Framework/MultiBox/SMenuSeparatorBlock.h"
#include "Framework/MultiBox/SToolBarSeparatorBlock.h"
#include "Framework/MultiBox/SToolBarButtonBlock.h"
#include "Framework/MultiBox/SToolBarComboButtonBlock.h"
#include "Framework/MultiBox/SEditableTextBlock.h"
#include "Framework/MultiBox/SButtonRowBlock.h"
#include "Framework/MultiBox/SWidgetBlock.h"
#include "Framework/MultiBox/SGroupMarkerBlock.h"
#include "Framework/MultiBox/ToolMenuBase.h"
#include "Widgets/Layout/SScrollBox.h"


FMultiBoxBuilder::FMultiBoxBuilder( const EMultiBoxType InType, FMultiBoxCustomization InCustomization, const bool bInShouldCloseWindowAfterMenuSelection, const TSharedPtr< const FUICommandList >& InCommandList, TSharedPtr<FExtender> InExtender, FName InTutorialHighlightName, FName InMenuName )
	: MultiBox( FMultiBox::Create( InType, InMenuName != NAME_None ? FMultiBoxCustomization::AllowCustomization(InMenuName) : InCustomization, bInShouldCloseWindowAfterMenuSelection ) )
	, CommandListStack()
	, TutorialHighlightName(InTutorialHighlightName)
	, MenuName(InMenuName)
	, bExtendersEnabled(true)
{
	CommandListStack.Push( InCommandList );
	ExtenderStack.Push(InExtender);
}

void FMultiBoxBuilder::AddEditableText( const FText& InLabel, const FText& InToolTip, const FSlateIcon& InIcon, const TAttribute< FText >& InTextAttribute, const FOnTextCommitted& InOnTextCommitted, const FOnTextChanged& InOnTextChanged, bool bInReadOnly )
{
	MultiBox->AddMultiBlock( MakeShareable( new FEditableTextBlock( InLabel, InToolTip, InIcon, InTextAttribute, bInReadOnly, InOnTextCommitted, InOnTextChanged ) ) );
}

void FMultiBoxBuilder::PushCommandList( const TSharedRef< const FUICommandList > CommandList )
{
	CommandListStack.Push( CommandList );
}

void FMultiBoxBuilder::PopCommandList()
{
	// Never allowed to pop the last command-list!  This command-list was set when the multibox was first created and is canonical.
	if( ensure( CommandListStack.Num() > 1 ) )
	{
		CommandListStack.Pop();
	}
}

TSharedPtr<const FUICommandList> FMultiBoxBuilder::GetTopCommandList()
{
	return (CommandListStack.Num() > 0) ? CommandListStack.Top() : TSharedPtr<const FUICommandList>(NULL);
}

void FMultiBoxBuilder::PushExtender( TSharedRef< FExtender > InExtender )
{
	ExtenderStack.Push( InExtender );
}

void FMultiBoxBuilder::PopExtender()
{
	// Never allowed to pop the last extender! This extender was set when the multibox was first created and is canonical.
	if( ensure( ExtenderStack.Num() > 1 ) )
	{
		ExtenderStack.Pop();
	}
}

const ISlateStyle* FMultiBoxBuilder::GetStyleSet() const 
{ 
	return MultiBox->GetStyleSet();
}

const FName& FMultiBoxBuilder::GetStyleName() const 
{ 
	return MultiBox->GetStyleName();
}

void FMultiBoxBuilder::SetStyle( const ISlateStyle* InStyleSet, const FName& InStyleName ) 
{ 
	MultiBox->SetStyle( InStyleSet, InStyleName ); 
}

FMultiBoxCustomization FMultiBoxBuilder::GetCustomization() const
{
	return FMultiBoxCustomization( MultiBox->GetCustomizationName() ); 
}

TSharedRef< class SWidget > FMultiBoxBuilder::MakeWidget( FMultiBox::FOnMakeMultiBoxBuilderOverride* InMakeMultiBoxBuilderOverride, uint32 MaxHeight /* = nullptr */ )
{
	return MultiBox->MakeWidget( false, InMakeMultiBoxBuilderOverride );
}

TSharedRef< class FMultiBox > FMultiBoxBuilder::GetMultiBox()
{
	return MultiBox;
}

/** Helper function to generate unique widget-identifying names given various bits of information */
static FName GenerateTutorialIdentfierName(FName InContainerName, FName InElementName, const TSharedPtr< const FUICommandInfo > InCommand, int32 InIndex)
{
	FString BaseName;
	if(InContainerName != NAME_None)
	{
		BaseName = InContainerName.ToString() + TEXT(".");
	}

	if(InElementName != NAME_None)
	{
		return FName(*(BaseName + InElementName.ToString()));
	}
	else if(InCommand.IsValid() && InCommand->GetCommandName() != NAME_None)
	{
		return FName(*(BaseName + InCommand->GetCommandName().ToString()));
	}
	else
	{
		// default to index if no other info is available
		const FString IndexedName = FString::Printf(TEXT("MultiboxWidget%d"), InIndex);
		return FName(*(BaseName + IndexedName));
	}
}

FBaseMenuBuilder::FBaseMenuBuilder( const EMultiBoxType InType, const bool bInShouldCloseWindowAfterMenuSelection, TSharedPtr< const FUICommandList > InCommandList, bool bInCloseSelfOnly, TSharedPtr<FExtender> InExtender, const ISlateStyle* InStyleSet, FName InTutorialHighlightName, FName InMenuName )
	: FMultiBoxBuilder( InType, FMultiBoxCustomization::None, bInShouldCloseWindowAfterMenuSelection, InCommandList, InExtender, InTutorialHighlightName, InMenuName )
	, bCloseSelfOnly( bInCloseSelfOnly )
{
	MultiBox->SetStyle(InStyleSet, "Menu");
}

void FBaseMenuBuilder::AddMenuEntry( const TSharedPtr< const FUICommandInfo > InCommand, FName InExtensionHook, const TAttribute<FText>& InLabelOverride, const TAttribute<FText>& InToolTipOverride, const FSlateIcon& InIconOverride, FName InTutorialHighlightName )
{
	ApplySectionBeginning();

	ApplyHook(InExtensionHook, EExtensionHook::Before);
	
	// The command must be valid
	check( InCommand.IsValid() );
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( InExtensionHook, InCommand, CommandListStack.Last(), InLabelOverride, InToolTipOverride, InIconOverride, bCloseSelfOnly ) );
	NewMenuEntryBlock->SetTutorialHighlightName(GenerateTutorialIdentfierName(TutorialHighlightName, InTutorialHighlightName, InCommand, MultiBox->GetBlocks().Num()));
	MultiBox->AddMultiBlock( NewMenuEntryBlock );

	ApplyHook(InExtensionHook, EExtensionHook::After);
}

void FBaseMenuBuilder::AddMenuEntry( const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const FSlateIcon& InIcon, const FUIAction& InAction, FName InExtensionHook, const EUserInterfaceActionType UserInterfaceActionType, FName InTutorialHighlightName )
{
	ApplySectionBeginning();

	ApplyHook(InExtensionHook, EExtensionHook::Before);
	
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( InExtensionHook, InLabel, InToolTip, InIcon, InAction, UserInterfaceActionType, bCloseSelfOnly ) );
	NewMenuEntryBlock->SetTutorialHighlightName(GenerateTutorialIdentfierName(TutorialHighlightName, InTutorialHighlightName, nullptr, MultiBox->GetBlocks().Num()));
	MultiBox->AddMultiBlock( NewMenuEntryBlock );
	
	ApplyHook(InExtensionHook, EExtensionHook::After);
}

void FBaseMenuBuilder::AddMenuEntry( const FUIAction& UIAction, const TSharedRef< SWidget > Contents, const FName& InExtensionHook, const TAttribute<FText>& InToolTip, const EUserInterfaceActionType UserInterfaceActionType, FName InTutorialHighlightName )
{
	ApplySectionBeginning();

	ApplyHook(InExtensionHook, EExtensionHook::Before);

	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( InExtensionHook, UIAction, Contents, InToolTip, UserInterfaceActionType, bCloseSelfOnly ) );
	NewMenuEntryBlock->SetTutorialHighlightName(GenerateTutorialIdentfierName(TutorialHighlightName, InTutorialHighlightName, nullptr, MultiBox->GetBlocks().Num()));
	MultiBox->AddMultiBlock( NewMenuEntryBlock );

	ApplyHook(InExtensionHook, EExtensionHook::After);
}

TSharedRef< class SWidget > FMenuBuilder::MakeWidget( FMultiBox::FOnMakeMultiBoxBuilderOverride* InMakeMultiBoxBuilderOverride /* = nullptr */, uint32 MaxHeight)
{
	// Make menu builders searchable (by default)
	TSharedRef< class SWidget > MenuWidget = MultiBox->MakeWidget(bSearchable, InMakeMultiBoxBuilderOverride);
	if (MaxHeight < INT_MAX)
	{
		TSharedRef<SWidget> ConstrainedMenu = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.MaxHeight((float)MaxHeight)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					MenuWidget
				]
			];
		return ConstrainedMenu;
	}
	return MenuWidget;
}

void FMenuBuilder::BeginSection( FName InExtensionHook, const TAttribute< FText >& InHeadingText )
{
	check(CurrentSectionExtensionHook == NAME_None && !bSectionNeedsToBeApplied);

	ApplyHook(InExtensionHook, EExtensionHook::Before);
	
	// Do not actually apply the section header, because if this section is ended immediately
	// then nothing ever gets created, preventing empty sections from ever appearing
	bSectionNeedsToBeApplied = true;
	CurrentSectionExtensionHook = InExtensionHook;
	CurrentSectionHeadingText = InHeadingText.Get();

	// Do apply the section beginning if we are in developer "show me all the hooks" mode
	if (FMultiBoxSettings::DisplayMultiboxHooks.Get())
	{
		ApplySectionBeginning();
	}

	ApplyHook(InExtensionHook, EExtensionHook::First);
}

void FMenuBuilder::EndSection()
{
	FName SectionExtensionHook = CurrentSectionExtensionHook;
	CurrentSectionExtensionHook = NAME_None;
	bSectionNeedsToBeApplied = false;
	CurrentSectionHeadingText = FText::GetEmpty();

	ApplyHook(SectionExtensionHook, EExtensionHook::After);
}

void FMenuBuilder::AddMenuSeparator(FName InExtensionHook)
{
	ApplySectionBeginning();

	ApplyHook(InExtensionHook, EExtensionHook::Before);

	// Never add a menu separate as the first item, even if we were asked to
	if( MultiBox->GetBlocks().Num() > 0 || FMultiBoxSettings::DisplayMultiboxHooks.Get() )
	{
		TSharedRef< FMenuSeparatorBlock > NewMenuSeparatorBlock( new FMenuSeparatorBlock(InExtensionHook, /* bInIsPartOfHeading=*/ false) );
		MultiBox->AddMultiBlock( NewMenuSeparatorBlock );
	}

	ApplyHook(InExtensionHook, EExtensionHook::After);
}

void FMenuBuilder::AddSubMenu( const TAttribute<FText>& InMenuLabel, const TAttribute<FText>& InToolTip, const FNewMenuDelegate& InSubMenu, const FUIAction& InUIAction, FName InExtensionHook, const EUserInterfaceActionType InUserInterfaceActionType, const bool bInOpenSubMenuOnClick, const FSlateIcon& InIcon, const bool bInShouldCloseWindowAfterMenuSelection /*= true*/ )
{
	ApplySectionBeginning();

	const bool bIsSubMenu = true;
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( InExtensionHook, InMenuLabel, InToolTip, InSubMenu, ExtenderStack.Top(), bIsSubMenu, bInOpenSubMenuOnClick, InIcon, InUIAction, InUserInterfaceActionType, bCloseSelfOnly, bInShouldCloseWindowAfterMenuSelection ) );

	MultiBox->AddMultiBlock( NewMenuEntryBlock );
}

void FMenuBuilder::AddSubMenu( const TAttribute<FText>& InMenuLabel, const TAttribute<FText>& InToolTip, const FNewMenuDelegate& InSubMenu, const bool bInOpenSubMenuOnClick /*= false*/, const FSlateIcon& InIcon /*= FSlateIcon()*/, const bool bInShouldCloseWindowAfterMenuSelection /*= true*/, FName InExtensionHook /*=NAME_None*/)
{
	ApplySectionBeginning();

	const bool bIsSubMenu = true;
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( InExtensionHook, InMenuLabel, InToolTip, InSubMenu, ExtenderStack.Top(), bIsSubMenu, bInOpenSubMenuOnClick, CommandListStack.Last(), bCloseSelfOnly, InIcon, bInShouldCloseWindowAfterMenuSelection ) );

	MultiBox->AddMultiBlock( NewMenuEntryBlock );
}

void FMenuBuilder::AddSubMenu( const TSharedRef< SWidget > Contents, const FNewMenuDelegate& InSubMenu, const bool bInOpenSubMenuOnClick /*= false*/, const bool bInShouldCloseWindowAfterMenuSelection /*= true*/ )
{
	ApplySectionBeginning();

	const bool bIsSubMenu = true;
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( NAME_None, Contents, InSubMenu, ExtenderStack.Top(), bIsSubMenu, bInOpenSubMenuOnClick, CommandListStack.Last(), bCloseSelfOnly, bInShouldCloseWindowAfterMenuSelection ) );

	MultiBox->AddMultiBlock( NewMenuEntryBlock );
}

void FMenuBuilder::AddSubMenu( const FUIAction& UIAction, const TSharedRef< SWidget > Contents, const FNewMenuDelegate& InSubMenu, const bool bInShouldCloseWindowAfterMenuSelection /*= true*/ )
{
	ApplySectionBeginning();

	const bool bIsSubMenu = true;
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( NAME_None, UIAction, Contents, InSubMenu, ExtenderStack.Top(), bIsSubMenu, CommandListStack.Last(), bCloseSelfOnly, bInShouldCloseWindowAfterMenuSelection ) );

	MultiBox->AddMultiBlock( NewMenuEntryBlock );
}

void FMenuBuilder::AddWrapperSubMenu( const FText& InMenuLabel, const FText& InToolTip, const FOnGetContent& InSubMenu, const FSlateIcon& InIcon )
{
	ApplySectionBeginning();

	const bool bIsSubMenu = true;
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( NAME_None, InMenuLabel, InToolTip, InSubMenu, ExtenderStack.Top(), bIsSubMenu, false, CommandListStack.Last(), bCloseSelfOnly, InIcon ) );

	MultiBox->AddMultiBlock( NewMenuEntryBlock );
}

void FMenuBuilder::AddWrapperSubMenu( const FText& InMenuLabel, const FText& InToolTip, const TSharedPtr<SWidget>& InSubMenu, const FSlateIcon& InIcon )
{
	ApplySectionBeginning();

	const bool bIsSubMenu = true;
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock( new FMenuEntryBlock( NAME_None, InMenuLabel, InToolTip, InSubMenu, ExtenderStack.Top(), bIsSubMenu, false, CommandListStack.Last(), bCloseSelfOnly, InIcon ) );

	MultiBox->AddMultiBlock( NewMenuEntryBlock );
}

void FMenuBuilder::AddWidget( TSharedRef<SWidget> InWidget, const FText& Label, bool bNoIndent, bool bInSearchable )
{
	ApplySectionBeginning();

	TSharedRef< FWidgetBlock > NewWidgetBlock(new FWidgetBlock( InWidget, Label, bNoIndent ));
	NewWidgetBlock->SetSearchable( bInSearchable );

	MultiBox->AddMultiBlock( NewWidgetBlock );
}

void FMenuBuilder::AddSearchWidget()
{
	MultiBox->SearchTextWidget = SNew(STextBlock)
		.Visibility( EVisibility::Visible )
		.Text( FText::FromString("Search Start") );

	AddWidget( MultiBox->SearchTextWidget.ToSharedRef(), FText::GetEmpty(), false, false );
}

void FMenuBuilder::ApplyHook(FName InExtensionHook, EExtensionHook::Position HookPosition)
{
	if (ExtendersEnabled())
	{
		// this is a virtual function to get a properly typed "this" pointer
		auto& Extender = ExtenderStack.Top();
		if (InExtensionHook != NAME_None && Extender.IsValid())
		{
			if (!MultiBox->IsInEditMode())
			{
				Extender->Apply(InExtensionHook, HookPosition, *this);
			}
		}
	}
}

void FMenuBuilder::ApplySectionBeginning()
{
	if (bSectionNeedsToBeApplied)
	{
		// Do not count search block, which starts as invisible
		if( MultiBox->GetBlocks().Num() > 1 || FMultiBoxSettings::DisplayMultiboxHooks.Get() )
		{
			MultiBox->AddMultiBlock( MakeShareable( new FMenuSeparatorBlock(CurrentSectionExtensionHook, /* bInIsPartOfHeading=*/ true) ) );
		}
		if (!CurrentSectionHeadingText.IsEmpty())
		{
			MultiBox->AddMultiBlock( MakeShareable( new FHeadingBlock(CurrentSectionExtensionHook, CurrentSectionHeadingText) ) );
		}
		bSectionNeedsToBeApplied = false;
		CurrentSectionHeadingText = FText::GetEmpty();
	}
}

void FMenuBarBuilder::AddPullDownMenu(const FText& InMenuLabel, const FText& InToolTip, const FNewMenuDelegate& InPullDownMenu, FName InExtensionHook, FName InTutorialHighlightName)
{
	ApplySectionBeginning();

	ApplyHook(InExtensionHook, EExtensionHook::Before);

	const bool bIsSubMenu = false;
	const bool bOpenSubMenuOnClick = false;
	// Pulldown menus always close all menus not just themselves
	const bool bShouldCloseSelfOnly = false;
	TSharedRef< FMenuEntryBlock > NewMenuEntryBlock(new FMenuEntryBlock(InExtensionHook, InMenuLabel, InToolTip, InPullDownMenu, ExtenderStack.Top(), bIsSubMenu, bOpenSubMenuOnClick, CommandListStack.Last(), bShouldCloseSelfOnly));
	NewMenuEntryBlock->SetTutorialHighlightName(GenerateTutorialIdentfierName(TutorialHighlightName, InTutorialHighlightName, nullptr, MultiBox->GetBlocks().Num()));

	MultiBox->AddMultiBlock(NewMenuEntryBlock);

	ApplyHook(InExtensionHook, EExtensionHook::After);
}

void FMenuBarBuilder::ApplyHook(FName InExtensionHook, EExtensionHook::Position HookPosition)
{
	if (ExtendersEnabled())
	{
		// this is a virtual function to get a properly typed "this" pointer
		auto& Extender = ExtenderStack.Top();
		if (InExtensionHook != NAME_None && Extender.IsValid())
		{
			Extender->Apply(InExtensionHook, HookPosition, *this);
		}
	}
}

void FToolBarBuilder::AddToolBarButton(const TSharedPtr< const FUICommandInfo > InCommand, FName InExtensionHook, const TAttribute<FText>& InLabelOverride, const TAttribute<FText>& InToolTipOverride, const TAttribute<FSlateIcon>& InIconOverride, FName InTutorialHighlightName )
{
	ApplySectionBeginning();

	ApplyHook(InExtensionHook, EExtensionHook::Before);

	TSharedRef< FToolBarButtonBlock > NewToolBarButtonBlock( new FToolBarButtonBlock( InCommand.ToSharedRef(), CommandListStack.Last(), InLabelOverride, InToolTipOverride, InIconOverride ) );

	if ( LabelVisibility.IsSet() )
	{
		NewToolBarButtonBlock->SetLabelVisibility( LabelVisibility.GetValue() );
	}

	NewToolBarButtonBlock->SetIsFocusable(bIsFocusable);
	NewToolBarButtonBlock->SetForceSmallIcons(bForceSmallIcons);
	NewToolBarButtonBlock->SetTutorialHighlightName(GenerateTutorialIdentfierName(TutorialHighlightName, InTutorialHighlightName, InCommand, MultiBox->GetBlocks().Num()));

	MultiBox->AddMultiBlock( NewToolBarButtonBlock );

	ApplyHook(InExtensionHook, EExtensionHook::After);
}

void FToolBarBuilder::AddToolBarButton(const FUIAction& InAction, FName InExtensionHook, const TAttribute<FText>& InLabelOverride, const TAttribute<FText>& InToolTipOverride, const TAttribute<FSlateIcon>& InIconOverride, const EUserInterfaceActionType UserInterfaceActionType, FName InTutorialHighlightName )
{
	ApplySectionBeginning();

	ApplyHook(InExtensionHook, EExtensionHook::Before);

	TSharedRef< FToolBarButtonBlock > NewToolBarButtonBlock( new FToolBarButtonBlock( InLabelOverride, InToolTipOverride, InIconOverride, InAction, UserInterfaceActionType ) );

	if ( LabelVisibility.IsSet() )
	{
		NewToolBarButtonBlock->SetLabelVisibility( LabelVisibility.GetValue() );
	}

	NewToolBarButtonBlock->SetIsFocusable(bIsFocusable);
	NewToolBarButtonBlock->SetForceSmallIcons(bForceSmallIcons);
	NewToolBarButtonBlock->SetTutorialHighlightName(GenerateTutorialIdentfierName(TutorialHighlightName, InTutorialHighlightName, nullptr, MultiBox->GetBlocks().Num()));

	MultiBox->AddMultiBlock( NewToolBarButtonBlock );

	ApplyHook(InExtensionHook, EExtensionHook::After);
}

void FToolBarBuilder::AddComboButton( const FUIAction& InAction, const FOnGetContent& InMenuContentGenerator, const TAttribute<FText>& InLabelOverride, const TAttribute<FText>& InToolTipOverride, const TAttribute<FSlateIcon>& InIconOverride, bool bInSimpleComboBox, FName InTutorialHighlightName )
{
	ApplySectionBeginning();

	TSharedRef< FToolBarComboButtonBlock > NewToolBarComboButtonBlock( new FToolBarComboButtonBlock( InAction, InMenuContentGenerator, InLabelOverride, InToolTipOverride, InIconOverride, bInSimpleComboBox ) );

	if ( LabelVisibility.IsSet() )
	{
		NewToolBarComboButtonBlock->SetLabelVisibility( LabelVisibility.GetValue() );
	}

	NewToolBarComboButtonBlock->SetForceSmallIcons(bForceSmallIcons);
	NewToolBarComboButtonBlock->SetTutorialHighlightName(GenerateTutorialIdentfierName(TutorialHighlightName, InTutorialHighlightName, nullptr, MultiBox->GetBlocks().Num()));

	MultiBox->AddMultiBlock( NewToolBarComboButtonBlock );
}

void FToolBarBuilder::AddToolBarWidget( TSharedRef<SWidget> InWidget, const TAttribute<FText>& InLabel, FName InTutorialHighlightName, bool bSearchable )
{
	ApplySectionBeginning();

	// If tutorial name specified, wrap in tutorial wrapper
	const FName WrapperName = GenerateTutorialIdentfierName(InTutorialHighlightName, NAME_None, nullptr, MultiBox->GetBlocks().Num());

	TSharedRef<SWidget> ChildWidget = InWidget;
	InWidget = 
		SNew( SVerticalBox )
		.AddMetaData<FTagMetaData>(FTagMetaData(InTutorialHighlightName))

		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign( HAlign_Center )
		[
			ChildWidget
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign( HAlign_Center )
		[
			SNew( STextBlock )
			// .Visibility_Lambda( [this] () -> EVisibility { return /*LabelVisibility.IsSet() ? LabelVisibility.GetValue() :*/ (bForceSmallIcons || FMultiBoxSettings::UseSmallToolBarIcons.Get()) ? EVisibility::Collapsed : EVisibility::Visible; } ) 
			.Visibility_Lambda( [] () -> EVisibility { return FMultiBoxSettings::UseSmallToolBarIcons.Get() ? EVisibility::Collapsed : EVisibility::Visible; } ) 
			.Text( InLabel )
			.TextStyle( GetStyleSet(), ISlateStyle::Join( GetStyleName(), ".Label" ) )	// Smaller font for tool tip labels
			.ShadowOffset( FVector2D::UnitVector )
		] ;
	
	TSharedRef< FWidgetBlock > NewWidgetBlock( new FWidgetBlock( InWidget, FText::GetEmpty(), true ) );
	MultiBox->AddMultiBlock( NewWidgetBlock );
	NewWidgetBlock->SetSearchable(bSearchable);
}

void FToolBarBuilder::AddWidget( TSharedRef<SWidget> InWidget, FName InTutorialHighlightName, bool bSearchable )
{
	ApplySectionBeginning();

	// If tutorial name specified, wrap in tutorial wrapper
	const FName WrapperName = GenerateTutorialIdentfierName(InTutorialHighlightName, NAME_None, nullptr, MultiBox->GetBlocks().Num());

	TSharedRef<SWidget> ChildWidget = InWidget;
	InWidget = 
		SNew( SBox )
		.AddMetaData<FTagMetaData>(FTagMetaData(InTutorialHighlightName))
		[
			ChildWidget
		];
	
	TSharedRef< FWidgetBlock > NewWidgetBlock( new FWidgetBlock( InWidget, FText::GetEmpty(), true ) );
	MultiBox->AddMultiBlock( NewWidgetBlock );
	NewWidgetBlock->SetSearchable(bSearchable);
}

void FToolBarBuilder::AddSeparator(FName InExtensionHook)
{
	ApplySectionBeginning();

	ApplyHook(InExtensionHook, EExtensionHook::Before);

	// Never add a menu separate as the first item, even if we were asked to
	if( MultiBox->GetBlocks().Num() > 0 || FMultiBoxSettings::DisplayMultiboxHooks.Get() )
	{
		MultiBox->AddMultiBlock( MakeShareable( new FToolBarSeparatorBlock(InExtensionHook) ) );
	}

	ApplyHook(InExtensionHook, EExtensionHook::After);
}

void FToolBarBuilder::BeginSection( FName InExtensionHook )
{
	checkf(CurrentSectionExtensionHook == NAME_None && !bSectionNeedsToBeApplied, TEXT("Did you forget to call EndSection()?"));

	ApplyHook(InExtensionHook, EExtensionHook::Before);
	
	// Do not actually apply the section header, because if this section is ended immediately
	// then nothing ever gets created, preventing empty sections from ever appearing
	bSectionNeedsToBeApplied = true;
	CurrentSectionExtensionHook = InExtensionHook;
	
	// Do apply the section beginning if we are in developer "show me all the hooks" mode
	if (FMultiBoxSettings::DisplayMultiboxHooks.Get())
	{
		ApplySectionBeginning();
	}
	
	ApplyHook(InExtensionHook, EExtensionHook::First);
}

void FToolBarBuilder::EndSection()
{
	FName SectionExtensionHook = CurrentSectionExtensionHook;
	CurrentSectionExtensionHook = NAME_None;
	bSectionNeedsToBeApplied = false;

	ApplyHook(SectionExtensionHook, EExtensionHook::After);
}

void FToolBarBuilder::ApplyHook(FName InExtensionHook, EExtensionHook::Position HookPosition)
{
	if (ExtendersEnabled())
	{
		// this is a virtual function to get a properly typed "this" pointer
		auto& Extender = ExtenderStack.Top();
		if (InExtensionHook != NAME_None && Extender.IsValid())
		{
			Extender->Apply(InExtensionHook, HookPosition, *this);
		}
	}
}

void FToolBarBuilder::ApplySectionBeginning()
{
	if (bSectionNeedsToBeApplied)
	{
		if( MultiBox->GetBlocks().Num() > 0 || FMultiBoxSettings::DisplayMultiboxHooks.Get() )
		{
			MultiBox->AddMultiBlock( MakeShareable( new FToolBarSeparatorBlock(CurrentSectionExtensionHook) ) );
		}
		bSectionNeedsToBeApplied = false;
	}
}

void FToolBarBuilder::EndBlockGroup()
{
	ApplySectionBeginning();

	TSharedRef< FGroupEndBlock > NewGroupEndBlock( new FGroupEndBlock() );

	MultiBox->AddMultiBlock( NewGroupEndBlock );
}

void FToolBarBuilder::BeginBlockGroup()
{
	ApplySectionBeginning();

	TSharedRef< FGroupStartBlock > NewGroupStartBlock( new FGroupStartBlock() );

	MultiBox->AddMultiBlock( NewGroupStartBlock );
}

void FButtonRowBuilder::AddButton( const TSharedPtr< const FUICommandInfo > InCommand, const TAttribute<FText>& InLabelOverride, const TAttribute<FText>& InToolTipOverride, const FSlateIcon& InIconOverride )
{
	ApplySectionBeginning();

	TSharedRef< FButtonRowBlock > NewButtonRowBlock( new FButtonRowBlock( InCommand.ToSharedRef(), CommandListStack.Last(), InLabelOverride, InToolTipOverride, InIconOverride ) );

	MultiBox->AddMultiBlock( NewButtonRowBlock );
}

void FButtonRowBuilder::AddButton( const FText& InLabel, const FText& InToolTip, const FUIAction& UIAction, const FSlateIcon& InIcon, const EUserInterfaceActionType UserInterfaceActionType )
{
	ApplySectionBeginning();

	TSharedRef< FButtonRowBlock > NewButtonRowBlock( new FButtonRowBlock( InLabel, InToolTip, InIcon, UIAction, UserInterfaceActionType ) );

	MultiBox->AddMultiBlock( NewButtonRowBlock );
}

