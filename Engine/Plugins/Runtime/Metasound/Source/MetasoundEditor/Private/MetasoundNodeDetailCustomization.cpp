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
#include "MetasoundEditorGraphSchema.h"
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
			static const FText InputNameText = LOCTEXT("Input_Name", "Input Name");
			static const FText InputDisplayNameText = LOCTEXT("InputDisplay_Name", "Input Display Name");

			static const FText OutputNameText = LOCTEXT("Output_Name", "Output Name");
			static const FText OutputDisplayNameText = LOCTEXT("OutputDisplay_Name", "Output Display Name");

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
				FloatLiteral->OnClampInputChanged.Remove(OnClampInputChangedDelegateHandle);
				FloatLiteral->OnRangeChanged.Remove(InputWidgetOnRangeChangedDelegateHandle);
			}
		}

		void FMetasoundFloatLiteralCustomization::CustomizeLiteral(UMetasoundEditorGraphInputLiteral& InLiteral, TSharedPtr<IPropertyHandle> InDefaultValueHandle)
		{
			check(InputCategoryBuilder);

			UMetasoundEditorGraphInputFloat* InputFloat = Cast<UMetasoundEditorGraphInputFloat>(&InLiteral);
			if (!ensure(InputFloat))
			{
				return;
			}
			FloatLiteral = InputFloat;

			if (IDetailPropertyRow* Row = InputCategoryBuilder->AddExternalObjectProperty(TArray<UObject*>({ InputFloat }), GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphInputFloat, ClampDefault)))
			{
				// If clamping or using slider, clamp default value to given range 
				if (InputFloat->ClampDefault || InputFloat->InputWidgetType != EMetasoundInputWidget::None)
				{
					FVector2D Range = InputFloat->GetRange();
					InDefaultValueHandle->SetInstanceMetaData("ClampMin", FString::Printf(TEXT("%f"), Range.X));
					InDefaultValueHandle->SetInstanceMetaData("ClampMax", FString::Printf(TEXT("%f"), Range.Y));
				}
				else // Stop clamping
				{
					InDefaultValueHandle->SetInstanceMetaData("ClampMin", "");
					InDefaultValueHandle->SetInstanceMetaData("ClampMax", "");
				}

				InputFloat->OnClampInputChanged.Remove(OnClampInputChangedDelegateHandle);
				OnClampInputChangedDelegateHandle = InputFloat->OnClampInputChanged.AddLambda([this](bool ClampInput)
				{
					if (FloatLiteral.IsValid())
					{
						FloatLiteral->OnClampInputChanged.Remove(OnClampInputChangedDelegateHandle);
						if (FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(FloatLiteral->GetOutermostObject()))
						{
							MetasoundAsset->SetSynchronizationRequired();
						}
					}
				});
			}
			if (IDetailPropertyRow* Row = InputCategoryBuilder->AddExternalObjectProperty(TArray<UObject*>({ InputFloat }), GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphInputFloat, Range)))
			{
				TSharedPtr<IPropertyHandle> RangeHandle = Row->GetPropertyHandle();
				if (RangeHandle.IsValid())
				{
					TWeakObjectPtr<UMetasoundEditorGraphInput> Input = Cast<UMetasoundEditorGraphInput>(FloatLiteral->GetOuter());
					FSimpleDelegate UpdateDocumentInput = FSimpleDelegate::CreateLambda([Input]()
					{
						if (Input.IsValid())
						{
							if (UMetasoundEditorGraphInputLiteral* InputLiteral = Input->Literal)
							{
								InputLiteral->UpdateDocumentInputLiteral();
							}
						}
					});
					RangeHandle->SetOnPropertyValueChanged(UpdateDocumentInput);
					RangeHandle->SetOnChildPropertyValueChanged(UpdateDocumentInput);

					// If the range is changed, we want to update these details in case we now need to clamp the value
					if (!InputWidgetOnRangeChangedDelegateHandle.IsValid())
					{
						InputWidgetOnRangeChangedDelegateHandle = InputFloat->OnRangeChanged.AddLambda([this](FVector2D Range)
						{
							if (FloatLiteral.IsValid())
							{
								FloatLiteral->OnRangeChanged.Remove(InputWidgetOnRangeChangedDelegateHandle);
								if (FMetasoundAssetBase* MetasoundAsset = Metasound::IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(FloatLiteral->GetOutermostObject()))
								{
									MetasoundAsset->SetSynchronizationRequired();
								}
							}
						});
					}
				}
			}
		}

		void FMetasoundInputBoolDetailCustomization::CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle)
		{
			DataTypeName = FName();

			const FString* MetadataDataTypeName = ProxyHandle->GetInstanceMetaData(MemberCustomizationPrivate::DataTypeNameIdentifier);
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

			const FString* MetadataDataTypeName = ProxyHandle->GetInstanceMetaData(MemberCustomizationPrivate::DataTypeNameIdentifier);
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

		void FMetasoundInputObjectDetailCustomization::CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle)
		{
			ProxyGenClass.Reset();

			const FString* MetadataProxyGenClass = ProxyHandle->GetInstanceMetaData(MemberCustomizationPrivate::ProxyGeneratorClassNameIdentifier);
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

		TSharedRef<SWidget> FMetasoundInputObjectDetailCustomization::CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const
		{
			TSharedPtr<IPropertyHandle> PropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundEditorGraphInputObjectRef, Object));

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
				.Text(MemberCustomizationPrivate::DefaultPropertyText)
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
			FDetailWidgetRow& ValueRow = ChildBuilder.AddCustomRow(MemberCustomizationPrivate::DefaultPropertyText);
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

			FSimpleDelegate UpdateDocumentInput = FSimpleDelegate::CreateLambda([InInputs = Inputs]()
			{
				for (const TWeakObjectPtr<UMetasoundEditorGraphInput>& GraphInput : InInputs)
				{
					if (GraphInput.IsValid())
					{
						if (UMetasoundEditorGraphInputLiteral* InputLiteral = GraphInput->Literal)
						{
							InputLiteral->UpdateDocumentInputLiteral();
						}
					}
				}
			});
			StructPropertyHandle->SetOnChildPropertyValueChanged(UpdateDocumentInput);

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
				return GraphMember->TypeName;
			}

			return FName();
		}

		void FMetasoundDataTypeSelector::OnDataTypeSelected(FName InSelectedTypeName)
		{
			FName ArrayDataTypeName = FName(*InSelectedTypeName.ToString() + MemberCustomizationPrivate::ArrayIdentifier);
			FName NewDataTypeName;

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
					check(EditorModule.IsRegisteredDataType(InSelectedTypeName));
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
					check(EditorModule.IsRegisteredDataType(ArrayDataTypeName));
					NewDataTypeName = ArrayDataTypeName;
				}
			}

			if (NewDataTypeName == GraphMember->TypeName)
			{
				return;
			}

			// Have to stop playback to avoid attempting to change live edit data on invalid input type.
			check(GEditor);
			GEditor->ResetPreviewAudioComponent();

			if (GraphMember.IsValid())
			{
				GraphMember->SetDataType(NewDataTypeName);
			}

			// Required to rebuild the literal details customization.
			// This is seemingly dangerous (as the Builder's raw ptr is cached),
			// but the builder cannot be accessed any other way and instances of
			// this type are always built from and managed by the parent DetailLayoutBuilder.
			check(DetailLayoutBuilder);
			DetailLayoutBuilder->ForceRefreshDetails();
		}

		void FMetasoundDataTypeSelector::AddDataTypeSelector(IDetailLayoutBuilder& InDetailLayout, const FText& InRowName, TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, bool bIsEnabled)
		{
			if (!InGraphMember.IsValid())
			{
				return;
			}

			DetailLayoutBuilder = &InDetailLayout;
			GraphMember = InGraphMember;

			IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory("General");
			FString CurrentTypeName = GraphMember->TypeName.ToString();

			bool bCurrentTypeIsArray = CurrentTypeName.EndsWith(MemberCustomizationPrivate::ArrayIdentifier);
			if (bCurrentTypeIsArray)
			{
				CurrentTypeName.LeftChopInline(MemberCustomizationPrivate::ArrayIdentifier.Len());
			}

			IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");

			// Not all types have an equivalent array type. Base types without array
			// types should have the "Is Array" checkbox disabled. 
			const FName ArrayType = *(CurrentTypeName + MemberCustomizationPrivate::ArrayIdentifier);
			const bool bIsArrayTypeRegistered = EditorModule.IsRegisteredDataType(ArrayType);
			const bool bIsArrayTypeRegisteredHidden = MemberCustomizationPrivate::HiddenInputTypeNames.Contains(ArrayType);

			TArray<FName> DataTypeNames;
			EditorModule.IterateDataTypes([&](const FEditorDataType& EditorDataType)
			{
				const FName& TypeName = EditorDataType.RegistryInfo.DataTypeName;
				FString TypeNameString = TypeName.ToString();

				// Array types are handled separately via checkbox
				if (TypeNameString.EndsWith(MemberCustomizationPrivate::ArrayIdentifier))
				{
					return;
				}

				// Hidden input types should be omitted from the drop down.
				if (!MemberCustomizationPrivate::HiddenInputTypeNames.Contains(EditorDataType.RegistryInfo.DataTypeName))
				{
					DataTypeNames.Add(TypeName);
				}
			});

			DataTypeNames.Sort([](const FName& DataTypeNameL, const FName& DataTypeNameR)
			{
				return DataTypeNameL.LexicalLess(DataTypeNameR);
			});

			Algo::Transform(DataTypeNames, ComboOptions, [](const FName& Name) { return MakeShared<FString>(Name.ToString()); });

			CategoryBuilder.AddCustomRow(InRowName)
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
							return FText::FromName(GetDataType());
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

			auto NameMatchesPredicate = [CurrentTypeName](const TSharedPtr<FString>& Item) { return *Item == CurrentTypeName; };
			const TSharedPtr<FString>* SelectedItem = ComboOptions.FindByPredicate(NameMatchesPredicate);
			if (ensure(SelectedItem))
			{
				DataTypeComboBox->SetSelectedItem(*SelectedItem);
			}
		}

		ECheckBoxState FMetasoundDataTypeSelector::OnGetDataTypeArrayCheckState(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember) const
		{
			if (InGraphMember.IsValid())
			{
				FString CurrentTypeName = InGraphMember->TypeName.ToString();
				bool bCurrentTypeIsArray = CurrentTypeName.EndsWith(MemberCustomizationPrivate::ArrayIdentifier);
				return bCurrentTypeIsArray ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}

			return ECheckBoxState::Undetermined;
		}

		void FMetasoundInputDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Frontend;

			TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphInput>::CustomizeDetails(DetailLayout);

			if (!GraphMember.IsValid())
			{
				return;
			}

			IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory("General");

			const bool bIsInterfaceMember = CastChecked<UMetasoundEditorGraphVertex>(GraphMember)->IsInterfaceMember();
			const bool bIsGraphEditable = IsGraphEditable();
			NameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundInputDetailCustomization::GetName)
				.OnTextChanged(this, &FMetasoundInputDetailCustomization::OnNameChanged)
				.OnTextCommitted(this, &FMetasoundInputDetailCustomization::OnNameCommitted)
				.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			DisplayNameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundInputDetailCustomization::GetDisplayName)
				.OnTextCommitted(this, &FMetasoundInputDetailCustomization::OnDisplayNameCommitted)
				.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::InputNameText)
			.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(MemberCustomizationPrivate::InputNameText)
				.ToolTipText(LOCTEXT("InputName_Description", "Name used by external systems to identify input. Used as DisplayName within MetaSound Graph Editor if no DisplayName is provided."))
			]
			.ValueContent()
			[
				NameEditableTextBox.ToSharedRef()
			];

			// TODO: Enable and use proper FText property editor
// 			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::InputDisplayNameText)
// 			.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
// 			.NameContent()
// 			[
// 				SNew(STextBlock)
// 				.Font(IDetailLayoutBuilder::GetDetailFontBold())
// 				.Text(MemberCustomizationPrivate::InputDisplayNameText)
// 				.ToolTipText(LOCTEXT("InputDisplayName_Description", "Optional, localized name used within the MetaSounds editor(s) to describe the given input."))
// 			]
// 			.ValueContent()
// 			[
// 				DisplayNameEditableTextBox.ToSharedRef()
// 			];

			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::NodeTooltipText)
			.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(MemberCustomizationPrivate::NodeTooltipText)
			]
			.ValueContent()
			[
				SNew(SMultiLineEditableTextBox)
				.Text(this, &FMetasoundInputDetailCustomization::GetTooltip)
				.OnTextCommitted(this, &FMetasoundInputDetailCustomization::OnTooltipCommitted)
				.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
				.ModiferKeyForNewLine(EModifierKey::Shift)
				.RevertTextOnEscape(true)
				.WrapTextAt(MemberCustomizationPrivate::DetailsTitleMaxWidth - MemberCustomizationPrivate::DetailsTitleWrapPadding)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

			DataTypeSelector->AddDataTypeSelector(DetailLayout, MemberCustomizationPrivate::DataTypeNameText, GraphMember, !bIsInterfaceMember && bIsGraphEditable);

			IDetailCategoryBuilder& DefaultCategoryBuilder = DetailLayout.EditCategory("DefaultValue");
			TSharedPtr<IPropertyHandle> LiteralHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphInput, Literal));
			if (ensure(GraphMember.IsValid()) && ensure(LiteralHandle.IsValid()))
			{
				UObject* LiteralObject = nullptr;
				if (LiteralHandle->GetValue(LiteralObject) == FPropertyAccess::Success)
				{
					if (ensure(LiteralObject))
					{
						LiteralHandle->MarkHiddenByCustomization();

						TSharedPtr<IPropertyHandle> DefaultValueHandle;

						if (IDetailPropertyRow* Row = DefaultCategoryBuilder.AddExternalObjectProperty(TArray<UObject*>({ LiteralObject }), "Default"))
						{
							DefaultValueHandle = Row->GetPropertyHandle();
							if (DefaultValueHandle.IsValid())
							{
								SetDefaultPropertyMetaData(DefaultValueHandle.ToSharedRef());

								FSimpleDelegate UpdateDocumentInput = FSimpleDelegate::CreateLambda([GraphMember = this->GraphMember]()
								{
									if (GraphMember.IsValid())
									{
										if (UMetasoundEditorGraphInputLiteral* InputLiteral = GraphMember->Literal)
										{
											InputLiteral->UpdateDocumentInputLiteral();
										}
									}
								});

								DefaultValueHandle->SetOnPropertyValueChanged(UpdateDocumentInput);
								DefaultValueHandle->SetOnChildPropertyValueChanged(UpdateDocumentInput);

								TSharedPtr<IPropertyHandleArray> DefaultValueArray = DefaultValueHandle->AsArray();
								if (DefaultValueArray.IsValid())
								{
									DefaultValueArray->SetOnNumElementsChanged(UpdateDocumentInput);
								}
							}
						}

						IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
						LiteralCustomization = EditorModule.CreateInputLiteralCustomization(*LiteralObject->GetClass(), DefaultCategoryBuilder);
						if (LiteralCustomization.IsValid())
						{
							LiteralCustomization->CustomizeLiteral(*CastChecked<UMetasoundEditorGraphInputLiteral>(LiteralObject), DefaultValueHandle);
						}

						UMetasoundEditorGraphInputFloat* InputFloat = Cast<UMetasoundEditorGraphInputFloat>(LiteralObject);
						if (InputFloat)
						{
							// add input widget properties 
							IDetailCategoryBuilder& WidgetCategoryBuilder = DetailLayout.EditCategory("Widget");
							WidgetCategoryBuilder.AddExternalObjectProperty(TArray<UObject*>({ InputFloat }), GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphInputFloat, InputWidgetType));
							WidgetCategoryBuilder.AddExternalObjectProperty(TArray<UObject*>({ InputFloat }), GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphInputFloat, InputWidgetOrientation));
							WidgetCategoryBuilder.AddExternalObjectProperty(TArray<UObject*>({ InputFloat }), GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphInputFloat, InputWidgetValueType));
						}
					}
					else
					{
						DefaultCategoryBuilder.AddProperty(LiteralHandle);
					}
				}
			}
		}

		void FMetasoundDataTypeSelector::OnDataTypeArrayChanged(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, ECheckBoxState InNewState)
		{
			if (InGraphMember.IsValid() && DataTypeComboBox.IsValid())
			{
				TSharedPtr<FString> DataTypeRoot = DataTypeComboBox->GetSelectedItem();
				if (ensure(DataTypeRoot.IsValid()))
				{
					FString DataTypeString = *DataTypeRoot.Get();
					if (InNewState == ECheckBoxState::Checked)
					{
						DataTypeString += MemberCustomizationPrivate::ArrayIdentifier;
					}

					// Have to stop playback to avoid attempting to change live edit data on invalid input type.
					check(GEditor);
					GEditor->ResetPreviewAudioComponent();

					InGraphMember->SetDataType(FName(DataTypeString));

					// Required to rebuild the literal details customization.
					// This is seemingly dangerous (as the Builder's raw ptr is cached),
					// but the builder cannot be accessed any other way and instances of
					// this type are always built from and managed by the parent DetailLayoutBuilder.
					check(DetailLayoutBuilder);
					DetailLayoutBuilder->ForceRefreshDetails();
				}
			}
		}

		void FMetasoundInputDetailCustomization::SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const
		{
			using namespace Frontend;

			if (!GraphMember.IsValid())
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
			if (TypeNameString.EndsWith(MemberCustomizationPrivate::ArrayIdentifier))
			{
				TypeNameString = TypeNameString.LeftChop(MemberCustomizationPrivate::ArrayIdentifier.Len());
			}
			InDefaultPropertyHandle->SetInstanceMetaData(MemberCustomizationPrivate::DataTypeNameIdentifier, TypeNameString);

			FDataTypeRegistryInfo DataTypeInfo;
			if (!ensure(IDataTypeRegistry::Get().GetDataTypeInfo(TypeName, DataTypeInfo)))
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
				InDefaultPropertyHandle->SetInstanceMetaData(MemberCustomizationPrivate::ProxyGeneratorClassNameIdentifier, ClassName);
			}
		}

		FName FMetasoundInputDetailCustomization::GetLiteralDataType() const
		{
			return GraphMember->TypeName;
		}

		void FMetasoundOutputDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Frontend;

			TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphOutput>::CustomizeDetails(DetailLayout);
			if (!GraphMember.IsValid())
			{
				return;
			}

			IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory("General");

			const bool bIsInterfaceMember = CastChecked<UMetasoundEditorGraphVertex>(GraphMember)->IsInterfaceMember();
			const bool bIsGraphEditable = IsGraphEditable();

			NameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundOutputDetailCustomization::GetName)
				.OnTextChanged(this, &FMetasoundOutputDetailCustomization::OnNameChanged)
				.OnTextCommitted(this, &FMetasoundOutputDetailCustomization::OnNameCommitted)
				.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			DisplayNameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundOutputDetailCustomization::GetDisplayName)
				.OnTextCommitted(this, &FMetasoundOutputDetailCustomization::OnDisplayNameCommitted)
				.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::OutputNameText)
			.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(MemberCustomizationPrivate::OutputNameText)
				.ToolTipText(LOCTEXT("OutputName_Description", "Name used by external systems to identify output. Used as DisplayName within MetaSound Graph Editor if no DisplayName is provided."))
			]
			.ValueContent()
			[
				NameEditableTextBox.ToSharedRef()
			];

			// TODO: Enable and use proper FText property editor
// 			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::OutputDisplayNameText)
// 			.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
// 			.NameContent()
// 			[
// 				SNew(STextBlock)
// 				.Font(IDetailLayoutBuilder::GetDetailFontBold())
// 				.Text(MemberCustomizationPrivate::OutputDisplayNameText)
// 				.ToolTipText(LOCTEXT("OutputDisplayName_Description", "Optional, localized name used within the MetaSounds editor(s) to describe the given output."))
// 			]
// 			.ValueContent()
// 			[
// 				DisplayNameEditableTextBox.ToSharedRef()
// 			];

			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::NodeTooltipText)
			.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(MemberCustomizationPrivate::NodeTooltipText)
			]
			.ValueContent()
			[
				SNew(SMultiLineEditableTextBox)
				.Text(this, &FMetasoundOutputDetailCustomization::GetTooltip)
				.OnTextCommitted(this, &FMetasoundOutputDetailCustomization::OnTooltipCommitted)
				.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
				.ModiferKeyForNewLine(EModifierKey::Shift)
				.RevertTextOnEscape(true)
				.WrapTextAt(MemberCustomizationPrivate::DetailsTitleMaxWidth - MemberCustomizationPrivate::DetailsTitleWrapPadding)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

			DataTypeSelector->AddDataTypeSelector(DetailLayout, MemberCustomizationPrivate::DataTypeNameText, GraphMember, !bIsInterfaceMember && bIsGraphEditable);
		}

		void FMetasoundOutputDetailCustomization::SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const
		{
			using namespace Frontend;

			if (!GraphMember.IsValid())
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
			if (TypeNameString.EndsWith(MemberCustomizationPrivate::ArrayIdentifier))
			{
				TypeNameString = TypeNameString.LeftChop(MemberCustomizationPrivate::ArrayIdentifier.Len());
			}
			InDefaultPropertyHandle->SetInstanceMetaData(MemberCustomizationPrivate::DataTypeNameIdentifier, TypeNameString);

			FDataTypeRegistryInfo DataTypeInfo;
			if (!ensure(IDataTypeRegistry::Get().GetDataTypeInfo(TypeName, DataTypeInfo)))
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
				InDefaultPropertyHandle->SetInstanceMetaData(MemberCustomizationPrivate::ProxyGeneratorClassNameIdentifier, ClassName);
			}
		}

		FName FMetasoundOutputDetailCustomization::GetLiteralDataType() const
		{
			return GraphMember->TypeName;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
