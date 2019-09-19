// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "SRigCurveContainer.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/STextEntryPopup.h"
#include "PropertyCustomizationHelpers.h"
#include "Framework/Commands/GenericCommands.h"
#include "RigCurveContainerCommands.h"
#include "ControlRigEditor.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "SRigCurveContainer"

static const FName ColumnId_RigCurveNameLabel( "Curve" );
static const FName ColumnID_RigCurveValueLabel( "Value" );

//////////////////////////////////////////////////////////////////////////
// SRigCurveListRow

void SRigCurveListRow::Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	Item = InArgs._Item;
	OnTextCommitted = InArgs._OnTextCommitted;
	OnSetRigCurveValue = InArgs._OnSetRigCurveValue;
	OnGetRigCurveValue = InArgs._OnGetRigCurveValue;
	OnGetFilterText = InArgs._OnGetFilterText;

	check( Item.IsValid() );

	SMultiColumnTableRow< FDisplayedRigCurveInfoPtr >::Construct( FSuperRowType::FArguments(), InOwnerTableView );
}

TSharedRef< SWidget > SRigCurveListRow::GenerateWidgetForColumn( const FName& ColumnName )
{
	if ( ColumnName == ColumnId_RigCurveNameLabel )
	{
		return
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4)
			.VAlign(VAlign_Center)
			[
				SAssignNew(Item->EditableText, SInlineEditableTextBlock)
				.OnTextCommitted(OnTextCommitted)
				.ColorAndOpacity(this, &SRigCurveListRow::GetItemTextColor)
				.IsSelected(this, &SRigCurveListRow::IsSelected)
				.Text(this, &SRigCurveListRow::GetItemName)
				.HighlightText(this, &SRigCurveListRow::GetFilterText)
			];
	}
	else if ( ColumnName == ColumnID_RigCurveValueLabel )
	{
		// Encase the SSpinbox in an SVertical box so we can apply padding. Setting ItemHeight on the containing SListView has no effect :-(
		return
			SNew( SVerticalBox )

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding( 0.0f, 1.0f )
			.VAlign( VAlign_Center )
			[
				SNew( SSpinBox<float> )
				.Value( this, &SRigCurveListRow::GetValue )
				.OnValueChanged( this, &SRigCurveListRow::OnRigCurveValueChanged )
				.OnValueCommitted( this, &SRigCurveListRow::OnRigCurveValueValueCommitted )
				.IsEnabled(false)
			];
	}

	return SNullWidget::NullWidget;
}

void SRigCurveListRow::OnRigCurveValueChanged( float NewValue )
{
	Item->Value = NewValue;

	OnSetRigCurveValue.ExecuteIfBound(Item->CurveName, NewValue);
}

void SRigCurveListRow::OnRigCurveValueValueCommitted( float NewValue, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
	{
		OnRigCurveValueChanged(NewValue);
	}
}


FText SRigCurveListRow::GetItemName() const
{
	return FText::FromName(Item->CurveName);
}

FText SRigCurveListRow::GetFilterText() const
{
	if (OnGetFilterText.IsBound())
	{
		return OnGetFilterText.Execute();
	}
	else
	{
		return FText::GetEmpty();
	}
}

FSlateColor SRigCurveListRow::GetItemTextColor() const
{
	// If row is selected, show text as black to make it easier to read
	if (IsSelected())
	{
		return FLinearColor(0, 0, 0);
	}

	return FLinearColor(1, 1, 1);
}

float SRigCurveListRow::GetValue() const 
{ 
	if (OnGetRigCurveValue.IsBound())
	{
		return OnGetRigCurveValue.Execute(Item->CurveName);
	}

	return 0.f;
}

//////////////////////////////////////////////////////////////////////////
// SRigCurveContainer

void SRigCurveContainer::Construct(const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor)
{
	ControlRigEditor = InControlRigEditor;
	ControlRigBlueprint = InControlRigEditor.Get().GetControlRigBlueprint();
	bIsChangingRigHierarchy = false;
	ControlRigBlueprint->HierarchyContainer.OnElementAdded.AddRaw(this, &SRigCurveContainer::OnRigElementAdded);
	ControlRigBlueprint->HierarchyContainer.OnElementRemoved.AddRaw(this, &SRigCurveContainer::OnRigElementRemoved);
	ControlRigBlueprint->HierarchyContainer.OnElementRenamed.AddRaw(this, &SRigCurveContainer::OnRigElementRenamed);
	ControlRigBlueprint->HierarchyContainer.OnElementSelected.AddRaw(this, &SRigCurveContainer::OnRigElementSelected);

	// Register and bind all our menu commands
	FCurveContainerCommands::Register();
	BindCommands();

	ChildSlot
	[
		SNew( SVerticalBox )
		
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0,2)
		[
			SNew(SHorizontalBox)
			// Filter entry
			+SHorizontalBox::Slot()
			.FillWidth( 1 )
			[
				SAssignNew( NameFilterBox, SSearchBox )
				.SelectAllTextWhenFocused( true )
				.OnTextChanged( this, &SRigCurveContainer::OnFilterTextChanged )
				.OnTextCommitted( this, &SRigCurveContainer::OnFilterTextCommitted )
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight( 1.0f )		// This is required to make the scrollbar work, as content overflows Slate containers by default
		[
			SAssignNew( RigCurveListView, SRigCurveListType )
			.ListItemsSource( &RigCurveList )
			.OnGenerateRow( this, &SRigCurveContainer::GenerateRigCurveRow )
			.OnContextMenuOpening( this, &SRigCurveContainer::OnGetContextMenuContent )
			.ItemHeight( 22.0f )
			.SelectionMode(ESelectionMode::Multi)
			.OnSelectionChanged( this, &SRigCurveContainer::OnSelectionChanged )
			.HeaderRow
			(
				SNew( SHeaderRow )
				+ SHeaderRow::Column( ColumnId_RigCurveNameLabel )
				.FillWidth(1.f)
				.DefaultLabel( LOCTEXT( "RigCurveNameLabel", "Curve" ) )

				+ SHeaderRow::Column( ColumnID_RigCurveValueLabel )
				.FillWidth(1.f)
				.DefaultLabel( LOCTEXT( "RigCurveValueLabel", "Value" ) )
			)
		]
	];

	CreateRigCurveList();
}

SRigCurveContainer::~SRigCurveContainer()
{
	if (ControlRigEditor.IsValid())
	{
		ControlRigBlueprint = ControlRigEditor.Pin()->GetControlRigBlueprint();
		if (ControlRigBlueprint.IsValid())
		{
			ControlRigBlueprint->HierarchyContainer.OnElementAdded.RemoveAll(this);
			ControlRigBlueprint->HierarchyContainer.OnElementRemoved.RemoveAll(this);
			ControlRigBlueprint->HierarchyContainer.OnElementRenamed.RemoveAll(this);
			ControlRigBlueprint->HierarchyContainer.OnElementSelected.RemoveAll(this);
		}
	}
}

FReply SRigCurveContainer::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (UICommandList.IsValid() && UICommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SRigCurveContainer::BindCommands()
{
	// This should not be called twice on the same instance
	check(!UICommandList.IsValid());

	UICommandList = MakeShareable(new FUICommandList);

	FUICommandList& CommandList = *UICommandList;

	// Grab the list of menu commands to bind...
	const FCurveContainerCommands& MenuActions = FCurveContainerCommands::Get();

	// ...and bind them all

	CommandList.MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SRigCurveContainer::OnRenameClicked),
		FCanExecuteAction::CreateSP(this, &SRigCurveContainer::CanRename));

	CommandList.MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SRigCurveContainer::OnDeleteNameClicked),
		FCanExecuteAction::CreateSP(this, &SRigCurveContainer::CanDelete));

	CommandList.MapAction(
		MenuActions.AddCurve,
		FExecuteAction::CreateSP(this, &SRigCurveContainer::OnAddClicked),
		FCanExecuteAction());
}

void SRigCurveContainer::OnPreviewMeshChanged(class USkeletalMesh* OldPreviewMesh, class USkeletalMesh* NewPreviewMesh)
{
	RefreshCurveList();
}

void SRigCurveContainer::OnFilterTextChanged( const FText& SearchText )
{
	FilterText = SearchText;

	RefreshCurveList();
}

void SRigCurveContainer::OnFilterTextCommitted( const FText& SearchText, ETextCommit::Type CommitInfo )
{
	// Just do the same as if the user typed in the box
	OnFilterTextChanged( SearchText );
}

TSharedRef<ITableRow> SRigCurveContainer::GenerateRigCurveRow(FDisplayedRigCurveInfoPtr InInfo, const TSharedRef<STableViewBase>& OwnerTable)
{
	check( InInfo.IsValid() );

	return
		SNew( SRigCurveListRow, OwnerTable)
		.Item( InInfo )
		.OnTextCommitted(this, &SRigCurveContainer::OnNameCommitted, InInfo)
		.OnSetRigCurveValue(this, &SRigCurveContainer::SetCurveValue)
		.OnGetRigCurveValue(this, &SRigCurveContainer::GetCurveValue)
		.OnGetFilterText(this, &SRigCurveContainer::GetFilterText);
}

TSharedPtr<SWidget> SRigCurveContainer::OnGetContextMenuContent() const
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder( bShouldCloseWindowAfterMenuSelection, UICommandList);

	const FCurveContainerCommands& Actions = FCurveContainerCommands::Get();

	MenuBuilder.BeginSection("RigCurveAction", LOCTEXT( "CurveAction", "Curve Actions" ) );

	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename, NAME_None, LOCTEXT("RenameSmartNameLabel", "Rename Curve"), LOCTEXT("RenameSmartNameToolTip", "Rename the selected curve"));
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete, NAME_None, LOCTEXT("DeleteSmartNameLabel", "Delete Curve"), LOCTEXT("DeleteSmartNameToolTip", "Delete the selected curve"));
	MenuBuilder.AddMenuEntry(Actions.AddCurve);
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddSubMenu(
		LOCTEXT("ImportSubMenu", "Import"),
		LOCTEXT("ImportSubMenu_ToolTip", "Import curves to the current rig. This only imports non-existing curve."),
		FNewMenuDelegate::CreateSP(const_cast<SRigCurveContainer*>(this), &SRigCurveContainer::CreateImportMenu)
	);


	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SRigCurveContainer::OnRenameClicked()
{
	TArray< FDisplayedRigCurveInfoPtr > SelectedItems = RigCurveListView->GetSelectedItems();

	SelectedItems[0]->EditableText->EnterEditingMode();
}

bool SRigCurveContainer::CanRename()
{
	return RigCurveListView->GetNumItemsSelected() == 1;
}

void SRigCurveContainer::OnAddClicked()
{
	TSharedRef<STextEntryPopup> TextEntry =
		SNew(STextEntryPopup)
		.Label(LOCTEXT("NewSmartnameLabel", "New Name"))
		.OnTextCommitted(this, &SRigCurveContainer::CreateNewNameEntry);

	FSlateApplication& SlateApp = FSlateApplication::Get();
	SlateApp.PushMenu(
		AsShared(),
		FWidgetPath(),
		TextEntry,
		SlateApp.GetCursorPos(),
		FPopupTransitionEffect::TypeInPopup
		);
}


void SRigCurveContainer::CreateNewNameEntry(const FText& CommittedText, ETextCommit::Type CommitType)
{
	FSlateApplication::Get().DismissAllMenus();
	if (!CommittedText.IsEmpty() && CommitType == ETextCommit::OnEnter)
	{
		FRigCurveContainer* Container = GetCurveContainer();
		if (Container)
		{
			TGuardValue<bool> GuardReentry(bIsChangingRigHierarchy, true);

			FName NewName = FName(*CommittedText.ToString());
			Container->Add(NewName);
			Container->ClearSelection();
			Container->Select(NewName);
		}

		FSlateApplication::Get().DismissAllMenus();
		RefreshCurveList();
	}
}

void SRigCurveContainer::CreateRigCurveList( const FString& SearchText )
{
	const FRigCurveContainer* Container = GetCurveContainer();
	if (Container)
	{
		RigCurveList.Reset();

		// Iterate through all curves..
		for (const FRigCurve& Curve : (*Container))
		{
			FString CurveString = Curve.Name.ToString();

			// See if we pass the search filter
			if (!FilterText.IsEmpty() && !CurveString.Contains(*FilterText.ToString()))
			{
				continue;
			}

			TSharedRef<FDisplayedRigCurveInfo> NewItem = FDisplayedRigCurveInfo::Make(Curve.Name);
			RigCurveList.Add(NewItem);
		}

		// Sort final list
		struct FSortNamesAlphabetically
		{
			bool operator()(const FDisplayedRigCurveInfoPtr& A, const FDisplayedRigCurveInfoPtr& B) const
			{
				return (A.Get()->CurveName.Compare(B.Get()->CurveName) < 0);
			}
		};

		RigCurveList.Sort(FSortNamesAlphabetically());
	}
	RigCurveListView->RequestListRefresh();

	if (Container)
	{
		for (const FName& SelectedCurve : Container->CurrentSelection())
		{
			OnRigElementSelected(&ControlRigBlueprint->HierarchyContainer, FRigElementKey(SelectedCurve, ERigElementType::Curve), true);
		}
	}

}

void SRigCurveContainer::RefreshCurveList()
{
	CreateRigCurveList(FilterText.ToString());
}

void SRigCurveContainer::OnNameCommitted(const FText& InNewName, ETextCommit::Type CommitType, FDisplayedRigCurveInfoPtr Item)
{
	FRigCurveContainer* Container = GetCurveContainer();
	if (Container)
	{
		if (CommitType == ETextCommit::OnEnter)
		{
			FName NewName = FName(*InNewName.ToString());
			FName OldName = Item->CurveName;
			Container->Rename(OldName, NewName);
		}
	}
}

void SRigCurveContainer::OnDeleteNameClicked()
{
	FRigCurveContainer* Container = GetCurveContainer();
	if (Container)
	{
		TArray< FDisplayedRigCurveInfoPtr > SelectedItems = RigCurveListView->GetSelectedItems();
		for (auto Item : SelectedItems)
		{
			Container->Remove(Item->CurveName);
		}
	}
}

bool SRigCurveContainer::CanDelete()
{
	return RigCurveListView->GetNumItemsSelected() > 0;
}

void SRigCurveContainer::SetCurveValue(const FName& CurveName, float CurveValue)
{
	FRigCurveContainer* Container = GetCurveContainer();
	if (Container)
	{
		Container->SetValue(CurveName, CurveValue);
	}
}

float SRigCurveContainer::GetCurveValue(const FName& CurveName)
{
	FRigCurveContainer* Container = GetInstanceCurveContainer();
	if (Container)
	{
		return Container->GetValue(CurveName);
	}

	return 0.f;
}

void SRigCurveContainer::ChangeCurveName(const FName& OldName, const FName& NewName)
{
	TGuardValue<bool> GuardReentry(bIsChangingRigHierarchy, true);

	FRigCurveContainer* Container = GetCurveContainer();
	if (Container)
	{
		Container->Rename(OldName, NewName);
	}
}

void SRigCurveContainer::OnSelectionChanged(FDisplayedRigCurveInfoPtr Selection, ESelectInfo::Type SelectInfo)
{
	if (bIsChangingRigHierarchy)
	{
		return;
	}

	FRigCurveContainer* Container = GetCurveContainer();

	if (Container)
	{
		TGuardValue<bool> GuardRigHierarchyChanges(bIsChangingRigHierarchy, true);

		TArray<FName> OldSelection = Container->CurrentSelection();
		TArray<FName> NewSelection;

		TArray<FDisplayedRigCurveInfoPtr> SelectedItems = RigCurveListView->GetSelectedItems();
		for (const FDisplayedRigCurveInfoPtr& SelectedItem : SelectedItems)
		{
			NewSelection.Add(SelectedItem->CurveName);
		}

		for (const FName& PreviouslySelected : OldSelection)
		{
			if (NewSelection.Contains(PreviouslySelected))
			{
				continue;
			}
			Container->Select(PreviouslySelected, false);
		}

		for (const FName& NewlySelected : NewSelection)
		{
			Container->Select(NewlySelected, true);
		}
	}
}

void SRigCurveContainer::OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if (bIsChangingRigHierarchy || InKey.Type != ERigElementType::Curve)
	{
		return;
	}
	RefreshCurveList();
}

void SRigCurveContainer::OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey)
{
	if (bIsChangingRigHierarchy || InKey.Type != ERigElementType::Curve)
	{
		return;
	}
	RefreshCurveList();
}

void SRigCurveContainer::OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName)
{
	if (bIsChangingRigHierarchy || ElementType != ERigElementType::Curve)
	{
		return;
	}
	RefreshCurveList();
}

void SRigCurveContainer::OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected)
{
	if (bIsChangingRigHierarchy || InKey.Type != ERigElementType::Curve)
	{
		return;
	}

	for(const FDisplayedRigCurveInfoPtr& Item : RigCurveList)
	{
		if (Item->CurveName == InKey.Name)
		{
			RigCurveListView->SetItemSelection(Item, bSelected);
			break;
		}
	}
}

void SRigCurveContainer::CreateImportMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddWidget(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3)
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("ControlRig.Curve.Menu"))
			.Text(LOCTEXT("ImportMesh_Title", "Select Mesh"))
			.ToolTipText(LOCTEXT("ImportMesh_Tooltip", "Select Mesh to import Curve from... It will only import if the node doens't exists in the current Curve."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3)
		[
			SNew(SObjectPropertyEntryBox)
			.OnShouldFilterAsset(this, &SRigCurveContainer::ShouldFilterOnImport)
			.OnObjectChanged(this, &SRigCurveContainer::ImportCurve)
		]
		,
		FText()
		);
}

bool SRigCurveContainer::ShouldFilterOnImport(const FAssetData& AssetData) const
{
	return (AssetData.AssetClass != USkeletalMesh::StaticClass()->GetFName() &&
		AssetData.AssetClass != USkeleton::StaticClass()->GetFName());
}

void SRigCurveContainer::ImportCurve(const FAssetData& InAssetData)
{
	FRigCurveContainer* Container = GetCurveContainer();
	if (Container)
	{
		const USkeleton* Skeleton = nullptr;
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(InAssetData.GetAsset()))
		{
			Skeleton = Mesh->Skeleton;
			ControlRigBlueprint->SourceCurveImport = Mesh;
		}
		else 
		{
			Skeleton = Cast<USkeleton>(InAssetData.GetAsset());
			ControlRigBlueprint->SourceCurveImport = Skeleton;
		}

		if (Skeleton)
		{
			FScopedTransaction Transaction(LOCTEXT("CurveImport", "Import Curve"));
			ControlRigBlueprint->Modify();
				
			const FSmartNameMapping* SmartNameMapping = Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName);

			Container->ClearSelection();

			TArray<FName> NameArray;
			SmartNameMapping->FillNameArray(NameArray);
			for (int32 Index = 0; Index < NameArray.Num() ; ++Index)
			{
				TGuardValue<bool> GuardReentry(bIsChangingRigHierarchy, true);
				Container->Add(NameArray[Index]);
			}

			for (int32 Index = 0; Index < NameArray.Num(); ++Index)
			{
				Container->Select(NameArray[Index]);
			}

			FSlateApplication::Get().DismissAllMenus();
			RefreshCurveList();
		}
	}
}

FRigCurveContainer* SRigCurveContainer::GetInstanceCurveContainer() const
{
	if (ControlRigEditor.IsValid())
	{
		UControlRig* ControlRig = ControlRigEditor.Pin()->GetInstanceRig();
		if (ControlRig)
		{
			return &ControlRig->Hierarchy.CurveContainer;
		}
	}

	return nullptr;
}

FRigCurveContainer* SRigCurveContainer::GetCurveContainer() const
{
	if (ControlRigBlueprint.IsValid())
	{
		return &ControlRigBlueprint->HierarchyContainer.CurveContainer;
	}

	return nullptr;
}
#undef LOCTEXT_NAMESPACE
