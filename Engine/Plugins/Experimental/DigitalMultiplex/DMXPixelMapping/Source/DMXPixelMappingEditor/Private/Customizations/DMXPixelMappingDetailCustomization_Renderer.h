// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakObjectPtr.h"

class FDMXPixelMappingToolkit;
class IPropertyHandle;
class IDetailCategoryBuilder;
class UDMXPixelMappingRendererComponent;
struct EVisibility;
enum class EDMXPixelMappingRendererType : uint8;

class FDMXPixelMappingDetailCustomization_Renderer
	: public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
	{
		return MakeShared<FDMXPixelMappingDetailCustomization_Renderer>(InToolkitWeakPtr);
	}

	/** Constructor */
	FDMXPixelMappingDetailCustomization_Renderer(const TWeakPtr<FDMXPixelMappingToolkit> InToolkitWeakPtr)
		: ToolkitWeakPtr(InToolkitWeakPtr)
	{}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	EVisibility IsSelectedRendererType(EDMXPixelMappingRendererType PropertyRendererType) const;

	EVisibility IsNotSelectedRendererType(EDMXPixelMappingRendererType PropertyRendererType) const;

	EVisibility GetInputTextureWarning() const;

	FText GetInputTextureWarningText() const;

	void AddInputTextureWarning(IDetailCategoryBuilder& InCategory);

	/** Add warning for material with domain != MD_UI */
	void AddMaterialWarning(IDetailCategoryBuilder& InCategory);

	/** Visibility for non ui material warning */
	EVisibility GetMaterialWarningVisibility() const;

protected:
	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXPixelMappingToolkit> ToolkitWeakPtr;

private:
	TSharedPtr<IPropertyHandle> RendererTypePropertyHandle;
	TSharedPtr<IPropertyHandle> InputTexturePropertyHandle;
	TSharedPtr<IPropertyHandle> InputMaterialPropertyHandle;
	TSharedPtr<IPropertyHandle> InputWidgetPropertyHandle;
	TSharedPtr<IPropertyHandle> SizeXPropertyHandle;
	TSharedPtr<IPropertyHandle> SizeYPropertyHandle;

	TWeakObjectPtr<UDMXPixelMappingRendererComponent> RendererComponent;
};