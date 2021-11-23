// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundVariableDetailCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Internationalization/Text.h"
#include "MetasoundNodeDetailCustomization.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		namespace MemberCustomizationPrivate
		{
			static const FText VariableNameText = LOCTEXT("Variable_Name", "Variable Name");
			static const FText VariableDisplayNameText = LOCTEXT("VariableDisplay_Name", "Variable Display Name");
		}

		FMetasoundVariableDetailCustomization::FMetasoundVariableDetailCustomization()
			: TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphVariable>(LOCTEXT("VariableGraphMemberLabel", "Variable"))
		{
		}

		void FMetasoundVariableDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Frontend;

			TMetasoundGraphMemberDetailCustomization<UMetasoundEditorGraphVariable>::CustomizeDetails(DetailLayout);

			if (!GraphMember.IsValid())
			{
				return;
			}

			IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory("General");

			const bool bIsGraphEditable = IsGraphEditable();
			NameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundVariableDetailCustomization::GetName)
				.OnTextChanged(this, &FMetasoundVariableDetailCustomization::OnNameChanged)
				.OnTextCommitted(this, &FMetasoundVariableDetailCustomization::OnNameCommitted)
				.IsReadOnly(!bIsGraphEditable)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			DisplayNameEditableTextBox = SNew(SEditableTextBox)
				.Text(this, &FMetasoundVariableDetailCustomization::GetDisplayName)
				.OnTextCommitted(this, &FMetasoundVariableDetailCustomization::OnDisplayNameCommitted)
				.IsReadOnly(!bIsGraphEditable)
				.Font(IDetailLayoutBuilder::GetDetailFont());

			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::VariableNameText)
			.EditCondition(bIsGraphEditable, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(MemberCustomizationPrivate::VariableNameText)
				.ToolTipText(LOCTEXT("VariableName_Description", "Name used by external systems to identify input. Used as DisplayName within MetaSound Graph Editor if no DisplayName is provided."))
			]
			.ValueContent()
			[
				NameEditableTextBox.ToSharedRef()
			];

			// TODO: Enable and use proper FText property editor
// 			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::VariableDisplayNameText)
// 			.EditCondition(!bIsInterfaceMember && bIsGraphEditable, nullptr)
// 			.NameContent()
// 			[
// 				SNew(STextBlock)
// 				.Font(IDetailLayoutBuilder::GetDetailFontBold())
// 				.Text(MemberCustomizationPrivate::VariableDisplayNameText)
// 				.ToolTipText(LOCTEXT("VariableDisplayName_Description", "Optional, localized name used within the MetaSounds editor(s) to describe the given input."))
// 			]
// 			.ValueContent()
// 			[
// 				DisplayNameEditableTextBox.ToSharedRef()
// 			];

			CategoryBuilder.AddCustomRow(MemberCustomizationPrivate::NodeTooltipText)
			.EditCondition(bIsGraphEditable, nullptr)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.Text(MemberCustomizationPrivate::NodeTooltipText)
			]
			.ValueContent()
			[
				SNew(SMultiLineEditableTextBox)
				.Text(this, &FMetasoundVariableDetailCustomization::GetTooltip)
				.OnTextCommitted(this, &FMetasoundVariableDetailCustomization::OnTooltipCommitted)
				.IsReadOnly(!bIsGraphEditable)
				.ModiferKeyForNewLine(EModifierKey::Shift)
				.RevertTextOnEscape(true)
				.WrapTextAt(MemberCustomizationPrivate::DetailsTitleMaxWidth - MemberCustomizationPrivate::DetailsTitleWrapPadding)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];

			DataTypeSelector->AddDataTypeSelector(DetailLayout, MemberCustomizationPrivate::DataTypeNameText, GraphMember, bIsGraphEditable);

			IDetailCategoryBuilder& DefaultCategoryBuilder = DetailLayout.EditCategory("DefaultValue");
			TSharedPtr<IPropertyHandle> LiteralHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMetasoundEditorGraphVariable, Literal));
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

								FSimpleDelegate UpdateDocumentVariable = FSimpleDelegate::CreateLambda([GraphMember = this->GraphMember]()
								{
									if (GraphMember.IsValid())
									{
										GraphMember->UpdateDocumentVariable();
									}
								});

								DefaultValueHandle->SetOnPropertyValueChanged(UpdateDocumentVariable);
								DefaultValueHandle->SetOnChildPropertyValueChanged(UpdateDocumentVariable);

								TSharedPtr<IPropertyHandleArray> DefaultValueArray = DefaultValueHandle->AsArray();
								if (DefaultValueArray.IsValid())
								{
									DefaultValueArray->SetOnNumElementsChanged(UpdateDocumentVariable);
								}
							}
						}

						IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
						LiteralCustomization = EditorModule.CreateInputLiteralCustomization(*LiteralObject->GetClass(), DefaultCategoryBuilder);
						if (LiteralCustomization.IsValid())
						{
							LiteralCustomization->CustomizeLiteral(*CastChecked<UMetasoundEditorGraphInputLiteral>(LiteralObject), DefaultValueHandle);
						}
					}
					else
					{
						DefaultCategoryBuilder.AddProperty(LiteralHandle);
					}
				}
			}
		}

		void FMetasoundVariableDetailCustomization::SetDefaultPropertyMetaData(TSharedRef<IPropertyHandle> InDefaultPropertyHandle) const
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

			const FName TypeName = GraphMember->TypeName;
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
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
