// Copyright Epic Games, Inc. All Rights Reserved.
#include "SMetasoundGraphNode.h"

#include "Audio/AudioParameterInterface.h"
#include "Components/AudioComponent.h"
#include "EditorStyleSet.h"
#include "GraphEditorSettings.h"
#include "IDocumentation.h"
#include "KismetPins/SGraphPinBool.h"
#include "KismetPins/SGraphPinExec.h"
#include "KismetPins/SGraphPinInteger.h"
#include "KismetPins/SGraphPinNum.h"
#include "KismetPins/SGraphPinObject.h"
#include "KismetPins/SGraphPinString.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundTrigger.h"
#include "NodeFactory.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "SCommentBubble.h"
#include "SGraphNode.h"
#include "SGraphPin.h"
#include "SLevelOfDetailBranchNode.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateColor.h"
#include "TutorialMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWidget.h"
#include "SGraphPinComboBox.h"
#include "SMetasoundEnumPin.h"
#include "UObject/ScriptInterface.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphNode"


// namespace MetaSound
// {
// 	namespace Editor
// 	{
		class SMetaSoundGraphPinTrigger : public SGraphPin
		{
			bool bImagesCached = false;

		public:
			SLATE_BEGIN_ARGS(SGraphPin)
			{
			}

			SLATE_END_ARGS()

			void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
			{
				SGraphPin::Construct(SGraphPin::FArguments(), InPin);

				if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
				{
					CachedImg_Pin_Connected = MetasoundStyle->GetBrush(TEXT("MetasoundEditor.Graph.TriggerPin.Connected"));
					CachedImg_Pin_Disconnected = MetasoundStyle->GetBrush(TEXT("MetasoundEditor.Graph.TriggerPin.Disconnected"));
				}
			}
		};
// 	} // namespace Editor
// } // namespace MetaSound

void SMetasoundGraphNode::Construct(const FArguments& InArgs, class UEdGraphNode* InNode)
{
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

void SMetasoundGraphNode::ExecuteInputTrigger(UMetasoundEditorGraphInputLiteral& Literal)
{
	UMetasoundEditorGraphInput* Input = Cast<UMetasoundEditorGraphInput>(Literal.GetOuter());
	if (!ensure(Input))
	{
		return;
	}

	// If modifying graph not currently being previewed, do not forward request
	if (UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(Input->GetOuter()))
	{
		if (!Graph->IsPreviewing())
		{
			return;
		}
	}

	if (UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent())
	{
		if (TScriptInterface<IAudioParameterInterface> ParamInterface = PreviewComponent->GetParameterInterface())
		{
			// TODO: fix how identifying the parameter to update is determined. It should not be done
			// with a "DisplayName" but rather the vertex Guid.
			Metasound::Frontend::FConstNodeHandle NodeHandle = Input->GetConstNodeHandle();
			Metasound::FVertexKey VertexKey = Metasound::FVertexKey(NodeHandle->GetDisplayName().ToString());
			ParamInterface->Trigger(*VertexKey);
		}
	}
}

TSharedRef<SWidget> SMetasoundGraphNode::CreateTriggerSimulationWidget(UMetasoundEditorGraphInputLiteral& InputLiteral)
{
	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	.Padding(2.0f, 0.0f, 0.0f, 0.0f)
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Center)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.OnClicked_Lambda([LiteralPtr = TWeakObjectPtr<UMetasoundEditorGraphInputLiteral>(&InputLiteral)]()
		{
			if (LiteralPtr.IsValid())
			{
				ExecuteInputTrigger(*LiteralPtr.Get());
			}
			return FReply::Handled();
		})
		.ToolTipText(LOCTEXT("TriggerTestToolTip", "Executes trigger if currently previewing MetaSound."))
		.ForegroundColor(FSlateColor::UseForeground())
		.ContentPadding(0)
		.IsFocusable(false)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.CircleArrowDown"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
	];
}

void SMetasoundGraphNode::CreateOutputSideAddButton(TSharedPtr<SVerticalBox> OutputBox)
{
	TSharedRef<SWidget> AddPinButton = AddPinButtonContent(
		LOCTEXT("MetasoundGraphNode_AddPinButton", "Add input"),
		LOCTEXT("MetasoundGraphNode_AddPinButton_Tooltip", "Add an input to the parent Metasound node.")
	);

	FMargin AddPinPadding = Settings->GetOutputPinPadding();
	AddPinPadding.Top += 6.0f;

	OutputBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Center)
	.Padding(AddPinPadding)
	[
		AddPinButton
	];
}

UMetasoundEditorGraphNode& SMetasoundGraphNode::GetMetasoundNode()
{
	return *CastChecked<UMetasoundEditorGraphNode>(GraphNode);
}

const UMetasoundEditorGraphNode& SMetasoundGraphNode::GetMetasoundNode() const
{
	check(GraphNode);
	return *Cast<UMetasoundEditorGraphNode>(GraphNode);
}

TSharedPtr<SGraphPin> SMetasoundGraphNode::CreatePinWidget(UEdGraphPin* InPin) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (const UMetasoundEditorGraphSchema* GraphSchema = Cast<const UMetasoundEditorGraphSchema>(InPin->GetSchema()))
	{
		// Don't show default value field for container types
		if (InPin->PinType.ContainerType != EPinContainerType::None)
		{
			return SNew(SGraphPin, InPin);
		}

		if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryAudio)
		{
			return SNew(SGraphPin, InPin);
		}

		if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryBoolean)
		{
			return SNew(SGraphPinBool, InPin);
		}

		//if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryDouble)
		//{
		//	return SNew(SGraphPinNum<double>, InPin);
		//}

		if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryFloat)
		{
			return SNew(SGraphPinNum<float>, InPin);
		}

		if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryInt32)
		{
			if (SMetasoundEnumPin::FindEnumInterfaceFromPin(InPin))
			{
				return SNew(SMetasoundEnumPin, InPin);
			}
			return SNew(SGraphPinInteger, InPin);
		}

		//if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryInt64)
		//{
		//	return SNew(SGraphPinNum<int64>, InPin);
		//}

		if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryObject)
		{
			return SNew(SGraphPinObject, InPin);
		}

		if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryString)
		{
			return SNew(SGraphPinString, InPin);
		}

		if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryTrigger)
		{
			return SNew(SMetaSoundGraphPinTrigger, InPin);
		}
	}

	return SNew(SGraphPin, InPin);
}

void SMetasoundGraphNode::CreateStandardPinWidget(UEdGraphPin* InPin)
{
	const bool bShowPin = ShouldPinBeHidden(InPin);
	if (bShowPin)
	{
		TSharedPtr<SGraphPin> NewPin = CreatePinWidget(InPin);
		check(NewPin.IsValid());

		Metasound::Frontend::FNodeHandle NodeHandle = GetMetasoundNode().GetNodeHandle();
		if (InPin->Direction == EGPD_Input)
		{
			if (!NodeHandle->GetClassStyle().Display.bShowInputNames)
			{
				NewPin->SetShowLabel(false);
			}
		}
		else if (InPin->Direction == EGPD_Output)
		{
			if (!NodeHandle->GetClassStyle().Display.bShowOutputNames)
			{
				NewPin->SetShowLabel(false);
			}
		}

		AddPin(NewPin.ToSharedRef());
	}
}

TSharedRef<SWidget> SMetasoundGraphNode::CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle)
{
	Metasound::Frontend::FNodeHandle NodeHandle = GetMetasoundNode().GetNodeHandle();
	if (!NodeHandle->GetClassStyle().Display.bShowName)
	{
		return SNullWidget::NullWidget;
	}

	return SGraphNode::CreateTitleWidget(NodeTitle);
}

void SMetasoundGraphNode::SetDefaultTitleAreaWidget(TSharedRef<SOverlay> DefaultTitleAreaWidget)
{
	SGraphNode::SetDefaultTitleAreaWidget(DefaultTitleAreaWidget);

	Metasound::Frontend::FNodeHandle NodeHandle = GetMetasoundNode().GetNodeHandle();
	if (NodeHandle->GetClassStyle().Display.bShowName)
	{
		DefaultTitleAreaWidget->ClearChildren();
		TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);

		DefaultTitleAreaWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							CreateTitleWidget(NodeTitle)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							NodeTitle.ToSharedRef()
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			.AutoWidth()
			[
				CreateTitleRightWidget()
			]
		];

		DefaultTitleAreaWidget->AddSlot()
		.VAlign(VAlign_Top)
		[
			SNew(SBorder)
			.Visibility(EVisibility::HitTestInvisible)
			.BorderImage( FEditorStyle::GetBrush( "Graph.Node.TitleHighlight" ) )
			.BorderBackgroundColor( this, &SGraphNode::GetNodeTitleIconColor )
			[
				SNew(SSpacer)
				.Size(FVector2D(20,20))
			]
		];
	}
	else
	{
		DefaultTitleAreaWidget->SetVisibility(EVisibility::Collapsed);
	}
}

void SMetasoundGraphNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter);

	GetMetasoundNode().UpdatePosition();
}

const FSlateBrush* SMetasoundGraphNode::GetNodeBodyBrush() const
{
// 	if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
// 	{
// 		if (GetMetasoundNode().GetNodeHandle()->GetClassType() == EMetasoundFrontendClassType::Input)
// 		{
// 			if (const FSlateBrush* InputBrush = MetasoundStyle->GetBrush("MetasoundEditor.Graph.Node.Body.Input"))
// 			{
// 				return InputBrush;
// 			}
// 		}
// 
// 		if (const FSlateBrush* DefaultBrush = MetasoundStyle->GetBrush("MetasoundEditor.Graph.Node.Body.Default"))
// 		{
// 			return DefaultBrush;
// 		}
// 	}

	return FEditorStyle::GetBrush("Graph.Node.Body");
}

EVisibility SMetasoundGraphNode::IsAddPinButtonVisible() const
{
	EVisibility DefaultVisibility = SGraphNode::IsAddPinButtonVisible();
	if (DefaultVisibility == EVisibility::Visible)
	{
		if (!GetMetasoundNode().CanAddInputPin())
		{
			return EVisibility::Collapsed;
		}
	}

	return DefaultVisibility;
}

FReply SMetasoundGraphNode::OnAddPin()
{
	GetMetasoundNode().CreateInputPin();

	return FReply::Handled();
}

FName SMetasoundGraphNode::GetLiteralDataType() const
{
	using namespace Metasound::Frontend;

	FName TypeName;

	// Just take last type.  If more than one, all types are the same.
	const UMetasoundEditorGraphNode& Node = GetMetasoundNode();
	Node.GetNodeHandle()->IterateConstOutputs([InTypeName = &TypeName](FConstOutputHandle OutputHandle)
	{
		*InTypeName = OutputHandle->GetDataType();
	});

	return TypeName;
}

TSharedRef<SWidget> SMetasoundGraphNode::CreateTitleRightWidget()
{
	const FName TypeName = GetLiteralDataType();
	if (TypeName == Metasound::GetMetasoundDataTypeName<Metasound::FTrigger>())
	{
		if (UMetasoundEditorGraphInputNode* Node = Cast<UMetasoundEditorGraphInputNode>(&GetMetasoundNode()))
		{
			if (UMetasoundEditorGraphInput* Input = Node->Input)
			{
				if (Input->Literal)
				{
					return CreateTriggerSimulationWidget(*Input->Literal);
				}
			}
		}
	}

	return SGraphNode::CreateTitleRightWidget();
}

TSharedRef<SWidget> SMetasoundGraphNode::CreateNodeContentArea()
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	FNodeHandle NodeHandle = GetMetasoundNode().GetNodeHandle();
	const FMetasoundFrontendClassStyleDisplay& StyleDisplay = NodeHandle->GetClassStyle().Display;

	TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox);

	if (StyleDisplay.ImageName.IsNone())
	{
		ContentBox->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.FillWidth(1.0f)
			[
				SAssignNew(LeftNodeBox, SVerticalBox)
			];
	}
	else
	{
		ContentBox->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.AutoWidth()
			[
				SAssignNew(LeftNodeBox, SVerticalBox)
			];

		if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
		{
			const FSlateBrush* ImageBrush = MetasoundStyle->GetBrush(StyleDisplay.ImageName);
			ContentBox->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(ImageBrush)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.DesiredSizeOverride(FVector2D(20, 20))
			];
		}
	}

	ContentBox->AddSlot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		[
			SAssignNew(RightNodeBox, SVerticalBox)
		];

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0,3))
		[
			ContentBox
		];
}
#undef LOCTEXT_NAMESPACE // MetasoundGraphNode
