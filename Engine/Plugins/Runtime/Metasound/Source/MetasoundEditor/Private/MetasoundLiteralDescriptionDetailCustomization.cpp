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
#include "MetasoundFrontendLiteral.h"
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

#ifndef GET_STRUCT_NAME_CHECKED
#define GET_STRUCT_NAME_CHECKED(StructName) \
		((void)sizeof(StructName), TEXT(#StructName))
#endif

namespace Metasound
{
	namespace Editor
	{
		// Returns true if the property handle points to a property of a given CPP type.
		bool IsPropertyHandleOfType(const TSharedPtr<IPropertyHandle>& InHandle, const FString& InTypeName)
		{
			if (InHandle.IsValid())
			{
				if (const FProperty* Property = InHandle->GetProperty())
				{
					return Property->GetCPPType() == InTypeName;
				}
			}

			return false;
		}

		// Returns true if the TypeEnumHandle points to a EMeatsoundFrontendLiteralType property of equal value.
		bool IsLiteralTypeActive(TSharedRef<IPropertyHandle> TypeEnumHandle, EMetasoundFrontendLiteralType LiteralType)
		{
			uint8 Value = static_cast<uint8>(EMetasoundFrontendLiteralType::None);
			TypeEnumHandle->GetValue(Value);

			return Value == static_cast<uint8>(LiteralType);
		}

		// Sets the literal type on a IPropertyHandle pointing to a FMetasoundFrontendLiteral
		void SetActiveLiteralType(EMetasoundFrontendLiteralType InType, TSharedPtr<IPropertyHandle> InLiteralPropertyHandle)
		{
			check(IsPropertyHandleOfType(InLiteralPropertyHandle, GET_STRUCT_NAME_CHECKED(FMetasoundFrontendLiteral)));

			if (TSharedPtr<IPropertyHandle> TypeHandle= InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, Type)))
			{
				TypeHandle->SetValue(static_cast<uint8>(InType));
			}
		}

		// Returns the Metasound Data Type name for a literal by inspecting the 
		// parent properties.
		FName GetOwningMetasoundDataTypeName(TSharedRef<IPropertyHandle> InMetasoundFrontendLiteral)
		{
			FName OwningDataTypeName;

			// The association between a literal and a data type is not maintained on
			// a literal. The owning property must be inspected To find the data type name 
			// associated with the literal. There are no assurances that a literal will
			// be held on a property which contains a data type name, but the instances
			// of literals on a meatsound document USTRUCT have a hardcoded struct member
			// layout. This layout can be exploited to find the data type when a FMetasoundFrontendLiteral 
			// is a member of one of the various USTRUCTS in a metasound document.

			if (OwningDataTypeName.IsNone())
			{
				TSharedPtr<IPropertyHandle> OwningClassInputProperty = InMetasoundFrontendLiteral->GetParentHandle()->GetParentHandle()->GetParentHandle();
				if (IsPropertyHandleOfType(OwningClassInputProperty, GET_STRUCT_NAME_CHECKED(FMetasoundFrontendClassInput)))
				{
					// Literal is owned by a FMetasoundFrontendClassInput
					TSharedPtr<IPropertyHandle> DataTypeNameHandle = OwningClassInputProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClassInput, TypeName));
					DataTypeNameHandle->GetValue(OwningDataTypeName);
				}
			}

			if (OwningDataTypeName.IsNone())
			{
				TSharedPtr<IPropertyHandle> OwningNodeProperty = InMetasoundFrontendLiteral->GetParentHandle()->GetParentHandle()->GetParentHandle();
				if (IsPropertyHandleOfType(OwningNodeProperty, GET_STRUCT_NAME_CHECKED(FMetasoundFrontendNode)))
				{
					// Literal is owned by a FMetasoundFrontendNode. The literal will need
					// to be matched to the node input using the PointID in order to get the 
					// associated data type.
					checkNoEntry();

					// TODO: implement me. 
				}
			}

			return OwningDataTypeName;
		}

		// ILiteralPropertyAdapter creates a custom widget for a FMetasoundFrontendLiteral.
		// This interface can be subclasses to create different widgets under differing 
		// scenarios.
		class ILiteralPropertyAdapter
		{
			public:
				ILiteralPropertyAdapter() = default;
				virtual ~ILiteralPropertyAdapter() = default;

				// Create a widget for a given FMetasoundFrontendLiteral property handle.
				virtual TSharedRef<SWidget> CreatePropertyValueWidget(TSharedPtr<IPropertyHandle> InLiteralPropertyHandle, bool bDisplayDefaultPropertyButtons=false) const = 0;

				// Return the literal type associated with this property adapater.
				virtual EMetasoundFrontendLiteralType GetLiteralType() const = 0;

				// Return localized tooltip text for a given FMetasoundFrontendLiteral property handle.
				virtual const FText& GetToolTipText(TSharedPtr<IPropertyHandle> InLiteralPropertyHandle) const = 0;
		};

		// FLiteralPropertyBaseAdapter creates a default widget for the underlying
		// property determined by EMetasoundFrontendLiteralType.
		class FLiteralPropertyBaseAdapter : public ILiteralPropertyAdapter
		{
		public:

			FLiteralPropertyBaseAdapter(EMetasoundFrontendLiteralType InPreferredLiteralType)
			: PreferredLiteralType(InPreferredLiteralType)
			{
			}

			virtual ~FLiteralPropertyBaseAdapter() = default;

			// This sets the FMetasoundFrontendLiteral's active property to `GetLiteralType()` before creating a widget.
			TSharedRef<SWidget> CreatePropertyValueWidget(TSharedPtr<IPropertyHandle> InLiteralPropertyHandle, bool bDisplayDefaultPropertyButtons = false) const override
			{
				check(IsPropertyHandleOfType(InLiteralPropertyHandle, GET_STRUCT_NAME_CHECKED(FMetasoundFrontendLiteral)));

				// Set the FMetasoundFrontendLiteral::Type member if invalid // TODO: only do this if invalid.
				Metasound::Editor::SetActiveLiteralType(GetLiteralType(), InLiteralPropertyHandle);

				return CreatePropertyValueWidgetInternal(InLiteralPropertyHandle, bDisplayDefaultPropertyButtons);
			}

			EMetasoundFrontendLiteralType GetLiteralType() const override
			{
				return PreferredLiteralType;
			}

			const FText& GetToolTipText(TSharedPtr<IPropertyHandle> InLiteralPropertyHandle) const override
			{
				check(IsPropertyHandleOfType(InLiteralPropertyHandle, GET_STRUCT_NAME_CHECKED(FMetasoundFrontendLiteral)));

				if (TSharedPtr<IPropertyHandle> Property = GetValueHandle(InLiteralPropertyHandle))
				{
					Property->GetToolTipText();
				}
				return FText::GetEmpty();
			}

		protected:

			// Derived classes can override this method to produce a custom widget.
			virtual TSharedRef<SWidget>CreatePropertyValueWidgetInternal(TSharedPtr<IPropertyHandle> InLiteralPropertyHandle, bool bDisplayDefaultPropertyButtons) const
			{
				if (TSharedPtr<IPropertyHandle> ValueProperty = GetValueHandle(InLiteralPropertyHandle))
				{
					return ValueProperty->CreatePropertyValueWidget(bDisplayDefaultPropertyButtons);
				}
				return SNullWidget::NullWidget;
			}

			// If `InPropertyHandle` is an array, this will return the first element in the array.
			// If the array does not contain any elements, one will be added. 
			TSharedPtr<IPropertyHandle> GetOrAddFirstArrayElement(TSharedPtr<IPropertyHandle> InPropertyHandle) const
			{
				if (InPropertyHandle.IsValid())
				{	
					TSharedPtr<IPropertyHandleArray> PropertyArray = InPropertyHandle->AsArray();
					if (ensure(PropertyArray))
					{
						uint32 NumChildren = 0;
						if (FPropertyAccess::Result::Success == PropertyArray->GetNumElements(NumChildren))
						{
							if (NumChildren < 1)
							{
								PropertyArray->AddItem();
								if (FPropertyAccess::Result::Success != PropertyArray->GetNumElements(NumChildren))
								{
									return TSharedPtr<IPropertyHandle>();
								}
							}

							if (NumChildren > 0)
							{
								return PropertyArray->GetElement(0);
							}
						}
					}
				}
				return TSharedPtr<IPropertyHandle>();
			}

			// Returns the child IPropertyHandle from a FMetasoundFrontendLiteral for a specific EMetasoundFrontendLiteralType	
			// The returned handle points to the actual stored value (e.g. float, TArray<float>, int32, UObject*)
			TSharedPtr<IPropertyHandle> GetValueHandle(TSharedPtr<IPropertyHandle> InLiteralPropertyHandle) const
			{
				// Check that the property is the correct type.
				switch (GetLiteralType())
				{
					case EMetasoundFrontendLiteralType::BooleanArray:
					{
						return InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsBoolean));
					}

					case EMetasoundFrontendLiteralType::Boolean:
					{
						return GetOrAddFirstArrayElement(InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsBoolean)));
					}

					case EMetasoundFrontendLiteralType::FloatArray:
					{
						return InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsFloat));
					}

					case EMetasoundFrontendLiteralType::Float:
					{
						return GetOrAddFirstArrayElement(InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsFloat)));
					}

					case EMetasoundFrontendLiteralType::IntegerArray:
					{
						return InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsInteger));
					}

					case EMetasoundFrontendLiteralType::Integer:
					{
						return GetOrAddFirstArrayElement(InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsInteger)));
					}

					case EMetasoundFrontendLiteralType::StringArray:
					{
						return InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsString));
					}

					case EMetasoundFrontendLiteralType::String:
					{
						return GetOrAddFirstArrayElement(InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsString)));
					}

					case EMetasoundFrontendLiteralType::UObjectArray:
					{
						return InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsUObject));
					}

					case EMetasoundFrontendLiteralType::UObject:
					{
						return GetOrAddFirstArrayElement(InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsUObject)));
					}

					case EMetasoundFrontendLiteralType::NoneArray:
					{
						return InLiteralPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundFrontendLiteral, AsNumDefault));
					}
					case EMetasoundFrontendLiteralType::None:
					default:
						static_assert(static_cast<int32>(EMetasoundFrontendLiteralType::Invalid) == 12, "Possible missing case coverage for EMetasoundFrontendLiteralType");
						return TSharedPtr<IPropertyHandle>();
				}
			}

		private:

			EMetasoundFrontendLiteralType PreferredLiteralType;
		};

		// Special literal property adapater for Metasound enums.
		class FLiteralEnumPropertyAdapter : public FLiteralPropertyBaseAdapter
		{
		public:
			FLiteralEnumPropertyAdapter(EMetasoundFrontendLiteralType InLiteralType, TSharedRef<const Frontend::IEnumDataTypeInterface> InEnumInterface)
			: FLiteralPropertyBaseAdapter(InLiteralType)
			, EnumInterface(InEnumInterface)
			{
			}

			virtual ~FLiteralEnumPropertyAdapter() = default;

		protected:
			// Create enum widget.
			TSharedRef<SWidget> CreatePropertyValueWidgetInternal(TSharedPtr<IPropertyHandle> InLiteralPropertyHandle, bool bDisplayDefaultPropertyButtons = false) const override
			{
				if (TSharedPtr<IPropertyHandle> PropertyHandle = GetValueHandle(InLiteralPropertyHandle))
				{
					auto RemoveNamespace = [](const FString& InString) -> FString
					{ 
						FString EnumName;
						if (InString.Split(TEXT("::"), nullptr, &EnumName))
						{
							return EnumName;
						}
						return InString;
					};

					auto GetAll = [EnumInterface = EnumInterface, RemoveNamespace](TArray<TSharedPtr<FString>>& OutStrings, TArray<TSharedPtr<SToolTip>>& OutTooltips, TArray<bool>&)
					{
						for (const Metasound::Frontend::IEnumDataTypeInterface::FGenericInt32Entry& i : EnumInterface->GetAllEntries())
						{
							OutTooltips.Emplace(SNew(SToolTip).Text(i.Tooltip));

							// Trim the namespace off for display.
							OutStrings.Emplace(MakeShared<FString>(RemoveNamespace(i.Name.GetPlainNameString())));
						}
					};

					auto GetValue = [EnumInterface = EnumInterface, PropertyHandle = PropertyHandle, RemoveNamespace]() -> FString
					{
						int32 IntValue;
						if (PropertyHandle->GetValue(IntValue))
						{
							if (TOptional<FName> Result = EnumInterface->ToName(IntValue))
							{
								return RemoveNamespace(Result->GetPlainNameString());
							}
							UE_LOG(LogTemp, Warning, TEXT("Failed to Get Valid Value for Property '%s' with Value of '%d'"), *GetNameSafe(PropertyHandle->GetProperty()), IntValue);
						}
						return {};
					};

					auto SelectedValue = [EnumInterface = EnumInterface, PropertyHandle = PropertyHandle](const FString& InSelected)
					{
						FString FullyQualifiedName = FString::Printf(TEXT("%s::%s"), *EnumInterface->GetNamespace().GetPlainNameString(), *InSelected);
						TOptional<int32> Result = EnumInterface->ToValue(*FullyQualifiedName);
						if (ensure(Result))
						{
							PropertyHandle->SetValue(*Result);
						}
					};

					return PropertyCustomizationHelpers::MakePropertyComboBox(
						nullptr,
						FOnGetPropertyComboBoxStrings::CreateLambda(GetAll),
						FOnGetPropertyComboBoxValue::CreateLambda(GetValue),
						FOnPropertyComboBoxValueSelected::CreateLambda(SelectedValue)
					);
				}

				return SNullWidget::NullWidget;
			}

		private:
			TSharedRef<const Frontend::IEnumDataTypeInterface> EnumInterface;
		};


		// Special property adapter for UObject
		class FLiteralUObjectPropertyAdapter : public FLiteralPropertyBaseAdapter
		{
		public:
			FLiteralUObjectPropertyAdapter(UClass* InValidUClass)
			: FLiteralPropertyBaseAdapter(EMetasoundFrontendLiteralType::UObject)
			, ValidUClass(InValidUClass)
			{
			}

			virtual ~FLiteralUObjectPropertyAdapter() = default;

		protected:
			TSharedRef<SWidget> CreatePropertyValueWidgetInternal(TSharedPtr<IPropertyHandle> InLiteralPropertyHandle, bool bDisplayDefaultPropertyButtons = false) const override
			{
				TSharedPtr<IPropertyHandle> PropertyHandle = GetValueHandle(InLiteralPropertyHandle);

				auto ValidateAsset = [ValidUClass = this->ValidUClass](const UObject* InObject, FText& OutReason)  -> bool
				{
					UClass* SelectedObjectClass = InObject->GetClass();
					return ValidUClass && SelectedObjectClass && SelectedObjectClass->IsChildOf(ValidUClass);
				};

				auto CommitAsset = [PropertyHandle = PropertyHandle, ValidUClass = this->ValidUClass](const FAssetData& InAssetData) -> void
				{
					// if we've hit this code, the presumption is that the datatype for this parameter has already defined a corresponding UClass that can be used to set it.
					ensureAlways(ValidUClass && ValidUClass != UObject::StaticClass());

					UObject* InObject = InAssetData.GetAsset();
					PropertyHandle->SetValue(InObject);
				};

				auto GetAssetPath = [PropertyHandle = PropertyHandle]() -> FString
				{
					UObject* Obj = nullptr;

					PropertyHandle->GetValue(Obj);

					if (nullptr != Obj)
					{
						return Obj->GetPathName();
					}
					return FString();
				};

				// TODO: get the UFactory corresponding to this datatype's UClass.
				TArray<UFactory*> FactoriesToUse;

				return SNew(SObjectPropertyEntryBox)
					.ObjectPath_Lambda(GetAssetPath)
					.AllowedClass(ValidUClass)
					// .OnShouldSetAsset_Lambda(ValidateAsset)
					.OnObjectChanged_Lambda(CommitAsset)
					.AllowClear(false)
					.DisplayUseSelected(true)
					.DisplayBrowse(true)
					.DisplayThumbnail(true)
					.NewAssetFactories(FactoriesToUse);
			}

		private:
			UClass* ValidUClass;
		};

		// Return the literal property adapter associated with the Metasound data type.
		TSharedPtr<const ILiteralPropertyAdapter> GetLiteralPropertyAdapter(const FName& InDataTypeName)
		{
			static TMap<FName, TSharedPtr<ILiteralPropertyAdapter>> AdapterMap;

			// Return an adapter if one already exists for data type
			TSharedPtr<ILiteralPropertyAdapter> Adapter;
			if (TSharedPtr<ILiteralPropertyAdapter>* PtrToExistingAdapter = AdapterMap.Find(InDataTypeName))
			{
				Adapter = *PtrToExistingAdapter;
			}

			if ((!Adapter.IsValid()) && (!InDataTypeName.IsNone()))
			{
				// There was no existing adapter, so query the registry to see which adapter to instantiate. 
				if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
				{
					FDataTypeRegistryInfo DataTypeInfo;
					if (ensure(Registry->GetInfoForDataType(InDataTypeName, DataTypeInfo)))
					{
						const EMetasoundFrontendLiteralType LiteralType = Frontend::GetMetasoundFrontendLiteralType(DataTypeInfo.PreferredLiteralType);

						if (EMetasoundFrontendLiteralType::UObject == LiteralType)
						{
							Adapter = MakeShared<FLiteralUObjectPropertyAdapter>(DataTypeInfo.ProxyGeneratorClass);
						}
						else if (EMetasoundFrontendLiteralType::UObjectArray == LiteralType)
						{
							// TODO: implement me.
							ensure(false);
						}
						else if (DataTypeInfo.bIsEnum)
						{
							TSharedPtr<const Frontend::IEnumDataTypeInterface> EnumInterface = Registry->GetEnumInterfaceForDataType(InDataTypeName);
							if (ensure(EnumInterface))
							{
								Adapter = MakeShared<FLiteralEnumPropertyAdapter>(LiteralType, EnumInterface.ToSharedRef());
							}
						}
						else
						{
							Adapter = MakeShared<FLiteralPropertyBaseAdapter>(LiteralType);
						}

						if (Adapter.IsValid())
						{
							AdapterMap.Add(InDataTypeName, Adapter);
						}
					}
				}
			}

			return Adapter;
		}

	} // namespace Editor
} // namespace Metasound


void FMetasoundFrontendLiteralDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
}

void FMetasoundFrontendLiteralDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	// Get the datatype associated with value
	FName DataTypeName = Metasound::Editor::GetOwningMetasoundDataTypeName(StructPropertyHandle);

	// Get the literal property adapter associated with the datatype.
	if (TSharedPtr<const Metasound::Editor::ILiteralPropertyAdapter> Adapter = Metasound::Editor::GetLiteralPropertyAdapter(DataTypeName))
	{
		// Create customization.
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
					.ToolTipText(TAttribute<FText>::Create([=]()
						{
							return Adapter->GetToolTipText(StructPropertyHandle);
						})
					)
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
					Adapter->CreatePropertyValueWidget(StructPropertyHandle)
				]
			];
	}
}
#undef LOCTEXT_NAMESPACE // MetasoundEditor
