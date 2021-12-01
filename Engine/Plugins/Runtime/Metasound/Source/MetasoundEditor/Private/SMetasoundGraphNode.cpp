// Copyright Epic Games, Inc. All Rights Reserved.
#include "SMetasoundGraphNode.h"

#include "AudioParameterControllerInterface.h"
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
#include "MetasoundEditorGraphInputNodes.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorModule.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundTrigger.h"
#include "NodeFactory.h"
#include "PropertyCustomizationHelpers.h"
#include "SAudioRadialSlider.h"
#include "SAudioSlider.h"
#include "SCommentBubble.h"
#include "ScopedTransaction.h"
#include "SGraphNode.h"
#include "SGraphPinComboBox.h"
#include "SLevelOfDetailBranchNode.h"
#include "SMetasoundGraphEnumPin.h"
#include "SMetasoundGraphPin.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateStyleRegistry.h"
#include "TutorialMetaData.h"
#include "UObject/ScriptInterface.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "MetasoundGraphNode"

SMetasoundGraphNode::~SMetasoundGraphNode()
{
	UMetasoundEditorGraphNode& Node = GetMetasoundNode();
	if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(&Node))
	{
		if (UMetasoundEditorGraphInput* GraphInput = InputNode->Input)
		{
			if (UMetasoundEditorGraphInputFloat* InputFloat = Cast<UMetasoundEditorGraphInputFloat>(GraphInput->Literal))
			{
				InputFloat->OnDefaultValueChanged.Remove(InputSliderOnValueChangedDelegateHandle);
				InputFloat->OnRangeChanged.Remove(InputSliderOnRangeChangedDelegateHandle);
			}
		}
	}
}

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
		PreviewComponent->SetTriggerParameter(Input->GetMemberName());
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

void SMetasoundGraphNode::CreateInputSideAddButton(TSharedPtr<SVerticalBox> InputBox)
{
	TSharedRef<SWidget> AddPinButton = AddPinButtonContent(
		LOCTEXT("MetasoundGraphNode_AddPinInputButton", "Add Input"),
		LOCTEXT("MetasoundGraphNode_AddPinInputButton_Tooltip", "Add an input to the parent Metasound node.")
	);

	FMargin AddPinPadding = Settings->GetOutputPinPadding();
	AddPinPadding.Top += 6.0f;

	InputBox->AddSlot()
	.AutoHeight()
	.VAlign(VAlign_Center)
	.Padding(AddPinPadding)
	[
		AddPinButton
	];
}

void SMetasoundGraphNode::CreateOutputSideAddButton(TSharedPtr<SVerticalBox> OutputBox)
{
	TSharedRef<SWidget> AddPinButton = AddPinButtonContent(
		LOCTEXT("MetasoundGraphNode_AddPinOutputButton", "Add Output"),
		LOCTEXT("MetasoundGraphNode_AddPinOutputButton_Tooltip", "Add an output to the parent Metasound node.")
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

	TSharedPtr<SGraphPin> PinWidget;

	if (const UMetasoundEditorGraphSchema* GraphSchema = Cast<const UMetasoundEditorGraphSchema>(InPin->GetSchema()))
	{
		// Don't show default value field for container types
		if (InPin->PinType.ContainerType != EPinContainerType::None)
		{
			PinWidget = SNew(SMetasoundGraphPin, InPin)
				.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
		}

		else if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryAudio)
		{
			PinWidget = SNew(SMetasoundGraphPin, InPin)
				.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
		}

		else if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryBoolean)
		{
			PinWidget = SNew(SMetasoundGraphPinBool, InPin)
				.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
		}

		else if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryFloat)
		{
			PinWidget = SNew(SMetasoundGraphPinFloat, InPin)
				.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
		}

		else if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryInt32)
		{
			if (SMetasoundGraphEnumPin::FindEnumInterfaceFromPin(InPin))
			{
				PinWidget = SNew(SMetasoundGraphEnumPin, InPin)
					.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
			}
			else
			{
				PinWidget = SNew(SMetasoundGraphPinInteger, InPin)
					.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
			}
		}

		else if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryObject)
		{
			PinWidget = SNew(SMetasoundGraphPinObject, InPin)
				.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
		}

		else if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryString)
		{
			PinWidget = SNew(SMetasoundGraphPinString, InPin)
				.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
		}

		else if (InPin->PinType.PinCategory == FGraphBuilder::PinCategoryTrigger)
		{
			PinWidget = SNew(SMetasoundGraphPin, InPin)
				.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);

			if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
			{
				const FSlateBrush* PinConnectedBrush = MetasoundStyle->GetBrush(TEXT("MetasoundEditor.Graph.TriggerPin.Connected"));
				const FSlateBrush* PinDisconnectedBrush = MetasoundStyle->GetBrush(TEXT("MetasoundEditor.Graph.TriggerPin.Disconnected"));
				PinWidget->SetCustomPinIcon(PinConnectedBrush, PinDisconnectedBrush);
			}
		}
	}

	if (!PinWidget.IsValid())
	{
		PinWidget = SNew(SMetasoundGraphPin, InPin)
			.ToolTipText(this, &SMetasoundGraphNode::GetPinTooltip, InPin);
	}

	return PinWidget;
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

FText SMetasoundGraphNode::GetPinTooltip(UEdGraphPin* InPin) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	if (InPin->Direction == EGPD_Input)
	{
		FConstInputHandle InputHandle = FGraphBuilder::GetConstInputHandleFromPin(InPin);
		return InputHandle->GetTooltip();
	}
	else
	{
		FConstOutputHandle OutputHandle = FGraphBuilder::GetConstOutputHandleFromPin(InPin);
		return OutputHandle->GetTooltip();
	}
}

TSharedRef<SWidget> SMetasoundGraphNode::CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle)
{
	Metasound::Frontend::FNodeHandle NodeHandle = GetMetasoundNode().GetNodeHandle();
	if (!NodeHandle->GetClassStyle().Display.bShowName)
	{
		return SNullWidget::NullWidget;
	}

	TSharedPtr<SHorizontalBox> TitleBoxWidget = SNew(SHorizontalBox);

	FSlateIcon NodeIcon = GetMetasoundNode().GetNodeTitleIcon();
	if (const FSlateBrush* IconBrush = NodeIcon.GetIcon())
	{
		if (IconBrush != FStyleDefaults::GetNoBrush())
		{
			TSharedPtr<SImage> Image;
			TitleBoxWidget->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				[
					SAssignNew(Image, SImage)
				]
			];
			Image->SetColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]() { return FSlateColor(GetNodeTitleColorOverride()); }));
			Image->SetImage(IconBrush);
		}
	}

	TitleBoxWidget->AddSlot()
	.AutoWidth()
	[
		SGraphNode::CreateTitleWidget(NodeTitle)
	];

	InlineEditableText->SetColorAndOpacity(TAttribute<FLinearColor>::Create(TAttribute<FLinearColor>::FGetter::CreateSP(this, &SMetasoundGraphNode::GetNodeTitleColorOverride)));

	return TitleBoxWidget.ToSharedRef();
}

FLinearColor SMetasoundGraphNode::GetNodeTitleColorOverride() const
{
	FLinearColor ReturnTitleColor = GraphNode->IsDeprecated() ? FLinearColor::Red : GetNodeObj()->GetNodeTitleColor();

	if (!GraphNode->IsNodeEnabled() || GraphNode->IsDisplayAsDisabledForced() || GraphNode->IsNodeUnrelated())
	{
		ReturnTitleColor *= FLinearColor(0.5f, 0.5f, 0.5f, 0.4f);
	}
	else
	{
		ReturnTitleColor.A = FadeCurve.GetLerp();
	}

	return ReturnTitleColor;
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

void SMetasoundGraphNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);

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
	TSharedPtr<SWidget> OuterContentBox; // currently only used for input float nodes to accommodate the input widget

	// If float input node, add slider input widget 
	bool IsFloatInputNode = false;
	UMetasoundEditorGraphNode& Node = GetMetasoundNode();
	if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(&Node))
	{
		if (UMetasoundEditorGraphInput* GraphInput = InputNode->Input)
		{
			if (UMetasoundEditorGraphInputFloat* InputFloat = Cast<UMetasoundEditorGraphInputFloat>(GraphInput->Literal))
			{
				if (InputFloat->InputWidgetType != EMetasoundInputWidget::None)
				{
					const float InputWidgetPadding = 3.0f;
					const FVector2D SliderDesiredSizeVertical = FVector2D(30.0f, 250.0f);
					const FVector2D RadialSliderDesiredSize = FVector2D(56.0f, 87.0f);
					IsFloatInputNode = true;

					auto OnValueChangedLambda = [InputFloat, GraphInput, this](float Value)
					{
						if (InputWidget.IsValid())
						{
							float Output = InputWidget->GetOutputValue(Value);
							InputFloat->SetDefault(Output);
							if (UMetasoundEditorGraphInputLiteral* InputLiteral = GraphInput->Literal)
							{
								InputLiteral->UpdateDocumentInputLiteral();
							}
						}
					};
					if (InputFloat->InputWidgetType == EMetasoundInputWidget::Slider)
					{
						// Create slider 
						if (InputFloat->InputWidgetValueType == EMetasoundInputWidgetValueType::Frequency)
						{
							SAssignNew(InputWidget, SAudioFrequencySlider)
								.OnValueChanged_Lambda(OnValueChangedLambda);
						}
						else if (InputFloat->InputWidgetValueType == EMetasoundInputWidgetValueType::Volume)
						{
							SAssignNew(InputWidget, SAudioVolumeSlider)
								.OnValueChanged_Lambda(OnValueChangedLambda);
						}
						else
						{
							SAssignNew(InputWidget, SAudioSlider)
								.OnValueChanged_Lambda(OnValueChangedLambda);
							InputWidget->SetShowUnitsText(false);
						}
						// Slider layout 
						if (InputFloat->InputWidgetOrientation == Orient_Vertical)
						{
							SAssignNew(OuterContentBox, SVerticalBox)
							+ SVerticalBox::Slot()
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							.AutoHeight()
							[
								ContentBox
							]
							+ SVerticalBox::Slot()
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Top)
							.Padding(InputWidgetPadding, 0.0f, InputWidgetPadding, InputWidgetPadding)
							.AutoHeight()
							[
								InputWidget.ToSharedRef()
							];
							InputWidget->SetDesiredSizeOverride(SliderDesiredSizeVertical);
						}
						else // horizontal orientation
						{
							SAssignNew(OuterContentBox, SHorizontalBox)
							+ SHorizontalBox::Slot()
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Center)
							.AutoWidth()
							[
								InputWidget.ToSharedRef()
							]
							+SHorizontalBox::Slot()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Fill)
							.Padding(InputWidgetPadding, 0.0f, 0.0f, InputWidgetPadding)
							.AutoWidth()
							[
								ContentBox
							];
							InputWidget->SetDesiredSizeOverride(FVector2D(SliderDesiredSizeVertical.Y, SliderDesiredSizeVertical.X));
						}
						// safe downcast because the ptr was just assigned above 
						StaticCastSharedPtr<SAudioSliderBase>(InputWidget)->SetOrientation(InputFloat->InputWidgetOrientation);
					}
					else if (InputFloat->InputWidgetType == EMetasoundInputWidget::RadialSlider)
					{
						// Create slider 
						if (InputFloat->InputWidgetValueType == EMetasoundInputWidgetValueType::Frequency)
						{
							SAssignNew(InputWidget, SAudioFrequencyRadialSlider)
								.OnValueChanged_Lambda(OnValueChangedLambda);
						}
						else if (InputFloat->InputWidgetValueType == EMetasoundInputWidgetValueType::Volume)
						{
							SAssignNew(InputWidget, SAudioVolumeRadialSlider)
								.OnValueChanged_Lambda(OnValueChangedLambda);
						}
						else
						{
							SAssignNew(InputWidget, SAudioRadialSlider)
								.OnValueChanged_Lambda(OnValueChangedLambda);
							InputWidget->SetShowUnitsText(false);
						}
						// Only vertical layout for radial slider
						SAssignNew(OuterContentBox, SVerticalBox)
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.AutoHeight()
						[
							ContentBox
						]
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Top)
						.Padding(InputWidgetPadding, 0.0f, InputWidgetPadding, InputWidgetPadding)
						.AutoHeight()
						[
							InputWidget.ToSharedRef()
						];
						InputWidget->SetDesiredSizeOverride(RadialSliderDesiredSize);
					}
					InputWidget->SetOutputRange(InputFloat->GetRange());
					InputWidget->SetUnitsTextReadOnly(true);
					InputWidget->SetValue(InputWidget->GetLinValue(InputFloat->GetDefault()));
					// setup delegates 
					// clear delegate if necessary (ex. if was just saved) 
					if (InputSliderOnValueChangedDelegateHandle.IsValid())
					{
						InputFloat->OnDefaultValueChanged.Remove(InputSliderOnValueChangedDelegateHandle);
						InputSliderOnValueChangedDelegateHandle.Reset();
					}
					InputSliderOnValueChangedDelegateHandle = InputFloat->OnDefaultValueChanged.AddLambda([InputWidget = this->InputWidget](float Value)
					{
						if (InputWidget.IsValid())
						{
							const float LinValue = InputWidget->GetLinValue(Value);
							InputWidget->SetValue(LinValue);
						}
					});

					if (InputSliderOnRangeChangedDelegateHandle.IsValid())
					{
						InputFloat->OnRangeChanged.Remove(InputSliderOnRangeChangedDelegateHandle);
						InputSliderOnRangeChangedDelegateHandle.Reset();
					}
					InputSliderOnRangeChangedDelegateHandle = InputFloat->OnRangeChanged.AddLambda([InputWidget = this->InputWidget](FVector2D Range)
					{
						if (InputWidget.IsValid())
						{
							InputWidget->SetOutputRange(Range);
						}
					});
				}
			}
		}
	}
	
	if (StyleDisplay.ImageName.IsNone())
	{
		ContentBox->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.AutoWidth()
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
		.VAlign(VAlign_Center)
		[
			SAssignNew(RightNodeBox, SVerticalBox)
		];

	return SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0,3))
		[
			IsFloatInputNode ? OuterContentBox.ToSharedRef() : ContentBox
		];
}
#undef LOCTEXT_NAMESPACE // MetasoundGraphNode
