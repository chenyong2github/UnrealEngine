// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "IDetailRootObjectCustomization.h"
#include "PropertyRestriction.h"
#include "Widgets/SWidget.h"

class SHorizontalBox;
class UMeshVertexPaintingToolProperties;


class FVertexPaintingSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
protected:
	/** Callback for when texture weight type changed so we can update restrictions */
	void OnTextureWeightTypeChanged(TSharedRef<IPropertyHandle> WeightTypeProperty, TSharedRef<IPropertyHandle> PaintWeightProperty, TSharedRef<IPropertyHandle> EraseWeightProperty);
	FReply OnSwapColorsClicked(TSharedRef<IPropertyHandle> PaintColor, TSharedRef<IPropertyHandle> EraseColor);

	/** Property restriction applied to blend paint enum dropdown box */
	TSharedPtr<FPropertyRestriction> BlendPaintEnumRestriction;
};

class FColorPaintingSettingsCustomization : public FVertexPaintingSettingsCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
};

class FWeightPaintingSettingsCustomization : public FVertexPaintingSettingsCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
protected:
	void OnTextureWeightTypeChanged(TSharedRef<IPropertyHandle> WeightTypeProperty, TSharedRef<IPropertyHandle> PaintWeightProperty, TSharedRef<IPropertyHandle> EraseWeightProperty);

	/** Property restriction applied to blend paint enum dropdown box */
	TSharedPtr<FPropertyRestriction> BlendPaintEnumRestriction;

};

// class FTexturePaintSettingsCustomization : public IPropertyTypeCustomization
// {
// public:
// 	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
// 
// 	/** IPropertyTypeCustomization interface */
// 	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override {};
// 	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
// protected:
// 	/** Cached instance of the painter */
// 	FPaintModePainter* MeshPainter;
// 	/** Cached instance of the texture painting settings */
// 	FTexturePaintSettings* PaintSettings;
// };


	