// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingDetailCustomization_Renderer.h"
#include "Components/DMXPixelMappingRendererComponent.h"
#include "DMXPixelMappingTypes.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Layout/Visibility.h"
#include "Materials/Material.h"

#define LOCTEXT_NAMESPACE "DMXPixelMappingDetailCustomization_Renderer"

/**
 * Renderer editor warning message widget
 */
class SRendererCustomizationWarningMessage
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRendererCustomizationWarningMessage)
		: _WarningText(FText::GetEmpty())
		{}

		SLATE_ATTRIBUTE(FText, WarningText)

	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param	InArgs	The declaration data for this widget
	 */
	void Construct( const FArguments& InArgs )
	{
		const FSlateBrush* WarningIcon = FEditorStyle::GetBrush("SettingsEditor.WarningIcon");

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("SettingsEditor.CheckoutWarningBorder"))
			.BorderBackgroundColor(FColor (166,137,0))
			[
				SNew(SHorizontalBox)
				.Visibility(EVisibility::Visible)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SImage)
					.Image(WarningIcon)
				]

				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(InArgs._WarningText)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			]
		];
	}
};

void FDMXPixelMappingDetailCustomization_Renderer::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	IDetailCategoryBuilder& RenderSettingsSettingsCategory = DetailLayout.EditCategory("Render Settings", FText::GetEmpty(), ECategoryPriority::Important);

	// Register all hadles
	RendererTypePropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, RendererType));
	InputTexturePropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, InputTexture));
	InputMaterialPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, InputMaterial));
	InputWidgetPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingRendererComponent, InputWidget));
	SizeXPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeX), UDMXPixelMappingOutputComponent::StaticClass());
	SizeYPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeY), UDMXPixelMappingOutputComponent::StaticClass());

	// Hide properties
	TSharedRef<IPropertyHandle> PositionXPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionX), UDMXPixelMappingOutputComponent::StaticClass());
	TSharedRef<IPropertyHandle> PositionYPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionY), UDMXPixelMappingOutputComponent::StaticClass());
	TSharedRef<IPropertyHandle> IsLockInDesignerPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, bLockInDesigner), UDMXPixelMappingOutputComponent::StaticClass());
	TSharedRef<IPropertyHandle> IsVisibleInDesignerPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, bVisibleInDesigner), UDMXPixelMappingOutputComponent::StaticClass());
	TSharedRef<IPropertyHandle> PixelBlendingPropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, CellBlendingQuality), UDMXPixelMappingOutputComponent::StaticClass());
	DetailLayout.HideProperty(PositionXPropertyHandle);
	DetailLayout.HideProperty(PositionYPropertyHandle);
	DetailLayout.HideProperty(IsLockInDesignerPropertyHandle);
	DetailLayout.HideProperty(IsVisibleInDesignerPropertyHandle);
	DetailLayout.HideProperty(PixelBlendingPropertyHandle);

	// Add properties
	RenderSettingsSettingsCategory.AddProperty(RendererTypePropertyHandle);

	// Get editing UObject
	TArray<TWeakObjectPtr<UObject>> OuterObjects;
	DetailLayout.GetObjectsBeingCustomized(OuterObjects);
	if (OuterObjects.Num() == 1)
	{
		RendererComponent = Cast<UDMXPixelMappingRendererComponent>(OuterObjects[0]);

		// Add Warning InputTexture Row
		AddInputTextureWarning(RenderSettingsSettingsCategory);

		// Add non UI Material Warning
		AddMaterialWarning(RenderSettingsSettingsCategory);

		// Retister properties
		RenderSettingsSettingsCategory.AddProperty(InputTexturePropertyHandle)
			.Visibility(
				TAttribute<EVisibility>::Create(
					TAttribute<EVisibility>::FGetter::CreateSP(
						this,
						&FDMXPixelMappingDetailCustomization_Renderer::IsSelectedRendererType,
						EDMXPixelMappingRendererType::Texture
					)));

		RenderSettingsSettingsCategory.AddProperty(InputMaterialPropertyHandle)
			.Visibility(
				TAttribute<EVisibility>::Create(
					TAttribute<EVisibility>::FGetter::CreateSP(
						this,
						&FDMXPixelMappingDetailCustomization_Renderer::IsSelectedRendererType,
						EDMXPixelMappingRendererType::Material
					)));

		RenderSettingsSettingsCategory.AddProperty(InputWidgetPropertyHandle)
			.Visibility(
				TAttribute<EVisibility>::Create(
					TAttribute<EVisibility>::FGetter::CreateSP(
						this,
						&FDMXPixelMappingDetailCustomization_Renderer::IsSelectedRendererType,
						EDMXPixelMappingRendererType::UMG
					)));

		RenderSettingsSettingsCategory.AddProperty(SizeXPropertyHandle)
			.Visibility(
				TAttribute<EVisibility>::Create(
					TAttribute<EVisibility>::FGetter::CreateSP(
						this,
						&FDMXPixelMappingDetailCustomization_Renderer::IsNotSelectedRendererType,
						EDMXPixelMappingRendererType::Texture
					)));

		RenderSettingsSettingsCategory.AddProperty(SizeYPropertyHandle)
			.Visibility(
				TAttribute<EVisibility>::Create(
					TAttribute<EVisibility>::FGetter::CreateSP(
						this,
						&FDMXPixelMappingDetailCustomization_Renderer::IsNotSelectedRendererType,
						EDMXPixelMappingRendererType::Texture
					)));
	}
}

EVisibility FDMXPixelMappingDetailCustomization_Renderer::IsSelectedRendererType(EDMXPixelMappingRendererType PropertyRendererType) const
{
	if (UDMXPixelMappingRendererComponent* Component = RendererComponent.Get())
	{
		if (Component->RendererType == PropertyRendererType)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

EVisibility FDMXPixelMappingDetailCustomization_Renderer::IsNotSelectedRendererType(EDMXPixelMappingRendererType PropertyRendererType) const
{
	if (UDMXPixelMappingRendererComponent* Component = RendererComponent.Get())
	{
		if (Component->RendererType != PropertyRendererType)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

void FDMXPixelMappingDetailCustomization_Renderer::AddInputTextureWarning(IDetailCategoryBuilder& InCategory)
{
	const FSlateBrush* WarningIcon = FEditorStyle::GetBrush("SettingsEditor.WarningIcon");

	InCategory.AddCustomRow(FText::GetEmpty())
			.Visibility(TAttribute<EVisibility>(this, &FDMXPixelMappingDetailCustomization_Renderer::GetInputTextureWarning))
			.WholeRowContent()
			[
				SNew(SRendererCustomizationWarningMessage)
					.WarningText(this, &FDMXPixelMappingDetailCustomization_Renderer::GetInputTextureWarningText)
			];
}

void FDMXPixelMappingDetailCustomization_Renderer::AddMaterialWarning(IDetailCategoryBuilder& InCategory)
{
	InCategory.AddCustomRow(FText::GetEmpty())
			.Visibility(TAttribute<EVisibility>(this, &FDMXPixelMappingDetailCustomization_Renderer::GetMaterialWarningVisibility))
			.WholeRowContent()
			[
				SNew(SRendererCustomizationWarningMessage)
					.WarningText(LOCTEXT("WarningNonUIMaterial", "This is not UI Material.\nChange Material Domain to User Interface.\nOr select another Material."))
			];
}

EVisibility FDMXPixelMappingDetailCustomization_Renderer::GetMaterialWarningVisibility() const
{
	if (UDMXPixelMappingRendererComponent* Component = RendererComponent.Get())
	{
		if (Component->InputMaterial != nullptr)
		{
			if (UMaterial* Material = Component->InputMaterial->GetMaterial())
			{
				if (Component->RendererType == EDMXPixelMappingRendererType::Material && !Material->IsUIMaterial())
				{
					return EVisibility::Visible;
				}
			}
		}
	}

	return EVisibility::Collapsed;
}

EVisibility FDMXPixelMappingDetailCustomization_Renderer::GetInputTextureWarning() const
{
	if (UDMXPixelMappingRendererComponent* Component = RendererComponent.Get())
	{
		if (Component->GetRendererInputTexture() == nullptr)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

FText FDMXPixelMappingDetailCustomization_Renderer::GetInputTextureWarningText() const
{
	if (UDMXPixelMappingRendererComponent* Component = RendererComponent.Get())
	{
		if (Component->GetRendererInputTexture() == nullptr)
		{
			if (Component->RendererType == EDMXPixelMappingRendererType::Texture)
			{
				return LOCTEXT("WarningCategoryDisplayName.TextureNotSet", "Texture is not set.");
			}
			else if (Component->RendererType == EDMXPixelMappingRendererType::Material)
			{
				return LOCTEXT("WarningCategoryDisplayName.MaterialNotSet", "Material is not set.");
			}
			else if (Component->RendererType == EDMXPixelMappingRendererType::UMG)
			{
				return LOCTEXT("WarningCategoryDisplayName.UMGNotSet", "UMG is not set.");
			}
		}
	}

	return FText();
}

#undef LOCTEXT_NAMESPACE
