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
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundUObjectRegistry.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "SlateCore/Public/Styling/SlateColor.h"
#include "SMetasoundGraphNode.h"
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
		namespace VariableCustomizationPrivate
		{
			/** Minimum size of the details title panel */
			static const float DetailsTitleMinWidth = 125.f;
			/** Maximum size of the details title panel */
			static const float DetailsTitleMaxWidth = 300.f;
			/** magic number retrieved from SGraphNodeComment::GetWrapAt() */
			static const float DetailsTitleWrapPadding = 32.0f;

			static const FString ArrayIdentifier = TEXT(":Array");

			static const FText DataTypeNameText = LOCTEXT("Node_DataTypeName", "Type");
			static const FText DefaultPropertyText = LOCTEXT("Node_DefaultPropertyName", "Default Value");
			static const FText NodeTooltipText = LOCTEXT("Node_Tooltip", "Tooltip");

			static const FText InputNameText = LOCTEXT("Input_Name", "Input Name");
			static const FText OutputNameText = LOCTEXT("Output_Name", "Output Name");

			static const FName DataTypeNameIdentifier = "DataTypeName";
			static const FName ProxyGeneratorClassNameIdentifier = "GeneratorClass";

			/** Set of input types which are valid registered types, but should
			 * not show up as an input type option in the MetaSound editor. */
			static const TSet<FName> HiddenInputTypeNames =
			{
				"Audio:Mono",
				"Audio:Stereo"
			};
		}

		void FMetasoundInputBoolDetailCustomization::CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle)
		{
			DataTypeName = FName();

			const FString* MetadataDataTypeName = ProxyHandle->GetInstanceMetaData(VariableCustomizationPrivate::DataTypeNameIdentifier);
			if (ensure(MetadataDataTypeName))
			{
				DataTypeName = **MetadataDataTypeName;
			}
		}

		FText FMetasoundInputBoolDetailCustomization::GetPropertyNameOverride() const
		{
			if (DataTypeName == Metasound::GetMetasoundDataTypeName<Metasound::FTrigger>())
			{
				return LOCTEXT("TriggerInput_SimulateTitle", "Simulate");
			}

			return FText::GetEmpty();
		}

		TSharedRef<SWidget> FMetasoundInputBoolDetailCustomization::CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const
		{
			using namespace Frontend;

			if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
			{
				TSharedPtr<IPropertyHandle> ValueProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundEditorGraphInputBoolRef, Value));
				if (ValueProperty.IsValid())
				{
					// Not a trigger, so just display as underlying literal type (bool)
					if (DataTypeName != Metasound::GetMetasoundDataTypeName<Metasound::FTrigger>())
					{
						return ValueProperty->CreatePropertyValueWidget();
					}

					TArray<UObject*> OuterObjects;
					ValueProperty->GetOuterObjects(OuterObjects);
					for (UObject* Object : OuterObjects)
					{
						if (UMetasoundEditorGraphInputLiteral* Literal = Cast<UMetasoundEditorGraphInputLiteral>(Object))
						{
							return SMetasoundGraphNode::CreateTriggerSimulationWidget(*Literal);
						}
					}
				}
			}

			return SNullWidget::NullWidget;
		}

		void FMetasoundInputIntDetailCustomization::CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle)
		{
			DataTypeName = FName();

			const FString* MetadataDataTypeName = ProxyHandle->GetInstanceMetaData(VariableCustomizationPrivate::DataTypeNameIdentifier);
			if (ensure(MetadataDataTypeName))
			{
				DataTypeName = **MetadataDataTypeName;
			}
		}

		TSharedRef<SWidget> FMetasoundInputIntDetailCustomization::CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const
		{
			using namespace Frontend;

			if (FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get())
			{
				TSharedPtr<IPropertyHandle> ValueProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundEditorGraphInputIntRef, Value));
				if (ValueProperty.IsValid())
				{
					TSharedPtr<const IEnumDataTypeInterface> EnumInterface = Registry->GetEnumInterfaceForDataType(DataTypeName);

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

		void FMetasoundInputObjectDetailCustomization::CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle)
		{
			ProxyGenClass.Reset();

			const FString* MetadataProxyGenClass = ProxyHandle->GetInstanceMetaData(VariableCustomizationPrivate::ProxyGeneratorClassNameIdentifier);
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
				
				if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
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

		TSharedRef<SWidget> FMetasoundInputObjectDetailCustomization::CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const
		{
			TSharedPtr<IPropertyHandle> PropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundEditorGraphInputObjectRef, Object));

			auto ValidateAsset = [InProxyGenClass = ProxyGenClass](const FAssetData& InAsset)
			{
				if (!InProxyGenClass.IsValid())
				{
					return false;
				}

				if (UObject* Object = InAsset.GetAsset())
				{
					if (UClass* Class = Object->GetClass())
					{
						return Class == InProxyGenClass.Get();
					}
				}

				return false;
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

			TArray<const UClass*> AllowedClasses;
			AllowedClasses.Add(ProxyGenClass.Get());

			auto FilterAsset = [InProxyGenClass = ProxyGenClass](const FAssetData& InAsset)
			{
				if (InProxyGenClass.IsValid())
				{
					if (UObject* Object = InAsset.GetAsset())
					{
						if (UClass* Class = Object->GetClass())
						{
							return Class != InProxyGenClass.Get();
						}
					}
				}

				return true;
			};

			return SNew(SObjectPropertyEntryBox)
				.ObjectPath_Lambda(GetAssetPath)
				.AllowedClass(ProxyGenClass.Get())
				.OnShouldSetAsset_Lambda(ValidateAsset)
				.OnShouldFilterAsset_Lambda(FilterAsset)
				.PropertyHandle(PropertyHandle)
				.AllowClear(true)
				.DisplayUseSelected(true)
				.DisplayBrowse(true)
				.DisplayThumbnail(true)
				.NewAssetFactories(PropertyCustomizationHelpers::GetNewAssetFactoriesForClasses(AllowedClasses));
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
				.Text(VariableCustomizationPrivate::DefaultPropertyText)
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
				.FillWidth(0.95f)
				.Padding(1.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					ValueWidget
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.05f)
				.Padding(1.0f, 0.0f, 0.0f, 0.0f)
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
			FDetailWidgetRow& ValueRow = ChildBuilder.AddCustomRow(VariableCustomizationPrivate::DefaultPropertyText);
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

			FSimpleDelegate OnLiteralChanged = FSimpleDelegate::CreateLambda([InInputs = Inputs]()
			{
				for (const TWeakObjectPtr<UMetasoundEditorGraphInput>& GraphInput : InInputs)
				{
					if (GraphInput.IsValid())
					{
						GraphInput->OnLiteralChanged();
					}
				}
			});
			StructPropertyHandle->SetOnChildPropertyValueChanged(OnLiteralChanged);

			ValueRow.ValueContent()
			[
				ValueWidget
			];
		}

		void FMetasoundInputArrayDetailCustomizationBase::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
		{
		}

		void FMetasoundVariableDataTypeSelector::AddDataTypeSelector(IDetailLayoutBuilder& InDetailLayout, const FText& InRowName, TWeakObjectPtr<UMetasoundEditorGraphVariable> InGraphVariable, bool bIsRequired)
		{
			DetailLayoutBuilder = &InDetailLayout;

			IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory("General");

			TSharedPtr<FString> CurrentTypeString;
			FString CurrentTypeName = InGraphVariable->TypeName.ToString();
			bool bCurrentTypeIsArray = CurrentTypeName.EndsWith(VariableCustomizationPrivate::ArrayIdentifier);
			if (bCurrentTypeIsArray)
			{
				CurrentTypeName.LeftChopInline(VariableCustomizationPrivate::ArrayIdentifier.Len());
			}

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			// Not all types have an equivalent array type. Base types without array
			// types should have the "Is Array" checkbox disabled. 
			const FName ArrayType = *(CurrentTypeName + VariableCustomizationPrivate::ArrayIdentifier);
			const bool bIsArrayTypeRegistered = EditorModule.IsRegisteredDataType(ArrayType);
			const bool bIsArrayTypeRegisteredHidden = VariableCustomizationPrivate::HiddenInputTypeNames.Contains(ArrayType);

			DataTypeNames.Reset();
			EditorModule.IterateDataTypes([&](const FEditorDataType& EditorDataType)
			{
				const FString TypeName = EditorDataType.RegistryInfo.DataTypeName.ToString(); 

				// Array types are handled separately via checkbox
				if (TypeName.EndsWith(VariableCustomizationPrivate::ArrayIdentifier))
				{
					return;
				}


				TSharedPtr<FString> TypeStrPtr = MakeShared<FString>(TypeName);
				if (TypeName == CurrentTypeName)
				{
					CurrentTypeString = TypeStrPtr;
				}

				// Hidden input types should be omitted from the drop down.
				if (!VariableCustomizationPrivate::HiddenInputTypeNames.Contains(EditorDataType.RegistryInfo.DataTypeName))
				{
					DataTypeNames.Add(TypeStrPtr);
				}
			});

			if (!ensure(CurrentTypeString.IsValid()))
			{
				return;
			}

			DataTypeNames.Sort([](const TSharedPtr<FString>& DataTypeNameL, const TSharedPtr<FString>& DataTypeNameR)
			{
				if (DataTypeNameL.IsValid() && DataTypeNameR.IsValid())
				{
					return DataTypeNameR->Compare(*DataTypeNameL.Get()) > 0;
				}
				return false;
			});

			CategoryBuilder.AddCustomRow(InRowName)
			.IsEnabled(!bIsRequired)
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
				.FillWidth(0.60f)
				.Padding(1.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(DataTypeComboBox, STextComboBox)
					.OptionsSource(&DataTypeNames)
					.InitiallySelectedItem(CurrentTypeString)
					.OnSelectionChanged_Lambda([this, InGraphVariable](TSharedPtr<FString> ItemSelected, ESelectInfo::Type SelectInfo)
					{
						OnBaseDataTypeChanged(InGraphVariable, ItemSelected, SelectInfo);
					})
					.IsEnabled(!bIsRequired)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.40f)
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(DataTypeArrayCheckbox, SCheckBox)
					.IsEnabled(bIsArrayTypeRegistered && !bIsArrayTypeRegisteredHidden)
					.IsChecked_Lambda([this, InGraphVariable]()
					{
						return OnGetDataTypeArrayCheckState(InGraphVariable);
					})
					.OnCheckStateChanged_Lambda([this, InGraphVariable](ECheckBoxState InNewState)
					{
						OnDataTypeArrayChanged(InGraphVariable, InNewState);
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Node_IsArray", "Is Array"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
			];
		}

		ECheckBoxState FMetasoundVariableDataTypeSelector::OnGetDataTypeArrayCheckState(TWeakObjectPtr<UMetasoundEditorGraphVariable> InGraphVariable) const
		{
			if (InGraphVariable.IsValid())
			{
				FString CurrentTypeName = InGraphVariable->TypeName.ToString();
				bool bCurrentTypeIsArray = CurrentTypeName.EndsWith(VariableCustomizationPrivate::ArrayIdentifier);
				return bCurrentTypeIsArray ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}

			return ECheckBoxState::Undetermined;
		}

		void FMetasoundInputDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Frontend;

			TMetasoundVariableDetailCustomization<UMetasoundEditorGraphInput>::CustomizeDetails(DetailLayout);

			if (!GraphVariable.IsValid())
			{
				return;
			}

			IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory("General");

			const bool bIsRequired = IsRequired();
			DisplayNameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundInputDetailCustomization::GetDisplayName)
				.OnTextChanged(this, &FMetasoundInputDetailCustomization::OnDisplayNameChanged)
				.OnTextCommitted(this, &FMetasoundInputDetailCustomization::OnDisplayNameCommitted)
				.IsReadOnly(bIsRequired)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			CategoryBuilder.AddCustomRow(VariableCustomizationPrivate::InputNameText)
			.EditCondition(!bIsRequired, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(VariableCustomizationPrivate::InputNameText)
				.ToolTipText(TAttribute<FText>::Create([GraphVariable = this->GraphVariable]()
				{
					if (GraphVariable.IsValid())
					{
						FNodeHandle NodeHandle = GraphVariable->GetNodeHandle();
						FMetasoundFrontendNodeStyle NodeStyle = NodeHandle->GetNodeStyle();
						return NodeHandle->GetDescription();
					}

					return FText::GetEmpty();
				}))
			]
			.ValueContent()
			[
				DisplayNameEditableTextBox.ToSharedRef()
			];

			CategoryBuilder.AddCustomRow(VariableCustomizationPrivate::NodeTooltipText)
			.EditCondition(!bIsRequired, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(VariableCustomizationPrivate::NodeTooltipText)
			]
			.ValueContent()
			[
				SNew(SMultiLineEditableTextBox)
				.Text(this, &FMetasoundInputDetailCustomization::GetTooltip)
				.OnTextCommitted(this, &FMetasoundInputDetailCustomization::OnTooltipCommitted)
				.IsReadOnly(bIsRequired)
				.ModiferKeyForNewLine(EModifierKey::Shift)
				.RevertTextOnEscape(true)
				.WrapTextAt(VariableCustomizationPrivate::DetailsTitleMaxWidth - VariableCustomizationPrivate::DetailsTitleWrapPadding)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

			AddDataTypeSelector(DetailLayout, VariableCustomizationPrivate::DataTypeNameText, GraphVariable, bIsRequired);

			CategoryBuilder.AddCustomRow(LOCTEXT("InputPrivate", "Private"))
			.Visibility(TAttribute<EVisibility>(this, &FMetasoundInputDetailCustomization::ExposePrivateVisibility))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InputPrivate", "Private"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SCheckBox)
				.IsChecked(this, &FMetasoundInputDetailCustomization::OnGetPrivateCheckboxState)
				.OnCheckStateChanged(this, &FMetasoundInputDetailCustomization::OnPrivateChanged)
			];

			FNodeHandle NodeHandle = GraphVariable->GetNodeHandle();
			const TArray<FOutputHandle>& Outputs = NodeHandle->GetOutputs();
			if (!ensure(!Outputs.IsEmpty()))
			{
				return;
			}

			IDetailCategoryBuilder& DefaultCategoryBuilder = DetailLayout.EditCategory("DefaultValue");

			TSharedPtr<IPropertyHandle> LiteralHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphInput, Literal));
			if (ensure(GraphVariable.IsValid()) && ensure(LiteralHandle.IsValid()))
			{
				TSharedPtr<IPropertyHandle> DefaultValueHandle;
				UObject* LiteralObject = nullptr;
				if (LiteralHandle->GetValue(LiteralObject) == FPropertyAccess::Success)
				{
					if (ensure(LiteralObject))
					{
						LiteralHandle->MarkHiddenByCustomization();
						if (IDetailPropertyRow* Row = DefaultCategoryBuilder.AddExternalObjectProperty(TArray<UObject*>({ LiteralObject }), "Default"))
						{
							DefaultValueHandle = Row->GetPropertyHandle();
							if (DefaultValueHandle.IsValid())
							{
								SetDefaultPropertyMetaData(DefaultValueHandle.ToSharedRef());

								FSimpleDelegate OnLiteralChanged = FSimpleDelegate::CreateLambda([GraphVariable = this->GraphVariable]()
								{
									if (GraphVariable.IsValid())
									{
										GraphVariable->OnLiteralChanged();
									}
								});

								DefaultValueHandle->SetOnPropertyValueChanged(OnLiteralChanged);
								DefaultValueHandle->SetOnChildPropertyValueChanged(OnLiteralChanged);

								TSharedPtr<IPropertyHandleArray> DefaultValueArray = DefaultValueHandle->AsArray();
								if (DefaultValueArray.IsValid())
								{
									DefaultValueArray->SetOnNumElementsChanged(OnLiteralChanged);
								}
							}
						}
					}
					else
					{
						DefaultCategoryBuilder.AddProperty(LiteralHandle);
					}
				}
			}
		}

		void FMetasoundVariableDataTypeSelector::OnDataTypeArrayChanged(TWeakObjectPtr<UMetasoundEditorGraphVariable> InGraphVariable, ECheckBoxState InNewState)
		{
			if (InGraphVariable.IsValid())
			{
				TSharedPtr<FString> DataTypeRoot = DataTypeComboBox->GetSelectedItem();
				if (ensure(DataTypeRoot.IsValid()))
				{
					FString DataTypeString = *DataTypeRoot.Get();
					if (InNewState == ECheckBoxState::Checked)
					{
						DataTypeString += VariableCustomizationPrivate::ArrayIdentifier;
					}

					// Have to stop playback to avoid attempting to change live edit data on invalid input type.
					check(GEditor);
					GEditor->ResetPreviewAudioComponent();

					InGraphVariable->SetDataType(FName(DataTypeString));

					// Required to rebuild the literal details customization.
					// This is seemingly dangerous (as the Builder's raw ptr is cached),
					// but the builder cannot be accessed any other way and instances of
					// this type are always built from and managed by the parent DetailLayoutBuilder.
					check(DetailLayoutBuilder);
					DetailLayoutBuilder->ForceRefreshDetails();
				}
			}
		}

		void FMetasoundVariableDataTypeSelector::OnBaseDataTypeChanged(TWeakObjectPtr<UMetasoundEditorGraphVariable> InGraphVariable, TSharedPtr<FString> ItemSelected, ESelectInfo::Type SelectInfo)
		{
			if (ItemSelected.IsValid() && !ItemSelected->IsEmpty() && InGraphVariable.IsValid())
			{
				IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

				FName BaseDataTypeName = FName(*ItemSelected.Get());
				FName ArrayDataTypeName = FName(*ItemSelected.Get() + VariableCustomizationPrivate::ArrayIdentifier);

				FName NewDataTypeName;

				// Update data type based on "Is Array" checkbox and support for arrays.
				// If an array type is not supported, default to the base data type.
				if (DataTypeArrayCheckbox->GetCheckedState() == ECheckBoxState::Checked)
				{
					if (EditorModule.IsRegisteredDataType(ArrayDataTypeName))
					{
						NewDataTypeName = ArrayDataTypeName;
					}
					else
					{
						check(EditorModule.IsRegisteredDataType(BaseDataTypeName));
						NewDataTypeName = BaseDataTypeName;
					}
				}
				else
				{
					if (EditorModule.IsRegisteredDataType(BaseDataTypeName))
					{
						NewDataTypeName = BaseDataTypeName;
					}
					else
					{
						check(EditorModule.IsRegisteredDataType(ArrayDataTypeName));
						NewDataTypeName = ArrayDataTypeName;
					}
				}

				// Have to stop playback to avoid attempting to change live edit data on invalid input type.
				check(GEditor);
				GEditor->ResetPreviewAudioComponent();

				InGraphVariable->SetDataType(NewDataTypeName);

				// Required to rebuild the literal details customization.
				// This is seemingly dangerous (as the Builder's raw ptr is cached),
				// but the builder cannot be accessed any other way and instances of
				// this type are always built from and managed by the parent DetailLayoutBuilder.
				check(DetailLayoutBuilder);
				DetailLayoutBuilder->ForceRefreshDetails();
			}
		}

		void FMetasoundInputDetailCustomization::SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const
		{
			using namespace Frontend;

			if (!GraphVariable.IsValid())
			{
				return;
			}

			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
			if (!ensure(Registry))
			{
				return;
			}

			const FName TypeName = GetLiteralDataType();
			if (TypeName.IsNone())
			{
				return;
			}

			FString TypeNameString = TypeName.ToString();
			if (TypeNameString.EndsWith(VariableCustomizationPrivate::ArrayIdentifier))
			{
				TypeNameString = TypeNameString.LeftChop(VariableCustomizationPrivate::ArrayIdentifier.Len());
			}
			InDefaultPropertyHandle->SetInstanceMetaData(VariableCustomizationPrivate::DataTypeNameIdentifier, TypeNameString);

			FDataTypeRegistryInfo DataTypeInfo;
			if (!ensure(Registry->GetInfoForDataType(TypeName, DataTypeInfo)))
			{
				return;
			}

			const EMetasoundFrontendLiteralType LiteralType = GetMetasoundFrontendLiteralType(DataTypeInfo.PreferredLiteralType);
			if (LiteralType != EMetasoundFrontendLiteralType::UObject && LiteralType != EMetasoundFrontendLiteralType::UObjectArray)
			{
				return;
			}

			UClass* ProxyGenClass = DataTypeInfo.ProxyGeneratorClass;
			if (ProxyGenClass)
			{
				const FString ClassName = ProxyGenClass->GetName();
				InDefaultPropertyHandle->SetInstanceMetaData(VariableCustomizationPrivate::ProxyGeneratorClassNameIdentifier, ClassName);
			}
		}

		FName FMetasoundInputDetailCustomization::GetLiteralDataType() const
		{
			using namespace Frontend;

			FName TypeName;

			// Just take last type.  If more than one, all types are the same.
			FConstNodeHandle NodeHandle = GraphVariable->GetConstNodeHandle();
			NodeHandle->IterateConstOutputs([InTypeName = &TypeName](FConstOutputHandle OutputHandle)
			{
				*InTypeName = OutputHandle->GetDataType();
			});

			return TypeName;
		}

		void FMetasoundOutputDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Frontend;

			TMetasoundVariableDetailCustomization<UMetasoundEditorGraphOutput>::CustomizeDetails(DetailLayout);

			if (!GraphVariable.IsValid())
			{
				return;
			}

			IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory("General");

			const bool bIsRequired = IsRequired();
			DisplayNameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundOutputDetailCustomization::GetDisplayName)
				.OnTextChanged(this, &FMetasoundOutputDetailCustomization::OnDisplayNameChanged)
				.OnTextCommitted(this, &FMetasoundOutputDetailCustomization::OnDisplayNameCommitted)
				.IsReadOnly(bIsRequired)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			CategoryBuilder.AddCustomRow(VariableCustomizationPrivate::OutputNameText)
			.EditCondition(!bIsRequired, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(VariableCustomizationPrivate::OutputNameText)
				.ToolTipText(TAttribute<FText>::Create([GraphVariable = this->GraphVariable]()
				{
					if (GraphVariable.IsValid())
					{
						FNodeHandle NodeHandle = GraphVariable->GetNodeHandle();
						return NodeHandle->GetDescription();
					}

					return FText::GetEmpty();
				}))
			]
			.ValueContent()
			[
				DisplayNameEditableTextBox.ToSharedRef()
			];

			CategoryBuilder.AddCustomRow(VariableCustomizationPrivate::NodeTooltipText)
			.EditCondition(!bIsRequired, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(VariableCustomizationPrivate::NodeTooltipText)
			]
			.ValueContent()
			[
				SNew(SMultiLineEditableTextBox)
				.Text(this, &FMetasoundOutputDetailCustomization::GetTooltip)
				.OnTextCommitted(this, &FMetasoundOutputDetailCustomization::OnTooltipCommitted)
				.IsReadOnly(bIsRequired)
				.ModiferKeyForNewLine(EModifierKey::Shift)
				.RevertTextOnEscape(true)
				.WrapTextAt(VariableCustomizationPrivate::DetailsTitleMaxWidth - VariableCustomizationPrivate::DetailsTitleWrapPadding)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

			AddDataTypeSelector(DetailLayout, VariableCustomizationPrivate::DataTypeNameText, GraphVariable, bIsRequired);

			CategoryBuilder.AddCustomRow(LOCTEXT("OutputPrivate", "Private"))
			.Visibility(TAttribute<EVisibility>(this, &FMetasoundOutputDetailCustomization::ExposePrivateVisibility))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OutputPrivate", "Private"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SCheckBox)
				.IsChecked(this, &FMetasoundOutputDetailCustomization::OnGetPrivateCheckboxState)
				.OnCheckStateChanged(this, &FMetasoundOutputDetailCustomization::OnPrivateChanged)
			];
		}

		void FMetasoundOutputDetailCustomization::SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const
		{
			using namespace Frontend;

			if (!GraphVariable.IsValid())
			{
				return;
			}

			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
			if (!ensure(Registry))
			{
				return;
			}

			const FName TypeName = GetLiteralDataType();
			if (TypeName.IsNone())
			{
				return;
			}

			FString TypeNameString = TypeName.ToString();
			if (TypeNameString.EndsWith(VariableCustomizationPrivate::ArrayIdentifier))
			{
				TypeNameString = TypeNameString.LeftChop(VariableCustomizationPrivate::ArrayIdentifier.Len());
			}
			InDefaultPropertyHandle->SetInstanceMetaData(VariableCustomizationPrivate::DataTypeNameIdentifier, TypeNameString);

			FDataTypeRegistryInfo DataTypeInfo;
			if (!ensure(Registry->GetInfoForDataType(TypeName, DataTypeInfo)))
			{
				return;
			}

			const EMetasoundFrontendLiteralType LiteralType = GetMetasoundFrontendLiteralType(DataTypeInfo.PreferredLiteralType);
			if (LiteralType != EMetasoundFrontendLiteralType::UObject && LiteralType != EMetasoundFrontendLiteralType::UObjectArray)
			{
				return;
			}

			UClass* ProxyGenClass = DataTypeInfo.ProxyGeneratorClass;
			if (ProxyGenClass)
			{
				const FString ClassName = ProxyGenClass->GetName();
				InDefaultPropertyHandle->SetInstanceMetaData(VariableCustomizationPrivate::ProxyGeneratorClassNameIdentifier, ClassName);
			}
		}

		FName FMetasoundOutputDetailCustomization::GetLiteralDataType() const
		{
			using namespace Frontend;

			FName TypeName;

			// Just take last type.  If more than one, all types are the same.
			FConstNodeHandle NodeHandle = GraphVariable->GetConstNodeHandle();
			NodeHandle->IterateConstInputs([InTypeName = &TypeName](FConstInputHandle InputHandle)
			{
				*InTypeName = InputHandle->GetDataType();
			});

			return TypeName;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
