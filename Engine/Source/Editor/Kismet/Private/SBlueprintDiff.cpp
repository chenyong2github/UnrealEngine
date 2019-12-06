// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SBlueprintDiff.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SOverlay.h"
#include "Engine/GameViewportClient.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SSpacer.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "Animation/AnimBlueprint.h"
#include "K2Node_MathExpression.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorModes.h"
#include "DetailsDiff.h"
#include "EdGraphUtilities.h"
#include "GraphDiffControl.h"
#include "SMyBlueprint.h"
#include "SCSDiff.h"
#include "WorkflowOrientedApp/SModeWidget.h"
#include "Framework/Commands/GenericCommands.h"
#include "WidgetBlueprint.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Application/SlateApplication.h"

#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "SBlueprintDif"

class IDiffControl
{
public:
	virtual ~IDiffControl() {}

	/** Adds widgets to the tree of differences to show */
	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) = 0;
};

FText RightRevision = LOCTEXT("OlderRevisionIdentifier", "Right Revision");

typedef TMap< FName, const FProperty* > FNamePropertyMap;

const FName BlueprintTypeMode = FName(TEXT("BlueprintTypeMode"));
const FName MyBlueprintMode = FName(TEXT("MyBlueprintMode"));
const FName DefaultsMode = FName(TEXT("DefaultsMode"));
const FName ClassSettingsMode = FName(TEXT("ClassSettingsMode"));
const FName ComponentsMode = FName(TEXT("ComponentsMode"));
const FName GraphMode = FName(TEXT("GraphMode"));

TSharedRef<SWidget>	FDiffResultItem::GenerateWidget() const
{
	FText ToolTip = Result.ToolTip;
	FLinearColor Color = Result.DisplayColor;
	FText Text = Result.DisplayString;
	if (Text.IsEmpty())
	{
		Text = LOCTEXT("DIF_UnknownDiff", "Unknown Diff");
		ToolTip = LOCTEXT("DIF_Confused", "There is an unspecified difference");
	}
	return SNew(STextBlock)
		.ToolTipText(ToolTip)
		.ColorAndOpacity(Color)
		.Text(Text);
}

static TSharedRef<SWidget> GenerateObjectDiffWidget(FSingleObjectDiffEntry DiffEntry, FText ObjectName)
{
	return SNew(STextBlock)
		.Text(DiffViewUtils::PropertyDiffMessage(DiffEntry, ObjectName))
		.ToolTipText(DiffViewUtils::PropertyDiffMessage(DiffEntry, ObjectName))
		.ColorAndOpacity(DiffViewUtils::Differs());
}

static TSharedRef<SWidget> GenerateSimpleDiffWidget(FText DiffText)
{
	return SNew(STextBlock)
		.Text(DiffText)
		.ToolTipText(DiffText)
		.ColorAndOpacity(DiffViewUtils::Differs());
};

/** Shows all differences for the blueprint structure itself that aren't picked up elsewhere */
class FMyBlueprintDiffControl : public TSharedFromThis<FMyBlueprintDiffControl>, public IDiffControl
{
public:
	FMyBlueprintDiffControl(const UBlueprint* InOldBlueprint, const UBlueprint* InNewBlueprint, FOnDiffEntryFocused InSelectionCallback)
		: SelectionCallback(MoveTemp(InSelectionCallback)), OldBlueprint(InOldBlueprint), NewBlueprint(InNewBlueprint)
	{
	}

	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override
	{
		TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> > Children;

		if (OldBlueprint && NewBlueprint)
		{
			for (TFieldIterator<FProperty> PropertyIt(OldBlueprint->SkeletonGeneratedClass); PropertyIt; ++PropertyIt)
			{
				FProperty* OldProperty = *PropertyIt;
				FProperty* NewProperty = NewBlueprint->SkeletonGeneratedClass->FindPropertyByName(OldProperty->GetFName());

				FText PropertyText = FText::FromString(OldProperty->GetAuthoredName());

				if (NewProperty)
				{
					const int32 OldVarIndex = FBlueprintEditorUtils::FindNewVariableIndex(OldBlueprint, OldProperty->GetFName());
					const int32 NewVarIndex = FBlueprintEditorUtils::FindNewVariableIndex(NewBlueprint, OldProperty->GetFName());

					if (OldVarIndex != INDEX_NONE && NewVarIndex != INDEX_NONE)
					{
						TArray<FSingleObjectDiffEntry> DifferingProperties;
						DiffUtils::CompareUnrelatedStructs(FBPVariableDescription::StaticStruct(), &OldBlueprint->NewVariables[OldVarIndex], FBPVariableDescription::StaticStruct(), &NewBlueprint->NewVariables[NewVarIndex], DifferingProperties);
						for (const FSingleObjectDiffEntry& Difference : DifferingProperties)
						{
							TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
								SelectionCallback,
								FGenerateDiffEntryWidget::CreateStatic(&GenerateObjectDiffWidget, Difference, PropertyText));
							Children.Push(Entry);
							OutRealDifferences.Push(Entry);
						}
					}	
				}
				else
				{
					FText DiffText = FText::Format(LOCTEXT("VariableRemoved", "Removed Variable {0}"), PropertyText);

					TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
						SelectionCallback,
						FGenerateDiffEntryWidget::CreateStatic(&GenerateSimpleDiffWidget, DiffText));

					Children.Push(Entry);
					OutRealDifferences.Push(Entry);
				}
			}

			for (TFieldIterator<FProperty> PropertyIt(NewBlueprint->SkeletonGeneratedClass); PropertyIt; ++PropertyIt)
			{
				FProperty* NewProperty = *PropertyIt;
				FProperty* OldProperty = OldBlueprint->SkeletonGeneratedClass->FindPropertyByName(NewProperty->GetFName());

				if (!OldProperty)
				{
					FText DiffText = FText::Format(LOCTEXT("VariableAdded", "Added Variable {0}"), FText::FromString(NewProperty->GetAuthoredName()));

					TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
						SelectionCallback,
						FGenerateDiffEntryWidget::CreateStatic(&GenerateSimpleDiffWidget, DiffText));

					Children.Push(Entry);
					OutRealDifferences.Push(Entry);
				}
			}
		}
		const bool bHasDifferences = Children.Num() != 0;
		if (!bHasDifferences)
		{
			// make one child informing the user that there are no differences:
			Children.Push(FBlueprintDifferenceTreeEntry::NoDifferencesEntry());
		}

		OutTreeEntries.Push(FBlueprintDifferenceTreeEntry::CreateCategoryEntry(
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "MyBlueprintLabel", "My Blueprint"),
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "MyBlueprintTooltip", "The list of changes made to blueprint structure in the My Blueprint panel"),
			SelectionCallback,
			Children,
			bHasDifferences
		));
	}

private:
	FOnDiffEntryFocused SelectionCallback;
	const UBlueprint* OldBlueprint;
	const UBlueprint* NewBlueprint;
};

/** 
 * Each difference in the tree will either be a tree node that is added in one Blueprint 
 * or a tree node and an FName of a property that has been added or edited in one Blueprint
 */
class FSCSDiffControl : public TSharedFromThis<FSCSDiffControl>, public IDiffControl
{
public:
	FSCSDiffControl(const UBlueprint* InOldBlueprint, const UBlueprint* InNewBlueprint, FOnDiffEntryFocused InSelectionCallback)
		: SelectionCallback(InSelectionCallback)
		, OldSCS(InOldBlueprint)
		, NewSCS(InNewBlueprint)
	{
	}

	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override
	{
		TArray< FSCSResolvedIdentifier > OldHierarchy = OldSCS.GetDisplayedHierarchy();
		TArray< FSCSResolvedIdentifier > NewHierarchy = NewSCS.GetDisplayedHierarchy();
		DiffUtils::CompareUnrelatedSCS(OldSCS.GetBlueprint(), OldHierarchy, NewSCS.GetBlueprint(), NewHierarchy, DifferingProperties);

		const auto FocusSCSDifferenceEntry = [](FSCSDiffEntry Entry, FOnDiffEntryFocused InSelectionCallback, FSCSDiffControl* Owner)
		{
			InSelectionCallback.ExecuteIfBound();
			if (Entry.TreeIdentifier.Name != NAME_None)
			{
				Owner->OldSCS.HighlightProperty(Entry.TreeIdentifier.Name, FPropertyPath());
				Owner->NewSCS.HighlightProperty(Entry.TreeIdentifier.Name, FPropertyPath());
			}
		};

		const auto CreateSCSDifferenceWidget = [](FSCSDiffEntry Entry, FText ObjectName) -> TSharedRef<SWidget>
		{
			return SNew(STextBlock)
				.Text(DiffViewUtils::SCSDiffMessage(Entry, ObjectName))
				.ColorAndOpacity(DiffViewUtils::Differs());
		};

		TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> > Children;
		for (const FSCSDiffEntry& Difference : DifferingProperties.Entries)
		{
			TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
				FOnDiffEntryFocused::CreateStatic(FocusSCSDifferenceEntry, Difference, SelectionCallback, this),
				FGenerateDiffEntryWidget::CreateStatic(CreateSCSDifferenceWidget, Difference, RightRevision));
			Children.Push(Entry);
			OutRealDifferences.Push(Entry);
		}

		const bool bHasDifferences = Children.Num() != 0;
		if (!bHasDifferences)
		{
			// make one child informing the user that there are no differences:
			Children.Push(FBlueprintDifferenceTreeEntry::NoDifferencesEntry());
		}

		OutTreeEntries.Push(FBlueprintDifferenceTreeEntry::CreateCategoryEntry(
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "SCSLabel", "Components"),
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "SCSTooltip", "The list of changes made in the Components panel"),
			SelectionCallback,
			Children,
			bHasDifferences
		));
	}

	TSharedRef<SWidget> OldTreeWidget() { return OldSCS.TreeWidget(); }
	TSharedRef<SWidget> NewTreeWidget() { return NewSCS.TreeWidget(); }

private:
	FOnDiffEntryFocused SelectionCallback;
	FSCSDiffRoot DifferingProperties;

	FSCSDiff OldSCS;
	FSCSDiff NewSCS;
};

/** Generic wrapper around a details view, this does not actually fill out OutTreeEntries */
class FDetailsDiffControl : public TSharedFromThis<FDetailsDiffControl>, public IDiffControl
{
public:
	FDetailsDiffControl(const UObject* InOldObject, const UObject* InNewObject, FOnDiffEntryFocused InSelectionCallback)
		: SelectionCallback(InSelectionCallback)
		, OldDetails(InOldObject, FDetailsDiff::FOnDisplayedPropertiesChanged())
		, NewDetails(InNewObject, FDetailsDiff::FOnDisplayedPropertiesChanged())
	{
		OldDetails.DiffAgainst(NewDetails, DifferingProperties, true);
	}

	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override
	{
		for (const FSingleObjectDiffEntry& Difference : DifferingProperties)
		{
			TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
				FOnDiffEntryFocused::CreateSP(AsShared(), &FDetailsDiffControl::OnSelectDiffEntry, Difference.Identifier),
				FGenerateDiffEntryWidget::CreateStatic(&GenerateObjectDiffWidget, Difference, RightRevision));
			Children.Push(Entry);
			OutRealDifferences.Push(Entry);
		}
	}

	TSharedRef<SWidget> OldDetailsWidget() { return OldDetails.DetailsWidget(); }
	TSharedRef<SWidget> NewDetailsWidget() { return NewDetails.DetailsWidget(); }

protected:
	virtual void OnSelectDiffEntry(FPropertySoftPath PropertyName)
	{
		SelectionCallback.ExecuteIfBound();
		OldDetails.HighlightProperty(PropertyName);
		NewDetails.HighlightProperty(PropertyName);
	}

	FOnDiffEntryFocused SelectionCallback;
	FDetailsDiff OldDetails;
	FDetailsDiff NewDetails;

	TArray<FSingleObjectDiffEntry> DifferingProperties;
	TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> > Children;
};

/** Override for CDO special case */
class FCDODiffControl : public FDetailsDiffControl
{
public:
	FCDODiffControl(const UObject* InOldObject, const UObject* InNewObject, FOnDiffEntryFocused InSelectionCallback)
		: FDetailsDiffControl(InOldObject, InNewObject, InSelectionCallback)
	{
	}
	
	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override
	{
		FDetailsDiffControl::GenerateTreeEntries(OutTreeEntries, OutRealDifferences);

		const bool bHasDifferences = Children.Num() != 0;
		if (!bHasDifferences)
		{
			// make one child informing the user that there are no differences:
			Children.Push(FBlueprintDifferenceTreeEntry::NoDifferencesEntry());
		}

		OutTreeEntries.Push(FBlueprintDifferenceTreeEntry::CreateCategoryEntry(
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "DefaultsLabel", "Defaults"),
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "DefaultsTooltip", "The list of changes made in the Defaults panel"),
			SelectionCallback,
			Children,
			bHasDifferences
		));
	}
};

/** Override for class class settings */
class FClassSettingsDiffControl : public FDetailsDiffControl
{
public:
	FClassSettingsDiffControl(const UObject* InOldObject, const UObject* InNewObject, FOnDiffEntryFocused InSelectionCallback)
		: FDetailsDiffControl(InOldObject, InNewObject, InSelectionCallback)
	{
	}

	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override
	{
		FDetailsDiffControl::GenerateTreeEntries(OutTreeEntries, OutRealDifferences);

		// Check for parent class and interfaces here
		const UBlueprint* OldBlueprint = Cast<UBlueprint>(OldDetails.GetDisplayedObject());
		const UBlueprint* NewBlueprint = Cast<UBlueprint>(NewDetails.GetDisplayedObject());

		if (OldBlueprint && NewBlueprint)
		{
			if (OldBlueprint->ParentClass != NewBlueprint->ParentClass)
			{
				FText DiffText = FText::Format(LOCTEXT("ParentChanged", "Parent Class changed from {0} to {1}"), FText::FromString(OldBlueprint->ParentClass->GetName()), FText::FromString(NewBlueprint->ParentClass->GetName()));

				TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
					SelectionCallback,
					FGenerateDiffEntryWidget::CreateStatic(&GenerateSimpleDiffWidget, DiffText));

				Children.Push(Entry);
				OutRealDifferences.Push(Entry);
			}

			FString OldInterfaces, NewInterfaces;
			for (const FBPInterfaceDescription& Desc : OldBlueprint->ImplementedInterfaces)
			{
				if (!OldInterfaces.IsEmpty())
				{
					OldInterfaces += TEXT(", ");
				}
				OldInterfaces += GetNameSafe(*Desc.Interface);
			}

			for (const FBPInterfaceDescription& Desc : NewBlueprint->ImplementedInterfaces)
			{
				if (!NewInterfaces.IsEmpty())
				{
					NewInterfaces += TEXT(", ");
				}
				NewInterfaces += GetNameSafe(*Desc.Interface);
			}
			
			if (OldInterfaces != NewInterfaces)
			{
				FText DiffText = FText::Format(LOCTEXT("InterfacesChanged", "Interfaces changed from '{0}' to '{1}'"), FText::FromString(OldInterfaces), FText::FromString(NewInterfaces));

				TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
					SelectionCallback,
					FGenerateDiffEntryWidget::CreateStatic(&GenerateSimpleDiffWidget, DiffText));

				Children.Push(Entry);
				OutRealDifferences.Push(Entry);
			}

			if (OldBlueprint->SupportsNativization() != NewBlueprint->SupportsNativization())
			{
				FText DiffText = FText::Format(LOCTEXT("NativizationChanged", "Nativization changed from {0} to {1}"), FText::AsNumber(OldBlueprint->SupportsNativization()), FText::AsNumber(NewBlueprint->SupportsNativization()));

				TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
					SelectionCallback,
					FGenerateDiffEntryWidget::CreateStatic(&GenerateSimpleDiffWidget, DiffText));

				Children.Push(Entry);
				OutRealDifferences.Push(Entry);
			}
		}

		const bool bHasDifferences = Children.Num() != 0;
		if (!bHasDifferences)
		{
			// make one child informing the user that there are no differences:
			Children.Push(FBlueprintDifferenceTreeEntry::NoDifferencesEntry());
		}

		OutTreeEntries.Push(FBlueprintDifferenceTreeEntry::CreateCategoryEntry(
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "SettingsLabel", "Class Settings"),
			NSLOCTEXT("FBlueprintDifferenceTreeEntry", "SettingsTooltip", "The list of changes made in the Class Settings panel"),
			SelectionCallback,
			Children,
			bHasDifferences
		));
	}
};

/** Diff control to handle finding type-specific differences */
struct FBlueprintTypeDiffControl : public TSharedFromThis<FBlueprintTypeDiffControl>, public IDiffControl
{
	struct FSubObjectDiff
	{
		FDiffSingleResult SourceResult;
		FDetailsDiff OldDetails;
		FDetailsDiff NewDetails;
		TArray<TSharedPtr<FBlueprintDifferenceTreeEntry>> Diffs;

		FSubObjectDiff(const FDiffSingleResult& InSourceResult, const UObject* OldObject, const UObject* NewObject)
			: SourceResult(InSourceResult)
			, OldDetails(OldObject, FDetailsDiff::FOnDisplayedPropertiesChanged())
			, NewDetails(NewObject, FDetailsDiff::FOnDisplayedPropertiesChanged())
		{}
	};

	FBlueprintTypeDiffControl(const UBlueprint* InBlueprintOld, const UBlueprint* InBlueprintNew, FOnDiffEntryFocused InSelectionCallback)
		: BlueprintOld(InBlueprintOld), BlueprintNew(InBlueprintNew), SelectionCallback(InSelectionCallback), bDiffSucceeded(false)
	{
		check(InBlueprintNew && InBlueprintOld);
	}

	/** Generate difference tree widgets */
	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override;

	/** The old blueprint (left) */
	const UBlueprint* BlueprintOld;

	/** The new blueprint(right) */
	const UBlueprint* BlueprintNew;

	/** Boxes that will display the details diffs */
	TSharedPtr<SBox> OldDetailsBox;
	TSharedPtr<SBox> NewDetailsBox;

private:
	/** Generate Widget for top category */
	TSharedRef<SWidget> GenerateCategoryWidget(bool bHasRealDiffs);

	/** Build up the Diff Source Array*/
	void BuildDiffSourceArray();

	/** Handle selecting a diff */
	void OnSelectSubobjectDiff(FPropertySoftPath Identifier, TSharedPtr<FSubObjectDiff> SubObjectDiff);

	/** List of objects with differences */
	TArray<TSharedPtr<FSubObjectDiff>> SubObjectDiffs;

	/** Source for list view */
	TArray<TSharedPtr<FDiffResultItem>> DiffListSource;

	/** Selection callback */
	FOnDiffEntryFocused SelectionCallback;

	/** Did diff generation succeed? */
	bool bDiffSucceeded;
};

TSharedRef<SWidget> FBlueprintTypeDiffControl::GenerateCategoryWidget(bool bHasRealDiffs)
{
	FLinearColor Color = FLinearColor::White;

	if (bHasRealDiffs)
	{
		Color = DiffViewUtils::Differs();
	}

	FText Label = BlueprintNew->GetClass()->GetDisplayNameText();

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(STextBlock)
			.ColorAndOpacity(Color)
		.Text(Label)
		];
}

void FBlueprintTypeDiffControl::OnSelectSubobjectDiff(FPropertySoftPath Identifier, TSharedPtr<FSubObjectDiff> SubObjectDiff)
{
	// This allows the owning control to focus the correct tab (or do whatever else it likes):
	SelectionCallback.ExecuteIfBound();

	if (SubObjectDiff.IsValid())
	{
		SubObjectDiff->OldDetails.HighlightProperty(Identifier);
		SubObjectDiff->NewDetails.HighlightProperty(Identifier);

		OldDetailsBox->SetContent(SubObjectDiff->OldDetails.DetailsWidget());
		NewDetailsBox->SetContent(SubObjectDiff->NewDetails.DetailsWidget());
	}
}

void FBlueprintTypeDiffControl::BuildDiffSourceArray()
{
	TArray<FDiffSingleResult> BlueprintDiffResults;
	FDiffResults BlueprintDiffs(&BlueprintDiffResults);
	if (BlueprintNew->FindDiffs(BlueprintOld, BlueprintDiffs))
	{
		bDiffSucceeded = true;

		// Add manual diffs
		for (const FDiffSingleResult& CurrentDiff : BlueprintDiffResults)
		{
			if (CurrentDiff.Diff == EDiffType::OBJECT_REQUEST_DIFF)
			{
				// Turn into a subobject diff

				// Invert order, we want old then new
				TSharedPtr<FSubObjectDiff> SubObjectDiff = MakeShared<FSubObjectDiff>(CurrentDiff, CurrentDiff.Object2, CurrentDiff.Object1);

				TArray<FSingleObjectDiffEntry> DifferingProperties;
				SubObjectDiff->OldDetails.DiffAgainst(SubObjectDiff->NewDetails, DifferingProperties, true);

				if (DifferingProperties.Num() > 0)
				{
					// Actual differences, so add to tree
					SubObjectDiffs.Add(SubObjectDiff);

					for (const FSingleObjectDiffEntry& Difference : DifferingProperties)
					{
						TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
							FOnDiffEntryFocused::CreateSP(AsShared(), &FBlueprintTypeDiffControl::OnSelectSubobjectDiff, Difference.Identifier, SubObjectDiff),
							FGenerateDiffEntryWidget::CreateStatic(&GenerateObjectDiffWidget, Difference, RightRevision));
						SubObjectDiff->Diffs.Push(Entry);
					}
				}
			}
			else
			{
				DiffListSource.Add(MakeShared<FDiffResultItem>(CurrentDiff));
			}
		}

		struct SortDiff
		{
			bool operator () (const TSharedPtr<FDiffResultItem>& A, const TSharedPtr<FDiffResultItem>& B) const
			{
				return A->Result.Diff < B->Result.Diff;
			}
		};

		Sort(DiffListSource.GetData(), DiffListSource.Num(), SortDiff());
	}
}

void FBlueprintTypeDiffControl::GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences)
{
	BuildDiffSourceArray();

	TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> > Children;
	
	bool bHasRealChange = false;

	// First add manual diffs in main category
	for (const TSharedPtr<FDiffResultItem>& Difference : DiffListSource)
	{
		TSharedPtr<FBlueprintDifferenceTreeEntry> ChildEntry = MakeShared<FBlueprintDifferenceTreeEntry>(
			SelectionCallback,
			FGenerateDiffEntryWidget::CreateSP(Difference.ToSharedRef(), &FDiffResultItem::GenerateWidget));
		Children.Push(ChildEntry);
		OutRealDifferences.Push(ChildEntry);

		if (Difference->Result.IsRealDifference())
		{
			bHasRealChange = true;
		}
	}

	if (Children.Num() == 0)
	{
		// Make one child informing the user that there are no differences, or that it is unknown
		if (bDiffSucceeded)
		{
			Children.Push(FBlueprintDifferenceTreeEntry::NoDifferencesEntry());
		}
		else
		{
			Children.Push(FBlueprintDifferenceTreeEntry::UnknownDifferencesEntry());
		}
	}

	TSharedPtr<FBlueprintDifferenceTreeEntry> CategoryEntry = MakeShared<FBlueprintDifferenceTreeEntry>(
			SelectionCallback,
			FGenerateDiffEntryWidget::CreateSP(AsShared(), &FBlueprintTypeDiffControl::GenerateCategoryWidget, bHasRealChange),
			Children);
	OutTreeEntries.Push(CategoryEntry);

	// Now add subobject diffs, one category per object
	for (const TSharedPtr<FSubObjectDiff> SubObjectDiff : SubObjectDiffs)
	{
		Children.Reset();

		Children.Append(SubObjectDiff->Diffs);
		OutRealDifferences.Append(SubObjectDiff->Diffs);

		TSharedPtr<FBlueprintDifferenceTreeEntry> SubObjectEntry = FBlueprintDifferenceTreeEntry::CreateCategoryEntry(
			SubObjectDiff->SourceResult.DisplayString,
			SubObjectDiff->SourceResult.ToolTip,
			FOnDiffEntryFocused::CreateSP(AsShared(), &FBlueprintTypeDiffControl::OnSelectSubobjectDiff, FPropertySoftPath(), SubObjectDiff),
			Children,
			true);

		OutTreeEntries.Push(SubObjectEntry);
	}
}

/** Category list item for a graph*/
struct FGraphToDiff	: public TSharedFromThis<FGraphToDiff>, IDiffControl
{
	FGraphToDiff(SBlueprintDiff* DiffWidget, UEdGraph* GraphOld, UEdGraph* GraphNew, const FRevisionInfo& RevisionOld, const FRevisionInfo& RevisionNew);
	virtual ~FGraphToDiff();

	/** Add widgets to the differences tree */
	virtual void GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences) override;

	/** Get old(left) graph*/
	UEdGraph* GetGraphOld() const { return GraphOld; }

	/** Get new(right) graph*/
	UEdGraph* GetGraphNew() const { return GraphNew; }

	/** Source for list view */
	TArray<TSharedPtr<FDiffResultItem>> DiffListSource;

private:
	/** Get tooltip for category */
	FText GetToolTip();

	/** Generate Widget for category list */
	TSharedRef<SWidget> GenerateCategoryWidget();

	/** Called when the Newer Graph is modified*/
	void OnGraphChanged(const FEdGraphEditAction& Action);

	/** Build up the Diff Source Array*/
	void BuildDiffSourceArray();

	/** Diff widget */
	class SBlueprintDiff* DiffWidget;

	/** The old graph(left)*/
	UEdGraph* GraphOld;

	/** The new graph(right)*/
	UEdGraph* GraphNew;

	/** Description of Old and new graph*/
	FRevisionInfo	RevisionOld, RevisionNew;

	/** Handle to the registered OnGraphChanged delegate. */
	FDelegateHandle OnGraphChangedDelegateHandle;
};

FGraphToDiff::FGraphToDiff(SBlueprintDiff* InDiffWidget, UEdGraph* InGraphOld, UEdGraph* InGraphNew, const FRevisionInfo& InRevisionOld, const FRevisionInfo& InRevisionNew)
	: DiffWidget(InDiffWidget), GraphOld(InGraphOld), GraphNew(InGraphNew), RevisionOld(InRevisionOld), RevisionNew(InRevisionNew)
{
	check(InGraphOld || InGraphNew); //one of them needs to exist

	//need to know when it is modified
	if(InGraphNew)
	{
		OnGraphChangedDelegateHandle = InGraphNew->AddOnGraphChangedHandler( FOnGraphChanged::FDelegate::CreateRaw(this, &FGraphToDiff::OnGraphChanged));
	}

	BuildDiffSourceArray();
}

FGraphToDiff::~FGraphToDiff()
{
	if(GraphNew)
	{
		GraphNew->RemoveOnGraphChangedHandler( OnGraphChangedDelegateHandle);
	}
}

TSharedRef<SWidget> FGraphToDiff::GenerateCategoryWidget()
{
	const UEdGraph* Graph = GraphOld ? GraphOld : GraphNew;
	check(Graph);
	
	FLinearColor Color = (GraphOld && GraphNew) ? FLinearColor::White : FLinearColor(0.3f,0.3f,1.f);

	const bool bHasDiffs = DiffListSource.Num() > 0;

	if(bHasDiffs)
	{
		Color = DiffViewUtils::Differs();
	}

	FText GraphName;
	if (const UEdGraphSchema* Schema = Graph->GetSchema())
	{
		FGraphDisplayInfo DisplayInfo;
		Schema->GetGraphDisplayInformation(*Graph, DisplayInfo);

		GraphName = DisplayInfo.DisplayName;
	}
	else
	{
		GraphName = FText::FromName(Graph->GetFName());
	}

	return SNew(SHorizontalBox)
	+ SHorizontalBox::Slot()
	[
		SNew(STextBlock)
		.ColorAndOpacity(Color)
		.Text(GraphName)
		.ToolTipText(GetToolTip())
	]
	+ DiffViewUtils::Box( GraphOld != nullptr, Color )
	+ DiffViewUtils::Box( GraphNew != nullptr, Color );
}

FText FGraphToDiff::GetToolTip()
{
	if (GraphOld && GraphNew)
	{
		if (DiffListSource.Num() > 0)
		{
			return LOCTEXT("ContainsDifferences", "Revisions are different");
		}
		else
		{
			return LOCTEXT("GraphsIdentical", "Revisions appear to be identical");
		}
	}
	else
	{
		UEdGraph* GoodGraph = GraphOld ? GraphOld : GraphNew;
		check(GoodGraph);
		const FRevisionInfo& Revision = GraphNew ? RevisionOld : RevisionNew;
		FText RevisionText = LOCTEXT("CurrentRevision", "Current Revision");

		if (!Revision.Revision.IsEmpty())
		{
			RevisionText = FText::Format(LOCTEXT("Revision Number", "Revision {0}"), FText::FromString(Revision.Revision));
		}

		return FText::Format(LOCTEXT("MissingGraph", "Graph '{0}' missing from {1}"), FText::FromString(GoodGraph->GetName()), RevisionText);
	}
}

void FGraphToDiff::GenerateTreeEntries(TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutTreeEntries, TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> >& OutRealDifferences)
{
	TArray< TSharedPtr<FBlueprintDifferenceTreeEntry> > Children;
	for (const TSharedPtr<FDiffResultItem>& Difference : DiffListSource)
	{
		TSharedPtr<FBlueprintDifferenceTreeEntry> ChildEntry = MakeShared<FBlueprintDifferenceTreeEntry>(
				FOnDiffEntryFocused::CreateRaw(DiffWidget, &SBlueprintDiff::OnDiffListSelectionChanged, Difference),
				FGenerateDiffEntryWidget::CreateSP(Difference.ToSharedRef(), &FDiffResultItem::GenerateWidget));
		Children.Push(ChildEntry);
		OutRealDifferences.Push(ChildEntry);
	}

	if (Children.Num() == 0)
	{
		// make one child informing the user that there are no differences:
		Children.Push(FBlueprintDifferenceTreeEntry::NoDifferencesEntry());
	}

	TSharedPtr<FBlueprintDifferenceTreeEntry> Entry = MakeShared<FBlueprintDifferenceTreeEntry>(
			FOnDiffEntryFocused::CreateRaw(DiffWidget, &SBlueprintDiff::OnGraphSelectionChanged, TSharedPtr<FGraphToDiff>(AsShared()), ESelectInfo::Direct),
			FGenerateDiffEntryWidget::CreateSP(AsShared(), &FGraphToDiff::GenerateCategoryWidget),
			Children);
	OutTreeEntries.Push(Entry);
}

void FGraphToDiff::BuildDiffSourceArray()
{
	TArray<FDiffSingleResult> FoundDiffs;
	FGraphDiffControl::DiffGraphs(GraphOld, GraphNew, FoundDiffs);

	DiffListSource.Empty();
	for (const FDiffSingleResult& Diff : FoundDiffs)
	{
		DiffListSource.Add(MakeShared<FDiffResultItem>(Diff));
	}

	struct SortDiff
	{
		bool operator () (const TSharedPtr<FDiffResultItem>& A, const TSharedPtr<FDiffResultItem>& B) const
		{
			return A->Result.Diff < B->Result.Diff;
		}
	};

	Sort(DiffListSource.GetData(), DiffListSource.Num(), SortDiff());
}

void FGraphToDiff::OnGraphChanged( const FEdGraphEditAction& Action )
{
	DiffWidget->OnGraphChanged(this);
}

FDiffPanel::FDiffPanel()
{
	Blueprint = nullptr;
	LastFocusedPin = nullptr;
}

void FDiffPanel::InitializeDiffPanel()
{
	TSharedRef< SKismetInspector > Inspector = SNew(SKismetInspector)
		.HideNameArea(true)
		.ViewIdentifier(FName("BlueprintInspector"))
		.MyBlueprintWidget(MyBlueprint)
		.IsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateStatic([] { return false; }));
	DetailsView = Inspector;
	MyBlueprint->SetInspector(DetailsView);
}

static int32 GetCurrentIndex( SListView< TSharedPtr< FDiffSingleResult> > const& ListView, const TArray< TSharedPtr< FDiffSingleResult > >& ListViewSource )
{
	const TArray< TSharedPtr<FDiffSingleResult> >& Selected = ListView.GetSelectedItems();
	if (Selected.Num() == 1)
	{
		int32 Index = 0;
		for (const TSharedPtr<FDiffSingleResult>& Diff : ListViewSource)
		{
			if (Diff == Selected[0])
			{
				return Index;
			}
		}
	}
	return -1;
}

void DiffWidgetUtils::SelectNextRow( SListView< TSharedPtr< FDiffSingleResult> >& ListView, const TArray< TSharedPtr< FDiffSingleResult > >& ListViewSource )
{
	int32 CurrentIndex = GetCurrentIndex(ListView, ListViewSource);
	if( CurrentIndex == ListViewSource.Num() - 1 )
	{
		return;
	}

	ListView.SetSelection(ListViewSource[CurrentIndex + 1]);
}

void DiffWidgetUtils::SelectPrevRow(SListView< TSharedPtr< FDiffSingleResult> >& ListView, const TArray< TSharedPtr< FDiffSingleResult > >& ListViewSource )
{
	int32 CurrentIndex = GetCurrentIndex(ListView, ListViewSource);
	if (CurrentIndex == 0)
	{
		return;
	}

	ListView.SetSelection(ListViewSource[CurrentIndex - 1]);
}

bool DiffWidgetUtils::HasNextDifference(SListView< TSharedPtr< FDiffSingleResult> >& ListView, const TArray< TSharedPtr< FDiffSingleResult > >& ListViewSource)
{
	int32 CurrentIndex = GetCurrentIndex(ListView, ListViewSource);
	return ListViewSource.IsValidIndex(CurrentIndex+1);
}

bool DiffWidgetUtils::HasPrevDifference(SListView< TSharedPtr< FDiffSingleResult> >& ListView, const TArray< TSharedPtr< FDiffSingleResult > >& ListViewSource)
{
	int32 CurrentIndex = GetCurrentIndex(ListView, ListViewSource);
	return ListViewSource.IsValidIndex(CurrentIndex - 1);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SBlueprintDiff::Construct( const FArguments& InArgs)
{
	check(InArgs._BlueprintOld && InArgs._BlueprintNew);
	PanelOld.Blueprint = InArgs._BlueprintOld;
	PanelNew.Blueprint = InArgs._BlueprintNew;
	PanelOld.RevisionInfo = InArgs._OldRevision;
	PanelNew.RevisionInfo = InArgs._NewRevision;

	// Create a skeleton if we don't have one, this is true for revision history diffs
	if (!PanelOld.Blueprint->SkeletonGeneratedClass)
	{
		FKismetEditorUtilities::GenerateBlueprintSkeleton(const_cast<UBlueprint*>(PanelOld.Blueprint));
	}
	
	if (!PanelNew.Blueprint->SkeletonGeneratedClass)
	{
		FKismetEditorUtilities::GenerateBlueprintSkeleton(const_cast<UBlueprint*>(PanelNew.Blueprint));
	}

	// sometimes we want to clearly identify the assets being diffed (when it's
	// not the same asset in each panel)
	PanelOld.bShowAssetName = InArgs._ShowAssetNames;
	PanelNew.bShowAssetName = InArgs._ShowAssetNames;

	bLockViews = true;

	if (InArgs._ParentWindow.IsValid())
	{
		WeakParentWindow = InArgs._ParentWindow;

		AssetEditorCloseDelegate = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().AddSP(this, &SBlueprintDiff::OnCloseAssetEditor);
	}

	FToolBarBuilder ToolbarBuilder(TSharedPtr< const FUICommandList >(), FMultiBoxCustomization::None);
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SBlueprintDiff::PrevDiff),
			FCanExecuteAction::CreateSP( this, &SBlueprintDiff::HasPrevDiff)
		)
		, NAME_None
		, LOCTEXT("PrevDiffLabel", "Prev")
		, LOCTEXT("PrevDiffTooltip", "Go to previous difference")
		, FSlateIcon(FEditorStyle::GetStyleSetName(), "BlueprintDif.PrevDiff")
	);
	ToolbarBuilder.AddToolBarButton(
		FUIAction(
			FExecuteAction::CreateSP(this, &SBlueprintDiff::NextDiff),
			FCanExecuteAction::CreateSP(this, &SBlueprintDiff::HasNextDiff)
		)
		, NAME_None
		, LOCTEXT("NextDiffLabel", "Next")
		, LOCTEXT("NextDiffTooltip", "Go to next difference")
		, FSlateIcon(FEditorStyle::GetStyleSetName(), "BlueprintDif.NextDiff")
	);
	ToolbarBuilder.AddSeparator();
	ToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateSP(this, &SBlueprintDiff::OnToggleLockView))
		, NAME_None
		, LOCTEXT("LockGraphsLabel", "Lock/Unlock")
		, LOCTEXT("LockGraphsTooltip", "Force all graph views to change together, or allow independent scrolling/zooming")
		, TAttribute<FSlateIcon>(this, &SBlueprintDiff::GetLockViewImage)
	);

	DifferencesTreeView = DiffTreeView::CreateTreeView(&MasterDifferencesList);

	GenerateDifferencesList();

	const auto TextBlock = [](FText Text) -> TSharedRef<SWidget>
	{
		return SNew(SBox)
		.Padding(FMargin(4.0f,10.0f))
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SNew(STextBlock)
			.Visibility(EVisibility::HitTestInvisible)
			.TextStyle(FEditorStyle::Get(), "DetailsView.CategoryTextStyle")
			.Text(Text)
		];
	};

	TSharedRef<SWidget> Overlay = 
		SNew(SSplitter)
		.Visibility(EVisibility::HitTestInvisible)
		+ SSplitter::Slot()
		.Value(.2f)
		[
			SNew(SBox)
		]
		+ SSplitter::Slot()
		.Value(.8f)
		[
			SNew(SSplitter)
			.PhysicalSplitterHandleSize(10.0f)
			+ SSplitter::Slot()
			.Value(.5f)
			[
				TextBlock(DiffViewUtils::GetPanelLabel(PanelOld.Blueprint, PanelOld.RevisionInfo, FText()))
			]
			+ SSplitter::Slot()
			.Value(.5f)
			[
				TextBlock(DiffViewUtils::GetPanelLabel(PanelNew.Blueprint, PanelNew.RevisionInfo, FText()))
			]
		];

	this->ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush( "Docking.Tab", ".ContentAreaBrush" ))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 2.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(4.f)
						.AutoWidth()
						[
							ToolbarBuilder.MakeWidget()
						]
						+ SHorizontalBox::Slot()
						[
							SNew(SSpacer)
						]
					]
					+ SVerticalBox::Slot()
					[
						SNew(SSplitter)
						+ SSplitter::Slot()
						.Value(.2f)
						[
							SNew(SBorder)
							.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
							[
								DifferencesTreeView.ToSharedRef()
							]
						]
						+ SSplitter::Slot()
						.Value(.8f)
						[
							SAssignNew(ModeContents, SBox)
						]
					]
				]
				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				[
					Overlay
				]
			]
		];

	SetCurrentMode(MyBlueprintMode);

	// Bind to blueprint changed events as they may be real in memory blueprints that will be modified
	const_cast<UBlueprint*>(PanelNew.Blueprint)->OnChanged().AddSP(this, &SBlueprintDiff::OnBlueprintChanged);
	const_cast<UBlueprint*>(PanelOld.Blueprint)->OnChanged().AddSP(this, &SBlueprintDiff::OnBlueprintChanged);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

SBlueprintDiff::~SBlueprintDiff()
{
	if (AssetEditorCloseDelegate.IsValid())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().Remove(AssetEditorCloseDelegate);
	}
}

void SBlueprintDiff::OnCloseAssetEditor(UObject* Asset, EAssetEditorCloseReason CloseReason)
{
	if (PanelOld.Blueprint == Asset || PanelNew.Blueprint == Asset || CloseReason == EAssetEditorCloseReason::CloseAllAssetEditors)
	{
		// Tell our window to close and set our selves to collapsed to try and stop it from ticking
		SetVisibility(EVisibility::Collapsed);

		if (AssetEditorCloseDelegate.IsValid())
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorRequestClose().Remove(AssetEditorCloseDelegate);
		}

		if (WeakParentWindow.IsValid())
		{
			WeakParentWindow.Pin()->RequestDestroyWindow();
		}
	}
}

void SBlueprintDiff::CreateGraphEntry( UEdGraph* GraphOld, UEdGraph* GraphNew )
{
	Graphs.Add(MakeShared<FGraphToDiff>(this, GraphOld, GraphNew, PanelOld.RevisionInfo, PanelNew.RevisionInfo));
}

void SBlueprintDiff::OnGraphSelectionChanged(TSharedPtr<FGraphToDiff> Item, ESelectInfo::Type SelectionType)
{
	if(!Item.IsValid())
	{
		return;
	}

	FocusOnGraphRevisions(Item.Get());

}

void SBlueprintDiff::OnGraphChanged(FGraphToDiff* Diff)
{
	if(PanelNew.GraphEditor.IsValid() && PanelNew.GraphEditor.Pin()->GetCurrentGraph() == Diff->GetGraphNew())
	{
		FocusOnGraphRevisions(Diff);
	}
}

void SBlueprintDiff::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	if (InBlueprint == PanelOld.Blueprint || InBlueprint == PanelNew.Blueprint)
	{
		// After a BP has changed significantly, we need to regenerate the UI and set back to initial UI to avoid crashes
		GenerateDifferencesList();
		SetCurrentMode(MyBlueprintMode);
	}
}

TSharedRef<SWidget> SBlueprintDiff::DefaultEmptyPanel()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BlueprintDifGraphsToolTip", "Select Graph to Diff"))
		];
}

TSharedPtr<SWindow> SBlueprintDiff::CreateDiffWindow(FText WindowTitle, UBlueprint* OldBlueprint, UBlueprint* NewBlueprint, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision)
{
	// sometimes we're comparing different revisions of one single asset (other 
	// times we're comparing two completely separate assets altogether)
	bool bIsSingleAsset = (NewBlueprint->GetName() == OldBlueprint->GetName());

	TSharedPtr<SWindow> Window = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(FVector2D(1000, 800));

	Window->SetContent(SNew(SBlueprintDiff)
		.BlueprintOld(OldBlueprint)
		.BlueprintNew(NewBlueprint)
		.OldRevision(OldRevision)
		.NewRevision(NewRevision)
		.ShowAssetNames(!bIsSingleAsset)
		.ParentWindow(Window));

	// Make this window a child of the modal window if we've been spawned while one is active.
	TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
	if (ActiveModal.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window.ToSharedRef(), ActiveModal.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window.ToSharedRef());
	}

	return Window;
}

void SBlueprintDiff::NextDiff()
{
	DiffTreeView::HighlightNextDifference(DifferencesTreeView.ToSharedRef(), RealDifferences, MasterDifferencesList);
}

void SBlueprintDiff::PrevDiff()
{
	DiffTreeView::HighlightPrevDifference(DifferencesTreeView.ToSharedRef(), RealDifferences, MasterDifferencesList);
}

bool SBlueprintDiff::HasNextDiff() const
{
	return DiffTreeView::HasNextDifference(DifferencesTreeView.ToSharedRef(), RealDifferences);
}

bool SBlueprintDiff::HasPrevDiff() const
{
	return DiffTreeView::HasPrevDifference(DifferencesTreeView.ToSharedRef(), RealDifferences);
}

FGraphToDiff* SBlueprintDiff::FindGraphToDiffEntry(const FString& GraphPath)
{
	for(const TSharedPtr<FGraphToDiff>& Graph : Graphs)
	{
		FString SearchGraphPath = Graph->GetGraphOld() ? FGraphDiffControl::GetGraphPath(Graph->GetGraphOld()) : FGraphDiffControl::GetGraphPath(Graph->GetGraphNew());
		if (SearchGraphPath.Equals(GraphPath, ESearchCase::CaseSensitive))
		{
			return Graph.Get();
		}
	}
	return nullptr;
}

void SBlueprintDiff::FocusOnGraphRevisions( FGraphToDiff* Diff )
{
	UEdGraph* Graph = Diff->GetGraphOld() ? Diff->GetGraphOld() : Diff->GetGraphNew();

	FString GraphPath = FGraphDiffControl::GetGraphPath(Graph);

	HandleGraphChanged(GraphPath);

	ResetGraphEditors();
}

void SBlueprintDiff::OnDiffListSelectionChanged(TSharedPtr<FDiffResultItem> TheDiff )
{
	check( !TheDiff->Result.OwningObjectPath.IsEmpty() );
	FocusOnGraphRevisions( FindGraphToDiffEntry( TheDiff->Result.OwningObjectPath) );
	FDiffSingleResult Result = TheDiff->Result;

	const auto SafeClearSelection = []( TWeakPtr<SGraphEditor> GraphEditor )
	{
		TSharedPtr<SGraphEditor> GraphEditorPtr = GraphEditor.Pin();
		if( GraphEditorPtr.IsValid())
		{
			GraphEditorPtr->ClearSelectionSet();
		}
	};

	SafeClearSelection( PanelNew.GraphEditor );
	SafeClearSelection( PanelOld.GraphEditor );

	if (Result.Pin1)
	{
		GetDiffPanelForNode(*Result.Pin1->GetOwningNode()).FocusDiff(*Result.Pin1);
		if (Result.Pin2)
		{
			GetDiffPanelForNode(*Result.Pin2->GetOwningNode()).FocusDiff(*Result.Pin2);
		}
	}
	else if (Result.Node1)
	{
		GetDiffPanelForNode(*Result.Node1).FocusDiff(*Result.Node1);
		if (Result.Node2)
		{
			GetDiffPanelForNode(*Result.Node2).FocusDiff(*Result.Node2);
		}
	}
}

void SBlueprintDiff::OnToggleLockView()
{
	bLockViews = !bLockViews;
	ResetGraphEditors();
}

FSlateIcon SBlueprintDiff::GetLockViewImage() const
{
	return FSlateIcon(FEditorStyle::GetStyleSetName(), bLockViews ? "GenericLock" : "GenericUnlock");
}

void SBlueprintDiff::ResetGraphEditors()
{
	if(PanelOld.GraphEditor.IsValid() && PanelNew.GraphEditor.IsValid())
	{
		if(bLockViews)
		{
			PanelOld.GraphEditor.Pin()->LockToGraphEditor(PanelNew.GraphEditor);
			PanelNew.GraphEditor.Pin()->LockToGraphEditor(PanelOld.GraphEditor);
		}
		else
		{
			PanelOld.GraphEditor.Pin()->UnlockFromGraphEditor(PanelNew.GraphEditor);
			PanelNew.GraphEditor.Pin()->UnlockFromGraphEditor(PanelOld.GraphEditor);
		}	
	}
}

void FDiffPanel::GeneratePanel(UEdGraph* Graph, UEdGraph* GraphToDiff )
{
	if( GraphEditor.IsValid() && GraphEditor.Pin()->GetCurrentGraph() == Graph )
	{
		return;
	}

	if( LastFocusedPin )
	{
		LastFocusedPin->bIsDiffing = false;
	}
	LastFocusedPin = nullptr;

	TSharedPtr<SWidget> Widget = SNew(SBorder)
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text( LOCTEXT("BPDifPanelNoGraphTip", "Graph does not exist in this revision"))
								];

	if(Graph)
	{
		SGraphEditor::FGraphEditorEvents InEvents;
		{
			const auto SelectionChangedHandler = [](const FGraphPanelSelectionSet& SelectionSet, TSharedPtr<SKismetInspector> Container)
			{
				Container->ShowDetailsForObjects(SelectionSet.Array());
			};

			const auto ContextMenuHandler = [](UEdGraph* CurrentGraph, const UEdGraphNode* InGraphNode, const UEdGraphPin* InGraphPin, FMenuBuilder* MenuBuilder, bool bIsDebugging)
			{
				MenuBuilder->AddMenuEntry(FGenericCommands::Get().Copy);
				return FActionMenuContent(MenuBuilder->MakeWidget());
			};

			InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateStatic(SelectionChangedHandler, DetailsView);
			InEvents.OnCreateNodeOrPinMenu = SGraphEditor::FOnCreateNodeOrPinMenu::CreateStatic(ContextMenuHandler);
		}

		if ( !GraphEditorCommands.IsValid() )
		{
			GraphEditorCommands = MakeShared<FUICommandList>();

			GraphEditorCommands->MapAction( FGenericCommands::Get().Copy,
				FExecuteAction::CreateRaw( this, &FDiffPanel::CopySelectedNodes ),
				FCanExecuteAction::CreateRaw( this, &FDiffPanel::CanCopyNodes )
				);
		}

		MyBlueprint->SetFocusedGraph(Graph);
		MyBlueprint->Refresh();

		TSharedRef<SGraphEditor> Editor = SNew(SGraphEditor)
			.AdditionalCommands(GraphEditorCommands)
			.GraphToEdit(Graph)
			.GraphToDiff(GraphToDiff)
			.IsEditable(false)
			.GraphEvents(InEvents);

		GraphEditor = Editor;
		Widget = Editor;
	}

	GraphEditorBox->SetContent(Widget.ToSharedRef());
}

TSharedRef<SWidget> FDiffPanel::GenerateMyBlueprintWidget()
{
	return SAssignNew(MyBlueprint, SMyBlueprint, TWeakPtr<FBlueprintEditor>(), Blueprint);
}

FGraphPanelSelectionSet FDiffPanel::GetSelectedNodes() const
{
	FGraphPanelSelectionSet CurrentSelection;
	TSharedPtr<SGraphEditor> FocusedGraphEd = GraphEditor.Pin();
	if (FocusedGraphEd.IsValid())
	{
		CurrentSelection = FocusedGraphEd->GetSelectedNodes();
	}
	return CurrentSelection;
}

void FDiffPanel::CopySelectedNodes()
{
	// Export the selected nodes and place the text on the clipboard
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(SelectedNodes, /*out*/ ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FDiffPanel::CanCopyNodes() const
{
	// If any of the nodes can be duplicated then we should allow copying
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
		if ((Node != nullptr) && Node->CanDuplicateNode())
		{
			return true;
		}
	}
	return false;
}

void FDiffPanel::FocusDiff(UEdGraphPin& Pin)
{
	if( LastFocusedPin )
	{
		LastFocusedPin->bIsDiffing = false;
	}
	Pin.bIsDiffing = true;
	LastFocusedPin = &Pin;

	GraphEditor.Pin()->JumpToPin(&Pin);
}

void FDiffPanel::FocusDiff(UEdGraphNode& Node)
{
	if (LastFocusedPin)
	{
		LastFocusedPin->bIsDiffing = false;
	}
	LastFocusedPin = nullptr;

	if (GraphEditor.IsValid())
	{
		GraphEditor.Pin()->JumpToNode(&Node, false);
	}
}

FDiffPanel& SBlueprintDiff::GetDiffPanelForNode(UEdGraphNode& Node)
{
	TSharedPtr<SGraphEditor> OldGraphEditorPtr = PanelOld.GraphEditor.Pin();
	if (OldGraphEditorPtr.IsValid() && Node.GetGraph() == OldGraphEditorPtr->GetCurrentGraph())
	{
		return PanelOld;
	}
	TSharedPtr<SGraphEditor> NewGraphEditorPtr = PanelNew.GraphEditor.Pin();
	if (NewGraphEditorPtr.IsValid() && Node.GetGraph() == NewGraphEditorPtr->GetCurrentGraph())
	{
		return PanelNew;
	}
	ensureMsgf(false, TEXT("Looking for node %s but it cannot be found in provided panels"), *Node.GetName());
	static FDiffPanel Default;
	return Default;
}

void SBlueprintDiff::HandleGraphChanged( const FString& GraphPath )
{
	SetCurrentMode(GraphMode);
	
	TArray<UEdGraph*> GraphsOld, GraphsNew;
	PanelOld.Blueprint->GetAllGraphs(GraphsOld);
	PanelNew.Blueprint->GetAllGraphs(GraphsNew);

	UEdGraph* GraphOld = nullptr;
	for (UEdGraph* OldGraph : GraphsOld)
	{
		if (GraphPath.Equals(FGraphDiffControl::GetGraphPath(OldGraph)))
		{
			GraphOld = OldGraph;
			break;
		}
	}

	UEdGraph* GraphNew = nullptr;
	for (UEdGraph* NewGraph : GraphsNew)
	{
		if (GraphPath.Equals(FGraphDiffControl::GetGraphPath(NewGraph)))
		{
			GraphNew = NewGraph;
			break;
		}
	}

	PanelOld.GeneratePanel(GraphOld, GraphNew);
	PanelNew.GeneratePanel(GraphNew, GraphOld);
}

void SBlueprintDiff::GenerateDifferencesList()
{
	MasterDifferencesList.Empty();
	RealDifferences.Empty();
	Graphs.Empty();
	ModePanels.Empty();

	// SMyBlueprint needs to be created *before* the KismetInspector or the diffs are generated, because the KismetInspector's customizations
	// need a reference to the SMyBlueprint widget that is controlling them...
	const auto CreateInspector = [](TSharedPtr<SMyBlueprint> InMyBlueprint) {
		return SNew(SKismetInspector)
			.HideNameArea(true)
			.ViewIdentifier(FName("BlueprintInspector"))
			.MyBlueprintWidget(InMyBlueprint)
			.IsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateStatic([] { return false; }));
	};

	PanelOld.GenerateMyBlueprintWidget();
	PanelOld.DetailsView = CreateInspector(PanelOld.MyBlueprint);
	PanelOld.MyBlueprint->SetInspector(PanelOld.DetailsView);
	PanelNew.GenerateMyBlueprintWidget();
	PanelNew.DetailsView = CreateInspector(PanelNew.MyBlueprint);
	PanelNew.MyBlueprint->SetInspector(PanelNew.DetailsView);

	TArray<UEdGraph*> GraphsOld, GraphsNew;
	PanelOld.Blueprint->GetAllGraphs(GraphsOld);
	PanelNew.Blueprint->GetAllGraphs(GraphsNew);

	//Add Graphs that exist in both blueprints, or in blueprint 1 only
	for (UEdGraph* GraphOld : GraphsOld)
	{
		UEdGraph* GraphNew = nullptr;
		for (UEdGraph*& TestGraph : GraphsNew)
		{
			if (TestGraph && GraphOld->GetName() == TestGraph->GetName())
			{
				GraphNew = TestGraph;

				// Null reference inside array
				TestGraph = nullptr;
				break;
			}
		}
		// Do not worry about graphs that are contained in MathExpression nodes, they are recreated each compile
		if (IsGraphDiffNeeded(GraphOld))
		{
			CreateGraphEntry(GraphOld,GraphNew);
		}
	}

	//Add graphs that only exist in 2nd(new) blueprint
	for (UEdGraph* GraphNew : GraphsNew)
	{
		if (GraphNew != nullptr && IsGraphDiffNeeded(GraphNew))
		{
			CreateGraphEntry(nullptr, GraphNew);
		}
	}

	bool bHasComponents = false;
	UClass* BlueprintClass = PanelOld.Blueprint->GeneratedClass;
	if (BlueprintClass->IsChildOf<AActor>())
	{
		bHasComponents = true;
	}

	// If this isn't a normal blueprint type, add the type panel
	if (PanelOld.Blueprint->GetClass() != UBlueprint::StaticClass())
	{
		ModePanels.Add(BlueprintTypeMode, GenerateBlueprintTypePanel());
	}

	// Now that we have done the diffs, create the panel widgets
	ModePanels.Add(MyBlueprintMode, GenerateMyBlueprintPanel());
	ModePanels.Add(GraphMode, GenerateGraphPanel());
	ModePanels.Add(DefaultsMode, GenerateDefaultsPanel());
	ModePanels.Add(ClassSettingsMode, GenerateClassSettingsPanel());
	if (bHasComponents)
	{
		ModePanels.Add(ComponentsMode, GenerateComponentsPanel());
	}

	for (const TSharedPtr<FGraphToDiff>& Graph : Graphs)
	{
		Graph->GenerateTreeEntries(MasterDifferencesList, RealDifferences);
	}

	DifferencesTreeView->RebuildList();
}

SBlueprintDiff::FDiffControl SBlueprintDiff::GenerateBlueprintTypePanel()
{
	TSharedPtr<FBlueprintTypeDiffControl> NewDiffControl = MakeShared<FBlueprintTypeDiffControl>(PanelOld.Blueprint, PanelNew.Blueprint, FOnDiffEntryFocused::CreateRaw(this, &SBlueprintDiff::SetCurrentMode, BlueprintTypeMode));
	NewDiffControl->GenerateTreeEntries(MasterDifferencesList, RealDifferences);

	SBlueprintDiff::FDiffControl Ret;
	//Splitter for left and right blueprint. Current convention is for the local (probably newer?) blueprint to be on the right:
	Ret.DiffControl = NewDiffControl;
	Ret.Widget = SNew(SSplitter)
		.PhysicalSplitterHandleSize(10.0f)
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			SAssignNew(NewDiffControl->OldDetailsBox, SBox)
			.VAlign(VAlign_Fill)
			[
				DefaultEmptyPanel()
			]
		]
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			SAssignNew(NewDiffControl->NewDetailsBox, SBox)
			.VAlign(VAlign_Fill)
			[
				DefaultEmptyPanel()
			]
		];

	return Ret;
}

SBlueprintDiff::FDiffControl SBlueprintDiff::GenerateMyBlueprintPanel()
{
	TSharedPtr<FMyBlueprintDiffControl> NewDiffControl = MakeShared<FMyBlueprintDiffControl>(PanelOld.Blueprint, PanelNew.Blueprint, FOnDiffEntryFocused::CreateRaw(this, &SBlueprintDiff::SetCurrentMode, MyBlueprintMode));
	NewDiffControl->GenerateTreeEntries(MasterDifferencesList, RealDifferences);

	SBlueprintDiff::FDiffControl Ret;

	Ret.DiffControl = NewDiffControl;
	Ret.Widget = SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.FillHeight(1.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			//diff window
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+SSplitter::Slot()
			.Value(.8f)
			[
				SNew(SSplitter)
				.PhysicalSplitterHandleSize(10.0f)
				+ SSplitter::Slot()
				[
					PanelOld.MyBlueprint.ToSharedRef()
				]
				+ SSplitter::Slot()
				[
					PanelNew.MyBlueprint.ToSharedRef()
				]
			]
			+ SSplitter::Slot()
			.Value(.2f)
			[
				SNew(SSplitter)
				.PhysicalSplitterHandleSize(10.0f)
				+SSplitter::Slot()
				[
					PanelOld.DetailsView.ToSharedRef()
				]
				+ SSplitter::Slot()
				[
					PanelNew.DetailsView.ToSharedRef()
				]
			]
		]
	];

	return Ret;
}

SBlueprintDiff::FDiffControl SBlueprintDiff::GenerateGraphPanel()
{
	SBlueprintDiff::FDiffControl Ret;

	Ret.Widget = SNew(SVerticalBox)
	+ SVerticalBox::Slot()
	.FillHeight(1.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			//diff window
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+SSplitter::Slot()
			.Value(.8f)
			[
				SNew(SSplitter)
				.PhysicalSplitterHandleSize(10.0f)
				+ SSplitter::Slot()
				[
					SAssignNew(PanelOld.GraphEditorBox, SBox)
					.VAlign(VAlign_Fill)
					[
						DefaultEmptyPanel()
					]
				]
				+ SSplitter::Slot()
				[
					SAssignNew(PanelNew.GraphEditorBox, SBox)
					.VAlign(VAlign_Fill)
					[
						DefaultEmptyPanel()
					]
				]
			]
			+ SSplitter::Slot()
			.Value(.2f)
			[
				SNew(SSplitter)
				.PhysicalSplitterHandleSize(10.0f)
				+SSplitter::Slot()
				[
					PanelOld.DetailsView.ToSharedRef()
				]
				+ SSplitter::Slot()
				[
					PanelNew.DetailsView.ToSharedRef()
				]
			]
		]
	];

	return Ret;
}

SBlueprintDiff::FDiffControl SBlueprintDiff::GenerateDefaultsPanel()
{
	const UObject* A = DiffUtils::GetCDO(PanelOld.Blueprint);
	const UObject* B = DiffUtils::GetCDO(PanelNew.Blueprint);

	TSharedPtr<FCDODiffControl> NewDiffControl = MakeShared<FCDODiffControl>(A, B, FOnDiffEntryFocused::CreateRaw(this, &SBlueprintDiff::SetCurrentMode, DefaultsMode));
	NewDiffControl->GenerateTreeEntries(MasterDifferencesList, RealDifferences);

	SBlueprintDiff::FDiffControl Ret;
	Ret.DiffControl = NewDiffControl;
	Ret.Widget = SNew(SSplitter)
		.PhysicalSplitterHandleSize(10.0f)
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			NewDiffControl->OldDetailsWidget()
		]
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			NewDiffControl->NewDetailsWidget()
		];

	return Ret;
}

SBlueprintDiff::FDiffControl SBlueprintDiff::GenerateClassSettingsPanel()
{
	TSharedPtr<FClassSettingsDiffControl> NewDiffControl = MakeShared<FClassSettingsDiffControl>(PanelOld.Blueprint, PanelNew.Blueprint, FOnDiffEntryFocused::CreateRaw(this, &SBlueprintDiff::SetCurrentMode, ClassSettingsMode));
	NewDiffControl->GenerateTreeEntries(MasterDifferencesList, RealDifferences);

	SBlueprintDiff::FDiffControl Ret;
	Ret.DiffControl = NewDiffControl;
	Ret.Widget = SNew(SSplitter)
		.PhysicalSplitterHandleSize(10.0f)
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			NewDiffControl->OldDetailsWidget()
		]
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			NewDiffControl->NewDetailsWidget()
		];

	return Ret;
}

SBlueprintDiff::FDiffControl SBlueprintDiff::GenerateComponentsPanel()
{
	TSharedPtr<FSCSDiffControl> NewDiffControl = MakeShared<FSCSDiffControl>(PanelOld.Blueprint, PanelNew.Blueprint, FOnDiffEntryFocused::CreateRaw(this, &SBlueprintDiff::SetCurrentMode, ComponentsMode));
	NewDiffControl->GenerateTreeEntries(MasterDifferencesList, RealDifferences);

	SBlueprintDiff::FDiffControl Ret;
	Ret.DiffControl = NewDiffControl;
	Ret.Widget = SNew(SSplitter)
		.PhysicalSplitterHandleSize(10.0f)
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			NewDiffControl->OldTreeWidget()
		]
		+ SSplitter::Slot()
		.Value(0.5f)
		[
			NewDiffControl->NewTreeWidget()
		];

	return Ret;
}

void SBlueprintDiff::SetCurrentMode(FName NewMode)
{
	if( CurrentMode == NewMode )
	{
		return;
	}

	CurrentMode = NewMode;

	FDiffControl* FoundControl = ModePanels.Find(NewMode);

	if (FoundControl)
	{
		// Reset inspector view
		PanelOld.DetailsView->ShowDetailsForObjects(TArray<UObject*>());
		PanelNew.DetailsView->ShowDetailsForObjects(TArray<UObject*>());

		ModeContents->SetContent(FoundControl->Widget.ToSharedRef());
	}
	else
	{
		ensureMsgf(false, TEXT("Diff panel does not support mode %s"), *NewMode.ToString() );
	}
}

bool SBlueprintDiff::IsGraphDiffNeeded(UEdGraph* InGraph) const
{
	// Do not worry about graphs that are contained in MathExpression nodes, they are recreated each compile
	return !InGraph->GetOuter()->IsA<UK2Node_MathExpression>();
}

#undef LOCTEXT_NAMESPACE

