// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EditorStyleSet.h"
#include "KismetPins/SGraphPinBool.h"
#include "KismetPins/SGraphPinNum.h"
#include "KismetPins/SGraphPinInteger.h"
#include "KismetPins/SGraphPinObject.h"
#include "KismetPins/SGraphPinString.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphMemberDefaults.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontendController.h"
#include "SGraphPin.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"


namespace Metasound
{
	namespace Editor
	{
		template <typename ParentPinType>
		class METASOUNDEDITOR_API TMetasoundGraphPin : public ParentPinType
		{
		public:
			SLATE_BEGIN_ARGS(TMetasoundGraphPin<ParentPinType>)
			{
			}
			SLATE_END_ARGS()

			Frontend::FConstInputHandle GetConstInputHandle() const
			{
				using namespace Frontend;

				const bool bIsInput = (ParentPinType::GetDirection() == EGPD_Input);
				if (bIsInput)
				{
					if (const UEdGraphPin* Pin = ParentPinType::GetPinObj())
					{
						if (const UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(Pin->GetOwningNode()))
						{
							FConstNodeHandle NodeHandle = MetasoundEditorNode->GetConstNodeHandle();
							return NodeHandle->GetConstInputWithVertexName(Pin->GetFName());
						}
					}
				}

				return IInputController::GetInvalidHandle();
			}

			Frontend::FInputHandle GetInputHandle()
			{
				using namespace Frontend;

				const bool bIsInput = (ParentPinType::GetDirection() == EGPD_Input);
				if (bIsInput)
				{
					if (UEdGraphPin* Pin = ParentPinType::GetPinObj())
					{
						if (UMetasoundEditorGraphNode* MetasoundEditorNode = Cast<UMetasoundEditorGraphNode>(Pin->GetOwningNode()))
						{
							FNodeHandle NodeHandle = MetasoundEditorNode->GetNodeHandle();
							return NodeHandle->GetInputWithVertexName(Pin->GetFName());
						}
					}
				}

				return IInputController::GetInvalidHandle();
			}

			bool ShowDefaultValueWidget() const
			{
				UEdGraphPin* Pin = ParentPinType::GetPinObj();
				if (!Pin)
				{
					return true;
				}

				UMetasoundEditorGraphMemberNode* Node = Cast<UMetasoundEditorGraphMemberNode>(Pin->GetOwningNode());
				if (!Node)
				{
					return true;
				}

				UMetasoundEditorGraphMember* Member = Node->GetMember();
				if (!Member)
				{
					return true;
				}

				UMetasoundEditorGraphMemberDefaultFloat* DefaultFloat = Cast<UMetasoundEditorGraphMemberDefaultFloat>(Member->GetLiteral());
				if (!DefaultFloat)
				{
					return true;
				}

				return DefaultFloat->WidgetType == EMetasoundMemberDefaultWidget::None;
			}

			virtual TSharedRef<SWidget> GetDefaultValueWidget() override
			{
				using namespace Frontend;
				TSharedRef<SWidget> DefaultWidget = ParentPinType::GetDefaultValueWidget();

				if (!ShowDefaultValueWidget())
				{
					return SNullWidget::NullWidget;
				}

				// For now, arrays do not support literals.
				// TODO: Support array literals by displaying
				// default literals (non-array too) in inspector window.
				FConstInputHandle InputHandle = GetConstInputHandle();
				if (!InputHandle->IsValid() || ParentPinType::IsArray())
				{
					return DefaultWidget;
				}

				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						DefaultWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("ResetToClassDefaultToolTip", "Reset to class default"))
						.ButtonStyle(FEditorStyle::Get(), TEXT("NoBorder"))
						.ContentPadding(0.0f)
						.Visibility(TAttribute<EVisibility>::Create([this]
						{
							using namespace Frontend;
							if (!ParentPinType::IsConnected())
							{
								FConstInputHandle InputHandle = GetConstInputHandle();
								if (const FMetasoundFrontendLiteral* Literal = InputHandle->GetLiteral())
								{
									const bool bIsDefaultConstructed = Literal->GetType() == EMetasoundFrontendLiteralType::None;
									const bool bIsTriggerDataType = InputHandle->GetDataType() == GetMetasoundDataTypeName<FTrigger>();
									if (!bIsDefaultConstructed && !bIsTriggerDataType)
									{
										return EVisibility::Visible;
									}
								}
							}

							return EVisibility::Collapsed;
						}))
						.OnClicked(FOnClicked::CreateLambda([this]()
						{
							using namespace Frontend;

							if (UEdGraphPin* Pin = ParentPinType::GetPinObj())
							{
								if (UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(Pin->GetOwningNode()))
								{
									if (UMetasoundEditorGraph* MetaSoundGraph = CastChecked<UMetasoundEditorGraph>(Node->GetGraph()))
									{
										UObject& MetaSound = MetaSoundGraph->GetMetasoundChecked();

										{
											const FScopedTransaction Transaction(LOCTEXT("MetaSoundEditorResetToClassDefault", "Reset to Class Default"));
											MetaSound.Modify();
											MetaSoundGraph->Modify();

											if (UMetasoundEditorGraphMemberNode* MemberNode = Cast<UMetasoundEditorGraphMemberNode>(Node))
											{
												UMetasoundEditorGraphMember* Member = MemberNode->GetMember();
												if (ensure(Member))
												{
													Member->ResetToClassDefault();
												}
											}
											else
											{
												FInputHandle InputHandle = GetInputHandle();
												InputHandle->ClearLiteral();
											}
										}

										// Full node synchronization required as custom
										// node-level widgets may need to be refreshed
										MetaSoundGraph->SetSynchronizationRequired();
									}
								}
							}

							return FReply::Handled();
						}))
						.Content()
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						]
					];
			}
		};

		class SMetasoundGraphPin : public TMetasoundGraphPin<SGraphPin>
		{
		public:
			void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
			{
				SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
			}
		};

		class SMetasoundGraphPinBool : public TMetasoundGraphPin<SGraphPinBool>
		{
		public:
			void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
			{
				SGraphPinBool::Construct(SGraphPinBool::FArguments(), InGraphPinObj);
			}
		};

		class SMetasoundGraphPinFloat : public TMetasoundGraphPin<SGraphPinNum<float>>
		{
		public:
			void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
			{
				SGraphPinNum<float>::Construct(SGraphPinNum<float>::FArguments(), InGraphPinObj);
			}
		};

		class SMetasoundGraphPinInteger : public TMetasoundGraphPin<SGraphPinInteger>
		{
		public:
			void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
			{
				SGraphPinInteger::Construct(SGraphPinInteger::FArguments(), InGraphPinObj);
			}
		};

		class SMetasoundGraphPinObject : public TMetasoundGraphPin<SGraphPinObject>
		{
		public:
			void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
			{
				SGraphPinObject::Construct(SGraphPinObject::FArguments(), InGraphPinObj);
			}
		};

		class SMetasoundGraphPinString : public TMetasoundGraphPin<SGraphPinString>
		{
		public:
			void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
			{
				SGraphPinString::Construct(SGraphPinString::FArguments(), InGraphPinObj);
			}
		};
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
