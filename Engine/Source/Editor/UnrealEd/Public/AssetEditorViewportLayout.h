// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Stats/Stats.h"
#include "Misc/Attribute.h"
#include "Animation/CurveSequence.h"
#include "Layout/Visibility.h"
#include "Widgets/SWidget.h"
#include "Editor/UnrealEdTypes.h"
#include "Application/ThrottleManager.h"
#include "EditorViewportLayout.h"
#include "TickableEditorObject.h"
#include "SEditorViewport.h"

class FAssetEditorViewportLayout;
class SAssetEditorViewportsOverlay;
class SWindow;
class SAssetEditorViewport;

/** Arguments for constructing a viewport */
struct FAssetEditorViewportConstructionArgs
{
	FAssetEditorViewportConstructionArgs()
		: ViewportType(LVT_Perspective)
		, bRealtime(false)
	{}

	/** The viewport's parent layout */
	TSharedPtr<FAssetEditorViewportLayout> ParentLayout;
	/** The viewport's desired type */
	ELevelViewportType ViewportType;
	/** Whether the viewport should default to realtime */
	bool bRealtime;
	/** A config key for loading/saving settings for the viewport */
	FName ConfigKey;
	/** Widget enabled attribute */
	TAttribute<bool> IsEnabled;
};



namespace EditorViewportConfigurationNames
{
	static FName TwoPanesHoriz("TwoPanesHoriz");
	static FName TwoPanesVert("TwoPanesVert");
	static FName ThreePanesLeft("ThreePanesLeft");
	static FName ThreePanesRight("ThreePanesRight");
	static FName ThreePanesTop("ThreePanesTop");
	static FName ThreePanesBottom("ThreePanesBottom");
	static FName FourPanesLeft("FourPanesLeft");
	static FName FourPanesRight("FourPanesRight");
	static FName FourPanesTop("FourPanesTop");
	static FName FourPanesBottom("FourPanesBottom");
	static FName FourPanes2x2("FourPanes2x2");
	static FName OnePane("OnePane");
}
/**
 * Base class for viewport layout configurations
 * Handles maximizing and restoring well as visibility of specific viewports.
 */
class UNREALED_API FAssetEditorViewportLayout : public TSharedFromThis<FAssetEditorViewportLayout>, public FEditorViewportLayout, public FTickableEditorObject
{
public:
	/**
	 * Constructor
	 */
	FAssetEditorViewportLayout();

	/**
	 * Destructor
	 */
	virtual ~FAssetEditorViewportLayout();

	/** Create an instance of a custom viewport from the specified viewport type name */
	TSharedRef<IEditorViewportLayoutEntity> FactoryViewport(TFunction<TSharedRef<SEditorViewport>(void)> &Func, FName InTypeName, const FAssetEditorViewportConstructionArgs& ConstructionArgs) const;

	/** FTickableEditorObject interface */
	virtual void Tick(float DeltaTime) override {}
	virtual bool IsTickable() const override { return false; }
 	virtual TStatId GetStatId() const override { return TStatId(); }

	/**
	 * Builds a viewport layout and returns the widget containing the layout
	 * 
	 * @param InParentDockTab		The parent dock tab widget of this viewport configuration
	 * @param InParentTab			The parent tab object
	 * @param LayoutString			The layout string loaded from file to custom build the layout with
	 */
 	TSharedRef<SWidget> BuildViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func,  TSharedPtr<SDockTab> InParentDockTab, TSharedPtr<class FViewportTabContent> InParentTab, const FString& LayoutString );

	/** Returns the parent tab content object */
	TWeakPtr< class FViewportTabContent > GetParentTabContent() const { return ParentTabContent; }

	/** Generates a layout string for persisting settings for this layout based on the runtime type of layout */
	FString GetTypeSpecificLayoutString(const FString& LayoutString) const;

	/**
	 * Overridden in derived classes to set up custom layouts  
	 *
	 * @param LayoutString		The layout string loaded from a file
	 * @return The base widget representing the layout.  Usually a splitter
	 */
  	virtual TSharedRef<SWidget> MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString) = 0;

	/** The overlay widget that handles what viewports should be on top (non-maximized or maximized) */
	TWeakPtr< class SAssetEditorViewportsOverlay > ViewportsOverlayPtr;

	/** The parent tab content object where this layout resides */
	TWeakPtr< class FViewportTabContent > ParentTabContent;

	/** The parent tab where this layout resides */
	TWeakPtr< SDockTab > ParentTab;

};
