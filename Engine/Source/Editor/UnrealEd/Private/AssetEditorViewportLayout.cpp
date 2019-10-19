// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetEditorViewportLayout.h"
#include "Layout/Margin.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Framework/Docking/LayoutService.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SCanvas.h"
#include "EditorStyleSet.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "EditorViewportLayout.h"
#include "EditorViewportTypeDefinition.h"
#include "EditorViewportLayoutEntity.h"
#include "EditorViewportCommands.h"
#include "SAssetEditorViewport.h"
#include "Templates/SharedPointer.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"


#define LOCTEXT_NAMESPACE "AssetEditorViewportToolBar"


namespace ViewportLayoutDefs
{
	/** How many seconds to interpolate from restored to maximized state */
	static const float MaximizeTransitionTime = 0.15f;

	/** How many seconds to interpolate from maximized to restored state */
	static const float RestoreTransitionTime = 0.2f;

	/** Default maximized state for new layouts - will only be applied when no config data is restoring state */
	static const bool bDefaultShouldBeMaximized = true;

	/** Default immersive state for new layouts - will only be applied when no config data is restoring state */
	static const bool bDefaultShouldBeImmersive = false;
}


// SViewportsOverlay ////////////////////////////////////////////////

/**
* Overlay wrapper class so that we can cache the size of the widget
* It will also store the ViewportLayout data because that data can't be stored
* per app; it must be stored per viewport overlay in case the app that made it closes.
*/
class SViewportsOverlay : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS( SViewportsOverlay ){}
	SLATE_DEFAULT_SLOT( FArguments, Content )
		SLATE_ARGUMENT( TSharedPtr<FViewportTabContent>, ViewportTab )
		SLATE_END_ARGS()

		void Construct( const FArguments& InArgs );

	/** Default constructor */
	SViewportsOverlay()
		: CachedSize( FVector2D::ZeroVector )
	{}

	/** Overridden from SWidget */
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

	/** Wraps SOverlay::AddSlot() */
	SOverlay::FOverlaySlot& AddSlot();

	/** Wraps SOverlay::RemoveSlot() */
	void RemoveSlot();

	/**
	* Returns the cached size of this viewport overlay
	*
	* @return	The size that was cached
	*/
	const FVector2D& GetCachedSize() const;

	/** Gets the  Viewport Tab that created this overlay */
	TSharedPtr<FViewportTabContent> GetViewportTab() const;

private:

	/** Reference to the owning  viewport tab */
	TSharedPtr<FViewportTabContent> ViewportTab;

	/** The overlay widget we're containing */
	TSharedPtr< SOverlay > OverlayWidget;

	/** Cache our size, so that we can use this when animating a viewport maximize/restore */
	FVector2D CachedSize;
};


void SViewportsOverlay::Construct( const FArguments& InArgs )
{
	const TSharedRef<SWidget>& ContentWidget = InArgs._Content.Widget;
	ViewportTab = InArgs._ViewportTab;

	ChildSlot
		[
			SAssignNew( OverlayWidget, SOverlay )
			+ SOverlay::Slot()
		[
			ContentWidget
		]
		];
}

void SViewportsOverlay::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	CachedSize = AllottedGeometry.Size;
}

SOverlay::FOverlaySlot& SViewportsOverlay::AddSlot()
{
	return OverlayWidget->AddSlot();
}

void SViewportsOverlay::RemoveSlot()
{
	return OverlayWidget->RemoveSlot();
}

const FVector2D& SViewportsOverlay::GetCachedSize() const
{
	return CachedSize;
}

TSharedPtr<FViewportTabContent> SViewportsOverlay::GetViewportTab() const
{
	return ViewportTab;
}


// FAssetEditorViewportLayout /////////////////////////////

FAssetEditorViewportLayout::FAssetEditorViewportLayout() {}


FAssetEditorViewportLayout::~FAssetEditorViewportLayout()
{
	for (auto& Pair : Viewports)
	{
		Pair.Value->OnLayoutDestroyed();
	}
}

TSharedRef<IEditorViewportLayoutEntity> FAssetEditorViewportLayout::FactoryViewport(TFunction<TSharedRef<SEditorViewport>(void)> &Func, FName InTypeName, const FAssetEditorViewportConstructionArgs& ConstructionArgs) const
{
	TSharedRef<FEditorViewportLayoutEntity> LayoutEntity  = MakeShareable(new FEditorViewportLayoutEntity(Func, ConstructionArgs));
	return LayoutEntity;
}



TSharedRef<SWidget> FAssetEditorViewportLayout::BuildViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, TSharedPtr<SDockTab> InParentDockTab, TSharedPtr<class FViewportTabContent> InParentTab, const FString& LayoutString)
{
	// We don't support reconfiguring an existing layout object, as this makes handling of transitions
	// particularly difficult.  Instead just destroy the old layout and create a new layout object.
	check(!ParentTab.IsValid());
	ParentTab = InParentDockTab;
	ParentTabContent = InParentTab;

	// We use an overlay so that we can draw a maximized viewport on top of the other viewports
	TSharedPtr<SBorder> ViewportsBorder;
	TSharedRef<SViewportsOverlay> ViewportsOverlay =
		SNew(SViewportsOverlay)
		.ViewportTab(InParentTab)
		[
			SAssignNew(ViewportsBorder, SBorder)
			.Padding(0.0f)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		];

	ViewportsOverlayPtr = ViewportsOverlay;

	// Don't set the content until the OverlayPtr has been set, because it access this when we want to start with the viewports maximized.
	ViewportsBorder->SetContent(MakeViewportLayout(Func, LayoutString));

	return ViewportsOverlay;

}

TSharedRef<SWidget> FAssetEditorViewportLayout::GenerateLayoutMenu(TSharedPtr<SAssetEditorViewport> AssetEditorViewport)
{
	const bool bInShouldCloseWindowAfterMenuSelection = true;

	TSharedPtr<FUICommandList> CommandList = AssetEditorViewport->GetCommandList();

	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("OnePaneConfigHeader", "One Pane"));
	{
 		FToolBarBuilder OnePaneButton(CommandList, FMultiBoxCustomization::None);
		OnePaneButton.SetLabelVisibility(EVisibility::Collapsed);
		OnePaneButton.SetStyle(&FEditorStyle::Get(), "ViewportLayoutToolbar");

 		OnePaneButton.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_OnePane);

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				OnePaneButton.MakeWidget()
			]
		+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNullWidget::NullWidget
			],
			FText::GetEmpty(), true
			);
	}
	MenuBuilder.EndSection();
 
	MenuBuilder.BeginSection("EditorViewportTwoPaneConfigs", LOCTEXT("TwoPaneConfigHeader", "Two Panes"));
	{
		FToolBarBuilder TwoPaneButtons(CommandList, FMultiBoxCustomization::None);
		TwoPaneButtons.SetLabelVisibility(EVisibility::Collapsed);
		TwoPaneButtons.SetStyle(&FEditorStyle::Get(), "ViewportLayoutToolbar");

		TwoPaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_TwoPanesH, NAME_None, FText());
		TwoPaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_TwoPanesV, NAME_None, FText());

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				TwoPaneButtons.MakeWidget()
			]
		+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNullWidget::NullWidget
			],
			FText::GetEmpty(), true
			);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("EditorViewportThreePaneConfigs", LOCTEXT("ThreePaneConfigHeader", "Three Panes"));
	{
		FToolBarBuilder ThreePaneButtons(CommandList, FMultiBoxCustomization::None);
		ThreePaneButtons.SetLabelVisibility(EVisibility::Collapsed);
		ThreePaneButtons.SetStyle(&FEditorStyle::Get(), "ViewportLayoutToolbar");

		ThreePaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_ThreePanesLeft, NAME_None, FText());
		ThreePaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_ThreePanesRight, NAME_None, FText());
		ThreePaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_ThreePanesTop, NAME_None, FText());
		ThreePaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_ThreePanesBottom, NAME_None, FText());

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ThreePaneButtons.MakeWidget()
			]
		+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNullWidget::NullWidget
			],
			FText::GetEmpty(), true
			);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("EditorViewportFourPaneConfigs", LOCTEXT("FourPaneConfigHeader", "Four Panes"));
	{
		FToolBarBuilder FourPaneButtons(CommandList, FMultiBoxCustomization::None);
		FourPaneButtons.SetLabelVisibility(EVisibility::Collapsed);
		FourPaneButtons.SetStyle(&FEditorStyle::Get(), "ViewportLayoutToolbar");

		FourPaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_FourPanes2x2, NAME_None, FText());
		FourPaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_FourPanesLeft, NAME_None, FText());
		FourPaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_FourPanesRight, NAME_None, FText());
		FourPaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_FourPanesTop, NAME_None, FText());
		FourPaneButtons.AddToolBarButton(FEditorViewportCommands::Get().ViewportConfig_FourPanesBottom, NAME_None, FText());

		MenuBuilder.AddWidget(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				FourPaneButtons.MakeWidget()
			]
		+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNullWidget::NullWidget
			],
			FText::GetEmpty(), true
			);
	}
	MenuBuilder.EndSection();


	return MenuBuilder.MakeWidget();

}

#undef LOCTEXT_NAMESPACE

