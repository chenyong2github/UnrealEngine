// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/Widget.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraph/EdGraphSchema.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
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
#include "STextPropertyEditableTextBox.h"
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
		class FGraphMemberEditableTextBase : public IEditableTextProperty
		{
		protected:
			TWeakObjectPtr<UMetasoundEditorGraphMember> GraphMember;
			FText ToolTip;

		public:
			FGraphMemberEditableTextBase(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, const FText& InToolTip)
				: GraphMember(InGraphMember)
				, ToolTip(InToolTip)
			{
			}

			virtual ~FGraphMemberEditableTextBase() = default;

			virtual bool IsMultiLineText() const override { return true; }
			virtual bool IsPassword() const override { return false; }
			virtual bool IsReadOnly() const override { return false; }
			virtual int32 GetNumTexts() const override { return 1; }
			virtual bool IsValidText(const FText& InText, FText& OutErrorMsg) const override { return true; }
			virtual void RequestRefresh() override { }

			virtual FText GetToolTipText() const override
			{
				return ToolTip;
			}

			virtual bool IsDefaultValue() const override
			{
				return GetText(0).EqualTo(FText::GetEmpty());
			}

#if USE_STABLE_LOCALIZATION_KEYS
			virtual void GetStableTextId(const int32 InIndex, const ETextPropertyEditAction InEditAction, const FString& InTextSource, const FString& InProposedNamespace, const FString& InProposedKey, FString& OutStableNamespace, FString& OutStableKey) const override
			{
				check(InIndex == 0);
				StaticStableTextId(GraphMember->GetPackage(), InEditAction, InTextSource, InProposedNamespace, InProposedKey, OutStableNamespace, OutStableKey);
			}
#endif // USE_STABLE_LOCALIZATION_KEYS

		};

		class FGraphMemberEditableTextDescription : public FGraphMemberEditableTextBase
		{
		public:
			FGraphMemberEditableTextDescription(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, const FText& InToolTip)
				: FGraphMemberEditableTextBase(InGraphMember, InToolTip)
			{
			}

			virtual ~FGraphMemberEditableTextDescription() = default;

			virtual FText GetText(const int32 InIndex) const override
			{
				check(InIndex == 0);

				if (GraphMember.IsValid())
				{
					return GraphMember->GetDescription();
				}

				return FText::GetEmpty();
			}

			virtual void SetText(const int32 InIndex, const FText& InText) override
			{
				check(InIndex == 0);

				if (GraphMember.IsValid())
				{
					GraphMember->SetDescription(InText, true);
				}
			}
		};

		class FGraphMemberEditableTextDisplayName : public FGraphMemberEditableTextBase
		{
		public:
			FGraphMemberEditableTextDisplayName(TWeakObjectPtr<UMetasoundEditorGraphMember> InGraphMember, const FText& InToolTip)
				: FGraphMemberEditableTextBase(InGraphMember, InToolTip)
			{
			}

			virtual ~FGraphMemberEditableTextDisplayName() = default;

			virtual FText GetText(const int32 InIndex) const override
			{
				check(InIndex == 0);

				if (GraphMember.IsValid())
				{
					if (const UMetasoundEditorGraphVertex* Vertex = Cast<UMetasoundEditorGraphVertex>(GraphMember.Get()))
					{
						return Vertex->GetConstNodeHandle()->GetDisplayName();
					}

					if (const UMetasoundEditorGraphVariable* Variable = Cast<UMetasoundEditorGraphVariable>(GraphMember.Get()))
					{
						return Variable->GetConstVariableHandle()->GetDisplayName();
					}

					return GraphMember->GetDisplayName();
				}

				return FText::GetEmpty();
			}

			virtual void SetText(const int32 InIndex, const FText& InText) override
			{
				check(InIndex == 0);

				if (GraphMember.IsValid())
				{
					GraphMember->SetDisplayName(InText, true);
				}
			}
		};

		// TODO: Move to actual style
		namespace MemberCustomizationStyle
		{
			/** Maximum size of the details title panel */
			static constexpr float DetailsTitleMaxWidth = 300.f;
			/** magic number retrieved from SGraphNodeComment::GetWrapAt() */
			static constexpr float DetailsTitleWrapPadding = 32.0f;

			static const FText DataTypeNameText = LOCTEXT("Node_DataTypeName", "Type");
			static const FText DefaultPropertyText = LOCTEXT("Node_DefaultPropertyName", "Default Value");
		} // namespace MemberCustomizationStyle

		class FMetasoundFloatLiteralCustomization : public FMetasoundDefaultLiteralCustomizationBase
		{
			TWeakObjectPtr<UMetasoundEditorGraphMemberDefaultFloat> FloatLiteral;

			// Delegate for clamping the input value or not
			FDelegateHandle OnClampChangedDelegateHandle;

		public:
			FMetasoundFloatLiteralCustomization(IDetailCategoryBuilder& InDefaultCategoryBuilder)
				: FMetasoundDefaultLiteralCustomizationBase(InDefaultCategoryBuilder)
			{
			}
			virtual ~FMetasoundFloatLiteralCustomization();

			virtual TArray<IDetailPropertyRow*> CustomizeLiteral(UMetasoundEditorGraphMemberDefaultLiteral& InLiteral, IDetailLayoutBuilder& InDetailLayout) override;
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

			virtual TArray<IDetailPropertyRow*> CustomizeLiteral(UMetasoundEditorGraphMemberDefaultLiteral& InLiteral, IDetailLayoutBuilder& InDetailLayout) override;
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

		template <typename TMemberType>
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
			TSharedPtr<FMetasoundDataTypeSelector> DataTypeSelector;

			bool bIsNameInvalid = false;

			static IDetailCategoryBuilder& GetDefaultCategoryBuilder(IDetailLayoutBuilder& InDetailLayout)
			{
				return InDetailLayout.EditCategory("DefaultValue");
			}

			void UpdateRenameDelegate(UMetasoundEditorGraphMemberDefaultLiteral& InMemberDefaultLiteral)
			{
				if (UMetasoundEditorGraphMember* Member = InMemberDefaultLiteral.GetParentMember())
				{
					if (Member->CanRename())
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

			void CacheMemberData(IDetailLayoutBuilder& InDetailLayout)
			{
				TArray<TWeakObjectPtr<UObject>> Objects;
				InDetailLayout.GetObjectsBeingCustomized(Objects);
				if (!Objects.IsEmpty())
				{
					GraphMember = Cast<TMemberType>(Objects.Last().Get());

					TSharedPtr<IPropertyHandle> LiteralHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(TMemberType, Literal));
					if (ensure(GraphMember.IsValid()) && ensure(LiteralHandle.IsValid()))
					{
						// Always hide, even if no customization (LiteralObject isn't found) as this is the case
						// where the default object is not required (i.e. Default Member is default constructed)
						LiteralHandle->MarkHiddenByCustomization();

						UObject* LiteralObject = nullptr;
						if (LiteralHandle->GetValue(LiteralObject) == FPropertyAccess::Success)
						{
							MemberDefaultLiteral = Cast<UMetasoundEditorGraphMemberDefaultLiteral>(LiteralObject);
						}
					}
				}
			}

			void CustomizeGeneralCategory(IDetailLayoutBuilder& InDetailLayout)
			{
				IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory("General");

				const bool bIsInterfaceMember = IsInterfaceMember();
				const bool bIsGraphEditable = IsGraphEditable();

				NameEditableTextBox = SNew(SEditableTextBox)
					.Text(this, &TMetasoundGraphMemberDetailCustomization::GetName)
					.OnTextChanged(this, &TMetasoundGraphMemberDetailCustomization::OnNameChanged)
					.OnTextCommitted(this, &TMetasoundGraphMemberDetailCustomization::OnNameCommitted)
					.IsReadOnly(bIsInterfaceMember || !bIsGraphEditable)
					.SelectAllTextWhenFocused(true)
					.Font(IDetailLayoutBuilder::GetDetailFont());

				static const FText MemberNameToolTipFormat = LOCTEXT("GraphMember_NameDescriptionFormat", "Name used within the MetaSounds editor(s) and transacting systems (ex. Blueprints) if applicable to reference the given {0}.");
				CategoryBuilder.AddCustomRow(LOCTEXT("GraphMember_NameProperty", "Name"))
					.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
					.NameContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
						.Text(GraphMember->GetGraphMemberLabel())
						.ToolTipText(FText::Format(MemberNameToolTipFormat, GraphMember->GetGraphMemberLabel()))
					]
					.ValueContent()
					[
						NameEditableTextBox.ToSharedRef()
					];

				static const FText MemberDisplayNameText = LOCTEXT("GraphMember_DisplayNameProperty", "Display Name");
				static const FText MemberDisplayNameToolTipFormat = LOCTEXT("GraphMember_DisplayNameDescriptionFormat", "Optional, localized name used within the MetaSounds editor(s) to describe the given {0}.");
				const FText MemberDisplayNameTooltipText = FText::Format(MemberDisplayNameToolTipFormat, GraphMember->GetGraphMemberLabel());
				CategoryBuilder.AddCustomRow(MemberDisplayNameText)
					.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
					.NameContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
						.Text(MemberDisplayNameText)
						.ToolTipText(MemberDisplayNameTooltipText)
					]
					.ValueContent()
					[
						SNew(STextPropertyEditableTextBox, MakeShared<FGraphMemberEditableTextDisplayName>(GraphMember, MemberDisplayNameTooltipText))
						.WrapTextAt(500)
						.MinDesiredWidth(25.0f)
						.MaxDesiredHeight(200)
					];

				static const FText MemberDescriptionText = LOCTEXT("Member_DescriptionPropertyName", "Description");
				static const FText MemberDescriptionToolTipFormat = LOCTEXT("Member_DescriptionToolTipFormat", "Description for {0}. For example, used as a tooltip when displayed on another graph's referencing node.");
				const FText MemberDescriptionToolTipText = FText::Format(MemberDescriptionToolTipFormat, GraphMember->GetGraphMemberLabel());
				CategoryBuilder.AddCustomRow(MemberDescriptionText)
					.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
					.NameContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
						.Text(MemberDescriptionText)
						.ToolTipText(MemberDescriptionToolTipText)
					]
					.ValueContent()
					[
						SNew(STextPropertyEditableTextBox, MakeShared<FGraphMemberEditableTextDescription>(GraphMember, MemberDescriptionToolTipText))
						.WrapTextAt(500)
						.MinDesiredWidth(25.0f)
						.MaxDesiredHeight(200)
					];

				DataTypeSelector->AddDataTypeSelector(InDetailLayout, MemberCustomizationStyle::DataTypeNameText, GraphMember, !bIsInterfaceMember && bIsGraphEditable);
			}

			TArray<IDetailPropertyRow*> CustomizeDefaultCategory(IDetailLayoutBuilder& InDetailLayout)
			{
				TArray<IDetailPropertyRow*> DefaultPropertyRows;

				if (MemberDefaultLiteral.IsValid())
				{
					UpdateRenameDelegate(*MemberDefaultLiteral);

					UClass* MemberClass = MemberDefaultLiteral->GetClass();
					check(MemberClass);

					IDetailCategoryBuilder& DefaultCategoryBuilder = GetDefaultCategoryBuilder(InDetailLayout);
					IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
					TUniquePtr<FMetasoundDefaultLiteralCustomizationBase> LiteralCustomization = EditorModule.CreateMemberDefaultLiteralCustomization(*MemberClass, DefaultCategoryBuilder);
					if (LiteralCustomization.IsValid())
					{
						DefaultPropertyRows = LiteralCustomization->CustomizeLiteral(*MemberDefaultLiteral, InDetailLayout);
					}
					else
					{
						IDetailPropertyRow* DefaultPropertyRow = DefaultCategoryBuilder.AddExternalObjectProperty(TArray<UObject*>({ MemberDefaultLiteral.Get() }), "Default");
						ensureMsgf(DefaultPropertyRow, TEXT("Class '%s' missing expected 'Default' member."
							"Either add/rename default member or register customization to display default value/opt out appropriately."),
							*MemberClass->GetName());
						DefaultPropertyRows.Add(DefaultPropertyRow);
					}
				}

				return DefaultPropertyRows;
			}

			virtual bool IsDefaultEditable() const
			{
				return true;
			}

			virtual bool IsInterfaceMember() const
			{
				return false;
			}

			// IDetailCustomization interface
			virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override
			{
				CacheMemberData(InDetailLayout);
				if (GraphMember.IsValid())
				{
					CustomizeGeneralCategory(InDetailLayout);
					CustomizeDefaultCategory(InDetailLayout);
				}
			}
			// End of IDetailCustomization interface

			void OnNameChanged(const FText& InNewName)
			{
				using namespace Frontend;

				bIsNameInvalid = false;
				NameEditableTextBox->SetError(FText::GetEmpty());

				if (!ensure(GraphMember.IsValid()))
				{
					return;
				}

				FText Error;
				if (!GraphMember->CanRename(InNewName, Error))
				{
					bIsNameInvalid = true;
					NameEditableTextBox->SetError(Error);
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
					const FText TransactionLabel = FText::Format(LOCTEXT("RenameGraphMember_Format", "Set MetaSound {0}'s Name"), GraphMember->GetGraphMemberLabel());
					const FScopedTransaction Transaction(TransactionLabel);

					constexpr bool bPostTransaction = false;
					GraphMember->SetDisplayName(FText::GetEmpty(), bPostTransaction);
					GraphMember->SetMemberName(*InNewName.ToString(), bPostTransaction);
				}

				NameEditableTextBox->SetError(FText::GetEmpty());
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

		protected:
			TWeakObjectPtr<UMetasoundEditorGraphMemberDefaultLiteral> MemberDefaultLiteral;

			FDelegateHandle RenameRequestedHandle;
		};

		class FMetasoundInputDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphInput>
		{
		public:
			FMetasoundInputDetailCustomization()
				: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphInput>()
			{
			}

			virtual ~FMetasoundInputDetailCustomization() = default;

			virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override;
			virtual bool IsDefaultEditable() const override;
			virtual bool IsInterfaceMember() const override;
			static const FText MemberNameText;

		private:
			Frontend::FDocumentHandle GetDocumentHandle() const;
			bool GetInputInheritsDefault() const;
			void SetInputInheritsDefault();
			void ClearInputInheritsDefault();
		};

		class FMetasoundOutputDetailCustomization : public TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphOutput>
		{
		public:
			static const FText MemberNameText;

			FMetasoundOutputDetailCustomization()
				: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphOutput>()
			{
			}

			virtual ~FMetasoundOutputDetailCustomization() = default;

			virtual bool IsInterfaceMember() const override;
		};
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
