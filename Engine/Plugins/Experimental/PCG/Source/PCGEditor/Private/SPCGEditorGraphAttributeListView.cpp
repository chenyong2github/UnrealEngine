// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphAttributeListView.h"

#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "PCGComponent.h"
#include "PCGData.h"
#include "PCGEditor.h"
#include "PCGGraph.h"
#include "PCGNode.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "SPCGEditorGraphAttributeListView"

namespace PCGEditorGraphAttributeListView
{
	/** Names of the columns in the attribute list */
	const FName NAME_IndexColumn = FName(TEXT("IndexColumn"));
	const FName NAME_PointPositionX = FName(TEXT("PointPositionX"));
	const FName NAME_PointPositionY = FName(TEXT("PointPositionY"));
	const FName NAME_PointPositionZ = FName(TEXT("PointPositionZ"));
	const FName NAME_PointRotationX = FName(TEXT("PointRotationX"));
	const FName NAME_PointRotationY = FName(TEXT("PointRotationY"));
	const FName NAME_PointRotationZ = FName(TEXT("PointRotationZ"));
	const FName NAME_PointScaleX = FName(TEXT("PointScaleX"));
	const FName NAME_PointScaleY = FName(TEXT("PointScaleY"));
	const FName NAME_PointScaleZ = FName(TEXT("PointScaleZ"));
	const FName NAME_PointBoundsMinX = FName(TEXT("PointBoundsMinX"));
	const FName NAME_PointBoundsMinY = FName(TEXT("PointBoundsMinY"));
	const FName NAME_PointBoundsMinZ = FName(TEXT("PointBoundsMinZ"));
	const FName NAME_PointBoundsMaxX = FName(TEXT("PointBoundsMaxX"));
	const FName NAME_PointBoundsMaxY = FName(TEXT("PointBoundsMaxY"));
	const FName NAME_PointBoundsMaxZ = FName(TEXT("PointBoundsMaxZ"));
	const FName NAME_PointColorR = FName(TEXT("PointColorR"));
	const FName NAME_PointColorG = FName(TEXT("PointColorG"));
	const FName NAME_PointColorB = FName(TEXT("PointColorB"));
	const FName NAME_PointColorA = FName(TEXT("PointColorA"));
	const FName NAME_PointDensity = FName(TEXT("PointDensity"));
	const FName NAME_PointSteepness = FName(TEXT("PointSteepness"));
	const FName NAME_PointSeed = FName(TEXT("PointSeed"));

	/** Labels of the columns */
	const FText TEXT_IndexLabel = LOCTEXT("IndexLabel", "Index");
	const FText TEXT_PointPositionLabelX = LOCTEXT("PointPositionLabelX", "PositionX");
	const FText TEXT_PointPositionLabelY = LOCTEXT("PointPositionLabelY", "PositionY");
	const FText TEXT_PointPositionLabelZ = LOCTEXT("PointPositionLabelZ", "PositionZ");
	const FText TEXT_PointRotationLabelX = LOCTEXT("PointRotationLabelX", "RotationX");
	const FText TEXT_PointRotationLabelY = LOCTEXT("PointRotationLabelY", "RotationY");
	const FText TEXT_PointRotationLabelZ = LOCTEXT("PointRotationLabelZ", "RotationZ");
	const FText TEXT_PointScaleLabelX = LOCTEXT("PointScaleLabelX", "ScaleX");
	const FText TEXT_PointScaleLabelY = LOCTEXT("PointScaleLabelY", "ScaleY");
	const FText TEXT_PointScaleLabelZ = LOCTEXT("PointScaleLabelZ", "ScaleZ");
	const FText TEXT_PointBoundsLabelMinX = LOCTEXT("PointBoundsMinX", "BoundsMinX");
	const FText TEXT_PointBoundsLabelMinY = LOCTEXT("PointBoundsMinY", "BoundsMinY");
	const FText TEXT_PointBoundsLabelMinZ = LOCTEXT("PointBoundsMinZ", "BoundsMinZ");
	const FText TEXT_PointBoundsLabelMaxX = LOCTEXT("PointBoundsMaxX", "BoundsMaxX");
	const FText TEXT_PointBoundsLabelMaxY = LOCTEXT("PointBoundsMaxY", "BoundsMaxY");
	const FText TEXT_PointBoundsLabelMaxZ = LOCTEXT("PointBoundsMaxZ", "BoundsMaxZ");
	const FText TEXT_PointColorLabelR = LOCTEXT("PointColorR", "ColorR");
	const FText TEXT_PointColorLabelG = LOCTEXT("PointColorG", "ColorG");
	const FText TEXT_PointColorLabelB = LOCTEXT("PointColorB", "ColorB");
	const FText TEXT_PointColorLabelA = LOCTEXT("PointColorA", "ColorA");
	const FText TEXT_PointDensityLabel = LOCTEXT("PointDensityLabel", "Density");
	const FText TEXT_PointSteepnessLabel = LOCTEXT("PointSteepnessLabel", "Steepness");
	const FText TEXT_PointSeedLabel = LOCTEXT("PointSeedLabel", "Seed");
}

void SPCGListViewItemRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const PCGListviewItemPtr& Item)
{
	InternalItem = Item;

	SMultiColumnTableRow<PCGListviewItemPtr>::Construct(
		SMultiColumnTableRow::FArguments()
		.Style(FAppStyle::Get(), "DataTableEditor.CellListViewRow"),
		InOwnerTableView);
}

TSharedRef<SWidget> SPCGListViewItemRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	FText ColumnData = LOCTEXT("ColumnError", "Unrecognized Column");
	if (const FPCGPoint* PCGPoint = InternalItem->PCGPoint)
	{
		const FTransform& Transform = PCGPoint->Transform;
		if (ColumnId == PCGEditorGraphAttributeListView::NAME_IndexColumn)
		{
			ColumnData = FText::FromString(FString::FromInt(InternalItem->Index));
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointPositionX)
		{
			const FVector& Position = Transform.GetLocation();
			ColumnData = FText::AsNumber(Position.X);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointPositionY)
		{
			const FVector& Position = Transform.GetLocation();
			ColumnData = FText::AsNumber(Position.Y);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointPositionZ)
		{
			const FVector& Position = Transform.GetLocation();
			ColumnData = FText::AsNumber(Position.Z);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointRotationX)
		{
			const FRotator& Rotation = Transform.Rotator();
			ColumnData = FText::AsNumber(Rotation.Roll);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointRotationY)
		{
			const FRotator& Rotation = Transform.Rotator();
			ColumnData = FText::AsNumber(Rotation.Pitch);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointRotationZ)
		{
			const FRotator& Rotation = Transform.Rotator();
			ColumnData = FText::AsNumber(Rotation.Yaw);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointScaleX)
		{
			const FVector& Scale = Transform.GetScale3D();
			ColumnData = FText::AsNumber(Scale.X);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointScaleY)
		{
			const FVector& Scale = Transform.GetScale3D();
			ColumnData = FText::AsNumber(Scale.Y);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointScaleZ)
		{
			const FVector& Scale = Transform.GetScale3D();
			ColumnData = FText::AsNumber(Scale.Z);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointBoundsMinX)
		{
			ColumnData = FText::AsNumber(PCGPoint->BoundsMin.X);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointBoundsMinY)
		{
			ColumnData = FText::AsNumber(PCGPoint->BoundsMin.Y);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointBoundsMinZ)
		{
			ColumnData = FText::AsNumber(PCGPoint->BoundsMin.Z);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointBoundsMaxX)
		{
			ColumnData = FText::AsNumber(PCGPoint->BoundsMax.X);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointBoundsMaxY)
		{
			ColumnData = FText::AsNumber(PCGPoint->BoundsMax.Y);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointBoundsMaxZ)
		{
			ColumnData = FText::AsNumber(PCGPoint->BoundsMax.Z);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointColorR)
		{
			ColumnData = FText::AsNumber(PCGPoint->Color.X);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointColorG)
		{
			ColumnData = FText::AsNumber(PCGPoint->Color.Y);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointColorB)
		{
			ColumnData = FText::AsNumber(PCGPoint->Color.Z);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointColorA)
		{
			ColumnData = FText::AsNumber(PCGPoint->Color.W);
		}		
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointDensity)
		{
			const float Density = PCGPoint->Density;
			ColumnData = FText::AsNumber(Density);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointSteepness)
		{
			const float Steepness = PCGPoint->Steepness;
			ColumnData = FText::AsNumber(Steepness);
		}
		else if (ColumnId == PCGEditorGraphAttributeListView::NAME_PointSeed)
		{
			const int32 Seed = PCGPoint->Seed;
			ColumnData = FText::AsNumber(Seed);
		}
	}

	return SNew(STextBlock).Text(ColumnData);
}

SPCGEditorGraphAttributeListView::~SPCGEditorGraphAttributeListView()
{
	if (PCGEditorPtr.IsValid())
	{
		PCGEditorPtr.Pin()->OnDebugObjectChangedDelegate.RemoveAll(this);
		PCGEditorPtr.Pin()->OnInspectedNodeChangedDelegate.RemoveAll(this);
	}
}

void SPCGEditorGraphAttributeListView::Construct(const FArguments& InArgs, TSharedPtr<FPCGEditor> InPCGEditor)
{
	PCGEditorPtr = InPCGEditor;

	PCGEditorPtr.Pin()->OnDebugObjectChangedDelegate.AddSP(this, &SPCGEditorGraphAttributeListView::OnDebugObjectChanged);
	PCGEditorPtr.Pin()->OnInspectedNodeChangedDelegate.AddSP(this, &SPCGEditorGraphAttributeListView::OnInspectedNodeChanged);

	ListViewHeader = CreateHeaderRowWidget();

	const TSharedRef<SScrollBar> HorizontalScrollBar = SNew(SScrollBar)
		.Orientation(Orient_Horizontal)
		.Thickness(FVector2D(12.0f, 12.0f));

	const TSharedRef<SScrollBar> VerticalScrollBar = SNew(SScrollBar)
		.Orientation(Orient_Vertical)
		.Thickness(FVector2D(12.0f, 12.0f));
	
	SAssignNew(ListView, SListView<PCGListviewItemPtr>)
		.ListItemsSource(&ListViewItems)
		.HeaderRow(ListViewHeader)
		.OnGenerateRow(this, &SPCGEditorGraphAttributeListView::OnGenerateRow)
		.AllowOverscroll(EAllowOverscroll::No)
		.ExternalScrollbar(VerticalScrollBar)
		.ConsumeMouseWheel(EConsumeMouseWheel::Always);

	this->ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)
				.ExternalScrollbar(HorizontalScrollBar)
				+SScrollBox::Slot()
				[
					ListView->AsShared()
				]
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				VerticalScrollBar
			]
		]
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			[
				HorizontalScrollBar
			]
		]
	];
}

TSharedRef<SHeaderRow> SPCGEditorGraphAttributeListView::CreateHeaderRowWidget() const
{
	return SNew(SHeaderRow)
			.ResizeMode(ESplitterResizeMode::FixedPosition)
			.CanSelectGeneratedColumn(true)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_IndexColumn)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_IndexLabel)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(44)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointPositionX)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointPositionLabelX)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(94)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointPositionY)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointPositionLabelY)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(94)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointPositionZ)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointPositionLabelZ)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(94)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointRotationX)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointRotationLabelX)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(68)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointRotationY)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointRotationLabelY)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(68)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointRotationZ)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointRotationLabelZ)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(68)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointScaleX)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointScaleLabelX)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(50)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointScaleY)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointScaleLabelY)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(50)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointScaleZ)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointScaleLabelZ)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(50)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointBoundsMinX)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointBoundsLabelMinX)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(80)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointBoundsMinY)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointBoundsLabelMinY)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(80)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointBoundsMinZ)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointBoundsLabelMinZ)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(80)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointBoundsMaxX)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointBoundsLabelMaxX)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(88)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointBoundsMaxY)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointBoundsLabelMaxY)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(88)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointBoundsMaxZ)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointBoundsLabelMaxZ)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(88)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointColorR)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointColorLabelR)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(50)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointColorG)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointColorLabelG)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(50)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointColorB)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointColorLabelB)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(50)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointColorA)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointColorLabelA)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(50)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointDensity)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointDensityLabel)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(54)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointSteepness)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointSteepnessLabel)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(73)
			+ SHeaderRow::Column(PCGEditorGraphAttributeListView::NAME_PointSeed)
			.DefaultLabel(PCGEditorGraphAttributeListView::TEXT_PointSeedLabel)
			.HAlignHeader(HAlign_Center)
			.HAlignCell(HAlign_Right)
			.ManualWidth(88);
}

void SPCGEditorGraphAttributeListView::OnDebugObjectChanged(UPCGComponent* InPCGComponent)
{
	if (PCGComponent)
	{
		PCGComponent->OnPCGGraphGeneratedDelegate.RemoveAll(this);
		PCGComponent->OnPCGGraphCleanedDelegate.RemoveAll(this);
		PCGComponent->DisableInspection();
	}
	
	PCGComponent = InPCGComponent;

	if (PCGComponent)
	{
		PCGComponent->EnableInspection();
		PCGComponent->OnPCGGraphGeneratedDelegate.AddSP(this, &SPCGEditorGraphAttributeListView::RebuildAttributeList);
		PCGComponent->OnPCGGraphCleanedDelegate.AddSP(this, &SPCGEditorGraphAttributeListView::RebuildAttributeList);	
	}

	RebuildAttributeList();
}

void SPCGEditorGraphAttributeListView::OnInspectedNodeChanged(UPCGNode* /*InPCGNode*/)
{	
	RebuildAttributeList();
}

void SPCGEditorGraphAttributeListView::RebuildAttributeList(UPCGComponent* /*InPCGComponent*/)
{
	RebuildAttributeList();
}

void SPCGEditorGraphAttributeListView::RebuildAttributeList()
{
	ListViewItems.Empty();
	ListView->RequestListRefresh();

	const TSharedPtr<FPCGEditor> PCGEditor = PCGEditorPtr.Pin();
	if (!PCGEditor.IsValid())
	{
		return;
	}

	if (!PCGComponent)
	{
		return;
	}

	const UPCGNode* PCGNode = PCGEditor->GetPCGNodeBeingInspected();
	if (!PCGNode)
	{
		return;
	}
	
	const FPCGDataCollection* InspectionData = PCGComponent->GetInspectionData(PCGNode);
	if (!InspectionData)
	{
		return;
	}
	
	if (InspectionData->TaggedData.Num() > 0)
	{
		const FPCGTaggedData& TaggedData = InspectionData->TaggedData[0]; // TODO: Add dropdown to select tagged data.
		const UPCGData* PCGData = TaggedData.Data;
		if (const UPCGSpatialData* PCGSpatialData = Cast<UPCGSpatialData>(PCGData))
		{
			if (const UPCGPointData* PCGPointData = PCGSpatialData->ToPointData())
			{
				const TArray<FPCGPoint>& PCGPoints = PCGPointData->GetPoints();

				ListViewItems.Reserve(PCGPoints.Num());
				for (int32 PointIndex = 0; PointIndex < PCGPoints.Num(); PointIndex++)
				{
					const FPCGPoint& PCGPoint = PCGPoints[PointIndex];
					// TODO: Investigate swapping out the shared ptr's for better performance on huge data sets
					PCGListviewItemPtr ListViewItem = MakeShared<FPCGListViewItem>();
					ListViewItem->Index = PointIndex;
					ListViewItem->PCGPoint = &PCGPoint;
					ListViewItems.Add(ListViewItem);
				}
			}
		}
	}
}

TSharedRef<ITableRow> SPCGEditorGraphAttributeListView::OnGenerateRow(PCGListviewItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SPCGListViewItemRow, OwnerTable, Item);
}

#undef LOCTEXT_NAMESPACE
