// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableInstanceDetails.h"
#include "CustomizableObjectEditorModule.h"
#include "CustomizableObjectSystem.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "SCustomizableInstanceProperties.h"
#include "LevelEditorActions.h"

#define LOCTEXT_NAMESPACE "CustomizableInstanceDetails"


TSharedRef<IDetailCustomization> FCustomizableInstanceDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableInstanceDetails);
}


void FCustomizableInstanceDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	const IDetailsView* DetailsView = DetailBuilder->GetDetailsView();
	check(DetailsView->GetSelectedObjects().Num());

	CustomInstance = Cast<UCustomizableObjectInstance>(DetailsView->GetSelectedObjects()[0].Get());
	check(CustomInstance.IsValid());
	
	LayoutBuilder = DetailBuilder;
	
	IDetailCategoryBuilder& MainCategory = DetailBuilder->EditCategory( "Customizable Instance" );

	MainCategory.AddCustomRow( LOCTEXT("CustomizableInstanceDetails", "Instance Parameters") )
	[
		SAssignNew(InstancePropertiesWidget, SCustomizableInstanceProperties)
			.CustomInstance(CustomInstance)
			.InstanceDetails(SharedThis(this))
	];

	
	// If the current instance has texture parameters, show the user interface to set the possible preview
	// values for those parameters, in case no provider is registered.
	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstance();

	if (CustomInstance->GetTextureParameters().Num())
	{
		if (UCustomizableObjectImageProviderArray* ImageProvider = System->GetEditorExternalImageProvider())
		{
			ImageProvider->TexturesChangeDelegate.AddSP(this, &FCustomizableInstanceDetails::UpdateInstance);

			IDetailCategoryBuilder& PreviewCategory = DetailBuilder->EditCategory("Preview Texture Parameter Options");

			TArray<UObject*> Objs;
			Objs.Add(ImageProvider);
			PreviewCategory.AddExternalObjectProperty(Objs, FName("Textures"));
		}
	}
}


void FCustomizableInstanceDetails::Refresh() const
{
	if (IDetailLayoutBuilder* Layout = LayoutBuilder.Pin().Get()) // Raw because we don't want to keep alive the details builder when calling the force refresh details
	{
		Layout->ForceRefreshDetails();
	}
}


void FCustomizableInstanceDetails::UpdateInstance() const
{
		CustomInstance->UpdateSkeletalMeshAsync(true, true);
}


#undef LOCTEXT_NAMESPACE
