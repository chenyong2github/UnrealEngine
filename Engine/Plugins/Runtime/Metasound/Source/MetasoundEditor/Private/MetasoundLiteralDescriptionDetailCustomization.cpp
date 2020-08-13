// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundLiteralDescriptionDetailCustomization.h"

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "Layout/Visibility.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendRegistries.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Attribute.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Text/STextBlock.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"

namespace Metasound
{
	namespace Editor
	{
		TSharedPtr<IPropertyHandle> GetLiteralHandleForType(TSharedRef<IPropertyHandle> StructPropertyHandle, EMetasoundLiteralType LiteralType)
		{
			switch (LiteralType)
			{
			case EMetasoundLiteralType::Bool:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsBool));
			}

			case EMetasoundLiteralType::Float:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsFloat));
			}

			case EMetasoundLiteralType::Integer:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsInteger));
			}

			case EMetasoundLiteralType::String:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsString));
			}

			case EMetasoundLiteralType::UObject:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsUObject));
			}

			case EMetasoundLiteralType::UObjectArray:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsUObjectArray));
			}

			case EMetasoundLiteralType::None:
			default:
				static_assert(static_cast<int32>(EMetasoundLiteralType::Invalid) == 7, "Possible missing case coverage for EMetasoundLiteralType");
				return TSharedPtr<IPropertyHandle>();
			}
		}

		TSharedPtr<IPropertyHandle> GetActiveLiteralHandle(TSharedRef<IPropertyHandle> StructPropertyHandle, TSharedRef<IPropertyHandle> TypeEnumHandle)
		{
			uint8 Value = static_cast<uint8>(EMetasoundLiteralType::None);
			if (TypeEnumHandle->GetValue(Value) == FPropertyAccess::Result::Success)
			{
				const EMetasoundLiteralType LiteralType = static_cast<EMetasoundLiteralType>(Value);
				return GetLiteralHandleForType(StructPropertyHandle, LiteralType);
			}

			return TSharedPtr<IPropertyHandle>();
		}

		bool IsLiteralTypeActive(TSharedRef<IPropertyHandle> TypeEnumHandle, EMetasoundLiteralType LiteralType)
		{
			uint8 Value = static_cast<uint8>(EMetasoundLiteralType::None);
			TypeEnumHandle->GetValue(Value);

			return Value == static_cast<uint8>(LiteralType);
		}
	} // namespace Editor
} // namespace Metasound


void FMetasoundLiteralDescriptionDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

void FMetasoundLiteralDescriptionDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TSharedRef<IPropertyHandle> TypeEnumHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, LiteralType)).ToSharedRef();


	FName OwningDataTypeName;
	UClass* OwningDataTypeClass = nullptr;
	EMetasoundLiteralType PreferredLiteralType = EMetasoundLiteralType::None;

	// Grab the preferred uclass for the owning data type.
	TSharedPtr<IPropertyHandle> InputDescription = StructPropertyHandle->GetParentHandle();

	// TODO: Ensure that StructPropertyHandle is an FMetasoundInputDescription.
	if (InputDescription)
	{
		TSharedPtr<IPropertyHandle> DataTypeNameHandle = InputDescription->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundInputDescription, TypeName));
		if (DataTypeNameHandle)
		{
			
			DataTypeNameHandle->GetValue(OwningDataTypeName);

			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
			OwningDataTypeClass = Registry->GetLiteralUClassForDataType(OwningDataTypeName);
			PreferredLiteralType = Metasound::Frontend::GetMetasoundLiteralType(Registry->GetDesiredLiteralTypeForDataType(OwningDataTypeName));
		}
	}

	

	struct FTypeEditorInfo
	{
		EMetasoundLiteralType LiteralType;
		TSharedRef<IPropertyHandle> TypeEnumHandle;
		TSharedPtr<IPropertyHandle> PropertyHandle;
		FText DisplayName;
		UClass* OwningDataTypeUClass;

		TSharedRef<SWidget> CreatePropertyValueWidget(bool bDisplayDefaultPropertyButtons = true) const
		{
			if (LiteralType == EMetasoundLiteralType::UObject)
			{
				auto ValidateAsset = [CachedDataTypeUClass = OwningDataTypeUClass](const UObject* InObject, FText& OutReason)  -> bool
				{
					UClass* SelectedObjectClass = InObject->GetClass();
					return CachedDataTypeUClass && SelectedObjectClass && SelectedObjectClass->IsChildOf(CachedDataTypeUClass);
				};

				auto CommitAsset = [CachedLiteralType = LiteralType, CachedPropertyHandle = PropertyHandle, CachedEnumHandle = TypeEnumHandle, CachedDataTypeUClass = OwningDataTypeUClass](const FAssetData& InAssetData) -> void
				{
					// if we've hit this code, the presumption is that the datatype for this parameter has already defined a corresponding UClass that can be used to set it.
					ensureAlways(CachedDataTypeUClass && CachedDataTypeUClass != UObject::StaticClass());

					UObject* InObject = InAssetData.GetAsset();
					CachedPropertyHandle->SetValue(InObject);

					// If this asset selector was set to no asset, clear out the literal description.
					if (InObject)
					{
						CachedEnumHandle->SetValue(static_cast<uint8>(EMetasoundLiteralType::UObject));
					}
					else
					{
						CachedEnumHandle->SetValue(static_cast<uint8>(EMetasoundLiteralType::None));
					}
				};

				auto GetAssetPath = [CachedPropertyHandle = PropertyHandle]() -> FString
				{
					UObject* Obj = nullptr;
					CachedPropertyHandle->GetValue(Obj);
					return  Obj != nullptr ? Obj->GetPathName() : FString();
				};

				// TODO: get the UFactory corresponding to this datatype's UClass.
				TArray<UFactory*> FactoriesToUse;

				return SNew(SObjectPropertyEntryBox)
					.ObjectPath_Lambda(GetAssetPath)
					.AllowedClass(OwningDataTypeUClass)
					// .OnShouldSetAsset_Lambda(ValidateAsset)
					.OnObjectChanged_Lambda(CommitAsset)
					.AllowClear(false)
					.DisplayUseSelected(true)
					.DisplayBrowse(true)
					.DisplayThumbnail(true)
					.NewAssetFactories(FactoriesToUse);
			}
			else if (LiteralType == EMetasoundLiteralType::UObjectArray)
			{
				// TODO: Implement.
				return SNullWidget::NullWidget;
			}
			else
			{
				return PropertyHandle->CreatePropertyValueWidget();
			}
		}
	};

	static_assert(static_cast<int32>(EMetasoundLiteralType::Invalid) == 7, "Possible missing property descriptor for literal customization display");
	const TArray<FTypeEditorInfo> TypePropertyInfo =
	{
		FTypeEditorInfo
		{
			EMetasoundLiteralType::Bool,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsBool)),
			LOCTEXT("Metasound_LiteralDisplayNameBool", "Bool"),
			nullptr
		},

		FTypeEditorInfo
		{
			EMetasoundLiteralType::Integer,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsInteger)),
			LOCTEXT("Metasound_LiteralDisplayNameInteger", "Int32"),
			nullptr
		},

		FTypeEditorInfo
		{
			EMetasoundLiteralType::Float,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsFloat)),
			LOCTEXT("Metasound_LiteralDisplayNameFloat", "Float"),
			nullptr
		},

		FTypeEditorInfo
		{
			EMetasoundLiteralType::String,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsString)),
			LOCTEXT("Metasound_LiteralDisplayNameString", "String"),
			nullptr
		},

		FTypeEditorInfo
		{
			EMetasoundLiteralType::UObject,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsUObject)),
			LOCTEXT("Metasound_LiteralDisplayNameUObject", "UObject"),
			OwningDataTypeClass
		},

		FTypeEditorInfo
		{
			EMetasoundLiteralType::UObjectArray,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsUObjectArray)),
			LOCTEXT("Metasound_LiteralDisplayNameUObjectArray", "UObjectArray"),
			OwningDataTypeClass
		},
	};

	for (const FTypeEditorInfo& Info : TypePropertyInfo)
	{
		ChildBuilder.AddCustomRow(StructPropertyHandle->GetPropertyDisplayName())
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.FillWidth(0.5f)
					.Padding(1.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text(LOCTEXT("Metasound_LiteralDefault", "Default"))
						.ToolTipText(TAttribute<FText>::Create([StructPropertyHandle, TypeEnumHandle]()
						{
							TSharedPtr<IPropertyHandle> VisibleHandle = Metasound::Editor::GetActiveLiteralHandle(StructPropertyHandle, TypeEnumHandle);
							if (VisibleHandle.IsValid())
							{
								return VisibleHandle->GetToolTipText();
							}
							return FText::GetEmpty();
						}))
					]
			]
			.ValueContent()
			.MinDesiredWidth(120.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.FillWidth(0.5f)
					.Padding(1.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						Info.CreatePropertyValueWidget()
					]
			]
			.Visibility(TAttribute<EVisibility>::Create([PreferredLiteralType, LiteralType = Info.LiteralType]()
			{
				const bool bIsLiteralActive = LiteralType == PreferredLiteralType;
				return bIsLiteralActive ? EVisibility::Visible : EVisibility::Hidden;
			}));
	}
}
#undef LOCTEXT_NAMESPACE // MetasoundEditor