// Copyright Epic Games, Inc. All Rights Reserved.

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


// AssetEditorViewport ////////////////////////////////////////////////

/**
* Overlay wrapper class so that we can cache the size of the widget
* It will also store the ViewportLayout data because that data can't be stored
* per app; it must be stored per viewport overlay in case the app that made it closes.
*/
class SAssetEditorViewportsOverlay : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS( SAssetEditorViewportsOverlay ){}
	SLATE_DEFAULT_SLOT( FArguments, Content )
		SLATE_ARGUMENT( TSharedPtr<FViewportTabContent>, ViewportTab )
		SLATE_END_ARGS()

		void Construct( const FArguments& InArgs );

	/** Default constructor */
	SAssetEditorViewportsOverlay()
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


void SAssetEditorViewportsOverlay::Construct( const FArguments& InArgs )
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

void SAssetEditorViewportsOverlay::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	CachedSize = AllottedGeometry.Size;
}

SOverlay::FOverlaySlot& SAssetEditorViewportsOverlay::AddSlot()
{
	return OverlayWidget->AddSlot();
}

void SAssetEditorViewportsOverlay::RemoveSlot()
{
	return OverlayWidget->RemoveSlot();
}

const FVector2D& SAssetEditorViewportsOverlay::GetCachedSize() const
{
	return CachedSize;
}

TSharedPtr<FViewportTabContent> SAssetEditorViewportsOverlay::GetViewportTab() const
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
	TSharedRef<SAssetEditorViewportsOverlay> ViewportsOverlay =
		SNew(SAssetEditorViewportsOverlay)
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

FString FAssetEditorViewportLayout::GetTypeSpecificLayoutString(const FString& LayoutString) const
{
	if (LayoutString.IsEmpty())
	{
		return LayoutString;
	}
	return FString::Printf(TEXT("%s.%s"), *GetLayoutTypeName().ToString(), *LayoutString);
}



#undef LOCTEXT_NAMESPACE

