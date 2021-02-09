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
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "Misc/AssertionMacros.h"
#include "Misc/Attribute.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Text/STextBlock.h"
#include "PropertyCustomizationHelpers.h"
#include "Slate/Public/Widgets/SToolTip.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"

namespace Metasound
{
	namespace Editor
	{
		TSharedPtr<IPropertyHandle> GetLiteralHandleForType(TSharedRef<IPropertyHandle> StructPropertyHandle, EMetasoundFrontendLiteralType LiteralType)
		{
			switch (LiteralType)
			{
			case EMetasoundFrontendLiteralType::Bool:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsBool));
			}

			case EMetasoundFrontendLiteralType::Float:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsFloat));
			}

			case EMetasoundFrontendLiteralType::Integer:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsInteger));
			}

			case EMetasoundFrontendLiteralType::String:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsString));
			}

			case EMetasoundFrontendLiteralType::UObject:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsUObject));
			}

			case EMetasoundFrontendLiteralType::UObjectArray:
			{
				return StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsUObjectArray));
			}

			case EMetasoundFrontendLiteralType::None:
			default:
				static_assert(static_cast<int32>(EMetasoundFrontendLiteralType::Invalid) == 7, "Possible missing case coverage for EMetasoundFrontendLiteralType");
				return TSharedPtr<IPropertyHandle>();
			}
		}

		TSharedPtr<IPropertyHandle> GetActiveLiteralHandle(TSharedRef<IPropertyHandle> StructPropertyHandle, TSharedRef<IPropertyHandle> TypeEnumHandle)
		{
			uint8 Value = static_cast<uint8>(EMetasoundFrontendLiteralType::None);
			if (TypeEnumHandle->GetValue(Value) == FPropertyAccess::Result::Success)
			{
				const EMetasoundFrontendLiteralType LiteralType = static_cast<EMetasoundFrontendLiteralType>(Value);
				return GetLiteralHandleForType(StructPropertyHandle, LiteralType);
			}

			return TSharedPtr<IPropertyHandle>();
		}

		bool IsLiteralTypeActive(TSharedRef<IPropertyHandle> TypeEnumHandle, EMetasoundFrontendLiteralType LiteralType)
		{
			uint8 Value = static_cast<uint8>(EMetasoundFrontendLiteralType::None);
			TypeEnumHandle->GetValue(Value);

			return Value == static_cast<uint8>(LiteralType);
		}
	} // namespace Editor
} // namespace Metasound


void FMetasoundFrontendLiteralDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

void FMetasoundFrontendLiteralDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	TSharedRef<IPropertyHandle> TypeEnumHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, Type)).ToSharedRef();


	FName OwningDataTypeName;
	UClass* OwningDataTypeClass = nullptr;
	EMetasoundFrontendLiteralType PreferredLiteralType = EMetasoundFrontendLiteralType::None;

	// Grab the preferred uclass for the owning data type.
	TSharedPtr<IPropertyHandle> InputDescription = StructPropertyHandle->GetParentHandle()->GetParentHandle()->GetParentHandle();

	// TODO: Ensure that StructPropertyHandle is an FMetasoundInputDescription.
	if (InputDescription)
	{
		TSharedPtr<IPropertyHandle> DataTypeNameHandle = InputDescription->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClassInput, TypeName));
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
		EMetasoundFrontendLiteralType LiteralType;
		TSharedRef<IPropertyHandle> TypeEnumHandle;
		TSharedPtr<IPropertyHandle> PropertyHandle;
		FText DisplayName;
		UClass* OwningDataTypeUClass;
		FName OwningDataTypeName;

		TSharedRef<SWidget> CreateEnumWidget(const TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface>& EnumInterface) const
		{
			auto RemoveNameSpace = [](const FString& InString) -> FString
			{ 
				FString Namespace, EnumName;
				InString.Split(TEXT("::"), &Namespace, &EnumName);
				return EnumName;
			};
			auto GetAll = [EnumInterface, RemoveNameSpace](TArray<TSharedPtr<FString>>& OutStrings, TArray<TSharedPtr<SToolTip>>& OutTooltips, TArray<bool>&)
			{
				for (const Metasound::Frontend::IEnumDataTypeInterface::FGenericInt32Entry& i : EnumInterface->GetAllEntries())
				{
					OutTooltips.Emplace(SNew(SToolTip).Text(i.Tooltip));

					// Trim the namespace off for display.
					OutStrings.Emplace(MakeShared<FString>(RemoveNameSpace(i.Name.GetPlainNameString())));
				}
			};
			auto GetValue = [EnumInterface, Prop = PropertyHandle, RemoveNameSpace]() -> FString
			{
				int32 IntValue;
				if (Prop->GetValue(IntValue))
				{
					if (TOptional<FName> Result = EnumInterface->ToName(IntValue))
					{
						return RemoveNameSpace(Result->GetPlainNameString());
					}
					UE_LOG(LogTemp, Warning, TEXT("Failed to Get Valid Value for Property '%s' with Value of '%d'"),
						*GetNameSafe(Prop->GetProperty()), IntValue);
				}
				return {};
			};
			auto SelectedValue = [EnumInterface, Prop = PropertyHandle](const FString& InSelected)
			{
				FString FullyQualifiedName = FString::Printf(TEXT("%s::%s"), *EnumInterface->GetNamespace().GetPlainNameString(), *InSelected);
				TOptional<int32> Result = EnumInterface->ToValue(*FullyQualifiedName);
				if (ensure(Result))
				{
					Prop->SetValue(*Result);
				}
			};

			return PropertyCustomizationHelpers::MakePropertyComboBox(
				nullptr,
				FOnGetPropertyComboBoxStrings::CreateLambda(GetAll),
				FOnGetPropertyComboBoxValue::CreateLambda(GetValue),
				FOnPropertyComboBoxValueSelected::CreateLambda(SelectedValue)
			);
		}

		TSharedRef<SWidget> CreatePropertyValueWidget(bool bDisplayDefaultPropertyButtons = true) const
		{
			if (LiteralType == EMetasoundFrontendLiteralType::UObject)
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
						CachedEnumHandle->SetValue(static_cast<uint8>(EMetasoundFrontendLiteralType::UObject));
					}
					else
					{
						CachedEnumHandle->SetValue(static_cast<uint8>(EMetasoundFrontendLiteralType::None));
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
			else if (LiteralType == EMetasoundFrontendLiteralType::Integer)
			{
				// Check if this is an Enum.
				if (TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> EnumInterface = 
					FMetasoundFrontendRegistryContainer::Get()->GetEnumInterfaceForDataType(OwningDataTypeName))
				{ 
					return CreateEnumWidget(EnumInterface);
				}

				// Fall back to regular widget if its not.
				return PropertyHandle->CreatePropertyValueWidget();
			}
			else if (LiteralType == EMetasoundFrontendLiteralType::UObjectArray)
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

	static_assert(static_cast<int32>(EMetasoundFrontendLiteralType::Invalid) == 7, "Possible missing property descriptor for literal customization display");
	const TArray<FTypeEditorInfo> TypePropertyInfo =
	{
		FTypeEditorInfo
		{
			EMetasoundFrontendLiteralType::Bool,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsBool)),
			LOCTEXT("Metasound_LiteralDisplayNameBool", "Bool"),
			nullptr,
			OwningDataTypeName
		},

		FTypeEditorInfo
		{
			EMetasoundFrontendLiteralType::Integer,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsInteger)),
			LOCTEXT("Metasound_LiteralDisplayNameInteger", "Int32"),
			nullptr,
			OwningDataTypeName
		},

		FTypeEditorInfo
		{
			EMetasoundFrontendLiteralType::Float,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsFloat)),
			LOCTEXT("Metasound_LiteralDisplayNameFloat", "Float"),
			nullptr,
			OwningDataTypeName
		},

		FTypeEditorInfo
		{
			EMetasoundFrontendLiteralType::String,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsString)),
			LOCTEXT("Metasound_LiteralDisplayNameString", "String"),
			nullptr,
			OwningDataTypeName
		},

		FTypeEditorInfo
		{
			EMetasoundFrontendLiteralType::UObject,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsUObject)),
			LOCTEXT("Metasound_LiteralDisplayNameUObject", "UObject"),
			OwningDataTypeClass,
			OwningDataTypeName
		},

		FTypeEditorInfo
		{
			EMetasoundFrontendLiteralType::UObjectArray,
			TypeEnumHandle,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsUObjectArray)),
			LOCTEXT("Metasound_LiteralDisplayNameUObjectArray", "UObjectArray"),
			OwningDataTypeClass,
			OwningDataTypeName
		}
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
