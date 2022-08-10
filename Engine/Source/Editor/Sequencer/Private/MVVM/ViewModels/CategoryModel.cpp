// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVM/ViewModels/CategoryModel.h"
#include "MVVM/ViewModels/ViewModelIterators.h"
#include "MVVM/ViewModels/ChannelModel.h"
#include "MVVM/ViewModels/SequencerModelUtils.h"
#include "MVVM/Views/SOutlinerItemViewBase.h"
#include "MVVM/Views/SSequencerKeyNavigationButtons.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"

#include "Styling/AppStyle.h"

#include "ISequencerSection.h"
#include "SequencerNodeTree.h"

#define LOCTEXT_NAMESPACE "SequencerCategoryModel"

namespace UE
{
namespace Sequencer
{

FCategoryModel::FCategoryModel(FName InCategoryName)
	: Children(EViewModelListType::Generic)
	, CategoryName(InCategoryName)
{
	RegisterChildList(&Children);
}

bool FCategoryModel::IsAnimated() const
{
	for (TSharedPtr<FChannelModel> ChannelModel : GetDescendantsOfType<FChannelModel>())
	{
		if (ChannelModel->IsAnimated())
		{
			return true;
		}
	}
	return false;
}

FOutlinerSizing FCategoryModel::GetDesiredSizing() const
{
	return FOutlinerSizing(15.f + 2.f*2.f);
}

TSharedPtr<ITrackLaneWidget> FCategoryModel::CreateTrackLaneView(const FCreateTrackLaneViewParams& InParams)
{
	return nullptr;
}

FTrackLaneVirtualAlignment FCategoryModel::ArrangeVirtualTrackLaneView() const
{
	TSharedPtr<ITrackLaneExtension> Parent = FindAncestorOfType<ITrackLaneExtension>();
	return Parent ? Parent->ArrangeVirtualTrackLaneView() : FTrackLaneVirtualAlignment();
}

FCategoryGroupModel::FCategoryGroupModel(FName InCategoryName, const FText& InDisplayText)
	: CategoryName(InCategoryName)
	, DisplayText(InDisplayText)
{
	SetIdentifier(InCategoryName);
}

FCategoryGroupModel::~FCategoryGroupModel()
{
}

bool FCategoryGroupModel::IsAnimated() const
{
	for (const TWeakViewModelPtr<FCategoryModel>& WeakCategory : Categories)
	{
		TSharedPtr<FCategoryModel> Category = WeakCategory.Pin();
		if (Category && Category->IsAnimated())
		{
			return true;
		}
	}
	return false;
}

void FCategoryGroupModel::AddCategory(TWeakViewModelPtr<FCategoryModel> InCategory)
{
	if (!Categories.Contains(InCategory))
	{
		Categories.Add(InCategory);
	}
}

TArrayView<const TWeakViewModelPtr<FCategoryModel>> FCategoryGroupModel::GetCategories() const
{
	return Categories;
}

FOutlinerSizing FCategoryGroupModel::RecomputeSizing()
{
	FOutlinerSizing MaxSizing;

	for (TWeakViewModelPtr<FCategoryModel> WeakCategory : Categories)
	{
		if (TSharedPtr<FCategoryModel> Category = WeakCategory.Pin())
		{
			FOutlinerSizing Desired = Category->GetDesiredSizing();

			MaxSizing.Height = FMath::Max(MaxSizing.Height, Desired.Height);
			MaxSizing.PaddingTop = FMath::Max(MaxSizing.PaddingTop, Desired.PaddingTop);
			MaxSizing.PaddingBottom = FMath::Max(MaxSizing.PaddingBottom, Desired.PaddingBottom);
		}
	}

	ComputedSizing = MaxSizing;

	for (const TWeakViewModelPtr<FCategoryModel>& WeakCategory : Categories)
	{
		if (TSharedPtr<FCategoryModel> Category = WeakCategory.Pin())
		{
			Category->SetComputedSizing(MaxSizing);
		}
	}

	return MaxSizing;
}

FOutlinerSizing FCategoryGroupModel::GetOutlinerSizing() const
{
	return ComputedSizing;
}

FText FCategoryGroupModel::GetLabel() const
{
	return GetDisplayText();
}

FSlateFontInfo FCategoryGroupModel::GetLabelFont() const
{
	return IsAnimated()
		? FAppStyle::GetFontStyle("Sequencer.AnimationOutliner.ItalicFont")
		: FOutlinerItemModel::GetLabelFont();
}

TSharedRef<SWidget> FCategoryGroupModel::CreateOutlinerView(const FCreateOutlinerViewParams& InParams)
{
	TSharedPtr<FSequencerEditorViewModel> EditorViewModel = GetEditor();

	return SNew(SOutlinerItemViewBase, SharedThis(this), InParams.Editor, InParams.TreeViewRow)
		.CustomContent()
		[
			SNew(SBox)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SSequencerKeyNavigationButtons, SharedThis(this), EditorViewModel->GetSequencer())
				]
			]
		];
}

FTrackAreaParameters FCategoryGroupModel::GetTrackAreaParameters() const
{
	FTrackAreaParameters Parameters;
	Parameters.LaneType = ETrackAreaLaneType::None;
	return Parameters;
}

FViewModelVariantIterator FCategoryGroupModel::GetTrackAreaModelList() const
{
	return &Categories;
}

bool FCategoryGroupModel::CanDelete(FText* OutErrorMessage) const
{
	return true;
}

void FCategoryGroupModel::Delete()
{
	TArray<FName> PathFromTrack;
	TViewModelPtr<ITrackExtension> Track = GetParentTrackNodeAndNamePath(this, PathFromTrack);
	check(Track);

	Track->GetTrack()->Modify();

	for (const FViewModelPtr& Category : GetTrackAreaModelList())
	{
		if (TViewModelPtr<FSectionModel> Section = Category->FindAncestorOfType<FSectionModel>())
		{
			Section->GetSectionInterface()->RequestDeleteCategory(PathFromTrack);
		}
	}
}

} // namespace Sequencer
} // namespace UE

#undef LOCTEXT_NAMESPACE

