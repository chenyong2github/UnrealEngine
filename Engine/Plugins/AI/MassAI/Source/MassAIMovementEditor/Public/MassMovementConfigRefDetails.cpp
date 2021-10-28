// Copyright Epic Games, Inc. All Rights Reserved.
#include "MassMovementConfigRefDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "MassSettings.h"
#include "MassMovementSettings.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"
#include "MassMovementPropertyUtils.h"

#define LOCTEXT_NAMESPACE "MassMovementEditor"

TSharedRef<IPropertyTypeCustomization> FMassMovementConfigRefDetails::MakeInstance()
{
	return MakeShareable(new FMassMovementConfigRefDetails);
}

void FMassMovementConfigRefDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();
	
	NameProperty = StructProperty->GetChildHandle(TEXT("Name"));
	IDProperty = StructProperty->GetChildHandle(TEXT("ID"));

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SComboButton)
			.OnGetMenuContent(this, &FMassMovementConfigRefDetails::OnGetProfileContent)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &FMassMovementConfigRefDetails::GetCurrentProfileDesc)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

void FMassMovementConfigRefDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

void FMassMovementConfigRefDetails::OnProfileComboChange(int32 Idx)
{
	if (Idx == -1)
	{
		const UMassSettings* MassSettings = GetDefault<UMassSettings>();
		check(MassSettings);
		
		// Goto settings to create new Profile
		FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer(MassSettings->GetContainerName(), MassSettings->GetCategoryName(), MassSettings->GetSectionName());
		return;
	}

	const UMassMovementSettings* MovementSettings = GetDefault<UMassMovementSettings>();
	check(MovementSettings);

	TConstArrayView<FMassMovementConfig> MovementConfigs = MovementSettings->GetMovementConfigs();
	if (MovementConfigs.IsValidIndex(Idx))
	{
		const FMassMovementConfig& Config = MovementConfigs[Idx];

		FScopedTransaction Transaction(FText::Format(LOCTEXT("SetPropertyValue", "Set {0}"), StructProperty->GetPropertyDisplayName()));

		if (NameProperty)
		{
			NameProperty->SetValue(Config.Name, EPropertyValueSetFlags::NotTransactable);
		}

		if (IDProperty)
		{
			UE::MassMovement::PropertyUtils::SetValue<FGuid>(IDProperty, Config.ID, EPropertyValueSetFlags::NotTransactable);
		}

		if (PropUtils)
		{
			PropUtils->ForceRefresh();
		}
	}
}

TSharedRef<SWidget> FMassMovementConfigRefDetails::OnGetProfileContent() const
{
	FMenuBuilder MenuBuilder(true, NULL);
	const UMassMovementSettings* Settings = GetDefault<UMassMovementSettings>();
	check(Settings);

	FUIAction NewItemAction(FExecuteAction::CreateSP(const_cast<FMassMovementConfigRefDetails*>(this), &FMassMovementConfigRefDetails::OnProfileComboChange, -1));
	MenuBuilder.AddMenuEntry(LOCTEXT("CreateOrEditConfigs", "Create or Edit Movement Configs..."), TAttribute<FText>(), FSlateIcon(), NewItemAction);
	MenuBuilder.AddMenuSeparator();

	TConstArrayView<FMassMovementConfig> MovementConfigs = Settings->GetMovementConfigs();
	for (int32 Index = 0; Index < MovementConfigs.Num(); Index++)
	{
		const FMassMovementConfig& Config = MovementConfigs[Index];
		FUIAction ItemAction(FExecuteAction::CreateSP(const_cast<FMassMovementConfigRefDetails*>(this), &FMassMovementConfigRefDetails::OnProfileComboChange, Index));
		MenuBuilder.AddMenuEntry(FText::FromName(Config.Name), TAttribute<FText>(), FSlateIcon(), ItemAction);
	}
	return MenuBuilder.MakeWidget();
}

FText FMassMovementConfigRefDetails::GetCurrentProfileDesc() const
{
	TOptional<FGuid> IDOpt = UE::MassMovement::PropertyUtils::GetValue<FGuid>(IDProperty);
	if (IDOpt.IsSet())
	{
		const FGuid ID = IDOpt.GetValue();
		if (ID.IsValid())
		{
			const UMassMovementSettings* Settings = GetDefault<UMassMovementSettings>();
			check(Settings);

			const FMassMovementConfig* Config = Settings->GetMovementConfigByID(ID);
			if (Config)
			{
				return FText::FromName(Config->Name);
			}
			else
			{
				FName OldProfileName;
				if (NameProperty && NameProperty->GetValue(OldProfileName) == FPropertyAccess::Success)
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("Identifier"), FText::FromName(OldProfileName));
					return FText::Format(LOCTEXT("InvalidConfig", "Invalid Config {Identifier}"), Args);
				}
			}
		}
		else
		{
			return LOCTEXT("Invalid", "Invalid");
		}
	}
	// TODO: handle multiple values
	return FText();
}

#undef LOCTEXT_NAMESPACE