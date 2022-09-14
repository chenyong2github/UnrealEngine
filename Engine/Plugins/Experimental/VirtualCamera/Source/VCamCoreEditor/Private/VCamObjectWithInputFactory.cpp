// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamObjectWithInputFactory.h"

#include "AssetThumbnail.h"
#include "Editor.h"
#include "PropertyCustomizationHelpers.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "VCamObjectFactories"

bool UVCamObjectWithInputFactory::ConfigureProperties()
{
	class FVCamWidgetFactoryUI : public TSharedFromThis<FVCamWidgetFactoryUI>
	{
	public:
		FReply OnCreate()
		{
			if (PickerWindow.IsValid())
			{
				PickerWindow->RequestDestroyWindow();
			}
			bWasCanceled= false;
			return FReply::Handled();
		}

		FReply OnCancel()
		{
			if (PickerWindow.IsValid())
			{
				PickerWindow->RequestDestroyWindow();
			}
			bWasCanceled = true;
			return FReply::Handled();
		}

		bool WasCanceled() const
		{
			return bWasCanceled;
		}

		void OpenMappingSelector(UVCamObjectWithInputFactory* Factory)
		{
			AssetThumbnailPool = MakeShared<FAssetThumbnailPool>(1);
			PickerWindow = SNew(SWindow)
				.Title(LOCTEXT("PickerTitle", "Select Input Mapping Context"))
				.ClientSize(FVector2D(350, 100))
				.SupportsMinimize(false)
				.SupportsMaximize(false)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("Menu.Background"))
					.Padding(10)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.FillHeight(1.f)
						[
							SNew(SObjectPropertyEntryBox)
							.DisplayThumbnail(true)
							.ThumbnailPool(AssetThumbnailPool)
							.AllowClear(true)
							.DisplayUseSelected(false)
							.DisplayBrowse(false)
							.AllowedClass(UInputMappingContext::StaticClass())
							.ObjectPath_Lambda([Factory]() -> FString
							{
								return (Factory && Factory->InputMappingContext) ? Factory->InputMappingContext->GetPathName() : TEXT("None");
							})
							.OnObjectChanged_Lambda([Factory](const FAssetData& AssetData) -> void
							{
								if (Factory)
								{
									Factory->InputMappingContext = Cast<UInputMappingContext>(AssetData.GetAsset());
								}
							})
						]
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Bottom)
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(LOCTEXT("OK", "OK"))
								.OnClicked(this, &FVCamWidgetFactoryUI::OnCreate)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(LOCTEXT("Cancel", "Cancel"))
								.OnClicked(this, &FVCamWidgetFactoryUI::OnCancel)
							]
						]
					]
				];
			if (GEditor)
			{
				GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());
			}
			PickerWindow.Reset();
		}

	private:
		TSharedPtr<SWindow> PickerWindow;
		TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;
		bool bWasCanceled = false;
	};

	const TSharedRef<FVCamWidgetFactoryUI> InputMappingSelector = MakeShared<FVCamWidgetFactoryUI>();
	InputMappingSelector->OpenMappingSelector(this);
	
	return !InputMappingSelector->WasCanceled();
}

#undef LOCTEXT_NAMESPACE