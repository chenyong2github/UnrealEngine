// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationNodes/SGraphNodeBlendSpaceGraph.h"
#include "BlendSpaceGraph.h"
#include "AnimGraphNode_BlendSpaceGraphBase.h"
#include "Widgets/SToolTip.h"
#include "PersonaModule.h"
#include "IDocumentationPage.h"
#include "IDocumentation.h"
#include "SBlendSpacePreview.h"
#include "AnimationNodes/SAnimationGraphNode.h"

#define LOCTEXT_NAMESPACE "SGraphNodeBlendSpaceGraph"

void SGraphNodeBlendSpaceGraph::Construct(const FArguments& InArgs, UAnimGraphNode_BlendSpaceGraphBase* InNode)
{
	GraphNode = InNode;

	SetCursor(EMouseCursor::CardinalCross);

	UpdateGraphNode();

	SAnimationGraphNode::ReconfigurePinWidgetsForPropertyBindings(CastChecked<UAnimGraphNode_Base>(GraphNode), SharedThis(this), [this](UEdGraphPin* InPin){ return FindWidgetForPin(InPin); });
}

UEdGraph* SGraphNodeBlendSpaceGraph::GetInnerGraph() const
{
	UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = CastChecked<UAnimGraphNode_BlendSpaceGraphBase>(GraphNode);

	return BlendSpaceNode->GetBlendSpaceGraph();
}

TSharedPtr<SToolTip> SGraphNodeBlendSpaceGraph::GetComplexTooltip()
{
	if (UBlendSpaceGraph* BlendSpaceGraph = CastChecked<UBlendSpaceGraph>(GetInnerGraph()))
	{
		struct LocalUtils
		{
			static bool IsInteractive()
			{
				const FModifierKeysState ModifierKeys = FSlateApplication::Get().GetModifierKeys();
				return ( ModifierKeys.IsAltDown() && ModifierKeys.IsControlDown() );
			}
		};

		FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");

		FBlendSpacePreviewArgs Args;
		Args.PreviewBlendSpace = BlendSpaceGraph->BlendSpace;

		TSharedPtr<SToolTip> FinalToolTip = nullptr;
		TSharedPtr<SVerticalBox> Container = nullptr;
		SAssignNew(FinalToolTip, SToolTip)
		.IsInteractive_Static(&LocalUtils::IsInteractive)
		[
			SAssignNew(Container, SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew( STextBlock )
				.Text(this, &SGraphNodeBlendSpaceGraph::GetTooltipTextForNode)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.WrapTextAt(160.0f)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(200.0f)
				.HeightOverride(150.0f)
				[
					PersonaModule.CreateBlendSpacePreviewWidget(Args)
				]
			]
		];

		// Check to see whether this node has a documentation excerpt. If it does, create a doc box for the tooltip
		TSharedRef<IDocumentationPage> DocPage = IDocumentation::Get()->GetPage(GraphNode->GetDocumentationLink(), nullptr);
		if(DocPage->HasExcerpt(GraphNode->GetDocumentationExcerptName()))
		{
			Container->AddSlot()
			.AutoHeight()
			.Padding(FMargin( 0.0f, 5.0f ))
			[
				IDocumentation::Get()->CreateToolTip(FText::FromString("Documentation"), nullptr, GraphNode->GetDocumentationLink(), GraphNode->GetDocumentationExcerptName())
			];
		}

		return FinalToolTip;
	}
	else
	{
		return SNew(SToolTip)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew( STextBlock )
					.Text(LOCTEXT("InvalidBlendspaceMessage", "ERROR: Invalid Blendspace"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.WrapTextAt(160.0f)
				]
			];
	}

}

void SGraphNodeBlendSpaceGraph::UpdateGraphNode()
{
	SGraphNodeK2Composite::UpdateGraphNode();

	// Extend to add below-widget controls (composite nodes do not do this by default)
	SNodePanel::SNode::FNodeSlot* Slot = GetSlot(ENodeZone::Center);
	check(Slot);
	TSharedPtr<SWidget> CenterWidget = Slot->DetachWidget();

	UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = CastChecked<UAnimGraphNode_BlendSpaceGraphBase>(GraphNode);

	(*Slot)
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			CenterWidget.ToSharedRef()
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBlendSpacePreview, CastChecked<UAnimGraphNode_Base>(GraphNode))
			.OnGetBlendSpaceSampleName(FOnGetBlendSpaceSampleName::CreateLambda([this, WeakBlendSpaceNode = TWeakObjectPtr<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceNode)](int32 InSampleIndex) -> FName
			{
				if(WeakBlendSpaceNode.Get())
				{
					UAnimGraphNode_BlendSpaceGraphBase* BlendSpaceNode = WeakBlendSpaceNode.Get();
					return BlendSpaceNode->GetGraphs()[InSampleIndex]->GetFName();
				}

				return NAME_None;
			}))
		]
	];
}

#undef LOCTEXT_NAMESPACE