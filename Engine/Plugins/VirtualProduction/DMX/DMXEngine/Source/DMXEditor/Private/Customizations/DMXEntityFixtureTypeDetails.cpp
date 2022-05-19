// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEntityFixtureTypeDetails.h"

#include "Library/DMXImportGDTF.h"
#include "Library/DMXEntityFixtureType.h"

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

	GDTFHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, GDTF));
	GDTFHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDMXEntityFixtureTypeDetails::OnDMXImportChanged));
}

void FDMXEntityFixtureTypeDetails::OnDMXImportChanged()
{
	UObject* DMXImportObject = nullptr;
	if (GDTFHandle->GetValue(DMXImportObject) == FPropertyAccess::Success)
	{
		if (UDMXImportGDTF* DMXImportGDTF = Cast<UDMXImportGDTF>(DMXImportObject))
		{
			const TArray<TWeakObjectPtr<UObject>>& SelectedObjects = PropertyUtilities->GetSelectedObjects();

			for (TWeakObjectPtr<UObject> WeakFixtureTypeObject : SelectedObjects)
			{
				if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(WeakFixtureTypeObject.Get()))
				{
					FixtureType->SetGDTF(DMXImportGDTF);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
