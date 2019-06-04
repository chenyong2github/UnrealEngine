// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_ACCESSIBILITY

#include "CoreMinimal.h"
#include "GenericPlatform/GenericAccessibleInterfaces.h"

class SWidget;
class SWindow;

/**
 * The base implementation of IAccessibleWidget for all Slate widgets. Any new accessible widgets should
 * inherit directly from FSlateAccessibleWidget, and optionally inherit from other IAccessible interfaces to
 * provide more functionality.
 */
class SLATECORE_API FSlateAccessibleWidget : public IAccessibleWidget
{
public:
	FSlateAccessibleWidget(TWeakPtr<SWidget> InWidget, EAccessibleWidgetType InWidgetType = EAccessibleWidgetType::Unknown);
	virtual ~FSlateAccessibleWidget();

	// IAccessibleWidget
	virtual AccessibleWidgetId GetId() const override final;
	virtual bool IsValid() const override final;
	virtual TSharedPtr<IAccessibleWidget> GetTopLevelWindow() const override final;
	virtual FBox2D GetBounds() const override final;
	virtual TSharedPtr<IAccessibleWidget> GetParent() override final;
	virtual TSharedPtr<IAccessibleWidget> GetNextSibling() override final;
	virtual TSharedPtr<IAccessibleWidget> GetPreviousSibling() override final;
	virtual TSharedPtr<IAccessibleWidget> GetChildAt(int32 Index) override final;
	virtual int32 GetNumberOfChildren() override final;
	virtual FString GetClassName() const override final;
	virtual bool IsEnabled() const override final;
	virtual bool IsHidden() const override final;
	virtual bool SupportsFocus() const override final;
	virtual bool HasFocus() const override final;
	virtual void SetFocus() override final;

	virtual EAccessibleWidgetType GetWidgetType() const override { return WidgetType; }
	virtual FString GetWidgetName() const override;
	virtual FString GetHelpText() const override;
	// ~

	/** Tell this widget to recompute its children the next time they are requested. */
	void MarkChildrenDirty();
	/**
	 * Detach this widget from its current parent and attach it to a new parent. This will emit notifications back to the accessible message handler.
	 *
	 * @param NewParent The widget to assign as the new parent widget.
	 */
	void UpdateParent(TSharedPtr<IAccessibleWidget> NewParent);
	/**
	 * If MarkChildrenDirty() has been called, recalculate the list of all accessible widgets below this one.
	 * Because SWidget->GetChildren() has no guarantees about what it returns and how it returns it, we can
	 * never truly 100% guarantee that the accessible tree will be in sync with the Slate tree.
	 *
	 * We make a reasonable assumption that widgets are smart about implementing this function to return the
	 * same widgets every time. However, we can't assume anything about when a child gets added or removed
	 * with respect to the ordering of the children. Because of this, we have to recompute their indices
	 * any time we suspect the hierarchy may have changed.
	 *
	 * @param bUpdateRecursively If true, calls UpdateAllChildren() on any children found.
	 */
	void UpdateAllChildren(bool bUpdateRecursively = false);

	/**
	 * Search the Slate hierarchy recursively and generate a list of all accessible widgets whose parent is this widget.
	 *
	 * @param AccessibleWidget The root widget to find children for.
	 * @return All Slate widgets whose accessible parent is the passed-in widget.
	 */
	static TArray<TSharedRef<SWidget>> GetAccessibleChildren(TSharedRef<SWidget> AccessibleWidget);

protected:
	/**
	 * Recursively find the accessible widget under the specified X,Y coordinates.
	 *
	 * @param X The X coordinate to search in absolute screen space.
	 * @param Y The Y coordinate to search in absolute screen space.
	 * @return The deepest accessible widget found.
	 */
	TSharedPtr<IAccessibleWidget> GetChildAtUsingGeometry(int32 X, int32 Y);

	/** The underlying Slate widget backing this accessible widget. */
	TWeakPtr<SWidget> Widget;
	/** What type of widget the platform's accessibility API should treat this as. */
	EAccessibleWidgetType WidgetType;
	/** The accessible parent to this widget. This should usually be valid on widgets in the hierarchy, except for SWindows. */
	TWeakPtr<FSlateAccessibleWidget> Parent;
	/** All accessible widgets whose parent is this widget. This is not necessarily correct unless UpdateAllChildren() is called first. */
	TArray<TWeakPtr<FSlateAccessibleWidget>> Children;
	/** The index of this widget in its parent's list of children. */
	int32 SiblingIndex;
	/** An application-unique identifier for GetId(). */
	AccessibleWidgetId Id;
	/** Whether the contents of the Children array has changed and UpdateAllChildren() needs to be called. */
	bool bChildrenDirty;

private:
	/**
	 * Find the Slate window containing this widget's underlying Slate widget.
	 *
	 * @return The parent SWindow for the Slate widget referenced by this accessible widget.
	 */
	TSharedPtr<SWindow> GetTopLevelSlateWindow() const;
};

// SWindow
class FSlateAccessibleWindow
	: public FSlateAccessibleWidget
	, public IAccessibleWindow
{
public:
	FSlateAccessibleWindow(TWeakPtr<SWidget> InWidget) : FSlateAccessibleWidget(InWidget, EAccessibleWidgetType::Window) {}
	virtual ~FSlateAccessibleWindow() {}

	// IAccessibleWidget
	virtual IAccessibleWindow* AsWindow() override { return this; }
	virtual FString GetWidgetName() const override;
	// ~

	// IAccessibleWindow
	virtual TSharedPtr<FGenericWindow> GetNativeWindow() const override;
	virtual TSharedPtr<IAccessibleWidget> GetChildAtPosition(int32 X, int32 Y) override;
	virtual TSharedPtr<IAccessibleWidget> GetFocusedWidget() const override;
	virtual void Close() override;
	virtual bool SupportsDisplayState(EWindowDisplayState State) const override;
	virtual EWindowDisplayState GetDisplayState() const override;
	virtual void SetDisplayState(EWindowDisplayState State) override;
	virtual bool IsModal() const override;
	// ~
};
// ~

// SImage
class SLATECORE_API FSlateAccessibleImage
	: public FSlateAccessibleWidget
{
public:
	FSlateAccessibleImage(TWeakPtr<SWidget> InWidget) : FSlateAccessibleWidget(InWidget, EAccessibleWidgetType::Image) {}
	virtual ~FSlateAccessibleImage() {}

	// IAccessibleWidget
	virtual FString GetHelpText() const override;
	// ~
};
// ~

#endif
