// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkCameraControllerCustomization.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Images/SImage.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "../../LiveLinkCamera/Public/LiveLinkCameraController.h"
#include "LiveLinkComponents/Public/LiveLinkComponentController.h"
#include "Roles/LiveLinkCameraRole.h"


#define LOCTEXT_NAMESPACE "FFileMediaSourceCustomization"


/* IDetailCustomization interface
 *****************************************************************************/

void FLiveLinkCameraControllerCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects = DetailBuilder.GetSelectedObjects();
	
	//If more than one don't add up warning icon logic
	if (SelectedObjects.Num() != 1)
	{
		return;
	}

	if (ULiveLinkComponentController* SelectedPtr = Cast<ULiveLinkComponentController>(SelectedObjects[0].Get()))
	{
		EditedObject = SelectedPtr;

		IDetailCategoryBuilder& LensCategory = DetailBuilder.EditCategory("Lens");
		{
			//Customize LensFile property to show a warning if it's needed
			TSharedPtr<IPropertyHandle> LensFileProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULiveLinkCameraController, LensFile));
			{
				IDetailPropertyRow& LensFileRow = LensCategory.AddProperty(LensFileProperty);
				LensFileRow
					.ShowPropertyButtons(false)
					.CustomWidget()
					.NameContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						LensFileProperty->CreatePropertyNameWidget()
					]
				+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("Icons.Warning"))
					.ToolTipText(LOCTEXT("LensFileWarning", "The selected LiveLink subjects requires encoder mapping the current lens file is invalid."))
					.Visibility(this, &FLiveLinkCameraControllerCustomization::HandleEncoderMappingWarningIconVisibility)
					]
					]
				.ValueContent()
					.MinDesiredWidth(250.f)
					[
						LensFileProperty->CreatePropertyValueWidget()
					];
				}
		}
	}


}


/* FFileMediaSourceCustomization callbacks
 *****************************************************************************/

EVisibility FLiveLinkCameraControllerCustomization::HandleEncoderMappingWarningIconVisibility() const
{
	EVisibility NewVisibility = EVisibility::Hidden;
	if (ULiveLinkComponentController* Component = EditedObject.Get())
	{
		if (ULiveLinkControllerBase** BasePtr = Component->ControllerMap.Find(ULiveLinkCameraRole::StaticClass()))
		{
			if (ULiveLinkCameraController* CameraController = Cast<ULiveLinkCameraController>(*BasePtr))
			{
				if (CameraController->IsEncoderMappingNeeded())
				{
					if (CameraController->LensFile == nullptr)
					{
						NewVisibility = EVisibility::Visible;
					}
				}
			}
		}
	}
	
	return NewVisibility;
}


#undef LOCTEXT_NAMESPACE
