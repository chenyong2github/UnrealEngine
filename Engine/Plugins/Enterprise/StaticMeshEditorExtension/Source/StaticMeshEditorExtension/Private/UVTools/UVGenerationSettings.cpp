// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVTools/UVGenerationSettings.h"

#include "DetailWidgetRow.h"
#include "Engine/StaticMesh.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "Math/Axis.h"
#include "MeshDescription.h"
#include "MeshUtilitiesCommon.h"
#include "PropertyHandle.h"
#include "StaticMeshAttributes.h"
#include "Widgets/Input/SVectorInputBox.h"

TSharedRef<IPropertyTypeCustomization> FUVGenerationSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FUVGenerationSettingsCustomization);
}

void FUVGenerationSettingsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TArray<void*> StructPtrs;
	PropertyHandle->AccessRawData(StructPtrs);
	GenerateUVSettings = (StructPtrs.Num() == 1) ? reinterpret_cast<FUVGenerationSettings*>(StructPtrs[0]) : nullptr;
}

void FUVGenerationSettingsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	uint32 NumChildProps = 0;

	StructPropertyHandle->GetNumChildren(NumChildProps);

	for (uint32 Idx = 0; Idx < NumChildProps; Idx++)
	{
		TSharedPtr<IPropertyHandle> PropHandle = StructPropertyHandle->GetChildHandle(Idx);

		FName PropertyName = PropHandle->GetProperty()->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FUVGenerationSettings, Size)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(FUVGenerationSettings, Position)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(FUVGenerationSettings, Rotation))
		{
			PropHandle->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FUVGenerationSettingsCustomization::OnShapePropertyChanged));
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FUVGenerationSettings, TargetChannel))
		{
			if (GenerateUVSettings && GenerateUVSettings->OnGetNumberOfUVs.IsBound())
			{
				PropHandle->SetInstanceMetaData(TEXT("ClampMax"), FString::FromInt(FMath::Min(GenerateUVSettings->OnGetNumberOfUVs.Execute(), (int32)MAX_MESH_TEXTURE_COORDS_MD - 1)));
			}
			else
			{
				PropHandle->SetInstanceMetaData(TEXT("ClampMax"), FString::FromInt(MAX_MESH_TEXTURE_COORDS_MD - 1));
			}
		}

		IDetailPropertyRow& PropertyRow = ChildBuilder.AddProperty(PropHandle.ToSharedRef());
	}
}

void FUVGenerationSettingsCustomization::OnShapePropertyChanged()
{
	GenerateUVSettings->OnShapeEditingValueChanged.Broadcast();
}