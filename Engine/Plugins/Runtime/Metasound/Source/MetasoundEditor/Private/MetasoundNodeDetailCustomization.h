// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/Widget.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraph/EdGraphSchema.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphMemberDefaults.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundEditorModule.h"
#include "MetasoundUObjectRegistry.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "ScopedTransaction.h"
#include "SSearchableComboBox.h"
#include "Types/SlateEnums.h"
#include "UObject/NameTypes.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "WorkflowOrientedApp/SModeWidget.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		// TODO: Move to actual style
		namespace MemberCustomizationStyle
		{
			/** Maximum size of the details title panel */
			static constexpr float DetailsTitleMaxWidth = 300.f;
			/** magic number retrieved from SGraphNodeComment::GetWrapAt() */
			static constexpr float DetailsTitleWrapPadding = 32.0f;

			static const FText DataTypeNameText = LOCTEXT("Node_DataTypeName", "Type");
			static const FText DefaultPropertyText = LOCTEXT("Node_DefaultPropertyName", "Default Value");
			static const FText NodeTooltipText = LOCTEXT("Node_Tooltip", "Tooltip");
		} // namespace MemberCustomizationStyle

		class FMetasoundFloatLiteralCustomization : public FMetasoundDefaultLiteralCustomizationBase
		{
			TWeakObjectPtr<UMetasoundEditorGraphMemberDefaultFloat> FloatLiteral;

			// Delegate for updating the clamp min/max of the input value when the slider range is changed 
			FDelegateHandle OnRangeChangedDelegateHandle;

			// Delegate for clamping the input value or not
			FDelegateHandle OnClampChangedDelegateHandle;

		public:
			FMetasoundFloatLiteralCustomization(IDetailCategoryBuilder& InDefaultCategoryBuilder)
				: FMetasoundDefaultLiteralCustomizationBase(InDefaultCategoryBuilder)
			{
			}
			virtual ~FMetasoundFloatLiteralCustomization();

			virtual void CustomizeLiteral(UMetasoundEditorGraphMemberDefaultLiteral& InLiteral, IDetailLayoutBuilder& InDetailLayout) override;
		};

		// Customization to support drag-and-drop of Proxy UObject types on underlying members that are structs.
		// Struct ownership of objects required to customize asset filters based on dynamic UObject MetaSound Registry DataTypes.
		class FMetasoundObjectArrayLiteralCustomization : public FMetasoundDefaultLiteralCustomizationBase
		{
		public:
			FMetasoundObjectArrayLiteralCustomization(IDetailCategoryBuilder& InDefaultCategoryBuilder)
				: FMetasoundDefaultLiteralCustomizationBase(InDefaultCategoryBuilder)
			{
			}

			virtual ~FMetasoundObjectArrayLiteralCustomization() = default;

			virtual void CustomizeLiteral(UMetasoundEditorGraphMemberDefaultLiteral& InLiteral, IDetailLayoutBuilder& InDetailLayout) override;
		};

		class FMetasoundDefaultLiteralCustomizationFactory : public IMemberDefaultLiteralCustomizationFactory
		{
		public:
			virtual TUniquePtr<FMetasoundDefaultLiteralCustomizationBase> CreateLiteralCustomization(IDetailCategoryBuilder& DefaultCategoryBuilder) const override
			{
				return TUniquePtr<FMetasoundDefaultLiteralCustomizationBase>(new FMetasoundDefaultLiteralCustomizationBase(DefaultCategoryBuilder));
			}
		};

		// Customization to support float widgets (ex. sliders, knobs)
		class FMetasoundFloatLiteralCustomizationFactory : public IMemberDefaultLiteralCustomizationFactory
		{
		public:
			virtual TUniquePtr<FMetasoundDefaultLiteralCustomizationBase> CreateLiteralCustomization(IDetailCategoryBuilder& DefaultCategoryBuilder) const override
			{
				return TUniquePtr<FMetasoundDefaultLiteralCustomizationBase>(new FMetasoundFloatLiteralCustomization(DefaultCategoryBuilder));
			}
		};

		class FMetasoundObjectArrayLiteralCustomizationFactory : public IMemberDefaultLiteralCustomizationFactory
		{
		public:
			virtual TUniquePtr<FMetasoundDefaultLiteralCustomizationBase> CreateLiteralCustomization(IDetailCategoryBuilder& DefaultCategoryBuilder) const override
			{
				return TUniquePtr<FMetasoundDefaultLiteralCustomizationBase>(new FMetasoundObjectArrayLiteralCustomization(DefaultCategoryBuilder));
			}
		};

		class FMetasoundDefaultMemberElementDetailCustomizationBase : public IPropertyTypeCustomization
		{
		public:
			//~ Begin IPropertyTypeCustomization
			virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
			virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
			//~ End IPropertyTypeCustomization

		protected:
			virtual FText GetPropertyNameOverride() const { return FText::GetEmpty(); }

			// TODO: Merge with FMetasoundDefaultLiteralCustomizationBaseFactory
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& PropertyHandle) const = 0;

			Frontend::FDataTypeRegistryInfo DataTypeInfo;

		private:
			TSharedRef<SWidget> CreateNameWidget(TSharedPtr<IPropertyHandle> StructPropertyHandle) const;
			TSharedRef<SWidget> CreateValueWidget(TSharedPtr<IPropertyHandleArray> ParentArrayProperty, TSharedPtr<IPropertyHandle> StructPropertyHandle) const;
		};

		class FMetasoundMemberDefaultBoolDetailCustomization : public FMetasoundDefaultMemberElementDetailCustomizationBase
		{
		protected:
			virtual FText GetPropertyNameOverride() const override;
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const override;
		};

		class FMetasoundMemberDefaultIntDetailCustomization : public FMetasoundDefaultMemberElementDetailCustomizationBase
		{
		protected:
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const override;
		};

		class FMetasoundMemberDefaultObjectDetailCustomization : public FMetasoundDefaultMemberElementDetailCustomizationBase
		{
		protected:
			virtual TSharedRef<SWidget> CreateStructureWidget(TSharedPtr<IPropertyHandle>& StructPropertyHandle) const override;
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

			FName BaseTypeName;
			FName ArrayTypeName;
		};

		template <typename TMemberType, typename TChildClass>
		class TMetasoundGraphMemberDetailCustomization : public IDetailCustomization
		{
		public:
			TMetasoundGraphMemberDetailCustomization()
				: IDetailCustomization()
			{
				DataTypeSelector = MakeShared<FMetasoundDataTypeSelector>();
			}

			virtual ~TMetasoundGraphMemberDetailCustomization()
			{
				RenameRequestedHandle.Reset();
			}

		protected:
			TWeakObjectPtr<TMemberType> GraphMember;
			TSharedPtr<SEditableTextBox> NameEditableTextBox;
			TSharedPtr<SEditableTextBox> DisplayNameEditableTextBox;
			TSharedPtr<FMetasoundDataTypeSelector> DataTypeSelector;

			bool bIsNameInvalid = false;

			static const FText& GetMemberDisplayNameText()
			{
				static const FText MemberDisplayNameTextFormat = LOCTEXT("GraphMember_NameFormat", "{0} Display Name");
				static const FText DisplayName = FText::Format(MemberDisplayNameTextFormat, TChildClass::MemberNameText);
				return DisplayName;
			}

			static const FText& GetMemberDisplayNameDescriptionText()
			{
				static const FText MemberDisplayNameDescriptionFormat = LOCTEXT("GraphMember_DisplayNameDescriptionFormat", "Optional, localized name used within the MetaSounds editor(s) to describe the given {0}.");
				static const FText DisplayName = FText::Format(MemberDisplayNameDescriptionFormat, TChildClass::MemberNameText);
				return DisplayName;
			}

			static const FText& GetMemberNameText()
			{
				static const FText MemberNameTextFormat = LOCTEXT("GraphMember_DisplayNameFormat", "{0} Name");
				static const FText Name = FText::Format(MemberNameTextFormat, TChildClass::MemberNameText);
				return Name;
			}

			static const FText& GetMemberNameDescriptionText()
			{
				static const FText MemberNameDescriptionFormat = LOCTEXT("GraphMember_DisplayDescriptionFormat", "Name used by external systems/referencing MetaSounds to identify {0}. Used as DisplayName within MetaSound Graph Editor if no DisplayName is provided.");
				static const FText Name = FText::Format(MemberNameDescriptionFormat, TChildClass::MemberNameText);
				return Name;
			}

			void UpdateRenameDelegate(UMetasoundEditorGraphMemberDefaultLiteral& InMemberDefaultLiteral)
			{
				if (UMetasoundEditorGraphMember* Member = InMemberDefaultLiteral.GetParentMember())
				{
					if (const UMetasoundEditorGraph* OwningGraph = GraphMember->GetOwningGraph())
					{
						TSharedPtr<FEditor> MetasoundEditor = FGraphBuilder::GetEditorForGraph(*OwningGraph);
						if (MetasoundEditor->CanRenameNodes())
						{
							if (!RenameRequestedHandle.IsValid())
							{
								Member->OnRenameRequested.Clear();
								RenameRequestedHandle = Member->OnRenameRequested.AddLambda([this]()
								{
									FSlateApplication::Get().SetKeyboardFocus(NameEditableTextBox.ToSharedRef(), EFocusCause::SetDirectly);
								});
							}
						}
					}
				}
			}

			virtual bool IsInterfaceMember() const
			{
				return false;
			}

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override
			{
				using namespace Frontend;

				TArray<TWeakObjectPtr<UObject>> Objects;
				InDetailLayout.GetObjectsBeingCustomized(Objects);
				if (Objects.IsEmpty())
				{
					return;
				}

				GraphMember = Cast<TMemberType>(Objects.Last().Get());
				if (!GraphMember.IsValid())
				{
					return;
				}

				IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory("General");

				const bool bIsInterfaceMember = IsInterfaceMember();
				const bool bIsGraphEditable = IsGraphEditable();

				NameEditableTextBox = SNew(SEditableTextBox)
					.Text(this, &TChildClass::GetName)
					.OnTextChanged(this, &TChildClass::OnNameChanged)
					.OnTextCommitted(this, &TChildClass::OnNameCommitted)
					.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
					.SelectAllTextWhenFocused(true)
					.Font(IDetailLayoutBuilder::GetDetailFont());

				DisplayNameEditableTextBox = SNew(SEditableTextBox)
					.Text(this, &TChildClass::GetDisplayName)
					.OnTextCommitted(this, &TChildClass::OnDisplayNameCommitted)
					.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
					.Font(IDetailLayoutBuilder::GetDetailFont());

				CategoryBuilder.AddCustomRow(TChildClass::MemberNameText)
					.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
					.NameContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.Text(TChildClass::MemberNameText)
					.ToolTipText(GetMemberNameDescriptionText())
					]
					.ValueContent()
					[
						NameEditableTextBox.ToSharedRef()
					];

				// TODO: Enable and use proper FText property editor
// 				CategoryBuilder.AddCustomRow(GetMemberDisplayNameText())
// 				.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
// 				.NameContent()
// 				[
// 					SNew(STextBlock)
// 					.Font(IDetailLayoutBuilder::GetDetailFontBold())
// 					.Text(GetMemberDisplayNameText())
// 					.ToolTipText(GetMemberDisplayNameDescriptionText())
// 				]
// 				.ValueContent()
// 				[
// 					DisplayNameEditableTextBox.ToSharedRef()
// 				];

				CategoryBuilder.AddCustomRow(MemberCustomizationStyle::NodeTooltipText)
				.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(MemberCustomizationStyle::NodeTooltipText)
				]
				.ValueContent()
				[
					SNew(SMultiLineEditableTextBox)
					.Text(this, &TChildClass::GetTooltip)
					.OnTextCommitted(this, &TChildClass::OnTooltipCommitted)
					.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
					.ModiferKeyForNewLine(EModifierKey::Shift)
					.RevertTextOnEscape(true)
					.WrapTextAt(MemberCustomizationStyle::DetailsTitleMaxWidth - MemberCustomizationStyle::DetailsTitleWrapPadding)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];

				DataTypeSelector->AddDataTypeSelector(InDetailLayout, MemberCustomizationStyle::DataTypeNameText, GraphMember, !bIsInterfaceMember && bIsGraphEditable);

				IDetailCategoryBuilder& DefaultCategoryBuilder = InDetailLayout.EditCategory("DefaultValue");
				TSharedPtr<IPropertyHandle> LiteralHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(TMemberType, Literal));
				if (ensure(GraphMember.IsValid()) && ensure(LiteralHandle.IsValid()))
				{
					// Always hide, even if no customization (LiteralObject isn't found) as this is the case
					// where the default object is not required (i.e. Default Member is default constructed)
					LiteralHandle->MarkHiddenByCustomization();

					UObject* LiteralObject = nullptr;
					if (LiteralHandle->GetValue(LiteralObject) == FPropertyAccess::Success)
					{
						if (UMetasoundEditorGraphMemberDefaultLiteral * MemberDefaultLiteral = Cast<UMetasoundEditorGraphMemberDefaultLiteral>(LiteralObject))
						{
							UpdateRenameDelegate(*MemberDefaultLiteral);

							IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
							LiteralCustomization = EditorModule.CreateMemberDefaultLiteralCustomization(*LiteralObject->GetClass(), DefaultCategoryBuilder);
							if (LiteralCustomization.IsValid())
							{
								LiteralCustomization->CustomizeLiteral(*MemberDefaultLiteral, InDetailLayout);
							}
							else if (LiteralObject)
							{
								IDetailPropertyRow* Row = DefaultCategoryBuilder.AddExternalObjectProperty(TArray<UObject*>({ LiteralObject }), "Default");
								UClass* LiteralClass = LiteralObject->GetClass();
								check(LiteralClass);
								ensureMsgf(Row, TEXT("Class '%s' missing expected 'Default' member."
									"Either add/rename default member or register customization to display default value/opt out appropriately."),
									*LiteralClass->GetName());
							}
						}
					}
				}
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

			FText GetDisplayName() const
			{
				using namespace Frontend;

				if (GraphMember.IsValid())
				{
					return GraphMember->GetDisplayName();
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

			void OnNameCommitted(const FText& InNewName, ETextCommit::Type InTextCommit)
			{
				using namespace Frontend;

				if (!bIsNameInvalid && GraphMember.IsValid())
				{
					const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphMember_Format", "Set MetaSound {0}'s Name"), TChildClass::MemberNameText);
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

		private:
			TUniquePtr<FMetasoundDefaultLiteralCustomizationBase> LiteralCustomization;

			FDelegateHandle RenameRequestedHandle;
		};

		class FMetasoundInputDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphInput, FMetasoundInputDetailCustomization>
		{
		public:
			FMetasoundInputDetailCustomization()
				: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphInput, FMetasoundInputDetailCustomization>()
			{
			}

			virtual ~FMetasoundInputDetailCustomization() = default;

			virtual bool IsInterfaceMember() const override;

			static const FText MemberNameText;

		private:
			TUniquePtr<FMetasoundDefaultLiteralCustomizationBase> LiteralCustomization;
		};

		class FMetasoundOutputDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphOutput, FMetasoundOutputDetailCustomization>
		{
		public:
			static const FText MemberNameText;

			FMetasoundOutputDetailCustomization()
				: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphOutput, FMetasoundOutputDetailCustomization>()
			{
			}

			virtual ~FMetasoundOutputDetailCustomization() = default;

			virtual bool IsInterfaceMember() const override;

		private:
			TUniquePtr<FMetasoundDefaultLiteralCustomizationBase> LiteralCustomization;
		};
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
