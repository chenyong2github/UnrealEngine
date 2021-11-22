// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/Widget.h"
#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "EdGraph/EdGraphSchema.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundEditorModule.h"
#include "MetasoundUObjectRegistry.h"
#include "PropertyHandle.h"
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
class SSearchableComboBox;

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		namespace MemberCustomizationPrivate
		{
			/** Minimum size of the details title panel */
			static constexpr float DetailsTitleMinWidth = 125.f;
			/** Maximum size of the details title panel */
			static constexpr float DetailsTitleMaxWidth = 300.f;
			/** magic number retrieved from SGraphNodeComment::GetWrapAt() */
			static constexpr float DetailsTitleWrapPadding = 32.0f;

			static const FString ArrayIdentifier = TEXT(":Array");

			static const FText DataTypeNameText = LOCTEXT("Node_DataTypeName", "Type");
			static const FText DefaultPropertyText = LOCTEXT("Node_DefaultPropertyName", "Default Value");
			static const FText NodeTooltipText = LOCTEXT("Node_Tooltip", "Tooltip");

			static const FName DataTypeNameIdentifier = "DataTypeName";
			static const FName ProxyGeneratorClassNameIdentifier = "GeneratorClass";

		} // namespace MemberCustomizationPrivate

		class FMetasoundFloatLiteralCustomization : public IMetaSoundInputLiteralCustomization
		{
			IDetailCategoryBuilder* InputCategoryBuilder = nullptr;
			TWeakObjectPtr<UMetasoundEditorGraphInputFloat> FloatLiteral;

			// Delegate for updating the clamp min/max of the input value when the slider range is changed 
			FDelegateHandle InputWidgetOnRangeChangedDelegateHandle;

			// Delegate for clamping the input value or not
			FDelegateHandle OnClampInputChangedDelegateHandle;

		public:
			FMetasoundFloatLiteralCustomization(IDetailCategoryBuilder& InInputCategoryBuilder)
				: InputCategoryBuilder(&InInputCategoryBuilder)
			{
			}

			virtual ~FMetasoundFloatLiteralCustomization();

			virtual void CustomizeLiteral(UMetasoundEditorGraphInputLiteral& InLiteral, TSharedPtr<IPropertyHandle> InDefaultValueHandle) override;
		};

		class FMetasoundFloatLiteralCustomizationFactory : public IMetaSoundInputLiteralCustomizationFactory
		{
		public:
			virtual TUniquePtr<IMetaSoundInputLiteralCustomization> CreateLiteralCustomization(IDetailCategoryBuilder& DefaultCategoryBuilder) const override
			{
				return TUniquePtr<IMetaSoundInputLiteralCustomization>(new FMetasoundFloatLiteralCustomization(DefaultCategoryBuilder));
			}
		};

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

		class FMetasoundDataTypeSelector : public TSharedFromThis<FMetasoundDataTypeSelector>
		{
		public:
			void AddDataTypeSelector(IDetailLayoutBuilder& InDetailLayoutBuilder, const FText& InRowName, TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, bool bInIsInterfaceMember);
			void OnDataTypeArrayChanged(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, ECheckBoxState InNewState);

		protected:
			ECheckBoxState OnGetDataTypeArrayCheckState(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember) const;
			void OnDataTypeSelected(FName InSelectedTypeName);
			FName GetDataType() const;

			TFunction<void()> OnDataTypeChanged;
		
		private:
			TWeakObjectPtr<UMetasoundEditorGraphMember> GraphMember;
			TSharedPtr<SCheckBox> DataTypeArrayCheckbox;
			TSharedPtr<SSearchableComboBox> DataTypeComboBox;
			TArray<TSharedPtr<FString>> ComboOptions;

			IDetailLayoutBuilder* DetailLayoutBuilder = nullptr;
		};

		template <typename GraphMemberType>
		class TMetasoundGraphMemberDetailCustomization : public IDetailCustomization
		{
		public:
			TMetasoundGraphMemberDetailCustomization(const FText& InGraphMemberLabel)
				: IDetailCustomization()
				, GraphMemberLabel(InGraphMemberLabel)
			{
				DataTypeSelector = MakeShared<FMetasoundDataTypeSelector>();
			}

		protected:
			FText GraphMemberLabel;

			TWeakObjectPtr<GraphMemberType> GraphMember;
			TSharedPtr<SEditableTextBox> NameEditableTextBox;
			TSharedPtr<SEditableTextBox> DisplayNameEditableTextBox;
			TSharedPtr<FMetasoundDataTypeSelector> DataTypeSelector;

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

				GraphMember = Cast<GraphMemberType>(Objects[0].Get());
			}
			// End of IDetailCustomization interface

			void OnNameChanged(const FText& InNewName)
			{
				using namespace Frontend;

				bIsNameInvalid = false;
				DisplayNameEditableTextBox->SetError(FText::GetEmpty());

				if (!ensure(GraphMember.IsValid()))
				{
					return;
				}

				FText Error;
				if (!GraphMember->CanRename(InNewName, Error))
				{
					bIsNameInvalid = true;
					DisplayNameEditableTextBox->SetError(Error);
				}
			}

			FText GetDisplayName() const
			{
				using namespace Frontend;

				if (GraphMember.IsValid())
				{
					return GraphMember->GetDisplayName();
				}

				return FText::GetEmpty();
			}

			FText GetName() const
			{
				if (GraphMember.IsValid())
				{
					return FText::FromName(GraphMember->GetMemberName());
				}

				return FText::GetEmpty();
			}

			bool IsGraphEditable() const
			{
				if (GraphMember.IsValid())
				{
					if (const UMetasoundEditorGraph* OwningGraph = GraphMember->GetOwningGraph())
					{
						return OwningGraph->IsEditable();
					}
				}

				return false;
			}

			void OnTooltipCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
			{
				using namespace Frontend;

				if (GraphMember.IsValid())
				{
					constexpr bool bPostTransaction = true;
					GraphMember->SetDescription(InNewText, bPostTransaction);
				}
			}

			FText GetTooltip() const
			{
				if (GraphMember.IsValid())
				{
					return GraphMember->GetDescription();
				}

				return FText::GetEmpty();
			}

			void OnDisplayNameCommitted(const FText& InNewName, ETextCommit::Type InTextCommit)
			{
				using namespace Frontend;

				if (!bIsNameInvalid && GraphMember.IsValid())
				{
					constexpr bool bPostTransaction = true;
					GraphMember->SetDisplayName(FText::GetEmpty(), bPostTransaction);
				}

				DisplayNameEditableTextBox->SetError(FText::GetEmpty());
				bIsNameInvalid = false;
			}

			void OnNameCommitted(const FText& InNewName, ETextCommit::Type InTextCommit)
			{
				using namespace Frontend;

				if (!bIsNameInvalid && GraphMember.IsValid())
				{
					const FText TransactionLabel = FText::Format(LOCTEXT("Rename Graph Member", "Set MetaSound {0}'s Name"), GraphMember->GetGraphMemberLabel());
					const FScopedTransaction Transaction(TransactionLabel);

					constexpr bool bPostTransaction = false;
					GraphMember->SetDisplayName(FText::GetEmpty(), bPostTransaction);
					GraphMember->SetMemberName(*InNewName.ToString(), bPostTransaction);
				}

				DisplayNameEditableTextBox->SetError(FText::GetEmpty());
				bIsNameInvalid = false;
			}

			ECheckBoxState OnGetPrivateCheckboxState() const
			{
				if (GraphMember.IsValid())
				{
					return GraphMember->GetNodeHandle()->GetNodeStyle().bIsPrivate ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}

				return ECheckBoxState::Unchecked;
			}

			void OnPrivateChanged(ECheckBoxState InNewState)
			{
				if (GraphMember.IsValid())
				{
					const bool bIsChecked = InNewState == ECheckBoxState::Checked;
					Frontend::FNodeHandle NodeHandle = GraphMember->GetNodeHandle();
					FMetasoundFrontendNodeStyle NodeStyle = NodeHandle->GetNodeStyle();
					NodeStyle.bIsPrivate = bIsChecked;
					NodeHandle->SetNodeStyle(NodeStyle);
				}
			}
		};

		class FMetasoundInputDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphInput>
		{
		public:
			FMetasoundInputDetailCustomization()
				: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphInput>(LOCTEXT("InputGraphMemberLabel", "Input"))
			{
			}
			virtual ~FMetasoundInputDetailCustomization() = default;

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
			// End of IDetailCustomization interface

		private:
			void SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const;

			FName GetLiteralDataType() const;

			TUniquePtr<IMetaSoundInputLiteralCustomization> LiteralCustomization;
		};

		class FMetasoundOutputDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphOutput>
		{
		public:
			FMetasoundOutputDetailCustomization()
				: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphOutput>(LOCTEXT("OutputGraphMemberLabel", "Output"))
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
