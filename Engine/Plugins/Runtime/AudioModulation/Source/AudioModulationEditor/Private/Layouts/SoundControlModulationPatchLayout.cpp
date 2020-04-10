// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundControlModulationPatchLayout.h"

#include "AudioModulationSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "IAudioModulation.h"
#include "SoundModulationPatch.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"


#define LOCTEXT_NAMESPACE "SoundControlModulationPatch"
namespace
{
	void GetPropertyHandleMap(TSharedRef<IPropertyHandle> StructPropertyHandle, TMap<FName, TSharedPtr<IPropertyHandle>>& PropertyHandles)
	{
		uint32 NumChildren;
		StructPropertyHandle->GetNumChildren(NumChildren);

		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
			const FName PropertyName = ChildHandle->GetProperty()->GetFName();
			PropertyHandles.Add(PropertyName, ChildHandle);
		}
	}
} // namespace <>

void FSoundModulationPatchLayoutCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Property handle here is the base struct. We are going to hide it since we're showing it's properties directly.
// 	PropertyHandle->MarkHiddenByCustomization();
}

void FSoundModulationPatchLayoutCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TMap<FName, TSharedPtr<IPropertyHandle>> PropertyHandles;
	GetPropertyHandleMap(StructPropertyHandle, PropertyHandles);
	CustomizeControl(PropertyHandles, ChildBuilder);
}

TAttribute<EVisibility> FSoundModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TSharedRef<IPropertyHandle>BypassHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationPatchBase, bBypass)).ToSharedRef();
	ChildBuilder.AddProperty(BypassHandle);

	TAttribute<EVisibility> VisibilityAttribute = TAttribute<EVisibility>::Create([this, BypassHandle]()
	{
		bool bIsBypassed = false;
		BypassHandle->GetValue(bIsBypassed);
		return bIsBypassed ? EVisibility::Hidden : EVisibility::Visible;
	});

	TSharedRef<IPropertyHandle>ValueHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FSoundModulationPatchBase, DefaultInputValue)).ToSharedRef();
	ChildBuilder.AddProperty(ValueHandle)
		.Visibility(VisibilityAttribute);

	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundVolumeModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TAttribute<EVisibility> VisibilityAttribute = FSoundModulationPatchLayoutCustomization::CustomizeControl(PropertyHandles, ChildBuilder);
	AddPatchProperties<FSoundVolumeModulationPatch>(VisibilityAttribute, PropertyHandles, ChildBuilder);
	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundPitchModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TAttribute<EVisibility> VisibilityAttribute = FSoundModulationPatchLayoutCustomization::CustomizeControl(PropertyHandles, ChildBuilder);
	AddPatchProperties<FSoundPitchModulationPatch>(VisibilityAttribute, PropertyHandles, ChildBuilder);
	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundHPFModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TAttribute<EVisibility> VisibilityAttribute = FSoundModulationPatchLayoutCustomization::CustomizeControl(PropertyHandles, ChildBuilder);
	AddPatchProperties<FSoundHPFModulationPatch>(VisibilityAttribute, PropertyHandles, ChildBuilder);
	return VisibilityAttribute;
}

TAttribute<EVisibility> FSoundLPFModulationPatchLayoutCustomization::CustomizeControl(TMap<FName, TSharedPtr<IPropertyHandle>> &PropertyHandles, IDetailChildrenBuilder &ChildBuilder)
{
	TAttribute<EVisibility> VisibilityAttribute = FSoundModulationPatchLayoutCustomization::CustomizeControl(PropertyHandles, ChildBuilder);
	AddPatchProperties<FSoundLPFModulationPatch>(VisibilityAttribute, PropertyHandles, ChildBuilder);
	return VisibilityAttribute;
}
#undef LOCTEXT_NAMESPACE
