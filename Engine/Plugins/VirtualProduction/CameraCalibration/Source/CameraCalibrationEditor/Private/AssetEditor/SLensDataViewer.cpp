// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLensDataViewer.h"

#include "SCameraCalibrationCurveEditorPanel.h"

#include "CurveEditor.h"
#include "ICurveEditorModule.h"
#include "ISinglePropertyView.h"
#include "LensFile.h"
#include "RichCurveEditorModel.h"
#include "SLensDataPointDialog.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "LensDataViewer"

namespace LensDataUtils
{
	const FName EncoderCategoryLabel(TEXT("Encoders"));
	const FName EncoderFocusLabel(TEXT("Focus"));
	const FName EncoderIrisLabel(TEXT("Iris"));
	const FName EncoderZoomLabel(TEXT("Zoom"));
	const FName DistortionCategoryLabel(TEXT("Distortion"));
	const FName FxLabel(TEXT("Fx"));
	const FName FyLabel(TEXT("Fy"));
	const FName MapsCategoryLabel(TEXT("STMaps"));
	const FName ImageCenterCategory(TEXT("Image Center"));
	const FName CxLabel(TEXT("Cx"));
	const FName CyLabel(TEXT("Cy"));
	const FName NodalOffsetCategoryLabel(TEXT("Nodal Offset"));
	const FName LocationXLabel(TEXT("Location - X"));
	const FName LocationYLabel(TEXT("Location - Y"));
	const FName LocationZLabel(TEXT("Location - Z"));
	const FName RotationXLabel(TEXT("Rotation - X"));
	const FName RotationYLabel(TEXT("Rotation - Y"));
	const FName RotationZLabel(TEXT("Rotation - Z"));

	template<typename Type>
	void FindFocusPoints(const TArray<Type>&SourceData, TArray<TSharedPtr<FLensDataItem>>& OutDataItems)
	{
		TSet<float> FocusPoints;
		FocusPoints.Reserve(SourceData.Num());
		OutDataItems.Reserve(SourceData.Num());
		for (const Type& Point : SourceData)
		{
			if (FocusPoints.Contains(Point.Focus) == false)
			{
				FocusPoints.Emplace(Point.Focus);
				OutDataItems.Add(MakeShared<FLensDataItem>(Point.Focus));
			}
		}
	}
}

FLensDataItem::FLensDataItem(float InFocus)
	: Focus(InFocus)
{

}

TSharedRef<ITableRow> FLensDataItem::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(SLensDataItem, InOwnerTable, AsShared());
}

void SLensDataItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FLensDataItem> InItemData)
{
	WeakItem = InItemData;

	STableRow<TSharedPtr<FLensDataItem>>::Construct(
		STableRow<TSharedPtr<FLensDataItem>>::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FocusLabel", "Focus: "))
			]
			+ SHorizontalBox::Slot()
			.Padding(5.0f, 5.0f)
			[
				SNew(STextBlock)
				.Text(this, &SLensDataItem::GetLabelText)
			]
		], OwnerTable);
}

FText SLensDataItem::GetLabelText() const
{
	return (FText::AsNumber(WeakItem.Pin()->Focus));
}

FLensDataCategoryItem::FLensDataCategoryItem(TWeakPtr<FLensDataCategoryItem> InParent, EDataCategories InCategory, FName InLabel)
	: Category(InCategory)
	, Label(InLabel)
	, Parent(InParent)
{

}

TSharedRef<ITableRow> FLensDataCategoryItem::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable)
{
	return SNew(SLensDataCategoryItem, InOwnerTable, AsShared());
}

void SLensDataCategoryItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FLensDataCategoryItem> InItemData)
{
	WeakItem = InItemData;

	STableRow<TSharedPtr<FLensDataCategoryItem>>::Construct(
		STableRow<TSharedPtr<FLensDataCategoryItem>>::FArguments()
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &SLensDataCategoryItem::GetLabelText)
		], OwnerTable);
}

FText SLensDataCategoryItem::GetLabelText() const
{
	return (FText::FromName(WeakItem.Pin()->Label));
}


void SLensDataViewer::Construct(const FArguments& InArgs, ULensFile* InLensFile)
{
	LensFile = TStrongObjectPtr<ULensFile>(InLensFile);

	//Setup curve editor
	CurveEditor = MakeShared<FCurveEditor>();
	FCurveEditorInitParams InitParams;
	CurveEditor->InitCurveEditor(InitParams);
	CurveEditor->GridLineLabelFormatXAttribute = LOCTEXT("GridXLabelFormat", "{0}");

	TUniquePtr<ICurveEditorBounds> EditorBounds = MakeUnique<FStaticCurveEditorBounds>();
	EditorBounds->SetInputBounds(0.05, 1.05);
	CurveEditor->SetBounds(MoveTemp(EditorBounds));

	// Snap only Y axis
	FCurveEditorAxisSnap SnapYAxisOnly = CurveEditor->GetAxisSnap();
	SnapYAxisOnly.RestrictedAxisList = EAxisList::Type::Y;
	CurveEditor->SetAxisSnap(SnapYAxisOnly);

	CurvePanel = SNew(SCameraCalibrationCurveEditorPanel, CurveEditor.ToSharedRef());
	CurveEditor->ZoomToFit();

	CachedFIZ = InArgs._CachedFIZData;

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeToolbarWidget(CurvePanel.ToSharedRef())
		]
		+ SVerticalBox::Slot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.4f)
			[
				MakeLensDataWidget()
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.6f)
			[
				CurvePanel.ToSharedRef()
			]
		]
	];

	Refresh();
}

TSharedPtr<FLensDataCategoryItem> SLensDataViewer::GetDataCategorySelection() const
{
	TArray<TSharedPtr<FLensDataCategoryItem>> SelectedNodes;
	TreeView->GetSelectedItems(SelectedNodes);
	if (SelectedNodes.Num())
	{
		return SelectedNodes[0];
	}
	return nullptr;
}

TSharedRef<ITableRow> SLensDataViewer::OnGenerateDataCategoryRow(TSharedPtr<FLensDataCategoryItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return Item->MakeTreeRowWidget(OwnerTable);
}

void SLensDataViewer::OnGetDataCategoryItemChildren(TSharedPtr<FLensDataCategoryItem> Item, TArray<TSharedPtr<FLensDataCategoryItem>>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

void SLensDataViewer::OnDataCategorySelectionChanged(TSharedPtr<FLensDataCategoryItem> Item, ESelectInfo::Type SelectInfo)
{
	if (!Item || SelectInfo != ESelectInfo::OnMouseClick)
	{
		return;
	}

	RefreshDataEntriesTree();
}

TSharedPtr<FLensDataItem> SLensDataViewer::GetSelectedDataEntry() const
{
	TArray<TSharedPtr<FLensDataItem>> SelectedNodes;
	DataEntriesTree->GetSelectedItems(SelectedNodes);
	if (SelectedNodes.Num())
	{
		return SelectedNodes[0];
	}
	return nullptr;
}

TSharedRef<ITableRow> SLensDataViewer::OnGenerateDataEntryRow(TSharedPtr<FLensDataItem> Node, const TSharedRef<STableViewBase>& OwnerTable)
{
	return Node->MakeTreeRowWidget(OwnerTable);
}

void SLensDataViewer::OnGetDataEntryChildren(TSharedPtr<FLensDataItem> Node, TArray<TSharedPtr<FLensDataItem>>& OutNodes)
{
	if (Node.IsValid())
	{
		OutNodes = Node->Children;
	}
}

void SLensDataViewer::OnDataEntrySelectionChanged(TSharedPtr<FLensDataItem> Node, ESelectInfo::Type SelectInfo)
{
	RefreshCurve();
}

TSharedRef<SWidget> SLensDataViewer::MakeLensDataWidget()
{
	const FSinglePropertyParams InitParams;
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	const TSharedPtr<ISinglePropertyView> DataModeWidget = PropertyEditorModule.CreateSingleProperty(
		LensFile.Get()
		, GET_MEMBER_NAME_CHECKED(ULensFile, DataMode)
		, InitParams);

	FSimpleDelegate OnDataModeChangedDelegate = FSimpleDelegate::CreateSP(this, &SLensDataViewer::OnDataModeChanged);
	DataModeWidget->SetOnPropertyValueChanged(OnDataModeChangedDelegate);

	return 
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
			[
				DataModeWidget.ToSharedRef()
			]
		]
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.FillHeight(.5f)
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FLensDataCategoryItem>>)
				.TreeItemsSource(&DataCategories)
				.ItemHeight(24.0f)
				.OnGenerateRow(this, &SLensDataViewer::OnGenerateDataCategoryRow)
				.OnGetChildren(this, &SLensDataViewer::OnGetDataCategoryItemChildren)
				.OnSelectionChanged(this, &SLensDataViewer::OnDataCategorySelectionChanged)
				.ClearSelectionOnClick(false)
			]
			
		]
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.FillHeight(.5f)
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(DataEntryNameWidget, STextBlock)
				]
				+ SVerticalBox::Slot()
				[
					SAssignNew(DataEntriesTree, STreeView<TSharedPtr<FLensDataItem>>)
					.TreeItemsSource(&DataEntries)
					.ItemHeight(24.0f)
					.OnGenerateRow(this, &SLensDataViewer::OnGenerateDataEntryRow)
					.OnGetChildren(this, &SLensDataViewer::OnGetDataEntryChildren)
					.OnSelectionChanged(this, &SLensDataViewer::OnDataEntrySelectionChanged)
					.ClearSelectionOnClick(false)
				]
			]
		];
}

TSharedRef<SWidget> SLensDataViewer::MakeToolbarWidget(TSharedRef<SCameraCalibrationCurveEditorPanel> InEditorPanel)
{
	// Curve toolbar
	FToolBarBuilder ToolBarBuilder(CurvePanel->GetCommands(), FMultiBoxCustomization::None, CurvePanel->GetToolbarExtender(), true);
	ToolBarBuilder.SetStyle(&FEditorStyle::Get(), "Sequencer.ToolBar");
	ToolBarBuilder.BeginSection("Asset");
	
	auto GetButtonWidget = [this]()
	{
		return SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
				.OnClicked(this, &SLensDataViewer::OnAddDataPointClicked)
				.IsEnabled(MakeAttributeLambda([this] { return true; }))
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.14"))
					.Text(FText::FromString(FString(TEXT("\xf067"))) /*fa-plus*/)
					.ColorAndOpacity(FLinearColor::White)
				];
	}; 

	ToolBarBuilder.AddWidget(GetButtonWidget());
		
	ToolBarBuilder.EndSection();

	return SNew(SBox)
		.Padding(FMargin(2.f, 0.f))
		[
			ToolBarBuilder.MakeWidget()
		];
}

FReply SLensDataViewer::OnAddDataPointClicked()
{
	const FSimpleDelegate OnDataPointAdded = FSimpleDelegate::CreateSP(this, &SLensDataViewer::OnLensDataPointAdded);
	SLensDataPointDialog::OpenDialog(LensFile.Get(), CachedFIZ, OnDataPointAdded);
	return FReply::Handled();
}

void SLensDataViewer::OnDataModeChanged()
{
	Refresh();
}

void SLensDataViewer::Refresh()
{
	RefreshDataCategoriesTree();
	RefreshDataEntriesTree();
}

void SLensDataViewer::RefreshDataCategoriesTree()
{
	//Builds the data category tree
	
	DataCategories.Reset();

	DataCategories.Add(MakeShared<FLensDataCategoryItem>(nullptr, EDataCategories::Focus,  LensDataUtils::EncoderFocusLabel));
	DataCategories.Add(MakeShared<FLensDataCategoryItem>(nullptr, EDataCategories::Iris, LensDataUtils::EncoderIrisLabel));
	DataCategories.Add(MakeShared<FLensDataCategoryItem>(nullptr, EDataCategories::Zoom, LensDataUtils::EncoderZoomLabel));
	
	if (LensFile->DataMode == ELensDataMode::Parameters)
	{
		TSharedPtr<FLensDataCategoryItem> DistortionEntry = MakeShared<FLensDataCategoryItem>(nullptr, EDataCategories::Distortion, LensDataUtils::DistortionCategoryLabel);
		DataCategories.Add(DistortionEntry);

		TArray<FText> Parameters;
		if (LensFile->LensInfo.LensModel)
		{
			Parameters = LensFile->LensInfo.LensModel.GetDefaultObject()->GetParameterDisplayNames();
		}

		for (const FText& Parameter : Parameters)
		{
			DistortionEntry->Children.Add(MakeShared<FLensDataCategoryItem>(DistortionEntry, EDataCategories::Distortion, *Parameter.ToString()));
		}

		DistortionEntry->Children.Add(MakeShared<FLensDataCategoryItem>(DistortionEntry, EDataCategories::Distortion, LensDataUtils::FxLabel));
		DistortionEntry->Children.Add(MakeShared<FLensDataCategoryItem>(DistortionEntry, EDataCategories::Distortion, LensDataUtils::FyLabel));
	}
	else
	{
		DataCategories.Add(MakeShared<FLensDataCategoryItem>(nullptr, EDataCategories::STMap, LensDataUtils::MapsCategoryLabel));
	}

	
	TSharedPtr<FLensDataCategoryItem> ImageCenterEntry = MakeShared<FLensDataCategoryItem>(nullptr, EDataCategories::ImageCenter, LensDataUtils::ImageCenterCategory);
	DataCategories.Add(ImageCenterEntry);
	ImageCenterEntry->Children.Add(MakeShared<FLensDataCategoryItem>(ImageCenterEntry, EDataCategories::ImageCenter, LensDataUtils::CxLabel));
	ImageCenterEntry->Children.Add(MakeShared<FLensDataCategoryItem>(ImageCenterEntry, EDataCategories::ImageCenter, LensDataUtils::CyLabel));

	TSharedPtr<FLensDataCategoryItem> NodalOffsetCategory = MakeShared<FLensDataCategoryItem>(nullptr, EDataCategories::NodalOffset, LensDataUtils::NodalOffsetCategoryLabel);
	DataCategories.Add(NodalOffsetCategory);

	NodalOffsetCategory->Children.Add(MakeShared<FLensDataCategoryItem>(NodalOffsetCategory, EDataCategories::NodalOffset, LensDataUtils::LocationXLabel));
	NodalOffsetCategory->Children.Add(MakeShared<FLensDataCategoryItem>(NodalOffsetCategory, EDataCategories::NodalOffset, LensDataUtils::LocationYLabel));
	NodalOffsetCategory->Children.Add(MakeShared<FLensDataCategoryItem>(NodalOffsetCategory, EDataCategories::NodalOffset, LensDataUtils::LocationZLabel));
	NodalOffsetCategory->Children.Add(MakeShared<FLensDataCategoryItem>(NodalOffsetCategory, EDataCategories::NodalOffset, LensDataUtils::RotationXLabel));
	NodalOffsetCategory->Children.Add(MakeShared<FLensDataCategoryItem>(NodalOffsetCategory, EDataCategories::NodalOffset, LensDataUtils::RotationYLabel));
	NodalOffsetCategory->Children.Add(MakeShared<FLensDataCategoryItem>(NodalOffsetCategory, EDataCategories::NodalOffset, LensDataUtils::RotationZLabel));

	TreeView->RequestTreeRefresh();
}

void SLensDataViewer::RefreshDataEntriesTree()
{
	DataEntries.Reset();

	if (TSharedPtr<FLensDataCategoryItem> CategoryItem = GetDataCategorySelection())
	{
		DataEntryNameWidget->SetText(FText::FromName(CategoryItem->Label));

		switch (CategoryItem->Category)
		{
			case EDataCategories::Distortion:
			{
				LensDataUtils::FindFocusPoints<FDistortionMapPoint>(LensFile->DistortionMapping, DataEntries);
				break;
			}
			case EDataCategories::ImageCenter:
			{
				LensDataUtils::FindFocusPoints<FIntrinsicMapPoint>(LensFile->IntrinsicMapping, DataEntries);
				break;
			}
			case EDataCategories::NodalOffset:
			{
				LensDataUtils::FindFocusPoints<FNodalOffsetMapPoint>(LensFile->NodalOffsetMapping, DataEntries);
				break;
			}
			case EDataCategories::STMap:
			{
				LensDataUtils::FindFocusPoints<FCalibratedMapPoint>(LensFile->CalibratedMapPoints, DataEntries);
				break;
			}
			default:
			{
				break;
			}
		}
	}
	else
	{
		DataEntryNameWidget->SetText(LOCTEXT("NoCategorySelected", "Select a category"));
	}

	//When data entries have been repopulated, refresh the tree and select first item
	DataEntriesTree->RequestListRefresh();

	if (DataEntries.Num())
	{
		DataEntriesTree->SetSelection(DataEntries[0]);
	}
	else
	{
		DataEntriesTree->SetSelection(nullptr);
	}
}

void SLensDataViewer::RefreshCurve()
{
	CurveEditor->RemoveAllCurves();
	TUniquePtr<FRichCurveEditorModelRaw> NewCurve;

	TSharedPtr<FLensDataCategoryItem> CategoryItem = GetDataCategorySelection();
	if (CategoryItem.IsValid())
	{
		switch (CategoryItem->Category)
		{
			case EDataCategories::Focus:
			{
				NewCurve = MakeUnique<FRichCurveEditorModelRaw>(&LensFile->EncoderMapping.Focus, LensFile.Get());
				break;
			}
			case EDataCategories::Iris:
			{
				NewCurve = MakeUnique<FRichCurveEditorModelRaw>(&LensFile->EncoderMapping.Iris, LensFile.Get());
				break;
			}
			case EDataCategories::Zoom:
			{
				NewCurve = MakeUnique<FRichCurveEditorModelRaw>(&LensFile->EncoderMapping.Zoom, LensFile.Get());
				break;
			}
			case EDataCategories::Distortion:
			{
				break;
			}
			case EDataCategories::ImageCenter:
			{
				break;
			}
			case EDataCategories::NodalOffset:
			{
				break;
			}
			case EDataCategories::STMap:
			{
				break;
			}
		default:
			{
				break;
			}
		}
	}

	//If a curve was setup, add it to the editor
	if (NewCurve)
	{
		NewCurve->SetShortDisplayName(FText::FromName(CategoryItem->Label));
		NewCurve->SetColor(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f));
		const FCurveModelID CurveId = CurveEditor->AddCurve(MoveTemp(NewCurve));
		CurveEditor->PinCurve(CurveId);
	}
}

void SLensDataViewer::OnLensDataPointAdded()
{
	RefreshDataEntriesTree();
}

#undef LOCTEXT_NAMESPACE /* LensDataViewer */


