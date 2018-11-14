// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Details/DetailWidgetExtensionHandler.h"
#include "Details/SPropertyBinding.h"
#include "UMGEditorProjectSettings.h"
#include "WidgetBlueprintEditor.h"
#include "Engine/Blueprint.h"
#include "Binding/WidgetBinding.h"
#include "WidgetBlueprint.h"

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

		UProperty* Property = InPropertyHandle.GetProperty();
		FString DelegateName = Property->GetName() + "Delegate";

		if ( UClass* ContainerClass = Cast<UClass>(Property->GetOuter()) )
		{
			UDelegateProperty* DelegateProperty = FindField<UDelegateProperty>(ContainerClass, FName(*DelegateName));
			if ( DelegateProperty )
			{
				return true;
			}
		}
	}

	return false;
}

TSharedRef<SWidget> FDetailWidgetExtensionHandler::GenerateExtensionWidget(const UClass* InObjectClass, TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	UProperty* Property = InPropertyHandle->GetProperty();
	FString DelegateName = Property->GetName() + "Delegate";
	
	UDelegateProperty* DelegateProperty = FindFieldChecked<UDelegateProperty>(CastChecked<UClass>(Property->GetOuter()), FName(*DelegateName));

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

	return SNew(SPropertyBinding, BlueprintEditor.Pin().ToSharedRef(), DelegateProperty, InPropertyHandle.ToSharedRef())
		.GeneratePureBindings(true);
}
