// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundNodeDetailCustomization.h"

#include "Components/AudioComponent.h"
#include "Containers/Set.h"
#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceMacro.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNode.h"
#include "MetasoundEditorGraphMemberDefaults.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundUObjectRegistry.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "SlateCore/Public/Styling/SlateColor.h"
#include "SMetasoundActionMenu.h"
#include "SMetasoundGraphNode.h"
#include "SSearchableComboBox.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		namespace MemberCustomizationPrivate
		{
			/** Set of input types which are valid registered types, but should
			 * not show up as an input type option in the MetaSound editor. */
			static const TSet<FName> HiddenInputTypeNames =
			{
				"Audio:Mono",
				"Audio:Stereo"
			};
		} // namespace MemberCustomizationPrivate

		FMetasoundFloatLiteralCustomization::~FMetasoundFloatLiteralCustomization()
		{
			if (FloatLiteral.IsValid())
			{
				FloatLiteral->OnClampChanged.Remove(OnClampChangedDelegateHandle);
				FloatLiteral->OnRangeChanged.Remove(OnRangeChangedDelegateHandle);
			}
		}

		void FMetasoundFloatLiteralCustomization::CustomizeLiteral(UMetasoundEditorGraphMemberDefaultLiteral& InLiteral, TSharedPtr<IPropertyHandle> InDefaultValueHandle)
		{
			check(InputCategoryBuilder);

			UMetasoundEditorGraphMemberDefaultFloat* DefaultFloat = Cast<UMetasoundEditorGraphMemberDefaultFloat>(&InLiteral);
			if (!ensure(DefaultFloat))
			{
				return;
			}
			FloatLiteral = DefaultFloat;

			if (IDetailPropertyRow* Row = InputCategoryBuilder->AddExternalObjectProperty(TArray<UObject*>({ DefaultFloat }), GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphMemberDefaultFloat, ClampDefault)))
			{
				// If clamping or using slider, clamp default value to given range 
				if (DefaultFloat->ClampDefault || DefaultFloat->WidgetType != EMetasoundMemberDefaultWidget::None)
				{
					FVector2D Range = DefaultFloat->GetRange();
					InDefaultValueHandle->SetInstanceMetaData("ClampMin", FString::Printf(TEXT("%f"), Range.X));
					InDefaultValueHandle->SetInstanceMetaData("ClampMax", FString::Printf(TEXT("%f"), Range.Y));
				}
				else // Stop clamping
				{
					InDefaultValueHandle->SetInstanceMetaData("ClampMin", "");
					InDefaultValueHandle->SetInstanceMetaData("ClampMax", "");
				}

				DefaultFloat->OnClampChanged.Remove(OnClampChangedDelegateHandle);
				OnClampChangedDelegateHandle = DefaultFloat->OnClampChanged.AddLambda([this](bool ClampInput)
				{
					if (FloatLiteral.IsValid())
					{
						FloatLiteral->OnClampChanged.Remove(OnClampChangedDelegateHandle);
						if (FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(FloatLiteral->GetOutermostObject()))
						{
							MetasoundAsset->SetSynchronizationRequired();
						}
					}
				});
			}
			InputCategoryBuilder->AddExternalObjectProperty(TArray<UObject*>({ DefaultFloat }), GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphMemberDefaultFloat, Range));
		}

		void FMetasoundMemberDefaultBoolDetailCustomization::CacheProxyData(TSharedPtr<IPropertyHandle> InPropertyHandle)
		{
			DataTypeName = FName();

			const FString* MetadataDataTypeName = InPropertyHandle->GetInstanceMetaData(MemberCustomizationStyle::DataTypeNameIdentifier);
			if (ensure(MetadataDataTypeName))
			{
				DataTypeName = **MetadataDataTypeName;
			}
		}

		FText FMetasoundMemberDefaultBoolDetailCustomization::GetPropertyNameOverride() const
		{
			if (DataTypeName == Metasound::GetMetasoundDataTypeName<Metasound::FTrigger>())
			{
				return LOCTEXT("TriggerInput_SimulateTitle", "Simulate");
			}

			return FText::GetEmpty();
		}

		TSharedRef<SWidget> FMetasoundMemberDefaultBoolDetailCustomization::CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const
		{
			using namespace Frontend;

			TSharedPtr<IPropertyHandle> ValueProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundEditorGraphMemberDefaultBoolRef, Value));
			if (ValueProperty.IsValid())
			{
				// Not a trigger, so just display as underlying literal type (bool)
				if (DataTypeName != Metasound::GetMetasoundDataTypeName<Metasound::FTrigger>())
				{
					return ValueProperty->CreatePropertyValueWidget();
				}

				TAttribute<bool> EnablementAttribute = false;
				TAttribute<EVisibility> VisibilityAttribute = EVisibility::Visible;

				TArray<UObject*> OuterObjects;
				ValueProperty->GetOuterObjects(OuterObjects);
				if (!OuterObjects.IsEmpty())
				{
					if (UMetasoundEditorGraphMemberDefaultLiteral* Literal = Cast<UMetasoundEditorGraphMemberDefaultLiteral>(OuterObjects.Last()))
					{
						if (UMetasoundEditorGraphInput* Input = Cast<UMetasoundEditorGraphInput>(Literal->GetParentMember()))
						{
							// Don't display trigger simulation widget if its a trigger
							// provided by an interface that does not support transmission.
							const FInterfaceRegistryKey Key = GetInterfaceRegistryKey(Input->GetInterfaceVersion());
							const IInterfaceRegistryEntry* Entry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(Key);
							if (!Entry || Entry->GetRouterName() == Audio::IParameterTransmitter::RouterName)
							{
								EnablementAttribute = true;
								return SMetaSoundGraphNode::CreateTriggerSimulationWidget(*Literal, MoveTemp(VisibilityAttribute), MoveTemp(EnablementAttribute));
							}

							const FText DisabledToolTip = LOCTEXT("NonTransmittibleInputTriggerSimulationDisabledTooltip", "Trigger simulation disabled: Parent interface does not support being updated by game thread parameters.");
							return SMetaSoundGraphNode::CreateTriggerSimulationWidget(*Literal, MoveTemp(VisibilityAttribute), MoveTemp(EnablementAttribute), &DisabledToolTip);
						}
					}
				}
			}

			return SNullWidget::NullWidget;
		}

		void FMetasoundMemberDefaultIntDetailCustomization::CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle)
		{
			DataTypeName = FName();

			const FString* MetadataDataTypeName = ProxyHandle->GetInstanceMetaData(MemberCustomizationStyle::DataTypeNameIdentifier);
			if (ensure(MetadataDataTypeName))
			{
				DataTypeName = **MetadataDataTypeName;
			}
		}

		TSharedRef<SWidget> FMetasoundMemberDefaultIntDetailCustomization::CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const
		{
			using namespace Frontend;

			if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
			{
				TSharedPtr<IPropertyHandle> ValueProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundEditorGraphMemberDefaultIntRef, Value));
				if (ValueProperty.IsValid())
				{
					TSharedPtr<const IEnumDataTypeInterface> EnumInterface = IDataTypeRegistry::Get().GetEnumInterfaceForDataType(DataTypeName);

					// Not an enum, so just display as underlying type (int32)
					if (!EnumInterface.IsValid())
					{
						return ValueProperty->CreatePropertyValueWidget();
					}

					auto GetAll = [Interface = EnumInterface](TArray<TSharedPtr<FString>>& OutStrings, TArray<TSharedPtr<SToolTip>>& OutTooltips, TArray<bool>&)
					{
						for (const IEnumDataTypeInterface::FGenericInt32Entry& i : Interface->GetAllEntries())
						{
							OutTooltips.Emplace(SNew(SToolTip).Text(i.Tooltip));
							OutStrings.Emplace(MakeShared<FString>(i.DisplayName.ToString()));
						}
					};
					auto GetValue = [Interface = EnumInterface, Prop = ValueProperty]()
					{
						int32 IntValue;
						if (Prop->GetValue(IntValue) != FPropertyAccess::Success)
						{
							IntValue = Interface->GetDefaultValue();
							UE_LOG(LogMetasoundEditor, Warning, TEXT("Failed to read int Property '%s', defaulting."), *GetNameSafe(Prop->GetProperty()));
						}
						if (TOptional<IEnumDataTypeInterface::FGenericInt32Entry> Result = Interface->FindByValue(IntValue))
						{
							return Result->DisplayName.ToString();
						}
						UE_LOG(LogMetasoundEditor, Warning, TEXT("Failed to resolve int value '%d' to a valid enum value for enum '%s'"),
							IntValue, *Interface->GetNamespace().ToString());

						// Return default (should always succeed as we can't have empty Enums and we must have a default).
						return Interface->FindByValue(Interface->GetDefaultValue())->DisplayName.ToString();
					};
					auto SelectedValue = [Interface = EnumInterface, Prop = ValueProperty](const FString& InSelected)
					{
						TOptional<IEnumDataTypeInterface::FGenericInt32Entry> Found =
							Interface->FindEntryBy([TextSelected = FText::FromString(InSelected)](const IEnumDataTypeInterface::FGenericInt32Entry& i)
						{
							return i.DisplayName.EqualTo(TextSelected);
						});

						if (Found)
						{
							// Only save the changes if its different and we can read the old value to check that.
							int32 CurrentValue;
							bool bReadCurrentValue = Prop->GetValue(CurrentValue) == FPropertyAccess::Success;
							if ((bReadCurrentValue && CurrentValue != Found->Value) || !bReadCurrentValue)
							{
								ensure(Prop->SetValue(Found->Value) == FPropertyAccess::Success);
							}
						}
						else
						{
							UE_LOG(LogMetasoundEditor, Warning, TEXT("Failed to Set Valid Value for Property '%s' with Value of '%s', writing default."),
								*GetNameSafe(Prop->GetProperty()), *InSelected);

							ensure(Prop->SetValue(Interface->GetDefaultValue()) == FPropertyAccess::Success);
						}
					};

					return PropertyCustomizationHelpers::MakePropertyComboBox(
						nullptr,
						FOnGetPropertyComboBoxStrings::CreateLambda(GetAll),
						FOnGetPropertyComboBoxValue::CreateLambda(GetValue),
						FOnPropertyComboBoxValueSelected::CreateLambda(SelectedValue)
					);
				}
			}

			return SNullWidget::NullWidget;
		}

		void FMetasoundMemberDefaultObjectDetailCustomization::CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle)
		{
			ProxyGenClass.Reset();

			const FString* MetadataProxyGenClass = ProxyHandle->GetInstanceMetaData(MemberCustomizationStyle::ProxyGeneratorClassNameIdentifier);
			TSharedPtr<IPropertyHandle> MetadataHandle = ProxyHandle->GetParentHandle();
			if (!ensure(MetadataProxyGenClass))
			{
				return;
			}

			const FName ClassName = FName(*MetadataProxyGenClass);
			for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
			{
				UClass* Class = *ClassIt;
				if (!Class->IsNative())
				{
					continue;
				}
				
				if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					continue;
				}

				if (ClassIt->GetFName() != ClassName)
				{
					continue;
				}

				ProxyGenClass = *ClassIt;
				return;
			}

			ensureMsgf(false, TEXT("Failed to find ProxyGeneratorClass. Class not set "));
		}

		TSharedRef<SWidget> FMetasoundMemberDefaultObjectDetailCustomization::CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const
		{
			TSharedPtr<IPropertyHandle> PropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundEditorGraphMemberDefaultObjectRef, Object));

			const IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			auto FilterAsset = [InEditorModule = &EditorModule, InProxyGenClass = ProxyGenClass](const FAssetData& InAsset)
			{
				if (InProxyGenClass.IsValid())
				{
					if (UClass* Class = InAsset.GetClass())
					{
						if (InEditorModule->IsExplicitProxyClass(*InProxyGenClass.Get()))
						{
							return Class != InProxyGenClass.Get();
						}

						return !Class->IsChildOf(InProxyGenClass.Get());
					}
				}

				return true;
			};

			auto ValidateAsset = [FilterAsset](const FAssetData& InAsset)
			{
				return !FilterAsset(InAsset);
			};

			auto GetAssetPath = [PropertyHandle = PropertyHandle]()
			{
				UObject* Object = nullptr;
				if (PropertyHandle->GetValue(Object) == FPropertyAccess::Success)
				{
					return Object->GetPathName();
				}
				return FString();
			};

			return SNew(SObjectPropertyEntryBox)
				.AllowClear(true)
				.AllowedClass(ProxyGenClass.Get())
				.DisplayBrowse(true)
				.DisplayThumbnail(true)
				.DisplayUseSelected(true)
				.NewAssetFactories(PropertyCustomizationHelpers::GetNewAssetFactoriesForClasses({ ProxyGenClass.Get() }))
				.ObjectPath_Lambda(GetAssetPath)
				.OnShouldFilterAsset_Lambda(FilterAsset)
				.OnShouldSetAsset_Lambda(ValidateAsset)
				.PropertyHandle(PropertyHandle);
		}

		TSharedRef<SWidget> FMetasoundInputArrayDetailCustomizationBase::CreateNameWidget(TSharedPtr<IPropertyHandle> StructPropertyHandle) const
		{
			const FText PropertyName = GetPropertyNameOverride();
			if (!PropertyName.IsEmpty())
			{
				return SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(PropertyName);
			}

			return SNew(STextBlock)
				.Text(MemberCustomizationStyle::DefaultPropertyText)
				.Font(IDetailLayoutBuilder::GetDetailFont());
		}

		TSharedRef<SWidget> FMetasoundInputArrayDetailCustomizationBase::CreateValueWidget(TSharedPtr<IPropertyHandleArray> ParentArrayProperty, TSharedPtr<IPropertyHandle> StructPropertyHandle, bool bIsInArray) const
		{
			TSharedRef<SWidget> ValueWidget = CreateStructureWidget(StructPropertyHandle);
			if (!bIsInArray)
			{
				return ValueWidget;
			}

			TSharedPtr<IPropertyHandle> StructPropertyPtr = StructPropertyHandle;
			FExecuteAction InsertAction = FExecuteAction::CreateLambda([ParentArrayProperty, StructPropertyPtr]
			{
				const int32 ArrayIndex = StructPropertyPtr.IsValid() ? StructPropertyPtr->GetIndexInArray() : INDEX_NONE;
				if (ParentArrayProperty.IsValid() && ArrayIndex >= 0)
				{
					ParentArrayProperty->Insert(ArrayIndex);
				}
			});

			FExecuteAction DeleteAction = FExecuteAction::CreateLambda([ParentArrayProperty, StructPropertyPtr]
			{
				const int32 ArrayIndex = StructPropertyPtr.IsValid() ? StructPropertyPtr->GetIndexInArray() : INDEX_NONE;
				if (ParentArrayProperty.IsValid() && ArrayIndex >= 0)
				{
					ParentArrayProperty->DeleteItem(ArrayIndex);
				}
			});

			FExecuteAction DuplicateAction = FExecuteAction::CreateLambda([ParentArrayProperty, StructPropertyPtr]
			{
				const int32 ArrayIndex = StructPropertyPtr.IsValid() ? StructPropertyPtr->GetIndexInArray() : INDEX_NONE;
				if (ParentArrayProperty.IsValid() && ArrayIndex >= 0)
				{
					ParentArrayProperty->DuplicateItem(ArrayIndex);
				}
			});

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(1.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					ValueWidget
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(-6.0f, 0.0f, 0.0f, 0.0f) // Negative padding intentional on the left to bring the dropdown closer to the other buttons
				.VAlign(VAlign_Center)
				[
					PropertyCustomizationHelpers::MakeInsertDeleteDuplicateButton(InsertAction, DeleteAction, DuplicateAction)
				];
		}

		void FMetasoundInputArrayDetailCustomizationBase::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
		{
			bool bIsInArray = false;
			TSharedPtr<IPropertyHandleArray> ParentArrayProperty;
			TSharedPtr<IPropertyHandle> ProxyProperty = StructPropertyHandle;
			{
				TSharedPtr<IPropertyHandle> ParentProperty = ProxyProperty->GetParentHandle();
				if (ProxyProperty.IsValid() && ParentProperty.IsValid())
				{
					ParentArrayProperty = ParentProperty->AsArray();
					if (ParentArrayProperty.IsValid())
					{
						ProxyProperty = ParentProperty;
						bIsInArray = true;
					}
				}
			}

			CacheProxyData(ProxyProperty);

			TSharedRef<SWidget> ValueWidget = CreateValueWidget(ParentArrayProperty, StructPropertyHandle, bIsInArray);
			FDetailWidgetRow& ValueRow = ChildBuilder.AddCustomRow(MemberCustomizationStyle::DefaultPropertyText);
			if (bIsInArray)
			{
				ValueRow.NameContent()
				[
					StructPropertyHandle->CreatePropertyNameWidget()
				];
			}
			else
			{
				ValueRow.NameContent()
				[
					CreateNameWidget(StructPropertyHandle)
				];
			}

			TArray<UObject*> OuterObjects;
			StructPropertyHandle->GetOuterObjects(OuterObjects);
			TArray<TWeakObjectPtr<UMetasoundEditorGraphInput>> Inputs;
			for (UObject* Object : OuterObjects)
			{
				if (UMetasoundEditorGraphInput* Input = Cast<UMetasoundEditorGraphInput>(Object))
				{
					Inputs.Add(Input);
				}
			}

			FSimpleDelegate UpdateFrontendDefaultLiteral = FSimpleDelegate::CreateLambda([InInputs = Inputs]()
			{
				for (const TWeakObjectPtr<UMetasoundEditorGraphInput>& GraphInput : InInputs)
				{
					if (GraphInput.IsValid())
					{
						constexpr bool bPostTransaction = true;
						GraphInput->UpdateFrontendDefaultLiteral(bPostTransaction);
					}
				}
			});
			StructPropertyHandle->SetOnChildPropertyValueChanged(UpdateFrontendDefaultLiteral);

			ValueRow.ValueContent()
			[
				ValueWidget
			];
		}

		void FMetasoundInputArrayDetailCustomizationBase::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
		{
		}

		FName FMetasoundDataTypeSelector::GetDataType() const
		{
			if (GraphMember.IsValid())
			{
				return GraphMember->GetDataType();
			}

			return FName();
		}

		void FMetasoundDataTypeSelector::OnDataTypeSelected(FName InSelectedTypeName)
		{
			FName NewDataTypeName;
			FName ArrayDataTypeName = CreateArrayTypeNameFromElementTypeName(InSelectedTypeName);

			// Update data type based on "Is Array" checkbox and support for arrays.
			// If an array type is not supported, default to the base data type.
			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
			if (DataTypeArrayCheckbox->GetCheckedState() == ECheckBoxState::Checked)
			{
				if (EditorModule.IsRegisteredDataType(ArrayDataTypeName))
				{
					NewDataTypeName = ArrayDataTypeName;
				}
				else
				{
					ensure(EditorModule.IsRegisteredDataType(InSelectedTypeName));
					NewDataTypeName = InSelectedTypeName;
				}
			}
			else
			{
				if (EditorModule.IsRegisteredDataType(InSelectedTypeName))
				{
					NewDataTypeName = InSelectedTypeName;
				}
				else
				{
					ensure(EditorModule.IsRegisteredDataType(ArrayDataTypeName));
					NewDataTypeName = ArrayDataTypeName;
				}
			}

			if (NewDataTypeName == GraphMember->GetDataType())
			{
				return;
			}

			// Have to stop playback to avoid attempting to change live edit data on invalid input type.
			check(GEditor);
			GEditor->ResetPreviewAudioComponent();

			if (GraphMember.IsValid())
			{
				GraphMember->SetDataType(NewDataTypeName);

				if (FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(GraphMember->GetOutermostObject()))
				{
					MetasoundAsset->SetUpdateDetailsOnSynchronization();
				}
			}
		}

		void FMetasoundDataTypeSelector::AddDataTypeSelector(IDetailLayoutBuilder& InDetailLayout, const FText& InRowName, TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, bool bIsEnabled)
		{
			using namespace Frontend;

			if (!InGraphMember.IsValid())
			{
				return;
			}

			GraphMember = InGraphMember;

			FDataTypeRegistryInfo DataTypeInfo;
			if (!ensure(IDataTypeRegistry::Get().GetDataTypeInfo(InGraphMember->GetDataType(), DataTypeInfo)))
			{
				return;
			}

			const bool bIsArrayType = DataTypeInfo.IsArrayType();
			if (bIsArrayType)
			{
				ArrayTypeName = GraphMember->GetDataType();
				BaseTypeName = CreateElementTypeNameFromArrayTypeName(InGraphMember->GetDataType());
			}
			else
			{
				ArrayTypeName = CreateArrayTypeNameFromElementTypeName(InGraphMember->GetDataType());
				BaseTypeName = GraphMember->GetDataType();
			}

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			// Not all types have an equivalent array type. Base types without array
			// types should have the "Is Array" checkbox disabled.
			const bool bIsArrayTypeRegistered = bIsArrayType || IDataTypeRegistry::Get().IsRegistered(ArrayTypeName);
			const bool bIsArrayTypeRegisteredHidden = MemberCustomizationPrivate::HiddenInputTypeNames.Contains(ArrayTypeName);

			TArray<FName> BaseDataTypes;
			IDataTypeRegistry::Get().IterateDataTypeInfo([&BaseDataTypes](const FDataTypeRegistryInfo& RegistryInfo)
			{
				// Hide the type from the combo selector if any of the following is true;
				const bool bIsArrayType = RegistryInfo.IsArrayType();
				const bool bIsVariable = RegistryInfo.bIsVariable;
				const bool bIsHiddenType = MemberCustomizationPrivate::HiddenInputTypeNames.Contains(RegistryInfo.DataTypeName);
				const bool bHideBaseType = bIsArrayType || bIsVariable || bIsHiddenType;
				if (!bHideBaseType)
				{
					BaseDataTypes.Add(RegistryInfo.DataTypeName);
				}
			});

			BaseDataTypes.Sort([](const FName& DataTypeNameL, const FName& DataTypeNameR)
			{
				return DataTypeNameL.LexicalLess(DataTypeNameR);
			});

			Algo::Transform(BaseDataTypes, ComboOptions, [](const FName& Name) { return MakeShared<FString>(Name.ToString()); });

			InDetailLayout.EditCategory("General").AddCustomRow(InRowName)
			.IsEnabled(bIsEnabled)
			.NameContent()
			[
				SNew(STextBlock)
				.Text(InRowName)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(1.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(DataTypeComboBox, SSearchableComboBox)
					.OptionsSource(&ComboOptions)
					.OnGenerateWidget_Lambda([](TSharedPtr<FString> InItem)
					{
						return SNew(STextBlock)
							.Text(FText::FromString(*InItem));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> InNewName, ESelectInfo::Type InSelectInfo)
					{
						if (InSelectInfo != ESelectInfo::OnNavigation)
						{
							OnDataTypeSelected(FName(*InNewName));
						}
					})
					.Content()
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return FText::FromName(BaseTypeName);
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(DataTypeArrayCheckbox, SCheckBox)
					.IsEnabled(bIsArrayTypeRegistered && !bIsArrayTypeRegisteredHidden)
					.IsChecked_Lambda([this, InGraphMember]()
					{
						return OnGetDataTypeArrayCheckState(InGraphMember);
					})
					.OnCheckStateChanged_Lambda([this, InGraphMember](ECheckBoxState InNewState)
					{
						OnDataTypeArrayChanged(InGraphMember, InNewState);
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Node_IsArray", "Is Array"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
			];

			auto NameMatchesPredicate = [TypeString = BaseTypeName.ToString()](const TSharedPtr<FString>& Item) { return *Item == TypeString; };
			const TSharedPtr<FString>* SelectedItem = ComboOptions.FindByPredicate(NameMatchesPredicate);
			if (ensure(SelectedItem))
			{
				DataTypeComboBox->SetSelectedItem(*SelectedItem);
			}
		}

		ECheckBoxState FMetasoundDataTypeSelector::OnGetDataTypeArrayCheckState(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember) const
		{
			using namespace Frontend;

			if (InGraphMember.IsValid())
			{
				FDataTypeRegistryInfo DataTypeInfo;
				if (ensure(IDataTypeRegistry::Get().GetDataTypeInfo(InGraphMember->GetDataType(), DataTypeInfo)))
				{
					return DataTypeInfo.IsArrayType() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}
			}

			return ECheckBoxState::Undetermined;
		}

		void FMetasoundDataTypeSelector::OnDataTypeArrayChanged(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, ECheckBoxState InNewState)
		{
			if (InGraphMember.IsValid() && DataTypeComboBox.IsValid())
			{
				TSharedPtr<FString> DataTypeRoot = DataTypeComboBox->GetSelectedItem();
				if (ensure(DataTypeRoot.IsValid()))
				{
					// Have to stop playback to avoid attempting to change live edit data on invalid input type.
					check(GEditor);
					GEditor->ResetPreviewAudioComponent();

					const FName DataType = InNewState == ECheckBoxState::Checked ? ArrayTypeName : BaseTypeName;
					InGraphMember->SetDataType(DataType);

					if (FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(GraphMember->GetOutermostObject()))
					{
						MetasoundAsset->SetUpdateDetailsOnSynchronization();
					}
				}
			}
		}

		const FText FMetasoundInputDetailCustomization::MemberNameText = LOCTEXT("InputGraphMemberLabel", "Input");

		bool FMetasoundInputDetailCustomization::IsInterfaceMember() const
		{
			if (GraphMember.IsValid())
			{
				return CastChecked<UMetasoundEditorGraphVertex>(GraphMember)->IsInterfaceMember();
			}

			return false;
		}

		void FMetasoundInputDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Frontend;

			TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphInput, FMetasoundInputDetailCustomization>::CustomizeDetails(DetailLayout);
		}

		const FText FMetasoundOutputDetailCustomization::MemberNameText = LOCTEXT("OutputGraphMemberLabel", "Output");

		bool FMetasoundOutputDetailCustomization::IsInterfaceMember() const
		{
			if (GraphMember.IsValid())
			{
				return CastChecked<UMetasoundEditorGraphVertex>(GraphMember)->IsInterfaceMember();
			}

			return false;
		}

		void FMetasoundOutputDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphOutput, FMetasoundOutputDetailCustomization>::CustomizeDetails(DetailLayout);
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
