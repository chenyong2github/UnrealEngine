// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SAnimGraphSchematicView.h"
#include "AnimationProvider.h"
#include "Widgets/Input/SSearchBox.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Insights/ITimingViewSession.h"
#include "GameplayProvider.h"
#include "TraceServices/Model/Frames.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"
#include "GameplayInsightsStyle.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "SAnimGraphSchematicView"

namespace AnimGraphSchematicPropertyColumns
{
	static const FName Name("Name");
	static const FName Value("Value");
};

namespace AnimGraphSchematicColumns
{
	static const FName Type("Type");
	static const FName Name("Name");
	static const FName Weight("Weight");
	static const FName RootMotionWeight("Root Motion Weight");
};

enum class EAnimGraphSchematicFilterState
{
	Hidden,
	Visible,
	Highlighted,
};

// A in the tree of 'properties' for an animation node's debug info
class FAnimGraphSchematicPropertyNode
{
public:
	FAnimGraphSchematicPropertyNode(const FText& InName, const TSharedPtr<FAnimNodeValueMessage>& InValue, const Trace::IAnalysisSession& InAnalysisSession)
		: AnalysisSession(InAnalysisSession)
		, Name(InName)
		, Value(InValue)
	{}

	TSharedRef<SWidget> MakeValueWidget()
	{
		TSharedPtr<FAnimNodeValueMessage> PinnedValue = Value.Pin();
		if(PinnedValue.IsValid())
		{
			return StaticMakeValueWidget(AnalysisSession, PinnedValue.ToSharedRef());
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

	static TSharedRef<SWidget> StaticMakeValueWidget(const Trace::IAnalysisSession& InAnalysisSession, const TSharedRef<FAnimNodeValueMessage>& InValue) 
	{ 
		switch(InValue->Type)
		{
		case EAnimNodeValueType::Bool:
			return 
				SNew(SCheckBox)
				.IsEnabled(false)
				.IsChecked(InValue->Bool.bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);

		case EAnimNodeValueType::Int32:
			return
				SNew(SBox)
				.WidthOverride(125.0f)
				[
					SNew(SEditableTextBox)
					.IsEnabled(false)
					.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
					.Text(FText::AsNumber(InValue->Int32.Value))
				];

		case EAnimNodeValueType::Float:
			return 
				SNew(SBox)
				.WidthOverride(125.0f)
				[
					SNew(SEditableTextBox)
					.IsEnabled(false)
					.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
					.Text(FText::AsNumber(InValue->Float.Value))
				];

		case EAnimNodeValueType::Vector:
			return SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(125.0f)
					[
						SNew(SEditableTextBox)
						.IsEnabled(false)
						.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
						.Text(FText::AsNumber(InValue->Vector.Value.X))
					]
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(125.0f)
					[
						SNew(SEditableTextBox)
						.IsEnabled(false)
						.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
						.Text(FText::AsNumber(InValue->Vector.Value.Y))
					]
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(125.0f)
					[
						SNew(SEditableTextBox)
						.IsEnabled(false)
						.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
						.Text(FText::AsNumber(InValue->Vector.Value.Z))
					]
				];

		case EAnimNodeValueType::String:
			return 
				SNew(STextBlock)
				.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
				.Text(FText::FromString(InValue->String.Value));

		case EAnimNodeValueType::Object:
		{
			const FGameplayProvider* GameplayProvider = InAnalysisSession.ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
			if(GameplayProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);

				const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(InValue->Object.Value);
#if WITH_EDITOR
				return 
					SNew(SHyperlink)
					.Text(FText::FromString(ObjectInfo.Name))
					.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
					.ToolTipText(FText::Format(LOCTEXT("AssetHyperlinkTooltipFormat", "Open asset '{0}'"), FText::FromString(ObjectInfo.PathName)))
					.OnNavigate_Lambda([ObjectInfo]()
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ObjectInfo.PathName);
					});
#else
				return 
					SNew(STextBlock)
					.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
					.Text(FText::FromString(ObjectInfo.Name))
					.ToolTipText(FText::FromString(ObjectInfo.PathName));
#endif
			}
		}
		break;

		case EAnimNodeValueType::Class:
		{
			const FGameplayProvider* GameplayProvider = InAnalysisSession.ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
			if(GameplayProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);

				const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(InValue->Class.Value);
#if WITH_EDITOR
				return 
					SNew(SHyperlink)
					.Text(FText::FromString(ClassInfo.Name))
					.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
					.ToolTipText(FText::Format(LOCTEXT("ClassHyperlinkTooltipFormat", "Open class '{0}'"), FText::FromString(ClassInfo.PathName)))
					.OnNavigate_Lambda([ClassInfo]()
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ClassInfo.PathName);
					});
#else
				return 
					SNew(STextBlock)
					.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
					.Text(FText::FromString(ClassInfo.Name))
					.ToolTipText(FText::FromString(ClassInfo.PathName));
#endif			
			}
		}
		break;
		}

		return SNullWidget::NullWidget; 
	}

	const Trace::IAnalysisSession& AnalysisSession;

	FText Name;

	TWeakPtr<FAnimNodeValueMessage> Value;

	TWeakPtr<FAnimGraphSchematicPropertyNode> Parent;

	TArray<TSharedRef<FAnimGraphSchematicPropertyNode>> Children;
};

// Container for an entry in the property view
class SAnimGraphSchematicPropertyNode : public SMultiColumnTableRow<TSharedRef<FAnimGraphSchematicPropertyNode>>
{
public:
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FAnimGraphSchematicPropertyNode> InNode)
	{
		Node = InNode;

		SMultiColumnTableRow<TSharedRef<FAnimGraphSchematicPropertyNode>>::Construct(
			FSuperRowType::FArguments()
			.Padding(1.0f),
			InOwnerTable
		);
	}

	// SMultiColumnTableRow interface
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		const bool bIsRoot = !Node->Parent.IsValid();

		if (InColumnName == AnimGraphSchematicPropertyColumns::Name)
		{
			return 
				SNew(SBorder)
				.BorderImage(bIsRoot ? FGameplayInsightsStyle::Get().GetBrush("SchematicViewRootLeft") : FCoreStyle::Get().GetBrush("NoBorder"))
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(6, 0, 0, 0)
					.VAlign(VAlign_Center)
					[
						SNew(SExpanderArrow, SharedThis(this))
						.IndentAmount(0)
					]
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::Get().GetFontStyle(bIsRoot ? "ExpandableArea.TitleFont" : "SmallFont"))
						.Text(Node->Name)
					]
				];
		}
		else if(InColumnName == AnimGraphSchematicPropertyColumns::Value)
		{
			return
				SNew(SBorder)
				.BorderImage(bIsRoot ? FGameplayInsightsStyle::Get().GetBrush("SchematicViewRootMid") : FCoreStyle::Get().GetBrush("NoBorder"))
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						Node->MakeValueWidget()
					]
				];
		}

		return SNullWidget::NullWidget;
	}

	TSharedPtr<FAnimGraphSchematicPropertyNode> Node;
};

// Node representing debug data for an anim node
class FAnimGraphSchematicNode : public TSharedFromThis<FAnimGraphSchematicNode>
{
public:
	FAnimGraphSchematicNode(int32 InNodeId, const FText& InType, const Trace::IAnalysisSession& InAnalysisSession)
		: AnalysisSession(InAnalysisSession)
		, NodeId(InNodeId)
		, Type(InType)
		, FilterState(EAnimGraphSchematicFilterState::Hidden)
		, bLinearized(false)
	{
	}

	void MakePropertyNodes(TArray<TSharedRef<FAnimGraphSchematicPropertyNode>>& OutNodes)
	{
		// Add 'root' representing the node itself
		TSharedRef<FAnimGraphSchematicPropertyNode> Root = MakeShared<FAnimGraphSchematicPropertyNode>(Type, nullptr, AnalysisSession);
		OutNodes.Add(Root);

		for(TSharedRef<FAnimNodeValueMessage> Value : Values)
		{
			TSharedRef<FAnimGraphSchematicPropertyNode> NewNode = MakeShared<FAnimGraphSchematicPropertyNode>(FText::FromString(Value->Key), Value, AnalysisSession);
			Root->Children.Add(NewNode);
			NewNode->Parent = Root;
		}
	}

	const Trace::IAnalysisSession& AnalysisSession;

	int32 NodeId;

	FText Type;

	TMap<FName, TSharedRef<FAnimNodeValueMessage>> KeysAndValues;

	TArray<TSharedRef<FAnimNodeValueMessage>> Values;

	TWeakPtr<FAnimGraphSchematicNode> Parent;

	TArray<TSharedRef<FAnimGraphSchematicNode>> Children;

	TArray<TSharedRef<FAnimGraphSchematicNode>> FlattenedLinearChildren;

	EAnimGraphSchematicFilterState FilterState;

	bool bLinearized;
};

class SAnimGraphSchematicNode : public SMultiColumnTableRow<TSharedRef<FAnimGraphSchematicNode>>
{
	SLATE_BEGIN_ARGS(SAnimGraphSchematicNode) {}

	SLATE_ATTRIBUTE(FText, FilterText)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FAnimGraphSchematicNode> InNode)
	{
		Node = InNode;
		FilterText = InArgs._FilterText;

		SMultiColumnTableRow<TSharedRef<FAnimGraphSchematicNode>>::Construct(
			FSuperRowType::FArguments()
			.Padding(1.0f),
			InOwnerTable
		);
	}

	// SMultiColumnTableRow interface
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		const bool bIsRoot = !Node->Parent.IsValid();

		if (InColumnName == AnimGraphSchematicColumns::Type)
		{
			return 
				SNew(SBorder)
				.BorderImage(bIsRoot ? FGameplayInsightsStyle::Get().GetBrush("SchematicViewRootLeft") : FCoreStyle::Get().GetBrush("NoBorder"))
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(6, 0, 0, 0)
					[
						SNew(SExpanderArrow, SharedThis(this))
						.IndentAmount(12)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::Get().GetFontStyle(bIsRoot ? "ExpandableArea.TitleFont" : "SmallFont"))
						.Text(Node->Type)
						.HighlightText(FilterText)
					]
				];
		}
		else
		{
			TSharedPtr<SWidget> ValueWidget = SNullWidget::NullWidget;
			TSharedRef<FAnimNodeValueMessage>* ValuePtr = Node->KeysAndValues.Find(InColumnName);
			if(ValuePtr)
			{
				ValueWidget = FAnimGraphSchematicPropertyNode::StaticMakeValueWidget(Node->AnalysisSession, *ValuePtr);
			}

			return
				SNew(SBorder)
				.BorderImage(bIsRoot ? FGameplayInsightsStyle::Get().GetBrush("SchematicViewRootMid") : FCoreStyle::Get().GetBrush("NoBorder"))
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						ValueWidget.ToSharedRef()
					]
				];
		}

		return SNullWidget::NullWidget;
	}

	TSharedPtr<FAnimGraphSchematicNode> Node;

	TAttribute<FText> FilterText;
};

void SAnimGraphSchematicView::Construct(const FArguments& InArgs, uint64 InAnimInstanceId, Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	AnimInstanceId = InAnimInstanceId;
	TimingViewSession = &InTimingViewSession;
	AnalysisSession = &InAnalysisSession;
	TimeMarker = InTimingViewSession.GetTimeMarker();

	InTimingViewSession.OnTimeMarkerChanged().AddSP(this, &SAnimGraphSchematicView::HandleTimeMarkerChanged);

	// Create header row and add default columns
	HeaderRow = SNew(SHeaderRow);

	// Make default columns
	Columns.Add(AnimGraphSchematicColumns::Type, { 0, true });
	Columns.Add(AnimGraphSchematicColumns::Name, { 1, true });
	Columns.Add(AnimGraphSchematicColumns::Weight, { 99999, true });
	Columns.Add(AnimGraphSchematicColumns::RootMotionWeight, { 100000, false });

	TreeView = SNew(STreeView<TSharedRef<FAnimGraphSchematicNode>>)
			.TreeItemsSource(&FilteredNodes)
			.OnGenerateRow(this, &SAnimGraphSchematicView::HandleGenerateRow)
			.OnGetChildren(this, &SAnimGraphSchematicView::HandleGetChildren)
			.HeaderRow(HeaderRow.ToSharedRef())
			.OnSelectionChanged(this, &SAnimGraphSchematicView::HandleSelectionChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(SearchBox, SSearchBox)
			.OnTextChanged_Lambda([this](const FText& InText){ FilterText = InText; RefreshFilter(); })
		]
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(Splitter, SSplitter)
			.Orientation(Orient_Vertical)
			+SSplitter::Slot()
			.Value(0.7f)
			[
				SNew(SScrollBorder, TreeView.ToSharedRef())
				[
					TreeView.ToSharedRef()
				]
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		[
			SAssignNew(ViewButton, SComboButton)
			.ContentPadding(0)
			.ForegroundColor(this, &SAnimGraphSchematicView::GetViewButtonForegroundColor)
			.ButtonStyle(FGameplayInsightsStyle::Get(), "SchematicViewViewButton")
			.OnGetMenuContent(this, &SAnimGraphSchematicView::HandleGetViewMenuContent)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FGameplayInsightsStyle::Get().GetBrush("SchematicViewViewButtonIcon"))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ViewButton", "View Options"))
				]
			]
		]
	];

	RefreshColumns();
	RefreshNodes();
}

TSharedRef<ITableRow> SAnimGraphSchematicView::HandleGenerateRow(TSharedRef<FAnimGraphSchematicNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return 
		SNew(SAnimGraphSchematicNode, OwnerTable, Item)
		.FilterText_Lambda([this](){ return FilterText; });
}

void SAnimGraphSchematicView::HandleGetChildren(TSharedRef<FAnimGraphSchematicNode> InItem, TArray<TSharedRef<FAnimGraphSchematicNode>>& OutChildren)
{
	for(const TSharedRef<FAnimGraphSchematicNode>& Child : InItem->FlattenedLinearChildren)
	{
		if(Child->FilterState != EAnimGraphSchematicFilterState::Hidden)
		{
			OutChildren.Add(Child);
		}
	}
}

TSharedRef<ITableRow> SAnimGraphSchematicView::HandleGeneratePropertyRow(TSharedRef<FAnimGraphSchematicPropertyNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return 
		SNew(SAnimGraphSchematicPropertyNode, OwnerTable, Item);
}

void SAnimGraphSchematicView::HandleGetPropertyChildren(TSharedRef<FAnimGraphSchematicPropertyNode> InItem, TArray<TSharedRef<FAnimGraphSchematicPropertyNode>>& OutChildren)
{
	OutChildren.Append(InItem->Children);
}

// Add children recursively if only one child is present, linearizing runs of single children
static void AddChildren_Helper(TSharedRef<FAnimGraphSchematicNode> InItem, TArray<TSharedRef<FAnimGraphSchematicNode>>& OutChildren)
{
	if(InItem->FlattenedLinearChildren.Num() == 0)
	{
		if(InItem->Children.Num() == 1)
		{
			if(!InItem->Children[0]->bLinearized)
			{
				InItem->Children[0]->bLinearized = true;
				OutChildren.Add(InItem->Children[0]);
				AddChildren_Helper(InItem->Children[0], OutChildren);
			}
		}
		else if(InItem->Children.Num() > 1)
		{
			for(const TSharedRef<FAnimGraphSchematicNode>& Child : InItem->Children)
			{
				if(!Child->bLinearized)
				{
					Child->bLinearized = true;
					OutChildren.Add(Child);
				}
			}
		}
	}
}

void SAnimGraphSchematicView::RefreshNodes()
{
	UnfilteredNodes.Reset();
	FilteredNodes.Reset();
	LinearNodes.Reset();
	
	TMap<int32, TSharedRef<FAnimGraphSchematicNode>> NodeMap;

	const FAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);
	const FGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(AnimationProvider && GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		AnimationProvider->ReadAnimGraphTimeline(AnimInstanceId, [this, &NodeMap, AnimationProvider, GameplayProvider](const FAnimationProvider::AnimGraphTimeline& InGraphTimeline)
		{
			const Trace::IFrameProvider& FramesProvider = Trace::ReadFrameProvider(*AnalysisSession);

			Trace::FFrame Frame;
			if(FramesProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, TimeMarker, Frame))
			{
				InGraphTimeline.EnumerateEvents(Frame.StartTime, Frame.EndTime, [this, &NodeMap, AnimationProvider, GameplayProvider](double InGraphStartTime, double InGraphEndTime, uint32 InDepth, const FAnimGraphMessage& InMessage)
				{
					// Check for an update phase (which contains weights)
					if(InMessage.Phase == EAnimGraphPhase::Update)
					{
						AnimationProvider->ReadAnimNodesTimeline(AnimInstanceId, [this, &NodeMap, InGraphStartTime, InGraphEndTime](const FAnimationProvider::AnimNodesTimeline& InNodesTimeline)
						{
							InNodesTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [this, &NodeMap](double InStartTime, double InEndTime, uint32 InDepth, const FAnimNodeMessage& InMessage)
							{
								if(InMessage.NodeId != INDEX_NONE && InMessage.Phase == EAnimGraphPhase::Update)
								{
									TSharedPtr<FAnimGraphSchematicNode> Node;
									TSharedRef<FAnimGraphSchematicNode>* ExistingNodePtr = NodeMap.Find(InMessage.NodeId);
									if(ExistingNodePtr == nullptr)
									{
										Node = NodeMap.Add(InMessage.NodeId, MakeShared<FAnimGraphSchematicNode>(InMessage.NodeId, FText::FromString(InMessage.NodeName), *AnalysisSession));

										// Add dummy values for weight and root motion weight
										TSharedRef<FAnimNodeValueMessage> WeightMessage = MakeShared<FAnimNodeValueMessage>();
										WeightMessage->NodeId = InMessage.NodeId;
										WeightMessage->FrameCounter = InMessage.FrameCounter;
										WeightMessage->Type = EAnimNodeValueType::Float;
										WeightMessage->Key = TEXT("Weight");
										WeightMessage->Float.Value = InMessage.Weight;

										Node->KeysAndValues.Add(AnimGraphSchematicColumns::Weight, WeightMessage);
										Node->Values.Add(WeightMessage);

										TSharedRef<FAnimNodeValueMessage> RootMotionWeightMessage = MakeShared<FAnimNodeValueMessage>();
										RootMotionWeightMessage->NodeId = InMessage.NodeId;
										RootMotionWeightMessage->FrameCounter = InMessage.FrameCounter;
										RootMotionWeightMessage->Type = EAnimNodeValueType::Float;
										RootMotionWeightMessage->Key = TEXT("Root Motion Weight");
										RootMotionWeightMessage->Float.Value = InMessage.RootMotionWeight;

										Node->KeysAndValues.Add(AnimGraphSchematicColumns::RootMotionWeight, RootMotionWeightMessage);
										Node->Values.Add(RootMotionWeightMessage);

										LinearNodes.Add(Node.ToSharedRef());
									}
									else
									{
										Node = *ExistingNodePtr;
									}

									if(InMessage.PreviousNodeId != INDEX_NONE)
									{
										check(InMessage.NodeId != InMessage.PreviousNodeId);
										TSharedRef<FAnimGraphSchematicNode>* ExistingPreviousNodePtr = NodeMap.Find(InMessage.PreviousNodeId);
										if(ExistingPreviousNodePtr != nullptr)
										{
											TSharedRef<FAnimGraphSchematicNode> PreviousNode = *ExistingPreviousNodePtr;
											PreviousNode->Children.Add(Node.ToSharedRef());
											Node->Parent = PreviousNode;
										}
									}
								}
							});
						});

						// TODO: Add visualizations of the reset of the data below
// 						AnimationProvider->ReadStateMachinesTimeline(AnimInstanceId, [InGraphStartTime, InGraphEndTime](const FAnimationProvider::StateMachinesTimeline& InStateMachinesTimeline)
// 						{
// 							InStateMachinesTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [](double InStartTime, double InEndTime, uint32 InDepth, const FAnimStateMachineMessage& InMessage)
// 							{
// 								//DebugData.RecordStateData(InMessage.StateMachineIndex, InMessage.StateIndex, InMessage.StateWeight, InMessage.ElapsedTime);
// 							});
// 						});
//						
// 						AnimationProvider->ReadAnimSequencePlayersTimeline(AnimInstanceId, [InGraphStartTime, InGraphEndTime, GameplayProvider](const FAnimationProvider::AnimSequencePlayersTimeline& InSequencePlayersTimeline)
// 						{
// 							InSequencePlayersTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [](double InStartTime, double InEndTime, uint32 InDepth, const FAnimSequencePlayerMessage& InMessage)
// 							{
// 								//DebugData.RecordSequencePlayer(InMessage.NodeId, InMessage.Position, InMessage.Length, InMessage.FrameCount);
// 							});
// 						});
// 
// 						AnimationProvider->ReadAnimBlendSpacePlayersTimeline(AnimInstanceId, [InGraphStartTime, InGraphEndTime, GameplayProvider](const FAnimationProvider::BlendSpacePlayersTimeline& InBlendSpacePlayersTimeline)
// 						{
// 							InBlendSpacePlayersTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [GameplayProvider](double InStartTime, double InEndTime, uint32 InDepth, const FBlendSpacePlayerMessage& InMessage)
// 							{
// // 								UBlendSpaceBase* BlendSpaceBase = nullptr;
// // 								const FObjectInfo* BlendSpaceInfo = GameplayProvider->FindObjectInfo(InMessage.BlendSpaceId);
// // 								if(BlendSpaceInfo)
// // 								{
// // 									BlendSpaceBase = TSoftObjectPtr<UBlendSpaceBase>(FSoftObjectPath(BlendSpaceInfo->PathName)).LoadSynchronous();
// // 								}
// // 
// // 								DebugData.RecordBlendSpacePlayer(InMessage.NodeId, BlendSpaceBase, InMessage.PositionX, InMessage.PositionY, InMessage.PositionZ);
// 							});
// 						});

					}

					AnimationProvider->ReadAnimNodeValuesTimeline(AnimInstanceId, [this, InGraphStartTime, InGraphEndTime, AnimationProvider, &NodeMap](const FAnimationProvider::AnimNodeValuesTimeline& InNodeValuesTimeline)
					{
						InNodeValuesTimeline.EnumerateEvents(InGraphStartTime, InGraphEndTime, [this, AnimationProvider, &NodeMap](double InStartTime, double InEndTime, uint32 InDepth, const FAnimNodeValueMessage& InMessage)
						{
							TSharedRef<FAnimGraphSchematicNode>* ExistingNodePtr = NodeMap.Find(InMessage.NodeId);
							if(ExistingNodePtr != nullptr)
							{
								TSharedRef<FAnimGraphSchematicNode> ExistingNode = *ExistingNodePtr;

								FName Key = InMessage.Key;

								// Add to columns (+2 to leave space for the fixed type and name columns in the index range)
								Columns.FindOrAdd(Key, { Columns.Num() + 2, false });
								TSharedRef<FAnimNodeValueMessage> SharedMessage = MakeShared<FAnimNodeValueMessage>();
								SharedMessage.Get() = InMessage;
								ExistingNode->KeysAndValues.Add(Key, SharedMessage);
								ExistingNode->Values.Add(SharedMessage);
							}
						});
					});
				});
			};
		});
	}

	// Re-sort columns
	Columns.ValueSort([](const FColumnState& Column0, const FColumnState& Column1)
	{
		return Column0.SortIndex < Column1.SortIndex;
	});

	// Add root nodes to unfiltered list
	for(const TSharedRef<FAnimGraphSchematicNode>& Node : LinearNodes)
	{
		if(!Node->Parent.IsValid())
		{
			UnfilteredNodes.Add(Node);
		}
	}

	// Build flattened linear children from runs of single-child nodes
	for(const TSharedRef<FAnimGraphSchematicNode>& Node : LinearNodes)
	{
		AddChildren_Helper(Node, Node->FlattenedLinearChildren);
	}

	RefreshFilter();
}

EAnimGraphSchematicFilterState SAnimGraphSchematicView::RefreshFilter_Helper(const TSharedRef<FAnimGraphSchematicNode>& InNode)
{
	InNode->FilterState = EAnimGraphSchematicFilterState::Hidden;

	for(const TSharedRef<FAnimGraphSchematicNode>& ChildNode : InNode->FlattenedLinearChildren)
	{
		EAnimGraphSchematicFilterState ChildFilterState = RefreshFilter_Helper(ChildNode);
		InNode->FilterState = FMath::Max(ChildFilterState, InNode->FilterState);
	}
	
	if(InNode->FilterState == EAnimGraphSchematicFilterState::Hidden)
	{
		if(FilterText.IsEmpty())
		{
			InNode->FilterState = EAnimGraphSchematicFilterState::Visible;
		}
		else if (InNode->Type.ToString().Contains(FilterText.ToString()))
		{
			InNode->FilterState = EAnimGraphSchematicFilterState::Highlighted;
		}
		else
		{
			InNode->FilterState = EAnimGraphSchematicFilterState::Hidden;
		}
	}

	bool bAutoExpand = (FilterText.IsEmpty() && InNode->FilterState != EAnimGraphSchematicFilterState::Hidden) ||
					  (!FilterText.IsEmpty() && InNode->FilterState == EAnimGraphSchematicFilterState::Highlighted);
	TreeView->SetItemExpansion(InNode, bAutoExpand);

	return InNode->FilterState;
}

void SAnimGraphSchematicView::RefreshFilter()
{
	FilteredNodes.Reset();

	for(const TSharedRef<FAnimGraphSchematicNode>& RootNode : UnfilteredNodes)
	{
		EAnimGraphSchematicFilterState FilterState = RefreshFilter_Helper(RootNode);
		if(FilterState != EAnimGraphSchematicFilterState::Hidden)
		{
			FilteredNodes.Add(RootNode);
		}
	}

	// re-select any previously selected nodes
	TArray<TSharedRef<FAnimGraphSchematicNode>> SelectedItems;
	for(const TSharedRef<FAnimGraphSchematicNode>& Node : LinearNodes)
	{
		if(SelectedNodeIds.Contains(Node->NodeId))
		{
			SelectedItems.Add(Node);
		}
	}

	if(SelectedItems.Num() > 0)
	{
		TreeView->SetItemSelection(SelectedItems, true);
		TreeView->RequestScrollIntoView(SelectedItems.Last());
	}

	TreeView->RequestTreeRefresh();
}

void SAnimGraphSchematicView::HandleTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker)
{
	TimeMarker = InTimeMarker;

	RefreshNodes();
}

TSharedRef<SWidget> SAnimGraphSchematicView::HandleGetViewMenuContent()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.BeginSection("Columns", LOCTEXT("ColumnsMenuHeader", "Columns"));
	{
		if(Columns.Num() > 0)
		{
			for(const TPair<FName, FColumnState>& ColumnPair : Columns)
			{
				const FName ColumnId = ColumnPair.Key;

				MenuBuilder.AddMenuEntry(
					FText::FromName(ColumnId), 
					LOCTEXT("ColumnTooltip", "Enable/disable this column"),
					FSlateIcon(), 
					FUIAction(
						FExecuteAction::CreateLambda([this, ColumnId]()
						{
							FColumnState* StatePtr = Columns.Find(ColumnId);
							if(StatePtr != nullptr && StatePtr->bEnabled)
							{
								StatePtr->bEnabled = false;
							}
							else
							{
								StatePtr->bEnabled = true;
							}

							RefreshColumns();
						}),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([this, ColumnId]()
						{
							return Columns.FindRef(ColumnId).bEnabled;
						})
					),
					NAME_None,
					EUserInterfaceActionType::Check
				);
			}
		}
		else
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("NoColumns", "No Optional Columns Found"), 
				LOCTEXT("NoColumnsTooltip", "No Optional Columns Found in the Current Session"), 
				FSlateIcon(), 
				FUIAction()
			);
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SAnimGraphSchematicView::RefreshColumns()
{
	HeaderRow->ClearColumns();

	Columns.ValueSort([](const FColumnState& Column0, const FColumnState& Column1)
	{
		return Column0.SortIndex < Column1.SortIndex;
	});

	for(const TPair<FName, FColumnState>& ColumnPair : Columns)
	{
		if(ColumnPair.Value.bEnabled)
		{
			HeaderRow->AddColumn(
				SHeaderRow::FColumn::FArguments()
				.ColumnId(ColumnPair.Key)
				.DefaultLabel(FText::FromName(ColumnPair.Key))
			);
		}
	}
}

FSlateColor SAnimGraphSchematicView::GetViewButtonForegroundColor() const
{
	static const FName InvertedForegroundName("InvertedForeground");
	static const FName DefaultForegroundName("DefaultForeground");

	return ViewButton->IsHovered() ? FCoreStyle::Get().GetSlateColor(InvertedForegroundName) : FCoreStyle::Get().GetSlateColor(DefaultForegroundName);
}

void SAnimGraphSchematicView::HandleSelectionChanged(TSharedPtr<FAnimGraphSchematicNode> InNode, ESelectInfo::Type InSelectInfo)
{
	TArray<TSharedRef<FAnimGraphSchematicNode>> SelectedNodes;
	TreeView->GetSelectedItems(SelectedNodes);

	// Preserve selection of node IDs so scrubbing will re-select them
	if(InSelectInfo != ESelectInfo::Direct)
	{
		SelectedNodeIds.Reset();
		for(const TSharedRef<FAnimGraphSchematicNode>& Item : SelectedNodes)
		{
			SelectedNodeIds.Add(Item->NodeId);
		}
	}

	RefreshDetails(SelectedNodes);
}

void SAnimGraphSchematicView::RefreshDetails(const TArray<TSharedRef<FAnimGraphSchematicNode>>& InNodes)
{
	if(InNodes.Num() > 0)
	{
		// Nodes to show, so create/re-use content box in splitter
		TSharedPtr<SVerticalBox> VerticalBox;

		if(DetailsContentBox.IsValid())
		{
			VerticalBox = DetailsContentBox.Pin();
			VerticalBox->ClearChildren();
		}
		else
		{
			VerticalBox = SNew(SVerticalBox);
			DetailsContentBox = VerticalBox;

			Splitter->AddSlot()
			.Value(0.3f)
			[
				VerticalBox.ToSharedRef()
			];
		}

		// Add content for each node
		PropertyNodes.Reset();

		for(const TSharedRef<FAnimGraphSchematicNode>& Node : InNodes)
		{
			Node->MakePropertyNodes(PropertyNodes);
		}

		PropertyTreeView = SNew(STreeView<TSharedRef<FAnimGraphSchematicPropertyNode>>)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SAnimGraphSchematicView::HandleGeneratePropertyRow)
			.OnGetChildren(this, &SAnimGraphSchematicView::HandleGetPropertyChildren)
			.TreeItemsSource(&PropertyNodes)
			.HeaderRow(
				SNew(SHeaderRow)
				.Visibility(EVisibility::Collapsed)
				+SHeaderRow::Column(AnimGraphSchematicPropertyColumns::Name)
				.DefaultLabel(LOCTEXT("PropertiesNameColumn", "Name"))
				+SHeaderRow::Column(AnimGraphSchematicPropertyColumns::Value)
				.DefaultLabel(LOCTEXT("PropertiesValueColumn", "Value"))
			);

		for(const TSharedRef<FAnimGraphSchematicPropertyNode>& RootNode : PropertyNodes)
		{
			PropertyTreeView->SetItemExpansion(RootNode, true);
		}

		VerticalBox->AddSlot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBorder, PropertyTreeView.ToSharedRef())
				[
					PropertyTreeView.ToSharedRef()
				]
			];
	}
	else
	{
		// no nodes to show, so remove splitter (and therefore content) if valid
		if(DetailsContentBox.IsValid())
		{
			DetailsContentBox.Pin()->ClearChildren();
			Splitter->RemoveAt(1);
		}
	}
}

#undef LOCTEXT_NAMESPACE