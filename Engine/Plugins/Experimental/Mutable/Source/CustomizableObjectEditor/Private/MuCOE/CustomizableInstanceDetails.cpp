// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableInstanceDetails.h"

#include "Containers/Array.h"
#include "Delegates/Delegate.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "HAL/PlatformCrt.h"
#include "IDetailsView.h"
#include "Internationalization/Internationalization.h"
#include "Misc/AssertionMacros.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCOE/SCustomizableInstanceProperties.h"
#include "Templates/Casts.h"
#include "UObject/NameTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UObject;

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
}


void FCustomizableInstanceDetails::Refresh() const
{
	if (IDetailLayoutBuilder* Layout = LayoutBuilder.Pin().Get()) // Raw because we don't want to keep alive the details builder when calling the force refresh details
	{
		Layout->ForceRefreshDetails();
	}
}


#undef LOCTEXT_NAMESPACE
