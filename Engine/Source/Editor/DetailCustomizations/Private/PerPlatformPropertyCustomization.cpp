// Copyright Epic Games, Inc. All Rights Reserved.

#include "PerPlatformPropertyCustomization.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "PlatformInfo.h"
#include "ScopedTransaction.h"
#include "IPropertyUtilities.h"
#include "UObject/MetaData.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SPerPlatformPropertiesWidget.h"

#define LOCTEXT_NAMESPACE "PerPlatformPropertyCustomization"

template<typename PerPlatformType>
void FPerPlatformPropertyCustomization<PerPlatformType>::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	int32 PlatformNumber = PlatformInfo::GetAllPlatformGroupNames().Num();

	TAttribute<TArray<FName>> PlatformOverrideNames = TAttribute<TArray<FName>>::Create(TAttribute<TArray<FName>>::FGetter::CreateSP(this, &FPerPlatformPropertyCustomization<PerPlatformType>::GetPlatformOverrideNames, StructPropertyHandle));

	FPerPlatformPropertyCustomNodeBuilderArgs Args;
	Args.NameWidget = StructPropertyHandle->CreatePropertyNameWidget();
	Args.PlatformOverrideNames = PlatformOverrideNames;
	Args.OnAddPlatformOverride = FOnPlatformOverrideAction::CreateSP(this, &FPerPlatformPropertyCustomization<PerPlatformType>::AddPlatformOverride, StructPropertyHandle);
	Args.OnRemovePlatformOverride = FOnPlatformOverrideAction::CreateSP(this, &FPerPlatformPropertyCustomization<PerPlatformType>::RemovePlatformOverride, StructPropertyHandle);
	Args.OnGenerateWidgetForPlatformRow = FOnGenerateWidget::CreateSP(this, &FPerPlatformPropertyCustomization<PerPlatformType>::GetWidget, StructPropertyHandle);

	StructBuilder.AddCustomBuilder(MakeShared<FPerPlatformPropertyCustomNodeBuilder>(MoveTemp(Args)));
}


template<typename PerPlatformType>
TSharedRef<SWidget> FPerPlatformPropertyCustomization<PerPlatformType>::GetWidget(FName PlatformGroupName, TSharedRef<IPropertyHandle> StructPropertyHandle) const
{
	TSharedPtr<IPropertyHandle>	EditProperty;

	if (PlatformGroupName == NAME_None)
	{
		EditProperty = StructPropertyHandle->GetChildHandle(FName("Default"));
	}
	else
	{
		TSharedPtr<IPropertyHandle>	MapProperty = StructPropertyHandle->GetChildHandle(FName("PerPlatform"));
		if (MapProperty.IsValid())
		{
			uint32 NumChildren = 0;
			MapProperty->GetNumChildren(NumChildren);
			for (uint32 ChildIdx = 0; ChildIdx < NumChildren; ChildIdx++)
			{
				TSharedPtr<IPropertyHandle> ChildProperty = MapProperty->GetChildHandle(ChildIdx);
				if (ChildProperty.IsValid())
				{
					TSharedPtr<IPropertyHandle> KeyProperty = ChildProperty->GetKeyHandle();
					if (KeyProperty.IsValid())
					{
						FName KeyName;
						if(KeyProperty->GetValue(KeyName) == FPropertyAccess::Success && KeyName == PlatformGroupName)
						{
							EditProperty = ChildProperty;
							break;
						}
					}
				}
			}
		}
	
	}

	// Push down struct metadata to per-platform properties
	{
		// First get the source map
		const TMap<FName, FString>* SourceMap = StructPropertyHandle->GetMetaDataProperty()->GetMetaDataMap();
		// Iterate through source map, setting each key/value pair in the destination
		for (const auto& It : *SourceMap)
		{
				EditProperty->SetInstanceMetaData(*It.Key.ToString(), *It.Value);
		}

		// Copy instance metadata as well
		const TMap<FName, FString>* InstanceSourceMap = StructPropertyHandle->GetInstanceMetaDataMap();		
		for (const auto& It : *InstanceSourceMap)
		{
			EditProperty->SetInstanceMetaData(*It.Key.ToString(), *It.Value);
		}
	}

	if (ensure(EditProperty.IsValid()))
	{
		return EditProperty->CreatePropertyValueWidget(false);
	}
	
	return SNullWidget::NullWidget;
}


template<typename PerPlatformType>
bool FPerPlatformPropertyCustomization<PerPlatformType>::AddPlatformOverride(FName PlatformGroupName, TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	FScopedTransaction Transaction(LOCTEXT("AddPlatformOverride", "Add Platform Override"));

	TSharedPtr<IPropertyHandle>	PerPlatformProperty = StructPropertyHandle->GetChildHandle(FName("PerPlatform"));
	TSharedPtr<IPropertyHandle>	DefaultProperty = StructPropertyHandle->GetChildHandle(FName("Default"));
	if (PerPlatformProperty.IsValid() && DefaultProperty.IsValid())
	{
		TSharedPtr<IPropertyHandleMap> MapProperty = PerPlatformProperty->AsMap();
		if (MapProperty.IsValid())
		{
			MapProperty->AddItem();
			uint32 NumChildren = 0;
			PerPlatformProperty->GetNumChildren(NumChildren);
			for (uint32 ChildIdx = 0; ChildIdx < NumChildren; ChildIdx++)
			{
				TSharedPtr<IPropertyHandle> ChildProperty = PerPlatformProperty->GetChildHandle(ChildIdx);
				if (ChildProperty.IsValid())
				{
					TSharedPtr<IPropertyHandle> KeyProperty = ChildProperty->GetKeyHandle();
					if (KeyProperty.IsValid())
					{
						FName KeyName;
						if (KeyProperty->GetValue(KeyName) == FPropertyAccess::Success && KeyName == NAME_None)
						{
							// Set Key
							KeyProperty->SetValue(PlatformGroupName);

							// Set Value
							typename PerPlatformType::ValueType DefaultValue;
							DefaultProperty->GetValue(DefaultValue);
							ChildProperty->SetValue(DefaultValue);

							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

template<typename PerPlatformType>
bool FPerPlatformPropertyCustomization<PerPlatformType>::RemovePlatformOverride(FName PlatformGroupName, TSharedRef<IPropertyHandle> StructPropertyHandle)
{
	FScopedTransaction Transaction(LOCTEXT("RemovePlatformOverride", "Remove Platform Override"));

	TSharedPtr<IPropertyHandle>	MapProperty = StructPropertyHandle->GetChildHandle(FName("PerPlatform"));
	if (MapProperty.IsValid())
	{
		TArray<const void*> RawData;
		MapProperty->AccessRawData(RawData);
		for (const void* Data : RawData)
		{
			TMap<FName, typename PerPlatformType::ValueType>* PerPlatformMap = (TMap<FName, typename PerPlatformType::ValueType>*)(Data);
			check(PerPlatformMap);
			TArray<FName> KeyArray;
			PerPlatformMap->GenerateKeyArray(KeyArray);
			for (FName PlatformName : KeyArray)
			{
				if (PlatformName == PlatformGroupName)
				{
					PerPlatformMap->Remove(PlatformName);

					return true;
				}
			}
		}
	}
	return false;

}

template<typename PerPlatformType>
TArray<FName> FPerPlatformPropertyCustomization<PerPlatformType>::GetPlatformOverrideNames(TSharedRef<IPropertyHandle> StructPropertyHandle) const
{
	TArray<FName> PlatformOverrideNames;

	TSharedPtr<IPropertyHandle>	MapProperty = StructPropertyHandle->GetChildHandle(FName("PerPlatform"));
	if (MapProperty.IsValid())
	{
		TArray<const void*> RawData;
		MapProperty->AccessRawData(RawData);
		for (const void* Data : RawData)
		{
			const TMap<FName, typename PerPlatformType::ValueType>* PerPlatformMap = (const TMap<FName, typename PerPlatformType::ValueType>*)(Data);
			check(PerPlatformMap);
			TArray<FName> KeyArray;
			PerPlatformMap->GenerateKeyArray(KeyArray);
			for (FName PlatformName : KeyArray)
			{
				PlatformOverrideNames.AddUnique(PlatformName);
			}
		}

	}
	return PlatformOverrideNames;
}

template<typename PerPlatformType>
TSharedRef<IPropertyTypeCustomization> FPerPlatformPropertyCustomization<PerPlatformType>::MakeInstance()
{
	return MakeShareable(new FPerPlatformPropertyCustomization<PerPlatformType>);
}

/* Only explicitly instantiate the types which are supported
*****************************************************************************/

template class FPerPlatformPropertyCustomization<FPerPlatformInt>;
template class FPerPlatformPropertyCustomization<FPerPlatformFloat>;
template class FPerPlatformPropertyCustomization<FPerPlatformBool>;

#undef LOCTEXT_NAMESPACE

void FPerPlatformPropertyCustomNodeBuilder::SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren)
{
	OnRebuildChildren = InOnRegenerateChildren;
}

void FPerPlatformPropertyCustomNodeBuilder::SetOnToggleExpansion(FOnToggleNodeExpansion InOnToggleExpansion)
{
	OnToggleExpansion = InOnToggleExpansion;
}

void FPerPlatformPropertyCustomNodeBuilder::GenerateHeaderRowContent(FDetailWidgetRow& HeaderRow)
{
	// Build Platform menu
	FMenuBuilder AddPlatformMenuBuilder(true, nullptr, nullptr, true);

	// Platform (group) names
	const TArray<FName>& PlatformGroupNameArray = PlatformInfo::GetAllPlatformGroupNames();
	const TArray<FName>& VanillaPlatformNameArray = PlatformInfo::GetAllVanillaPlatformNames();

	// Sanitized platform names
	TArray<FName> BasePlatformNameArray;
	// Mapping from platform group name to individual platforms
	TMultiMap<FName, FName> GroupToPlatform;

	TArray<FName> PlatformOverrides = Args.PlatformOverrideNames.Get();

	// Create mapping from platform to platform groups and remove postfixes and invalid platform names
	for (const FName& PlatformName : VanillaPlatformNameArray)
	{
		// Add platform name if it isn't already set, and also add to group mapping
		if (!PlatformOverrides.Contains(PlatformName))
		{
			BasePlatformNameArray.AddUnique(PlatformName);
			GroupToPlatform.AddUnique(PlatformInfo::FindPlatformInfo(PlatformName)->DataDrivenPlatformInfo->PlatformGroupName, PlatformName);
		}
	}

	// Create section for platform groups 
	const FName PlatformGroupSection(TEXT("PlatformGroupSection"));
	AddPlatformMenuBuilder.BeginSection(PlatformGroupSection, FText::FromString(TEXT("Platform Groups")));
	for (const FName& GroupName : PlatformGroupNameArray)
	{
		if (!PlatformOverrides.Contains(GroupName))
		{
			const FTextFormat Format = NSLOCTEXT("SPerPlatformPropertiesWidget", "AddOverrideGroupFor", "Add Override for Platforms part of the {0} Platform Group");
			AddPlatformToMenu(GroupName, Format, AddPlatformMenuBuilder);
		}
	}
	AddPlatformMenuBuilder.EndSection();

	for (const FName& GroupName : PlatformGroupNameArray)
	{
		// Create a section for each platform group and their respective platforms
		AddPlatformMenuBuilder.BeginSection(GroupName, FText::FromName(GroupName));

		TArray<FName> PlatformNames;
		GroupToPlatform.MultiFind(GroupName, PlatformNames);

		const FTextFormat Format = NSLOCTEXT("SPerPlatformPropertiesWidget", "AddOverrideFor", "Add Override specifically for {0}");
		for (const FName& PlatformName : PlatformNames)
		{
			AddPlatformToMenu(PlatformName, Format, AddPlatformMenuBuilder);
		}

		AddPlatformMenuBuilder.EndSection();
	}


	HeaderRow.NameContent()
	[
		Args.NameWidget.ToSharedRef()
	]
	.ValueContent()
	.MinDesiredWidth(125+28.0f)
	[
		SNew(SHorizontalBox)
		.IsEnabled(Args.IsEnabled)
		.ToolTipText(NSLOCTEXT("SPerPlatformPropertiesWidget", "DefaultPlatformDesc", "This property can have per-platform or platform group overrides.\nThis is the default value used when no override has been set for a platform or platform group."))
		+SHorizontalBox::Slot()
		[
			SNew(SPerPlatformPropertiesRow, NAME_None)
			.OnGenerateWidget(Args.OnGenerateWidgetForPlatformRow)
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f, 0.0f, 0.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ComboButtonStyle(FEditorStyle::Get(), "SimpleComboButton")
			.HasDownArrow(false)
			.ToolTipText(NSLOCTEXT("SPerPlatformPropertiesWidget", "AddOverrideToolTip", "Add an override for a specific platform or platform group"))
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Icons.PlusCircle"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			.MenuContent()
			[
				AddPlatformMenuBuilder.MakeWidget()
			]	
		]
	];
}

void FPerPlatformPropertyCustomNodeBuilder::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	TArray<FName> PlatformOverrides = Args.PlatformOverrideNames.Get();
	for (FName PlatformName : PlatformOverrides)
	{
		FText PlatformDisplayName = FText::AsCultureInvariant(PlatformName.ToString());
		FDetailWidgetRow& Row = ChildrenBuilder.AddCustomRow(PlatformDisplayName);
		Row.IsEnabled(Args.IsEnabled);

		Row.NameContent()
		[
			SNew(STextBlock)
			.Text(PlatformDisplayName)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];

		Row.ValueContent()
		[
			SNew(SPerPlatformPropertiesRow, PlatformName)
			.OnGenerateWidget(Args.OnGenerateWidgetForPlatformRow)
			.OnRemovePlatform(this, &FPerPlatformPropertyCustomNodeBuilder::OnRemovePlatformOverride)
		];
	}
}

FName FPerPlatformPropertyCustomNodeBuilder::GetName() const
{
	static const FName Name("FPerPlatformPropertyCustomNodeBuilder");
	return Name;
}

void FPerPlatformPropertyCustomNodeBuilder::OnAddPlatformOverride(const FName PlatformName)
{
	if (Args.OnAddPlatformOverride.IsBound() && Args.OnAddPlatformOverride.Execute(PlatformName))
	{
		OnRebuildChildren.ExecuteIfBound();
		OnToggleExpansion.ExecuteIfBound(true);
	}
}

bool FPerPlatformPropertyCustomNodeBuilder::OnRemovePlatformOverride(const FName PlatformName)
{
	if (Args.OnRemovePlatformOverride.IsBound() && Args.OnRemovePlatformOverride.Execute(PlatformName))
	{
		OnRebuildChildren.ExecuteIfBound();
	}

	return true;
}
void FPerPlatformPropertyCustomNodeBuilder::AddPlatformToMenu(const FName PlatformName, const FTextFormat Format, FMenuBuilder& AddPlatformMenuBuilder)
{
	const FText MenuText = FText::Format(FText::FromString(TEXT("{0}")), FText::AsCultureInvariant(PlatformName.ToString()));
	const FText MenuTooltipText = FText::Format(Format, FText::AsCultureInvariant(PlatformName.ToString()));
	AddPlatformMenuBuilder.AddMenuEntry(
		MenuText,
		MenuTooltipText,
		FSlateIcon(FEditorStyle::GetStyleSetName(), "PerPlatformWidget.AddPlatform"),
		FUIAction(FExecuteAction::CreateSP(this, &FPerPlatformPropertyCustomNodeBuilder::OnAddPlatformOverride, PlatformName))
	);
}
