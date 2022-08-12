// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Styling/AppStyle.h"
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
#include "SMetasoundPinValueInspector.h"
#include "Styling/SlateStyleRegistry.h"
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
			TSharedPtr<SMetasoundPinValueInspector> PinInspector;

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
						.ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
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
							.Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))
						]
					];
			}

			virtual const FSlateBrush* GetPinIcon() const override
			{
				const bool bIsConnected = ParentPinType::IsConnected();
				EMetasoundFrontendVertexAccessType AccessType = EMetasoundFrontendVertexAccessType::Reference;

				if (const UEdGraphPin* Pin = ParentPinType::GetPinObj())
				{
					if (const UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(Pin->GetOwningNode()))
					{
						if (const UMetasoundEditorGraphMemberNode* MemberNode = Cast<UMetasoundEditorGraphMemberNode>(Node))
						{
							if (const UMetasoundEditorGraphVertex* Vertex = Cast<UMetasoundEditorGraphVertex>(MemberNode->GetMember()))
							{
								AccessType = Vertex->GetVertexAccessType();
							}
						}
						else if (const UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(Node))
						{
							if (Pin->Direction == EGPD_Input)
							{
								Frontend::FInputHandle InputHandle = FGraphBuilder::GetInputHandleFromPin(Pin);
								AccessType = InputHandle->GetVertexAccessType();
							}
							else if (Pin->Direction == EGPD_Output)
							{
								Frontend::FOutputHandle OutputHandle = FGraphBuilder::GetOutputHandleFromPin(Pin);
								AccessType = OutputHandle->GetVertexAccessType();
							}
						}
					}
				}

				// Is constructor pin 
				if (AccessType == EMetasoundFrontendVertexAccessType::Value)
				{
					if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
					{
						if (ParentPinType::IsArray())
						{
							return bIsConnected ? MetasoundStyle->GetBrush(TEXT("MetasoundEditor.Graph.ConstructorPinArray")) :
								MetasoundStyle->GetBrush(TEXT("MetasoundEditor.Graph.ConstructorPinArrayDisconnected"));
						}
						else
						{
							return bIsConnected ? MetasoundStyle->GetBrush(TEXT("MetasoundEditor.Graph.ConstructorPin")) :
								MetasoundStyle->GetBrush(TEXT("MetasoundEditor.Graph.ConstructorPinDisconnected"));
						}
					}
				}
				return SGraphPin::GetPinIcon();
			}

			virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
			{
				ParentPinType::CachedNodeOffset = FVector2D(AllottedGeometry.AbsolutePosition) / AllottedGeometry.Scale - ParentPinType::OwnerNodePtr.Pin()->GetUnscaledPosition();
				ParentPinType::CachedNodeOffset.Y += AllottedGeometry.Size.Y * 0.5f;

				UEdGraphPin* GraphPin = ParentPinType::GetPinObj();

				// Pause updates if menu is hosted (so user can capture state for ex. using the copy value action)
				TSharedPtr<FPinValueInspectorTooltip> InspectorTooltip = ParentPinType::ValueInspectorTooltip.Pin();
				if (InspectorTooltip.IsValid())
				{
					const bool bIsHoveringPin = ParentPinType::IsHovered();
					const bool bIsInspectingPin = bIsHoveringPin && FGraphBuilder::CanInspectPin(GraphPin);
					if (bIsInspectingPin)
					{
						if (PinInspector.IsValid())
						{
							PinInspector->UpdateMessage();
						}
					}
					else
					{
						if (InspectorTooltip->TooltipCanClose())
						{
							PinInspector.Reset();
							InspectorTooltip->TryDismissTooltip();
						}
					}
				}
				else
				{
					const bool bIsHoveringPin = ParentPinType::IsHovered();
					const bool bCanInspectPin = FGraphBuilder::CanInspectPin(GraphPin);
					if (bIsHoveringPin && bCanInspectPin)
					{
						const FEdGraphPinReference* CurrentRef = nullptr;
						if (FPinValueInspectorTooltip::ValueInspectorWidget.IsValid())
						{
							CurrentRef = &FPinValueInspectorTooltip::ValueInspectorWidget->GetPinRef();
						}

						// Only update if reference is not already set.  This avoids ping-pong between pins which can happen
						// when hovering connections as this state causes IsHovered to return true for all the associated pins
						// for the given connection.
						if (!CurrentRef || !CurrentRef->Get() || CurrentRef->Get() == GraphPin)
						{
							PinInspector = SNew(SMetasoundPinValueInspector);
							ParentPinType::ValueInspectorTooltip = FPinValueInspectorTooltip::SummonTooltip(GraphPin, PinInspector);
							InspectorTooltip = ParentPinType::ValueInspectorTooltip.Pin();
							if (InspectorTooltip.IsValid())
							{
								FVector2D TooltipLocation;
								ParentPinType::GetInteractiveTooltipLocation(TooltipLocation);
								InspectorTooltip->MoveTooltip(TooltipLocation);
							}
						}
					}
				}
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
