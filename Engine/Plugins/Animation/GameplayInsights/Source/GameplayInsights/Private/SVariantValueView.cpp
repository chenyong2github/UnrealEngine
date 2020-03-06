// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVariantValueView.h"
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
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SCheckBox.h"
#include "VariantTreeNode.h"

#if WITH_EDITOR
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "SVariantValueView"

namespace VariantColumns
{
	static const FName Name("Name");
	static const FName Value("Value");
};

static TSharedRef<SWidget> MakeVariantValueWidget(const Trace::IAnalysisSession& InAnalysisSession, const FVariantValue& InValue) 
{ 
	switch(InValue.Type)
	{
	case EAnimNodeValueType::Bool:
		return 
			SNew(SCheckBox)
			.IsEnabled(false)
			.IsChecked(InValue.Bool.bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);

	case EAnimNodeValueType::Int32:
		return
			SNew(SBox)
			.WidthOverride(125.0f)
			[
				SNew(SEditableTextBox)
				.IsEnabled(false)
				.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
				.Text(FText::AsNumber(InValue.Int32.Value))
			];

	case EAnimNodeValueType::Float:
		return 
			SNew(SBox)
			.WidthOverride(125.0f)
			[
				SNew(SEditableTextBox)
				.IsEnabled(false)
				.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
				.Text(FText::AsNumber(InValue.Float.Value))
			];

	case EAnimNodeValueType::Vector2D:
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
					.Text(FText::AsNumber(InValue.Vector2D.Value.X))
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
					.Text(FText::AsNumber(InValue.Vector2D.Value.Y))
				]
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
					.Text(FText::AsNumber(InValue.Vector.Value.X))
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
					.Text(FText::AsNumber(InValue.Vector.Value.Y))
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
					.Text(FText::AsNumber(InValue.Vector.Value.Z))
				]
			];

	case EAnimNodeValueType::String:
		return 
			SNew(STextBlock)
			.Font(FCoreStyle::Get().GetFontStyle("SmallFont"))
			.Text(FText::FromString(InValue.String.Value));

	case EAnimNodeValueType::Object:
	{
		const FGameplayProvider* GameplayProvider = InAnalysisSession.ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
		if(GameplayProvider)
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);

			const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(InValue.Object.Value);
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

			const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(InValue.Class.Value);
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

// Container for an entry in the property view
class SVariantValueNode : public SMultiColumnTableRow<TSharedRef<FVariantTreeNode>>
{
public:
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FVariantTreeNode> InNode, const Trace::IAnalysisSession& InAnalysisSession)
	{
		Node = InNode;
		AnalysisSession = &InAnalysisSession;

		SMultiColumnTableRow<TSharedRef<FVariantTreeNode>>::Construct(
			FSuperRowType::FArguments()
			.Padding(1.0f),
			InOwnerTable
		);
	}

	// SMultiColumnTableRow interface
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		const bool bIsRoot = !Node->GetParent().IsValid();

		if (InColumnName == VariantColumns::Name)
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
						.Text(Node->GetName())
					]
				];
		}
		else if(InColumnName == VariantColumns::Value)
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
						MakeVariantValueWidget(*AnalysisSession, Node->GetValue())
					]
				];
		}

		return SNullWidget::NullWidget;
	}

	const Trace::IAnalysisSession* AnalysisSession;
	TSharedPtr<FVariantTreeNode> Node;
};

void SVariantValueView::Construct(const FArguments& InArgs, const Trace::IAnalysisSession& InAnalysisSession)
{
	OnGetVariantValues = InArgs._OnGetVariantValues;

	AnalysisSession = &InAnalysisSession;
	bNeedsRefresh = false;

	VariantTreeView = SNew(STreeView<TSharedRef<FVariantTreeNode>>)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &SVariantValueView::HandleGeneratePropertyRow)
		.OnGetChildren(this, &SVariantValueView::HandleGetPropertyChildren)
		.TreeItemsSource(&VariantTreeNodes)
		.HeaderRow(
			SNew(SHeaderRow)
			.Visibility(EVisibility::Collapsed)
			+SHeaderRow::Column(VariantColumns::Name)
			.DefaultLabel(LOCTEXT("ValueNameColumn", "Name"))
			+SHeaderRow::Column(VariantColumns::Value)
			.DefaultLabel(LOCTEXT("ValueValueColumn", "Value"))
		);

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBorder, VariantTreeView.ToSharedRef())
			[
				VariantTreeView.ToSharedRef()
			]
		]
	];

	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime)
	{
		if(bNeedsRefresh)
		{
			RefreshNodes();
			bNeedsRefresh = false;
		}
		return EActiveTimerReturnType::Continue;
	}));
}

TSharedRef<ITableRow> SVariantValueView::HandleGeneratePropertyRow(TSharedRef<FVariantTreeNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return 
		SNew(SVariantValueNode, OwnerTable, Item, *AnalysisSession);
}

void SVariantValueView::HandleGetPropertyChildren(TSharedRef<FVariantTreeNode> InItem, TArray<TSharedRef<FVariantTreeNode>>& OutChildren)
{
	for(const TSharedRef<FVariantTreeNode>& ChildNode : InItem->GetChildren())
	{
		VariantTreeView->SetItemExpansion(ChildNode, true);
	}

	OutChildren.Append(InItem->GetChildren());
}

void SVariantValueView::RefreshNodes()
{
	VariantTreeNodes.Reset();

	OnGetVariantValues.ExecuteIfBound(Frame, VariantTreeNodes);

	if(VariantTreeNodes.Num() > 0)
	{
		for(const TSharedRef<FVariantTreeNode>& VariantTreeNode : VariantTreeNodes)
		{
			VariantTreeView->SetItemExpansion(VariantTreeNode, true);
		}
	}

	VariantTreeView->RequestTreeRefresh();
}

#undef LOCTEXT_NAMESPACE
