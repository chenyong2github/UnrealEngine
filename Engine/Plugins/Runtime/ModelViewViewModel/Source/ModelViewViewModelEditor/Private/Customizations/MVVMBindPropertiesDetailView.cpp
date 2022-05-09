// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/MVVMBindPropertiesDetailView.h"

#include "Components/Widget.h"
#include "WidgetBlueprint.h"

#include "MVVMWidgetBlueprintExtension_View.h"
#include "MVVMPropertyAccess.h"

#include "Styling/AppStyle.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"

#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "BindPropertiesDetailView"

bool FMVVMBindPropertiesDetailView::IsSupported(const FOnGenerateGlobalRowExtensionArgs& InArgs)
{
	if (!InArgs.PropertyHandle)
	{
		return false;
	}

	if (!InArgs.PropertyHandle->GetOuterBaseClass()->IsChildOf(UWidget::StaticClass()))
	{
		return false;
	}

	static const FName BlueprintSetterMetaData = TEXT("BlueprintSetter");
	if (!InArgs.PropertyHandle->GetProperty()->HasMetaData(BlueprintSetterMetaData))
	{
		return false;
	}

	return true;
}

bool FMVVMBindPropertiesDetailView::GetBindingInfo(const TSharedPtr<IPropertyHandle>& PropertyHandle, FBinding& OutBinding)
{
	TArray<UObject*> OuterObjects;
	PropertyHandle->GetOuterObjects(OuterObjects);
	if (OuterObjects.Num() == 0)
	{
		return false;
	}

	UWidgetBlueprint* WidgetBlueprint = nullptr;
	for (UObject* OuterObject : OuterObjects)
	{
		if (UWidget* Widget = Cast<UWidget>(OuterObject))
		{
			if (!Widget->IsDesignTime())
			{
				return false;
			}

			UWidgetBlueprint* CurrentWidgetBlueprint = Cast<UWidgetBlueprint>(Widget->WidgetGeneratedBy);
			if (!CurrentWidgetBlueprint)
			{
				return false;
			}
			if (WidgetBlueprint && CurrentWidgetBlueprint != WidgetBlueprint)
			{
				return false;
			}

			OutBinding.Widgets.Add(Widget);
			WidgetBlueprint = CurrentWidgetBlueprint;
		}
		else
		{
			return false;
		}
	}


	OutBinding.WidgetBlueprint = WidgetBlueprint;
	OutBinding.PropertyHandle = PropertyHandle;
	return true;
}

void FMVVMBindPropertiesDetailView::CreatePropertyRowExtension(const FOnGenerateGlobalRowExtensionArgs& InArgs, TArray<FPropertyRowExtensionButton>& OutExtensions)
{
	if (IsSupported(InArgs))
	{
		FBinding BindingInfo;
		if (GetBindingInfo(InArgs.PropertyHandle, BindingInfo))
		{
			FPropertyRowExtensionButton& ExposeButton = OutExtensions.AddDefaulted_GetRef();
			ExposeButton.Icon = FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Level.ScriptIcon16x");
			ExposeButton.Label = LOCTEXT("ExposeProperty", "MVVM Property");
			ExposeButton.ToolTip = LOCTEXT("ExposePropertyToolTip", "Bind a View property to a ViewModel.");
			ExposeButton.UIAction = FUIAction(
				FExecuteAction(),
				//FExecuteAction::CreateRaw(this, &FMVVMBindPropertiesDetailView::HandleExecutePropertyExposed, BindingInfo),
				FCanExecuteAction(),
				FGetActionCheckState::CreateRaw(this, &FMVVMBindPropertiesDetailView::GetPropertyExposedCheckState, BindingInfo)
			);
			//ExposeButton.MenuContentGenerator = FOnGetContent::CreateRaw(this, &FMVVMBindPropertiesDetailView::HandleExecutePropertyExposed, BindingInfo);
		}
	}
}
//
//#include "Widgets/Input/SButton.h"
//TSharedRef<SWidget> FMVVMBindPropertiesDetailView::HandleExecutePropertyExposed(FBinding BindingInfo)
//{
//	if (UWidgetBlueprint* WidgetBlueprint = BindingInfo.WidgetBlueprint.Get())
//	{
//			if (TSharedPtr<IPropertyHandle> PropertyHandle = BindingInfo.PropertyHandle.Pin())
//			{
//				Poppup = MakeShared<FMVVMPropertyAccess>(WidgetBlueprint, PropertyHandle.ToSharedRef());
//				TSharedPtr<SWidget> MenuWidget = Poppup->MakeViewModelBindingMenu();
//				if (MenuWidget)
//				{
//					return MenuWidget.ToSharedRef();
//				}
//			}
// 
//			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
//			if (IAssetEditorInstance* AssetEditorInstance = FindEditorForAsset(UObject * Asset, bool bFocusIfOpen))
//			{
//				if (AssetEditorInstance->GetEditorName() == TEXT("WidgetBlueprintEditor"))
//				{
//					FWidgetBlueprintEditor* WidgetBlueprintEditor = static_cast<FWidgetBlueprintEditor>(AssetEditorInstance);
//					check(WidgetBlueprintEditor->GetBlueprintObj() == WidgetBlueprint);
//				}
//			}
//	}
//	return SNullWidget::NullWidget;
//}


ECheckBoxState FMVVMBindPropertiesDetailView::GetPropertyExposedCheckState(FBinding BindingInfo) const
{
	//if (UWidgetBlueprint* WidgetBlueprint = BindingInfo.WidgetBlueprint.Get())
	//{
	//	if (UMVVMViewModelBlueprintExtension* Extension = UWidgetBlueprintExtension::GetExtension<UMVVMViewModelBlueprintExtension>(WidgetBlueprint))
	//	{
	//		if (TSharedPtr<IPropertyHandle> PropertyHandle = BindingInfo.PropertyHandle.Pin())
	//		{
	//			int32 ValidCount = 0;
	//			int32 InvalidValidCount = 0;
	//			for (TWeakObjectPtr<UWidget>& WeakObject : BindingInfo.Widgets)
	//			{
	//				if (UObject* Object = WeakObject.Get())
	//				{
	//					if (Extension->FindViewBinding(Object, PropertyHandle.Get()))
	//					{
	//						++ValidCount;
	//					}
	//				}
	//				else
	//				{
	//					return ECheckBoxState::Undetermined;
	//				}
	//			}

	//			if (ValidCount == BindingInfo.Widgets.Num())
	//			{
	//				return ECheckBoxState::Checked;
	//			}
	//			else if (ValidCount == 0)
	//			{
	//				return ECheckBoxState::Unchecked;
	//			}
	//			else
	//			{
	//				return ECheckBoxState::Undetermined;
	//			}
	//		}
	//		else
	//		{
	//			return ECheckBoxState::Undetermined;
	//		}
	//	}
	//}
	return ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE
