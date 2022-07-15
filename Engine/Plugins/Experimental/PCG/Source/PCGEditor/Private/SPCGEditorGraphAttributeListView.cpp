// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphAttributeListView.h"

#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
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
	const FText NoDataAvailableText = LOCTEXT("NoDataAvailableText", "No data available");
	
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

		if (const UPCGMetadata* PCGMetadata = InternalItem->PCGMetadata)
		{
			if (const FPCGMetadataInfo* MetadataInfo = InternalItem->MetadataInfos->Find(ColumnId))
			{
				if (const FPCGMetadataAttributeBase* AttributeBase = PCGMetadata->GetConstAttribute((*MetadataInfo).MetadataId)) // todo investigate deadlock caused by read lock
				{
					if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<float>::Id)
					{
						const float MetaFloat = static_cast<const FPCGMetadataAttribute<float>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = FText::AsNumber(MetaFloat);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<double>::Id)
					{
						const double MetaDouble = static_cast<const FPCGMetadataAttribute<double>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = FText::AsNumber(MetaDouble);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<bool>::Id)
					{
						const bool bMetaBool = static_cast<const FPCGMetadataAttribute<bool>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = bMetaBool ? FText::FromString(TEXT("true")) : FText::FromString(TEXT("false"));
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector>::Id)
					{
						const FVector MetaVector = static_cast<const FPCGMetadataAttribute<FVector>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = FText::AsNumber(MetaVector[(*MetadataInfo).Index]);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FVector4>::Id)
					{
						const FVector4 MetaVector4 = static_cast<const FPCGMetadataAttribute<FVector4>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = FText::AsNumber(MetaVector4[(*MetadataInfo).Index]);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<int32>::Id)
					{
						const int32 MetaInt32 = static_cast<const FPCGMetadataAttribute<int32>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = FText::AsNumber(MetaInt32);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<int64>::Id)
					{
						const int64 MetaInt64 = static_cast<const FPCGMetadataAttribute<int64>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = FText::AsNumber(MetaInt64);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FString>::Id)
					{
						const FString MetaString = static_cast<const FPCGMetadataAttribute<FString>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = FText::FromString(MetaString);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FName>::Id)
					{
						const FName MetaName = static_cast<const FPCGMetadataAttribute<FName>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						ColumnData = FText::FromName(MetaName);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FQuat>::Id)
					{
						const FQuat MetaQuat = static_cast<const FPCGMetadataAttribute<FQuat>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						float QuatValue = 0.0f;
						if ((*MetadataInfo).Index == 0)
						{
							QuatValue = MetaQuat.X;
						}
						else if ((*MetadataInfo).Index == 1)
						{
							QuatValue = MetaQuat.Y;
						}
						else if ((*MetadataInfo).Index == 2)
						{
							QuatValue = MetaQuat.Z;
						}
						else if ((*MetadataInfo).Index == 3)
						{
							QuatValue = MetaQuat.W;
						}
						
						ColumnData = FText::AsNumber(QuatValue);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FRotator>::Id)
					{
						const FRotator MetaRotator = static_cast<const FPCGMetadataAttribute<FRotator>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						float RotatorValue = 0.0f;
						if ((*MetadataInfo).Index == 0)
						{
							RotatorValue = MetaRotator.Roll;
						}
						else if ((*MetadataInfo).Index == 1)
						{
							RotatorValue = MetaRotator.Pitch;
						}
						else if ((*MetadataInfo).Index == 2)
						{
							RotatorValue = MetaRotator.Yaw;
						}
						ColumnData = FText::AsNumber(RotatorValue);
					}
					else if (AttributeBase->GetTypeId() == PCG::Private::MetadataTypes<FTransform>::Id)
					{
						const FTransform& MetaTransform = static_cast<const FPCGMetadataAttribute<FTransform>*>(AttributeBase)->GetValueFromItemKey(PCGPoint->MetadataEntry);
						const int8 ComponentIndex = (*MetadataInfo).Index / 3;
						const int8 ValueIndex = (*MetadataInfo).Index % 3;
						if (ComponentIndex == 0)
						{
							const FVector& MetaVector = MetaTransform.GetLocation();
							ColumnData = FText::AsNumber(MetaVector[ValueIndex]);
						}
						else if(ComponentIndex == 1)
						{
							const FRotator& MetaRotator = MetaTransform.Rotator();
							float RotatorValue = 0.0f;
							if (ValueIndex == 0)
							{
								RotatorValue = MetaRotator.Roll;
							}
							else if (ValueIndex == 1)
							{
								RotatorValue = MetaRotator.Pitch;
							}
							else if (ValueIndex == 2)
							{
								RotatorValue = MetaRotator.Yaw;
							}
							ColumnData = FText::AsNumber(RotatorValue);
						}
						else if(ComponentIndex == 2)
						{
							const FVector& MetaVector = MetaTransform.GetScale3D();
							ColumnData = FText::AsNumber(MetaVector[ValueIndex]);
						}
					}
				}
			}
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

	SAssignNew(DataComboBox, SComboBox<TSharedPtr<FName>>)
		.OptionsSource(&DataComboBoxItems)
		.OnGenerateWidget(this, &SPCGEditorGraphAttributeListView::OnGenerateDataWidget)
		.OnSelectionChanged(this, &SPCGEditorGraphAttributeListView::OnSelectionChanged)
		[
			SNew(STextBlock)
			.Text(this, &SPCGEditorGraphAttributeListView::OnGenerateSelectedDataText)
		];
	
	this->ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			DataComboBox->AsShared()
		]
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
		PCGComponent->OnPCGGraphGeneratedDelegate.AddSP(this, &SPCGEditorGraphAttributeListView::OnGenerateUpdated);
		PCGComponent->OnPCGGraphCleanedDelegate.AddSP(this, &SPCGEditorGraphAttributeListView::OnGenerateUpdated);	
	}
	else
	{
		RefreshDataComboBox();
		RefreshAttributeList();
	}
}

void SPCGEditorGraphAttributeListView::OnInspectedNodeChanged(UPCGNode* /*InPCGNode*/)
{
	RefreshDataComboBox();
	RefreshAttributeList();
}

void SPCGEditorGraphAttributeListView::OnGenerateUpdated(UPCGComponent* /*InPCGComponent*/)
{
	RefreshDataComboBox();
	RefreshAttributeList();
}

void SPCGEditorGraphAttributeListView::RefreshAttributeList()
{
	ListViewItems.Empty();
	ListView->SetListItemsSource(EmptyList); // TODO: Investigate if it's possible to avoid this hack that's used to force refresh the list view. Without it the AddColumn will try to create widgets for old data.

	RemoveMetadataColumns();
	MetadataInfos.Empty();

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

	const int32 DataIndex = GetSelectedDataIndex();
	if (!InspectionData->TaggedData.IsValidIndex(DataIndex))
	{
		return;
	}
	
	const FPCGTaggedData& TaggedData = InspectionData->TaggedData[DataIndex];
	const UPCGData* PCGData = TaggedData.Data;

	if (const UPCGSpatialData* PCGSpatialData = Cast<UPCGSpatialData>(PCGData))
	{
		if (const UPCGPointData* PCGPointData = PCGSpatialData->ToPointData())
		{
			const UPCGMetadata* PCGMetadata = PCGPointData->ConstMetadata(); // TODO: add support for paramdata as well.
		
			TArray<FName> AttributeNames;
			TArray<EPCGMetadataTypes> AttributeTypes;
			PCGMetadata->GetAttributes(AttributeNames, AttributeTypes);

			for (int32 i = 0; i < AttributeNames.Num(); i++)
			{
				const FName& AttributeName = AttributeNames[i];
				const EPCGMetadataTypes& AttributeType = AttributeTypes[i];

				switch (AttributeType)
				{
				case EPCGMetadataTypes::Float:
				case EPCGMetadataTypes::Double:
				case EPCGMetadataTypes::Integer32:
				case EPCGMetadataTypes::Integer64:
				case EPCGMetadataTypes::Boolean:
				case EPCGMetadataTypes::String:
				case EPCGMetadataTypes::Name:
					{
						AddMetadataColumn(AttributeName);
						break;
					}
				case EPCGMetadataTypes::Vector:
					{
						AddMetadataColumn(AttributeName, 0, TEXT("_X"));
						AddMetadataColumn(AttributeName, 1, TEXT("_Y"));
						AddMetadataColumn(AttributeName, 2, TEXT("_Z"));
						break;
					}
				case EPCGMetadataTypes::Vector4:
				case EPCGMetadataTypes::Quaternion:
					{
						AddMetadataColumn(AttributeName, 0, TEXT("_X"));
						AddMetadataColumn(AttributeName, 1, TEXT("_Y"));
						AddMetadataColumn(AttributeName, 2, TEXT("_Z"));
						AddMetadataColumn(AttributeName, 3, TEXT("_W"));
						break;
					}
				case EPCGMetadataTypes::Transform:
					{
						AddMetadataColumn(AttributeName, 0, TEXT("_tX"));
						AddMetadataColumn(AttributeName, 1, TEXT("_tY"));
						AddMetadataColumn(AttributeName, 2, TEXT("_tZ"));
						AddMetadataColumn(AttributeName, 3, TEXT("_rX"));
						AddMetadataColumn(AttributeName, 4, TEXT("_rY"));
						AddMetadataColumn(AttributeName, 5, TEXT("_rZ"));
						AddMetadataColumn(AttributeName, 6, TEXT("_sX"));
						AddMetadataColumn(AttributeName, 7, TEXT("_sY"));
						AddMetadataColumn(AttributeName, 8, TEXT("_sZ"));
						break;
					}
				default:
					break;
				}
			}

			const TArray<FPCGPoint>& PCGPoints = PCGPointData->GetPoints();

			ListViewItems.Reserve(PCGPoints.Num());
			for (int32 PointIndex = 0; PointIndex < PCGPoints.Num(); PointIndex++)
			{
				const FPCGPoint& PCGPoint = PCGPoints[PointIndex];
				// TODO: Investigate swapping out the shared ptr's for better performance on huge data sets
				PCGListviewItemPtr ListViewItem = MakeShared<FPCGListViewItem>();
				ListViewItem->Index = PointIndex;
				ListViewItem->PCGPoint = &PCGPoint;
				ListViewItem->PCGMetadata = PCGMetadata;
				ListViewItem->MetadataInfos = &MetadataInfos;
				ListViewItems.Add(ListViewItem);
			}
		}
	}

	ListView->SetListItemsSource(ListViewItems);
}

void SPCGEditorGraphAttributeListView::RefreshDataComboBox()
{
	DataComboBoxItems.Empty();
	DataComboBox->ClearSelection();
	DataComboBox->RefreshOptions();

	if (!PCGComponent)
	{
		return;
	}

	const TSharedPtr<FPCGEditor> PCGEditor = PCGEditorPtr.Pin();
	if (!PCGEditor.IsValid())
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

	for (const FPCGTaggedData& TaggedData : InspectionData->TaggedData)
	{
		DataComboBoxItems.Add(MakeShared<FName>(TaggedData.Pin));
	}

	if (DataComboBoxItems.Num() > 0)
	{
		DataComboBox->SetSelectedItem(DataComboBoxItems[0]);
	}
}

TSharedRef<SWidget> SPCGEditorGraphAttributeListView::OnGenerateDataWidget(TSharedPtr<FName> InItem) const
{
	return SNew(STextBlock).Text(FText::FromName(InItem.IsValid() ? *InItem : NAME_None));
}

void SPCGEditorGraphAttributeListView::OnSelectionChanged(TSharedPtr<FName> Item, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		RefreshAttributeList();
	}
}

FText SPCGEditorGraphAttributeListView::OnGenerateSelectedDataText() const
{
	if (const TSharedPtr<FName> SelectedDataName = DataComboBox->GetSelectedItem())
	{
		return FText::FromName(*SelectedDataName);
	}
	else
	{
		return PCGEditorGraphAttributeListView::NoDataAvailableText;
	}
}

int32 SPCGEditorGraphAttributeListView::GetSelectedDataIndex() const
{
	int32 Index = INDEX_NONE;
	if (const TSharedPtr<FName> SelectedItem = DataComboBox->GetSelectedItem())
	{
		DataComboBoxItems.Find(SelectedItem, Index);
	}

	return Index;
}

TSharedRef<ITableRow> SPCGEditorGraphAttributeListView::OnGenerateRow(PCGListviewItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SPCGListViewItemRow, OwnerTable, Item);
}

void SPCGEditorGraphAttributeListView::AddMetadataColumn(const FName& InColumnId, const int8 InValueIndex,  const TCHAR* PostFix)
{
	FString ColumnIdString = InColumnId.ToString();

	if (PostFix)
	{
		ColumnIdString.Append(PostFix);
	}

	const FName ColumnId(ColumnIdString);
	
	FPCGMetadataInfo MetadataInfo;
	MetadataInfo.MetadataId = InColumnId;
	MetadataInfo.Index = InValueIndex;
	MetadataInfos.Add(ColumnId, MetadataInfo);

	SHeaderRow::FColumn::FArguments ColumnArguments;
	ColumnArguments.ColumnId(ColumnId);
	ColumnArguments.DefaultLabel(FText::FromName(ColumnId));
	ColumnArguments.HAlignHeader(EHorizontalAlignment::HAlign_Center);
	ColumnArguments.HAlignCell(EHorizontalAlignment::HAlign_Right);
	ColumnArguments.FillWidth(1.0f);
	ListViewHeader->AddColumn(ColumnArguments);
}

void SPCGEditorGraphAttributeListView::RemoveMetadataColumns()
{
	for (auto MetadataInfo : MetadataInfos)
	{
		const FName& MetadataKey = MetadataInfo.Key;
		ListViewHeader->RemoveColumn(MetadataKey);
	}
}

#undef LOCTEXT_NAMESPACE
