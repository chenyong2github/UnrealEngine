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
#include "Misc/AssertionMacros.h"
#include "Misc/Attribute.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Text/STextBlock.h"


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

			case EMetasoundLiteralType::None:
			default:
				static_assert(static_cast<int32>(EMetasoundLiteralType::Invalid) == 5, "Possible missing case coverage for EMetasoundLiteralType");
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

	struct FTypeEditorInfo
	{
		EMetasoundLiteralType LiteralType;
		TSharedPtr<IPropertyHandle> PropertyHandle;
		FText DisplayName;
	};

	static_assert(static_cast<int32>(EMetasoundLiteralType::Invalid) == 5, "Possible missing property descriptor for literal customization display");
	const TArray<FTypeEditorInfo> TypePropertyInfo =
	{
		FTypeEditorInfo
		{
			EMetasoundLiteralType::Bool,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsBool)),
			LOCTEXT("Metasound_LiteralDisplayNameBool", "Bool")
		},

		FTypeEditorInfo
		{
			EMetasoundLiteralType::Integer,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsInteger)),
			LOCTEXT("Metasound_LiteralDisplayNameInteger", "Int32")
		},

		FTypeEditorInfo
		{
			EMetasoundLiteralType::Float,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsFloat)),
			LOCTEXT("Metasound_LiteralDisplayNameFloat", "Float")
		},

		FTypeEditorInfo
		{
			EMetasoundLiteralType::String,
			StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundLiteralDescription, AsString)),
			LOCTEXT("Metasound_LiteralDisplayNameString", "String")
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
						Info.PropertyHandle->CreatePropertyValueWidget()
					]
			]
			.Visibility(TAttribute<EVisibility>::Create([TypeEnumHandle, LiteralType = Info.LiteralType]()
			{
				const bool bIsLiteralActive = Metasound::Editor::IsLiteralTypeActive(TypeEnumHandle, LiteralType);
				return bIsLiteralActive ? EVisibility::Visible : EVisibility::Hidden;
			}));
	}
}
#undef LOCTEXT_NAMESPACE // MetasoundEditor