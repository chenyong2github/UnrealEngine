// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConnectionTargetSettingsTypeCustomization.h"

#include "VCamComponent.h"
#include "UI/VCamConnectionStructs.h"
#include "Util/ConnectionUtils.h"

#include "Algo/ForEach.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "Selection.h"
#include "SSimpleComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FConnectionTargetSettingsTypeCustomization"

namespace UE::VCamCoreEditor::Private
{
	static TArray<FName> SortNamesAlphabeticallyAndPrependNone(TArray<FName>&& Names)
	{
		TArray<FName> Result = MoveTemp(Names);
		Result.Sort([](const FName& Left, const FName& Right){ return Left.LexicalLess(Right); });
		Result.Insert(NAME_None, 0);
		return Result;
	}
	
	TSharedRef<IPropertyTypeCustomization> FConnectionTargetSettingsTypeCustomization::MakeInstance()
	{
		return MakeShared<FConnectionTargetSettingsTypeCustomization>();
	}

	void FConnectionTargetSettingsTypeCustomization::CustomizeHeader(
		TSharedRef<IPropertyHandle> PropertyHandle,
		FDetailWidgetRow& HeaderRow,
		IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		HeaderRow
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				PropertyHandle->CreatePropertyValueWidget()
			];
	}

	void FConnectionTargetSettingsTypeCustomization::CustomizeChildren(
		TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailChildrenBuilder& ChildBuilder,
		IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		const FName WidgetProperty = GET_MEMBER_NAME_CHECKED(FVCamConnectionTargetSettings, TargetModifierName);
		const FName ConnectionTargetsProperty = GET_MEMBER_NAME_CHECKED(FVCamConnectionTargetSettings, TargetConnectionPoint);
		TSharedPtr<IPropertyHandle> TargetModifierNameProperty;
		TSharedPtr<IPropertyHandle> TargetConnectionPointProperty;
		
		uint32 NumChildren;
		PropertyHandle->GetNumChildren( NumChildren);	
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			TSharedPtr<IPropertyHandle> ChildProperty = PropertyHandle->GetChildHandle(ChildIndex);
			if (ChildProperty->GetProperty() && ChildProperty->GetProperty()->GetFName() == WidgetProperty)
			{
				TargetModifierNameProperty = ChildProperty;
			}
			else if (ChildProperty->GetProperty() && ChildProperty->GetProperty()->GetFName() == ConnectionTargetsProperty)
			{
				TargetConnectionPointProperty = ChildProperty;
			}
		}
		
		AddScopeRow(ChildBuilder, CustomizationUtils);
		
		const TSharedPtr<IPropertyHandle> ParentHandle = PropertyHandle->GetParentHandle();
		const FStructProperty* ParentStruct = CastField<FStructProperty>(ParentHandle->GetProperty());
		const bool bIsValidHandle = ParentHandle
			&& ParentStruct
			&& ParentStruct->Struct == FVCamConnection::StaticStruct();
		const TSharedPtr<IPropertyHandle> OptionalVCamConnectionParentStructHandle = bIsValidHandle
			? ParentHandle
			: nullptr;
		
		IDetailPropertyRow& ModifierRow = ChildBuilder.AddProperty(TargetModifierNameProperty.ToSharedRef());
		CustomizeModifier(TargetModifierNameProperty.ToSharedRef(), ModifierRow, OptionalVCamConnectionParentStructHandle);

		IDetailPropertyRow& TargetConnectionRow = ChildBuilder.AddProperty(TargetModifierNameProperty.ToSharedRef());
		CustomizeConnectionPoint(TargetModifierNameProperty.ToSharedRef(), TargetConnectionPointProperty.ToSharedRef(), TargetConnectionRow, OptionalVCamConnectionParentStructHandle);
	}

	void FConnectionTargetSettingsTypeCustomization::AddScopeRow(IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
		// This row tells the user where the suggestions are coming from
		ChildBuilder.AddCustomRow(FText::GetEmpty())
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Scope.Name", "Scope"))
				.ToolTipText(LOCTEXT("Scope.Tooltip", "Helps in suggesting connections points. Either:\n1. Select an Actor Blueprint containing a UVCamComponent, or\n2. Click an actor in the level containing a UVCamComponent"))
				.Font(CustomizationUtils.GetRegularFont())
			]
			.ValueContent()
			[
				SNew(STextBlock)
				.Text_Lambda([]()
				{
					const FSelectedComponentInfo ConnectionPointSourceInfo = GetUserFocusedConnectionPointSource();
					switch (ConnectionPointSourceInfo.ComponentSource)
					{
					case EComponentSource::ContentBrowser:
						check(ConnectionPointSourceInfo.Component.IsValid());
						return FText::Format(LOCTEXT("Scope.ContentBrowser", "Asset: {0}"), FText::FromString(ConnectionPointSourceInfo.Component->GetPackage()->GetName()));
					case EComponentSource::LevelSelection:
						check(ConnectionPointSourceInfo.Component.IsValid());
						return FText::Format(LOCTEXT("Scope.LevelSelection", "Actor: {0}"), FText::FromString(ConnectionPointSourceInfo.Component->GetOwner()->GetActorLabel()) );
					case EComponentSource::None:
					default:
						return LOCTEXT("Scope.None", "No object selected");
					}
				})
				.Font(CustomizationUtils.GetRegularFont())
			];
	}

	void FConnectionTargetSettingsTypeCustomization::CustomizeModifier(
		TSharedRef<IPropertyHandle> ModifierHandle,
		IDetailPropertyRow& Row,
		TSharedPtr<IPropertyHandle> OptionalVCamConnectionParentStructHandle // Can be null
		) const
	{
		CustomizeNameProperty(ModifierHandle, Row,
			TAttribute<TArray<FName>>::CreateLambda([OptionalVCamConnectionParentStructHandle]() -> TArray<FName>
			{
				UVCamComponent* DataSource = GetUserFocusedConnectionPointSource().Component.Get();
				if (!DataSource)
				{
					return {};
				}

				// If the property is within a FVCamConnection, narrow down the list of suggested modifiers
				void* ValueData = nullptr;
				if (OptionalVCamConnectionParentStructHandle
					&& OptionalVCamConnectionParentStructHandle->IsValidHandle()
					&& OptionalVCamConnectionParentStructHandle->GetValueData(ValueData) == FPropertyAccess::Success
					&& ValueData)
				{
					const FVCamConnection* ConnectionData = static_cast<FVCamConnection*>(ValueData);
					return SortNamesAlphabeticallyAndPrependNone(VCamCore::ConnectionUtils::FindCompatibleModifierNames(*ConnectionData, *DataSource));
				}
				
				return SortNamesAlphabeticallyAndPrependNone(DataSource->GetAllModifierNames());
			}),
			TAttribute<bool>::CreateLambda([](){ return GetUserFocusedConnectionPointSource().ComponentSource != EComponentSource::None; })
			);
	}

	void FConnectionTargetSettingsTypeCustomization::CustomizeConnectionPoint(
		TSharedRef<IPropertyHandle> ModifierHandle,
		TSharedRef<IPropertyHandle> ConnectionPointHandle,
		IDetailPropertyRow& Row,
		TSharedPtr<IPropertyHandle> OptionalVCamConnectionParentStructHandle // Can be null
		) const
	{
		CustomizeNameProperty(ConnectionPointHandle, Row,
			TAttribute<TArray<FName>>::CreateLambda([ModifierHandle, OptionalVCamConnectionParentStructHandle]() -> TArray<FName>
			{
				FName ModifierName;
				if (ModifierHandle->GetValue(ModifierName) != FPropertyAccess::Success)
				{
					return {};
				}

				UVCamComponent* DataSource = GetUserFocusedConnectionPointSource().Component.Get();
				if (!DataSource)
				{
					return {};
				}
				
				UVCamModifier* Modifier = DataSource->GetModifierByName(ModifierName);
				if (!Modifier)
				{
					return {};
				}

				// If the property is within a FVCamConnection, narrow down the list of suggested modifiers
				void* ValueData = nullptr;
				if (OptionalVCamConnectionParentStructHandle
					&& OptionalVCamConnectionParentStructHandle->IsValidHandle()
					&& OptionalVCamConnectionParentStructHandle->GetValueData(ValueData) == FPropertyAccess::Success
					&& ValueData)
				{
					const FVCamConnection* ConnectionData = static_cast<FVCamConnection*>(ValueData);
					return SortNamesAlphabeticallyAndPrependNone(VCamCore::ConnectionUtils::FindCompatibleConnectionPoints(*ConnectionData, *Modifier));
				}

				TArray<FName> ConnectionPoints;
				Modifier->ConnectionPoints.GenerateKeyArray(ConnectionPoints);
				return SortNamesAlphabeticallyAndPrependNone(MoveTemp(ConnectionPoints));
			}),
			TAttribute<bool>::CreateLambda([ModifierHandle]()
			{
				if (GetUserFocusedConnectionPointSource().ComponentSource == EComponentSource::None)
				{
					return false;
				}

				FName ModifierName;
				if (ModifierHandle->GetValue(ModifierName) != FPropertyAccess::Success)
				{
					return false;
				}
				
				UVCamComponent* DataSource = GetUserFocusedConnectionPointSource().Component.Get();
				return DataSource->GetModifierByName(ModifierName) != nullptr;
			}));
	}

	FConnectionTargetSettingsTypeCustomization::FSelectedComponentInfo FConnectionTargetSettingsTypeCustomization::GetUserFocusedConnectionPointSource()
	{
		// Content browser
		{
			FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();
			const UClass* const SelectedClass = GEditor->GetFirstSelectedClass(AActor::StaticClass());
			AActor* BlueprintCDO = SelectedClass
				? Cast<AActor>(SelectedClass->GetDefaultObject())
				: nullptr;
			UVCamComponent* VCamComponent = BlueprintCDO
				? BlueprintCDO->FindComponentByClass<UVCamComponent>()
				: nullptr;
			if (IsValid(VCamComponent))
			{
				return { EComponentSource::ContentBrowser, VCamComponent };
			}
		}

		// Level Editor
		USelection* Selection = GEditor->GetSelectedActors();
		if (Selection)
		{
			for (int32 i = 0; i < Selection->Num(); ++i)
			{
				UObject* SelectedObject = Selection->GetSelectedObject(i);
				AActor* SelectedActor = Cast<AActor>(SelectedObject);
				UVCamComponent* VCamComponent = SelectedActor
					? SelectedActor->FindComponentByClass<UVCamComponent>()
					: nullptr;
				if (IsValid(VCamComponent))
				{
					return { EComponentSource::LevelSelection, VCamComponent };
				}
			}
		}
			
		return {};
	}

	void FConnectionTargetSettingsTypeCustomization::CustomizeNameProperty(
		TSharedRef<IPropertyHandle> PropertyHandle,
		IDetailPropertyRow& Row,
		TAttribute<TArray<FName>> GetOptionsAttr,
		TAttribute<bool> HasDataSourceAttr
		) const
	{
		Row.CustomWidget()
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)

				// Warning icon if the entered value matches nothing from the list
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.DesiredSizeOverride(FVector2D{ 24.f, 24.f })
					.Image(FAppStyle::Get().GetBrush("Icons.WarningWithColor"))
					.ToolTipText(FText::Format(LOCTEXT("InvalidValue", "Invalid value: the scope object does not contain this value for property {0}"), PropertyHandle->GetPropertyDisplayName()))
					.Visibility_Lambda([PropertyHandle, HasDataSourceAttr, GetOptionsAttr]()
					{
						FName Value;
						if (PropertyHandle->GetValue(Value) != FPropertyAccess::Success
							// None is a "valid" value which means that the connection point should be reset
							|| Value.IsNone())
						{
							return EVisibility::Collapsed;
						}
						return HasDataSourceAttr.Get() && !GetOptionsAttr.Get().Contains(Value)
							? EVisibility::Visible
							: EVisibility::Collapsed;
					})
				]

				// Normal editing if no data source object is available
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.Visibility_Lambda([HasDataSourceAttr = HasDataSourceAttr](){ return !HasDataSourceAttr.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						PropertyHandle->CreatePropertyValueWidget()
					]
				]

				// Suggest data if data source object is available
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.Visibility_Lambda([HasDataSourceAttr = HasDataSourceAttr](){ return HasDataSourceAttr.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SComboButton)
						.HasDownArrow(true)
						.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
						.ForegroundColor(FSlateColor::UseStyle())
						.ButtonContent()
						[
							PropertyHandle->CreatePropertyValueWidget()
						]
						.OnGetMenuContent_Lambda([PropertyHandle, GetOptionsAttr = GetOptionsAttr]()
						{
							FMenuBuilder MenuBuilder(true, nullptr);
							for (const FName& Name : GetOptionsAttr.Get())
							{
								MenuBuilder.AddMenuEntry(
									FText::FromName(Name),
									FText::GetEmpty(),
									FSlateIcon(),
									FUIAction(
										FExecuteAction::CreateLambda([PropertyHandle, Name]()
										{
											PropertyHandle->SetValue(Name);
										})),
										NAME_None,
										EUserInterfaceActionType::Button
									);
							}
							return MenuBuilder.MakeWidget();
						})
					]
				]
			];
	}
}

#undef LOCTEXT_NAMESPACE