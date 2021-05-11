// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/Widget.h"
#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundUObjectRegistry.h"
#include "ScopedTransaction.h"
#include "Types/SlateEnums.h"
#include "UObject/NameTypes.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "WorkflowOrientedApp/SModeWidget.h"

// Forward Declarations
class FDetailWidgetRow;
class FPropertyRestriction;
class IDetailLayoutBuilder;
class IPropertyHandle;
class SCheckBox;
class STextComboBox;

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		class FMetasoundInputArrayDetailCustomizationBase : public IPropertyTypeCustomization
		{
		public:
			//~ Begin IPropertyTypeCustomization
			virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
			virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
			//~ End IPropertyTypeCustomization

		protected:
			virtual FText GetPropertyNameOverride() const { return FText::GetEmpty(); }
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& PropertyHandle) const = 0;
			virtual void CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle) { }

		private:
			TSharedRef<SWidget> CreateNameWidget(TSharedPtr<IPropertyHandle> StructPropertyHandle) const;
			TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandleArray> ParentArrayProperty, TSharedPtr<IPropertyHandle> StructPropertyHandle, bool bIsInArray) const;
		};

		class FMetasoundInputBoolDetailCustomization : public FMetasoundInputArrayDetailCustomizationBase
		{
		protected:
			virtual FText GetPropertyNameOverride() const override;
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const override;
			virtual void CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle) override;

		private:
			FName DataTypeName;
		};

		class FMetasoundInputIntDetailCustomization : public FMetasoundInputArrayDetailCustomizationBase
		{
		protected:
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const override;
			virtual void CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle) override;

		private:
			FName DataTypeName;
		};

		class FMetasoundInputObjectDetailCustomization : public FMetasoundInputArrayDetailCustomizationBase
		{
		protected:
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const override;
			virtual void CacheProxyData(TSharedPtr<IPropertyHandle> ProxyHandle) override;

		private:
			TWeakObjectPtr<UClass> ProxyGenClass;
		};

		class FMetasoundVariableDataTypeSelector
		{
		public:
			void AddDataTypeSelector(IDetailLayoutBuilder& InDetailLayoutBuilder, const FText& InRowName, TWeakObjectPtr<UMetasoundEditorGraphVariable> InGraphVariable, bool bIsRequired);

			void OnDataTypeArrayChanged(TWeakObjectPtr<UMetasoundEditorGraphVariable> InGraphVariable, ECheckBoxState InNewState);
			ECheckBoxState OnGetDataTypeArrayCheckState(TWeakObjectPtr<UMetasoundEditorGraphVariable> InGraphVariable) const;
			void OnBaseDataTypeChanged(TWeakObjectPtr<UMetasoundEditorGraphVariable> InGraphVariable, TSharedPtr<FString> ItemSelected, ESelectInfo::Type SelectInfo);

		protected:
			TFunction<void()> OnDataTypeChanged;
		
		private:
				TSharedPtr<SCheckBox> DataTypeArrayCheckbox;
				TSharedPtr<STextComboBox> DataTypeComboBox;
				TArray<TSharedPtr<FString>> DataTypeNames;

				IDetailLayoutBuilder* DetailLayoutBuilder = nullptr;
		};

		template <typename VariableType>
		class TMetasoundVariableDetailCustomization : public IDetailCustomization, public FMetasoundVariableDataTypeSelector
		{
		public:
			TMetasoundVariableDetailCustomization(const FText& InVariableLabel)
				: IDetailCustomization()
				, VariableLabel(InVariableLabel)
			{
			}

		protected:
			FText VariableLabel;

			TWeakObjectPtr<VariableType> GraphVariable;
			TSharedPtr<SEditableTextBox> DisplayNameEditableTextBox;
			bool bIsNameInvalid = false;

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override
			{
				TArray<TWeakObjectPtr<UObject>> Objects;
				DetailLayout.GetObjectsBeingCustomized(Objects);
				if (Objects.IsEmpty())
				{
					return;
				}

				GraphVariable = Cast<VariableType>(Objects[0].Get());
			}
			// End of IDetailCustomization interface

			void OnDisplayNameChanged(const FText& InNewName)
			{
				using namespace Frontend;

				bIsNameInvalid = false;
				DisplayNameEditableTextBox->SetError(FText::GetEmpty());

				if (!ensure(GraphVariable.IsValid()))
				{
					return;
				}

				FText Error;
				if (!GraphVariable->CanRename(InNewName, Error))
				{
					bIsNameInvalid = true;
					DisplayNameEditableTextBox->SetError(Error);
				}
			}

			FText GetDisplayName() const
			{
				using namespace Frontend;

				if (GraphVariable.IsValid())
				{
					return GraphVariable->GetNodeHandle()->GetDisplayName();
				}

				return FText::GetEmpty();
			}

			bool IsRequired() const
			{
				if (GraphVariable.IsValid())
				{
					Frontend::FConstNodeHandle NodeHandle = GraphVariable->GetConstNodeHandle();
					return NodeHandle->IsRequired();
				}

				return true;
			}

			void OnTooltipCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
			{
				using namespace Frontend;

				if (GraphVariable.IsValid())
				{
					const FText TransactionLabel = FText::Format(LOCTEXT("SetTooltip", "Set the MetaSound {0}'s tooltip"), VariableLabel);
					const FScopedTransaction Transaction(TransactionLabel);

					GraphVariable->Modify();
					if (UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(GraphVariable->GetOuter()))
					{
						Graph->GetMetasoundChecked().Modify();
					}

					FNodeHandle NodeHandle = GraphVariable->GetNodeHandle();
					NodeHandle->SetDescription(InNewText);
				}
			}

			FText GetTooltip() const
			{
				using namespace Frontend;
				if (GraphVariable.IsValid())
				{
					FNodeHandle NodeHandle = GraphVariable->GetNodeHandle();
					return NodeHandle->GetDescription();
				}

				return FText::GetEmpty();
			}

			void OnDisplayNameCommitted(const FText& InNewName, ETextCommit::Type InTextCommit)
			{
				using namespace Frontend;

				if (!bIsNameInvalid && GraphVariable.IsValid())
				{
					GraphVariable->SetDisplayName(InNewName);
				}

				DisplayNameEditableTextBox->SetError(FText::GetEmpty());
				bIsNameInvalid = false;
			}

			ECheckBoxState OnGetPrivateCheckboxState() const
			{
				if (GraphVariable.IsValid())
				{
					return GraphVariable->GetNodeHandle()->GetNodeStyle().bIsPrivate ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}

				return ECheckBoxState::Unchecked;
			}

			void OnPrivateChanged(ECheckBoxState InNewState)
			{
				if (GraphVariable.IsValid())
				{
					const bool bIsChecked = InNewState == ECheckBoxState::Checked;
					Frontend::FNodeHandle NodeHandle = GraphVariable->GetNodeHandle();
					FMetasoundFrontendNodeStyle NodeStyle = NodeHandle->GetNodeStyle();
					NodeStyle.bIsPrivate = bIsChecked;
					NodeHandle->SetNodeStyle(NodeStyle);
				}
			}

			EVisibility ExposePrivateVisibility() const
			{
				if (IsRequired())
				{
					return EVisibility::Collapsed;
				}

				return EVisibility::Visible;
			}
		};

		class FMetasoundInputDetailCustomization : public TMetasoundVariableDetailCustomization<UMetasoundEditorGraphInput>
		{
		public:
			FMetasoundInputDetailCustomization()
				: TMetasoundVariableDetailCustomization<UMetasoundEditorGraphInput>(LOCTEXT("InputVariableLabel", "Input"))
			{
			}

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
			// End of IDetailCustomization interface

		private:
			void SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const;

			FName GetLiteralDataType() const;
		};

		class FMetasoundOutputDetailCustomization : public TMetasoundVariableDetailCustomization<UMetasoundEditorGraphOutput>
		{
		public:
			FMetasoundOutputDetailCustomization()
				: TMetasoundVariableDetailCustomization<UMetasoundEditorGraphOutput>(LOCTEXT("OutputVariableLabel", "Output"))
			{
			}

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
			// End of IDetailCustomization interface

		private:
			void SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const;

			FName GetLiteralDataType() const;
		};
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
