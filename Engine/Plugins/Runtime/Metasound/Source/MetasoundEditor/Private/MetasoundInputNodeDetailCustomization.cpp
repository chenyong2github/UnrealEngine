// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundInputNodeDetailCustomization.h"

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
#include "IPropertyTypeCustomization.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundUObjectRegistry.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "ScopedTransaction.h"
#include "SlateCore/Public/Styling/SlateColor.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "MetasoundEditor"

namespace Metasound
{
	namespace Editor
	{
		namespace NodeCustomizationUtils
		{
			/** Minimum size of the details title panel */
			static const float DetailsTitleMinWidth = 125.f;
			/** Maximum size of the details title panel */
			static const float DetailsTitleMaxWidth = 300.f;
			/** magic number retrieved from SGraphNodeComment::GetWrapAt() */
			static const float DetailsTitleWrapPadding = 32.0f;

			static const FText NodeTooltipText = LOCTEXT("Node_Tooltip", "Tooltip");
			static const FText DefaultPropertyText = LOCTEXT("Node_DefaultPropertyName", "Default Value");

			static const FText InputNodeNameText = LOCTEXT("InputNode_Name", "Input Name");

			static const FName ProxyGeneratorClassNameIdentifier = "GeneratorClass";

			void UpdateInputLiteralFromNode(const TWeakObjectPtr<UMetasoundEditorGraphInputNode>& GraphNode)
			{
				using namespace Frontend;

				if (!GraphNode.IsValid())
				{
					return;
				}

				const FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
				const FString& NodeName = NodeHandle->GetNodeName();
				const FGraphHandle GraphHandle = GraphNode->GetRootGraphHandle();

				const TArray<FGuid> PointIDs = GraphHandle->GetDefaultIDsForInputVertex(NodeName);
				if (ensure(PointIDs.Num() == 1))
				{
					const FMetasoundFrontendLiteral Literal = GraphNode->GetLiteralDefault();
					const TArray<FOutputHandle> Outputs = NodeHandle->GetOutputs();
					if (ensure(Outputs.Num() == 1))
					{
						const FName TypeName = Outputs[0]->GetDataType();
						GraphHandle->SetDefaultInputToFrontendLiteral(NodeName, PointIDs[0], TypeName, Literal);
					}
				}
			}
		}

		void FMetasoundInputNodeObjectDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
		{
		}

		void FMetasoundInputNodeObjectDetailCustomization::CacheProxyClass(TSharedPtr<IPropertyHandle> InPropertyHandle)
		{
			ProxyGenClass.Reset();

			const FString* MetadataProxyGenClass = InPropertyHandle->GetInstanceMetaData(NodeCustomizationUtils::ProxyGeneratorClassNameIdentifier);
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

		TSharedRef<SWidget> FMetasoundInputNodeObjectDetailCustomization::CreateObjectPickerWidget(TSharedPtr<IPropertyHandle>& PropertyHandle) const
		{
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
						return Class->IsChildOf(InProxyGenClass.Get());
					}
				}

				return false;
			};

			auto CommitAsset = [InPropertyHandle = PropertyHandle, InProxyGenClass = ProxyGenClass](const FAssetData& InAssetData)
			{
				// if we've hit this code, the presumption is that the datatype for this parameter has already defined a corresponding UClass that can be used to set it.
				ensureAlways(InProxyGenClass.IsValid());

				UObject* InObject = InAssetData.GetAsset();

				InPropertyHandle->SetValue(InObject);
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

			return SNew(SObjectPropertyEntryBox)
				.ObjectPath_Lambda(GetAssetPath)
				.AllowedClass(ProxyGenClass.Get())
				.OnShouldSetAsset_Lambda(ValidateAsset)
				.OnObjectChanged_Lambda(CommitAsset)
				.AllowClear(false)
				.DisplayUseSelected(true)
				.DisplayBrowse(true)
				.DisplayThumbnail(true)
				.NewAssetFactories(PropertyCustomizationHelpers::GetNewAssetFactoriesForClasses(AllowedClasses));
		}

		TSharedRef<SWidget> FMetasoundInputNodeObjectDetailCustomization::CreateValueWidget(TSharedPtr<IPropertyHandleArray> ParentArrayProperty, TSharedPtr<IPropertyHandle> StructPropertyHandle, bool bIsInArray) const
		{
			TSharedPtr<IPropertyHandle> PropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMetasoundEditorGraphInputObject, Object));
			TSharedRef<SWidget> ObjectPickerWidget = CreateObjectPickerWidget(PropertyHandle);
			if (!bIsInArray)
			{
				return ObjectPickerWidget;
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
					ObjectPickerWidget
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.05f)
				.Padding(1.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					PropertyCustomizationHelpers::MakeInsertDeleteDuplicateButton(InsertAction, DeleteAction, DuplicateAction)
				];
		}

		void FMetasoundInputNodeObjectDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
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

			CacheProxyClass(ProxyProperty);

			TSharedRef<SWidget> ValueWidget = CreateValueWidget(ParentArrayProperty, StructPropertyHandle, bIsInArray);

			FDetailWidgetRow& ValueRow = ChildBuilder.AddCustomRow(NodeCustomizationUtils::DefaultPropertyText);
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
					SNew(STextBlock)
					.Text(NodeCustomizationUtils::DefaultPropertyText)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
			}

			TArray<UObject*> OuterObjects;
			StructPropertyHandle->GetOuterObjects(OuterObjects);
			TArray<TWeakObjectPtr<UMetasoundEditorGraphInputNode>> Inputs;
			for (UObject* Object : OuterObjects)
			{
				if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(Object))
				{
					Inputs.Add(InputNode);
				}
			}

			FSimpleDelegate OnLiteralChanged = FSimpleDelegate::CreateLambda([InInputs = Inputs]()
			{
				for (const TWeakObjectPtr<UMetasoundEditorGraphInputNode>& InputNode : InInputs)
				{
					NodeCustomizationUtils::UpdateInputLiteralFromNode(InputNode);
				}
			});
			StructPropertyHandle->SetOnChildPropertyValueChanged(OnLiteralChanged);

			ValueRow.ValueContent()
			[
				ValueWidget
			];
		}

		bool FMetasoundInputNodeDetailCustomization::IsRequired() const
		{
			if (GraphNode.IsValid())
			{
				return FGraphBuilder::IsRequiredInput(GraphNode->GetNodeHandle());
			}

			return false;
		}

		void FMetasoundInputNodeDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Metasound::Frontend;

			TMetasoundNodeVariableDetailCustomization<UMetasoundEditorGraphInputNode>::CustomizeDetails(DetailLayout);

			if (!GraphNode.IsValid())
			{
				return;
			}

			IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory("General");

			const bool bIsRequired = IsRequired();
			DisplayNameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundInputNodeDetailCustomization::GetDisplayName)
				.OnTextChanged(this, &FMetasoundInputNodeDetailCustomization::OnDisplayNameChanged)
				.OnTextCommitted(this, &FMetasoundInputNodeDetailCustomization::OnDisplayNameCommitted)
				.IsReadOnly(bIsRequired)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			CategoryBuilder.AddCustomRow(NodeCustomizationUtils::InputNodeNameText)
			.EditCondition(!bIsRequired, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(NodeCustomizationUtils::InputNodeNameText)
				.ToolTipText(TAttribute<FText>::Create([GraphNode = this->GraphNode]()
				{
					if (GraphNode.IsValid())
					{
						Frontend::FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
						FMetasoundFrontendNodeStyle NodeStyle = NodeHandle->GetNodeStyle();
						return NodeHandle->GetClassDescription();
					}

					return FText::GetEmpty();
				}))
			]
			.ValueContent()
			[
				DisplayNameEditableTextBox.ToSharedRef()
			];

			CategoryBuilder.AddCustomRow(NodeCustomizationUtils::NodeTooltipText)
			.EditCondition(!bIsRequired, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(NodeCustomizationUtils::NodeTooltipText)
			]
			.ValueContent()
			[
				SNew(SMultiLineEditableTextBox)
				.Text(this, &FMetasoundInputNodeDetailCustomization::GetTooltip)
				.OnTextCommitted(this, &FMetasoundInputNodeDetailCustomization::OnTooltipCommitted)
				.IsReadOnly(bIsRequired)
				.ModiferKeyForNewLine(EModifierKey::Shift)
				.RevertTextOnEscape(true)
				.WrapTextAt(NodeCustomizationUtils::DetailsTitleMaxWidth - NodeCustomizationUtils::DetailsTitleWrapPadding)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

			CategoryBuilder.AddCustomRow(LOCTEXT("InputPrivate", "Private"))
			.Visibility(TAttribute<EVisibility>(this, &FMetasoundInputNodeDetailCustomization::ExposePrivateVisibility))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InputPrivate", "Private"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SCheckBox)
				.IsChecked(this, &FMetasoundInputNodeDetailCustomization::OnGetPrivateCheckboxState)
				.OnCheckStateChanged(this, &FMetasoundInputNodeDetailCustomization::OnPrivateChanged)
			];

			FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
			const TArray<FOutputHandle>& Outputs = NodeHandle->GetOutputs();
			if (!ensure(!Outputs.IsEmpty()))
			{
				return;
			}

			IDetailCategoryBuilder& DefaultCategoryBuilder = DetailLayout.EditCategory("DefaultValue");

			TArray<TSharedRef<IPropertyHandle>> DefaultValueHandles;
			DefaultCategoryBuilder.GetDefaultProperties(DefaultValueHandles);

			for (TSharedRef<IPropertyHandle>& Handle : DefaultValueHandles)
			{
				const FName PropertyName = Handle->GetProperty()->GetFName();
				if (PropertyName == GraphNode->GetLiteralDefaultPropertyFName())
				{
					SetDefaultPropertyMetaData(Handle);
					FSimpleDelegate OnLiteralChanged = FSimpleDelegate::CreateLambda([InGraphNode = GraphNode]()
					{
						NodeCustomizationUtils::UpdateInputLiteralFromNode(InGraphNode);
					});

					Handle->SetOnPropertyValueChanged(OnLiteralChanged);

					TSharedPtr<IPropertyHandleArray> PropertyArray = Handle->AsArray();
					if (PropertyArray.IsValid())
					{
						PropertyArray->SetOnNumElementsChanged(OnLiteralChanged);
					}
				}

				DefaultCategoryBuilder.AddProperty(Handle);
			}
		}

		void FMetasoundInputNodeDetailCustomization::SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const
		{
			using namespace Metasound::Frontend;

			if (!GraphNode.IsValid())
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

			FDataTypeRegistryInfo DataTypeInfo;
			if (!ensure(Registry->GetInfoForDataType(TypeName, DataTypeInfo)))
			{
				return;
			}

			const EMetasoundFrontendLiteralType LiteralType = Frontend::GetMetasoundFrontendLiteralType(DataTypeInfo.PreferredLiteralType);
			if (LiteralType != EMetasoundFrontendLiteralType::UObject && LiteralType != EMetasoundFrontendLiteralType::UObjectArray)
			{
				return;
			}

			UClass* ProxyGenClass = DataTypeInfo.ProxyGeneratorClass;
			if (!ProxyGenClass)
			{
				return;
			}

			const FString ClassName = ProxyGenClass->GetName();
			InDefaultPropertyHandle->SetInstanceMetaData(NodeCustomizationUtils::ProxyGeneratorClassNameIdentifier, ClassName);
		}

		FName FMetasoundInputNodeDetailCustomization::GetLiteralDataType() const
		{
			using namespace Metasound::Frontend;

			FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
			const TArray<FOutputHandle> Outputs = NodeHandle->GetOutputs();

			if (!ensure(Outputs.Num() == 1))
			{
				return FName();
			}

			return Outputs[0]->GetDataType();
		}

		FText FMetasoundInputNodeDetailCustomization::GetDisplayName() const
		{
			using namespace Metasound::Frontend;

			if (GraphNode.IsValid())
			{
				FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
				const TArray<FOutputHandle>& OutputHandles = NodeHandle->GetOutputs();
				if (ensure(!OutputHandles.IsEmpty()))
				{
					FOutputHandle OutputHandle = OutputHandles[0];
					return OutputHandle->GetDisplayName();
				}
			}

			return FText::GetEmpty();
		}

		void FMetasoundInputNodeDetailCustomization::OnDisplayNameChanged(const FText& InNewText)
		{
			bIsNameInvalid = false;

			// Move validation here
// 			if (bBadThings)
// 			{
// 				bIsNameInvalid = true;
//	 			InputNameEditableTextBox->SetError(FText::Format(LOCTEXT("ComponentInputRenameFailed_InUse", "{0} is in use by another variable or function!"), InNewText));
// 			}
		}

		void FMetasoundInputNodeDetailCustomization::OnDisplayNameCommitted(const FText& InNewName, ETextCommit::Type InTextCommit)
		{
			using namespace Metasound::Frontend;

			if (!bIsNameInvalid && GraphNode.IsValid())
			{
				const FScopedTransaction Transaction(LOCTEXT("RenameInput", "Rename Metasound Input"));
				FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
				const TArray<FOutputHandle>& Outputs = NodeHandle->GetOutputs();
				if (ensure(!Outputs.IsEmpty()))
				{
					FOutputHandle OutputHandle = NodeHandle->GetOutputs()[0];
					FGraphHandle Graph = NodeHandle->GetOwningGraph();
					Graph->SetInputDisplayName(OutputHandle->GetName(), InNewName);
				}
			}

			bIsNameInvalid = false;
			DisplayNameEditableTextBox->SetError(FText::GetEmpty());
		}

		FText FMetasoundInputNodeDetailCustomization::GetTooltip() const
		{
			using namespace Metasound::Frontend;
			if (GraphNode.IsValid())
			{
				FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
				const TArray<FOutputHandle>& OutputHandles = NodeHandle->GetOutputs();
				if (ensure(!OutputHandles.IsEmpty()))
				{
					FOutputHandle OutputHandle = OutputHandles[0];
					return NodeHandle->GetOwningGraph()->GetInputDescription(OutputHandle->GetName());
				}
			}

			return FText::GetEmpty();
		}

		void FMetasoundInputNodeDetailCustomization::OnTooltipCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
		{
			using namespace Metasound::Frontend;

			if (GraphNode.IsValid())
			{
				FNodeHandle NodeHandle = GraphNode->GetNodeHandle();
				const FScopedTransaction Transaction(LOCTEXT("SetInputTooltip", "Set Metasound Input Tooltip"));
				const TArray<FOutputHandle>& OutputHandles = NodeHandle->GetOutputs();
				if (ensure(!OutputHandles.IsEmpty()))
				{
					Frontend::FOutputHandle OutputHandle = OutputHandles[0];
					return NodeHandle->GetOwningGraph()->SetInputDescription(OutputHandle->GetName(), InNewText);
				}
			}
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
