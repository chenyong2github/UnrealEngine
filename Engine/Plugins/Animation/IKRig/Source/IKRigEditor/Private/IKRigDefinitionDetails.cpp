// Copyright Epic Games, Inc. All Rights Reservekd.

#include "IKRigDefinitionDetails.h"
#include "Widgets/Input/SButton.h"
#include "AssetData.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailsView.h"

#include "IKRigDefinition.h"
#include "IKRigController.h"
#include "IKRigSolverDefinition.h"

#include "ScopedTransaction.h"
#include "PropertyCustomizationHelpers.h"
#include "Kismet2/SClassPickerDialog.h"
#include "ClassViewerFilter.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE	"IKRigDefinitionDetails"

TSharedRef<IDetailCustomization> FIKRigDefinitionDetails::MakeInstance()
{
	return MakeShareable(new FIKRigDefinitionDetails);
}

FIKRigDefinitionDetails::~FIKRigDefinitionDetails()
{
	if (ObjectChangedDelegate.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectChangedDelegate);
	}
}
void FIKRigDefinitionDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	DetailBuilderWeakPtr = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FIKRigDefinitionDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TArray< TWeakObjectPtr<UObject> >& SelectedObjectsList = DetailBuilder.GetSelectedObjects();
	TArray< TWeakObjectPtr<UIKRigDefinition> > SelectedIKRigDefinitions;

	for (auto SelectionIt = SelectedObjectsList.CreateConstIterator(); SelectionIt; ++SelectionIt)
	{
		if (UIKRigDefinition* TestIKRigDefinition = Cast<UIKRigDefinition>(SelectionIt->Get()))
		{
			SelectedIKRigDefinitions.Add(TestIKRigDefinition);
		}
	}

	// we only support 1 asset for now
	if (SelectedIKRigDefinitions.Num() > 1)
	{
		return;
	}

	IKRigDefinition = SelectedIKRigDefinitions[0];

	if (!IKRigDefinition.IsValid())
	{
		return;
	}

	// create controller
	if (!IKRigController.IsValid())
	{
		IKRigController = TStrongObjectPtr<UIKRigController>(NewObject<UIKRigController>());
	}
	
	IKRigController->SetIKRigDefinition(IKRigDefinition.Get());

	ObjectChangedDelegate = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FIKRigDefinitionDetails::OnObjectPostEditChange);
	/////////////////////////////////////////////////////////////////////////////////
	// skeleton set up
	/////////////////////////////////////////////////////////////////////////////////
	IDetailCategoryBuilder& HierarchyCategory = DetailBuilder.EditCategory("Hierarchy");
 
	SelectedAsset = IKRigDefinition->SourceAsset.Get();

	HierarchyCategory.AddCustomRow(FText::FromString("ChangeSkeleton"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("SelectSourceSkeleton", "Souce Skeleton"))
	]
	.ValueContent()
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor::Gray) // Darken the outer border
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(2, 2)
			[
				SNew(SBox)
				.WidthOverride(300)
				[
					SNew(SObjectPropertyEntryBox)
					.ObjectPath(this, &FIKRigDefinitionDetails::GetCurrentSourceAsset)
					.OnShouldFilterAsset(this, &FIKRigDefinitionDetails::ShouldFilterAsset)
					.OnObjectChanged(this, &FIKRigDefinitionDetails::OnAssetSelected)
					.AllowClear(false)
					.DisplayUseSelected(true)
					.DisplayBrowse(true)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(2, 2)
			[
				SNew(SButton)
				.ContentPadding(3)
				.IsEnabled(this, &FIKRigDefinitionDetails::CanImport)
				.OnClicked(this, &FIKRigDefinitionDetails::OnImportHierarchy)
				.ToolTipText(LOCTEXT("OnImportHierarchyTooltip", "Change Skeleton Data with Selected Asset. This replaces existing skeleton."))
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("UpdateHierarchyTitle", "Update"))
				]
			]
		]
	];

 	IDetailCategoryBuilder& SolverCategory = DetailBuilder.EditCategory("Solver");

	SolverCategory.AddCustomRow(FText::FromString("AddSolver"))
	.NameContent()
	[
		SNullWidget::NullWidget
	]
	.ValueContent()
	[
		SNew(SButton)
		.ContentPadding(3)
		.OnClicked(this, &FIKRigDefinitionDetails::OnShowClassPicker)
		.ToolTipText(LOCTEXT("OnShowSolverListTooltip", "Select Solver to Add"))
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("ShowSolverList", "Add Solver"))
		]
	];

	GoalPropertyHandle = DetailBuilder.GetProperty(TEXT("IKGoals"));

	TArray<FName> GoalNames;
	IKRigController->QueryGoals(GoalNames);

	GoalListNames.SetNumZeroed(GoalNames.Num());

	for (int32 Index = 0; Index < GoalNames.Num(); ++Index)
	{
		GoalListNames[Index] = MakeShareable(new FGoalNameListItem(GoalNames[Index]));
	}

	// I need to think about goal modified event OR just IKRigAssetMOdified event to update this
	// for now i'm commenting it out
// 	IDetailCategoryBuilder& GoalPropertyGroup = DetailBuilder.EditCategory("Goals");
// 	GoalPropertyGroup.AddCustomRow(LOCTEXT("GoalsTitleLabel", "Goals"))
// 	.NameContent()
// 	[
// 		GoalPropertyHandle->CreatePropertyNameWidget()
// 	]
// 	.ValueContent()
// 	[
// 		SAssignNew(GoalListView, SListView<FGoalNameListItemPtr>)
// 		.ListItemsSource(&GoalListNames)
// 		.OnGenerateRow(this, &FIKRigDefinitionDetails::OnGenerateWidgetForGoals)
// 	];

//	GoalPropertyHandle->MarkHiddenByCustomization();
}

class FIKSolverClassFilter : public IClassViewerFilter
{
public:
	/** All children of these classes will be included unless filtered out by another setting. */
	TSet< const UClass* > AllowedChildrenOfClasses;

	TSet< const UClass* > DisallowedClasses;

	/** Disallowed class flags. */
	EClassFlags DisallowedClassFlags;

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return !InClass->HasAnyClassFlags(DisallowedClassFlags) && InFilterFuncs->IfInClassesSet(DisallowedClasses, InClass) == EFilterReturn::Failed
			&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) != EFilterReturn::Failed;
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return !InUnloadedClassData->HasAnyClassFlags(DisallowedClassFlags) && InFilterFuncs->IfInClassesSet(DisallowedClasses, InUnloadedClassData) == EFilterReturn::Failed
			&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InUnloadedClassData) != EFilterReturn::Failed;
	}
};

FReply FIKRigDefinitionDetails::OnShowClassPicker()
{
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.bShowObjectRootClass = false;
	Options.bExpandRootNodes = true;
	Options.bShowUnloadedBlueprints = true;
	TSharedPtr<FIKSolverClassFilter> Filter = MakeShareable(new FIKSolverClassFilter);
	Options.ClassFilter = Filter;

	Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Transient;
	Filter->AllowedChildrenOfClasses.Add(UIKRigSolverDefinition::StaticClass());
	Filter->DisallowedClasses.Add(UIKRigSolverDefinition::StaticClass());

	const FText TitleText = LOCTEXT("SelectSolverClass", "Select Solver Class");
	UClass* ChosenClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, UIKRigSolverDefinition::StaticClass());

	if (bPressedOk)
	{
		IKRigController->AddSolver(ChosenClass);
	}

	return FReply::Handled();
}

bool FIKRigDefinitionDetails::CanImport() const
{
	return (SelectedAsset.IsValid());
}

FString FIKRigDefinitionDetails::GetCurrentSourceAsset() const
{
	return GetPathNameSafe(SelectedAsset.IsValid()? SelectedAsset.Get() : nullptr);
}

bool FIKRigDefinitionDetails::ShouldFilterAsset(const FAssetData& AssetData)
{
	return (AssetData.AssetClass != USkeletalMesh::StaticClass()->GetFName() && AssetData.AssetClass != USkeleton::StaticClass()->GetFName());
}

void FIKRigDefinitionDetails::OnAssetSelected(const FAssetData& AssetData)
{
	SelectedAsset = AssetData.GetAsset();
}

FReply FIKRigDefinitionDetails::OnImportHierarchy()
{
	if (SelectedAsset.IsValid())
	{
		FScopedTransaction Transaction(LOCTEXT("UpdateSkeleton", "Update Skeleton"));
		IKRigDefinition->Modify();

		const FReferenceSkeleton* RefSkeleton = nullptr;
		if (SelectedAsset->IsA(USkeleton::StaticClass()))
		{
			IKRigDefinition->SourceAsset = SelectedAsset.Get();
			RefSkeleton = &(CastChecked<USkeleton>(SelectedAsset)->GetReferenceSkeleton());
		}
		else if (SelectedAsset->IsA(USkeletalMesh::StaticClass()))
		{
			IKRigDefinition->SourceAsset = SelectedAsset.Get();
			RefSkeleton = &(CastChecked<USkeletalMesh>(SelectedAsset)->GetRefSkeleton());
		}

		if (RefSkeleton)
		{
			IKRigController->SetSkeleton(*RefSkeleton);
		}

		// Raw because we don't want to keep alive the details builder when calling the force refresh details
		IDetailLayoutBuilder* DetailLayoutBuilder = DetailBuilderWeakPtr.Pin().Get();
		if (DetailLayoutBuilder)
		{
			DetailLayoutBuilder->ForceRefreshDetails();
		}
	}

	return FReply::Handled();
}

void FIKRigDefinitionDetails::OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& InPropertyChangedEvent)
{
// 	if (Object == IKRigDefinition || Object->GetOuter())
// 	{
// 		IDetailLayoutBuilder* DetailLayoutBuilder = DetailBuilderWeakPtr.Pin().Get();
// 		if (DetailLayoutBuilder)
// 		{
// 			DetailLayoutBuilder->ForceRefreshDetails();
// 		}
// 	}
}

TSharedRef<ITableRow> FIKRigDefinitionDetails::OnGenerateWidgetForGoals(FGoalNameListItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<FGoalNameListItemPtr>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SEditableTextBox)
				.Text(this, &FIKRigDefinitionDetails::GetGoalNameText, InItem)
				.OnTextCommitted(this, &FIKRigDefinitionDetails::HandleGoalNameChanged, InItem)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

void FIKRigDefinitionDetails::HandleGoalNameChanged(const FText& NewName, ETextCommit::Type CommitType, FGoalNameListItemPtr InItem)
{
	//if (CommitType == ETextCommit::OnEnter)
	if (IKRigController.IsValid())
	{
		if (!NewName.IsEmptyOrWhitespace())
		{
			const FName NewFName = FName(*NewName.ToString());
			if (InItem->DisplayName != NewFName)
			{
				IKRigController->RenameGoal(InItem->GoalName, NewFName);
				InItem->GoalName = NewFName; // if you rename to the same as others, you'll reduce the number of goals
				InItem->DisplayName = NewFName;

				// refresh?

			}
		}
	}
}

FText FIKRigDefinitionDetails::GetGoalNameText(FGoalNameListItemPtr InItem) const
{
	return FText::FromName(InItem->DisplayName);
}
#undef LOCTEXT_NAMESPACE
