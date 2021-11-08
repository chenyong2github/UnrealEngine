// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEntityFixtureTypeDetails.h"

#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXImport.h"

#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"


#define LOCTEXT_NAMESPACE "DMXEntityFixtureTypeDetails"

TSharedRef<IDetailCustomization> FDMXEntityFixtureTypeDetails::MakeInstance()
{
	return MakeShared<FDMXEntityFixtureTypeDetails>();
}

void FDMXEntityFixtureTypeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();

	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));

	DMXImportHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXImport));
	DMXImportHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXEntityFixtureTypeDetails::OnDMXImportChanged));
}

void FDMXEntityFixtureTypeDetails::OnDMXImportChanged()
{
	UObject* DMXImportObject = nullptr;
	if (DMXImportHandle->GetValue(DMXImportObject) == FPropertyAccess::Success)
	{
		if (UDMXImport* DMXImport = Cast<UDMXImport>(DMXImportObject))
		{
			const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = PropertyUtilities->GetSelectedObjects();

			for (TWeakObjectPtr<UObject> WeakFixtureTypeObject : SelectedObjects)
			{
				if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(WeakFixtureTypeObject.Get()))
				{
					FixtureType->SetModesFromDMXImport(DMXImport);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
