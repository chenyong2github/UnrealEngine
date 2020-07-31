// Copyright Epic Games, Inc. All Rights Reserved.

#include "Details/DetailWidgetExtensionHandler.h"
#include "UMGEditorProjectSettings.h"
#include "WidgetBlueprintEditor.h"
#include "Engine/Blueprint.h"
#include "Binding/WidgetBinding.h"
#include "WidgetBlueprint.h"
#include "Customizations/UMGDetailCustomizations.h"

FDetailWidgetExtensionHandler::FDetailWidgetExtensionHandler(TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
	: BlueprintEditor( InBlueprintEditor )
{}

bool FDetailWidgetExtensionHandler::IsPropertyExtendable(const UClass* InObjectClass, const IPropertyHandle& InPropertyHandle) const
{
	// TODO UMG make this work for multiple widgets.
	if ( InPropertyHandle.GetNumOuterObjects() == 1 )
	{
		TArray<UObject*> Objects;
		InPropertyHandle.GetOuterObjects(Objects);

		// We don't allow bindings on the CDO.
		if (Objects[0] != nullptr && Objects[0]->HasAnyFlags(RF_ClassDefaultObject) )
		{
			return false;
		}

		TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();
		if (BPEd == nullptr || Objects[0] == BPEd->GetPreview())
		{
			return false;
		}

		FProperty* Property = InPropertyHandle.GetProperty();
		FString DelegateName = Property->GetName() + "Delegate";

		if ( UClass* ContainerClass = Property->GetOwner<UClass>() )
		{
			FDelegateProperty* DelegateProperty = FindFProperty<FDelegateProperty>(ContainerClass, FName(*DelegateName));
			if ( DelegateProperty )
			{
				return true;
			}
		}
	}

	return false;
}

TSharedRef<SWidget> FDetailWidgetExtensionHandler::GenerateExtensionWidget(const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass, TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	FProperty* Property = InPropertyHandle->GetProperty();
	FString DelegateName = Property->GetName() + "Delegate";
	
	FDelegateProperty* DelegateProperty = FindFieldChecked<FDelegateProperty>(Property->GetOwnerChecked<UClass>(), FName(*DelegateName));

	const bool bIsEditable = Property->HasAnyPropertyFlags(CPF_Edit | CPF_EditConst);
	const bool bDoSignaturesMatch = DelegateProperty->SignatureFunction->GetReturnProperty()->SameType(Property);

	if ( !ensure(bIsEditable && bDoSignaturesMatch) )
	{
		return SNullWidget::NullWidget;
	}

	UWidgetBlueprint* WidgetBlueprint = BlueprintEditor.Pin()->GetWidgetBlueprintObj();

	if (!WidgetBlueprint->ArePropertyBindingsAllowed())
	{
		// Even if they don't want them on, we need to show them so they can remove them if they had any.
		if (BlueprintEditor.Pin()->GetWidgetBlueprintObj()->Bindings.Num() == 0)
		{
			return SNullWidget::NullWidget;
		}
	}

	return FBlueprintWidgetCustomization::MakePropertyBindingWidget(BlueprintEditor, DelegateProperty, InPropertyHandle.ToSharedRef(), true);
}
