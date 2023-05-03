// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Library/DMXEntityReference.h"

struct FPropertyChangedEvent;

class FDMXPixelMappingToolkit;
class UDMXLibrary;
class UDMXPixelMapping;
class UDMXPixelMappingBaseComponent;
class UDMXPixelMappingFixtureGroupComponent;

class IDetailLayoutBuilder;
class IPropertyHandle;
class IPropertyUtilities;


class FDMXPixelMappingDetailCustomization_FixtureGroup
	: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
	{
		return MakeShared<FDMXPixelMappingDetailCustomization_FixtureGroup>(InToolkitWeakPtr);
	}

	/** Constructor */
	FDMXPixelMappingDetailCustomization_FixtureGroup(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
		: ToolkitWeakPtr(InToolkitWeakPtr)
	{}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	/** Called when a component was added */
	void OnComponentAdded(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component);

	/** Called when a component was removed */
	void OnComponentRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* Component);

	/** Called before the SizeX property changed */
	void OnSizePropertyPreChange();

	/** Called when the SizeX property changed */
	void OnSizePropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Handles the size property changed, useful to call on tick */
	void HandleSizePropertyChanged();

	/** Forces the detail layout to refresh */
	void ForceRefresh();

	/** Updates the bCachedScaleChildrenWithParent member from UDMXPixelMappingLayoutSettings */
	void UpdateCachedScaleChildrenWithParent();

	/** Helper that returns the library selected in for the group */
	UDMXLibrary* GetSelectedDMXLibrary(UDMXPixelMappingFixtureGroupComponent* FixtureGroupComponent) const;

	/** Returns the currently selected fixture group */
	UDMXPixelMappingFixtureGroupComponent* GetSelectedFixtureGroupComponent(const IDetailLayoutBuilder& InDetailLayout) const;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;

	/** The single fixture group component in use */
	TWeakObjectPtr<UDMXPixelMappingFixtureGroupComponent> WeakFixtureGroupComponent;

	/** SizeX before it got changed */
	TMap<TWeakObjectPtr<UDMXPixelMappingFixtureGroupComponent>, FVector2D> PreEditChangeComponentToSizeMap;

	/** Cached UDMXPixelMappingLayoutSettings::bScaleChildrenWithParen, to avoid repetitive reads during interactive changes */
	bool bCachedScaleChildrenWithParent = false;

	/** Property handle for the SizeX property */
	TSharedPtr<IPropertyHandle> SizeXHandle;

	/** Property handle for the SizeY property */
	TSharedPtr<IPropertyHandle> SizeYHandle;

	/** Delegate handle while being bound to the OnEndFrame event */
	FDelegateHandle RequestForceRefreshHandle;

	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
