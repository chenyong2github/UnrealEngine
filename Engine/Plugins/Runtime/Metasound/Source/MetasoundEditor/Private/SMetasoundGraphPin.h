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
#include "MetasoundEditorGraphNode.h"
#include "MetasoundFrontendController.h"
#include "SGraphPin.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
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

			virtual TSharedRef<SWidget> GetDefaultValueWidget() override
			{
				using namespace Frontend;
				TSharedRef<SWidget> DefaultWidget = ParentPinType::GetDefaultValueWidget();

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
									if (Literal->GetType() != EMetasoundFrontendLiteralType::None)
									{
										return EVisibility::Visible;
									}
								}
							}

							return EVisibility::Hidden;
						}))
						.OnClicked(FOnClicked::CreateLambda([this]()
						{
							using namespace Frontend;

							if (UEdGraphPin* Pin = ParentPinType::GetPinObj())
							{
								if (UEdGraphNode* Node = Pin->GetOwningNode())
								{
									if (UMetasoundEditorGraph* MetaSoundGraph = CastChecked<UMetasoundEditorGraph>(Node->GetGraph()))
									{
										UObject& MetaSound = MetaSoundGraph->GetMetasoundChecked();

										{
											const FScopedTransaction Transaction(LOCTEXT("MetaSoundEditorSetLiteralToClassDefault", "Set Literal to Class Default"));
											MetaSound.Modify();
											MetaSoundGraph->Modify();

											FInputHandle InputHandle = GetInputHandle();
											InputHandle->ClearLiteral();
										}

										FGraphBuilder::SynchronizePinLiteral(*Pin);
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
