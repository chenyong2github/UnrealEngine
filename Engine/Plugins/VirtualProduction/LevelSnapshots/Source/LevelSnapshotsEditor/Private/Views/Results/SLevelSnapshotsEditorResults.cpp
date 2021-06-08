// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/SLevelSnapshotsEditorResults.h"


#include "ClassIconFinder.h"
#include "Data/FilteredResults.h"
#include "Data/LevelSnapshot.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsStats.h"
#include "PropertyInfoHelpers.h"
#include "PropertySelection.h"

#include "Algo/Find.h"
#include "DebugViewModeHelpers.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Actor.h"
#include "IDetailPropertyRow.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "SnapshotRestorability.h"
#include "Stats/StatsMisc.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

TWeakPtr<IPropertyRowGenerator> FRowGeneratorInfo::GetGeneratorObject() const
{
	return GeneratorObject;
}

void FRowGeneratorInfo::FlushReferences()
{
	GeneratorObject.Reset();
}

FPropertyHandleHierarchy::FPropertyHandleHierarchy(const TSharedPtr<IDetailTreeNode>& InNode, const TSharedPtr<IPropertyHandle>& InHandle)
	: Node(InNode)
	, Handle(InHandle)
{
	const FString& DefaultPropertyName = "Base";

	if (Node.IsValid())
	{
		if (ensure(Handle.IsValid()))
		{
			// Check first if map
			const TSharedPtr<IPropertyHandle> KeyHandle = Handle->GetKeyHandle();

			if (KeyHandle.IsValid() && KeyHandle->IsValidHandle())
			{
				KeyHandle->GetValueAsFormattedString(PropertyName);
				PropertyName = "Key: " + PropertyName;
			}
			else
			{
				PropertyName = Handle->GetPropertyDisplayName().ToString();
			}

			const TSharedPtr<IPropertyHandle> ParentHandle = Handle->GetParentHandle();

			if (ParentHandle.IsValid() && ParentHandle->IsValidHandle())
			{
				ParentPropertyName = ParentHandle->GetPropertyDisplayName().ToString();
			}
			else
			{
				ParentPropertyName = DefaultPropertyName;
			}
		}
	}
	else
	{
		PropertyName = DefaultPropertyName;
		ParentPropertyName = DefaultPropertyName;
	}
}

FLevelSnapshotsEditorResultsRow::~FLevelSnapshotsEditorResultsRow()
{
	FlushReferences();
}

void FLevelSnapshotsEditorResultsRow::FlushReferences()
{
	if (ChildRows.Num())
	{
		ChildRows.Empty();
	}

	if (HeaderColumns.Num())
	{
		HeaderColumns.Empty();
	}

	if (SnapshotObject.IsValid())
	{
		SnapshotObject.Reset();
	}
	if (WorldObject.IsValid())
	{
		WorldObject.Reset();
	}
	if (ResultsViewPtr.IsValid())
	{
		ResultsViewPtr.Reset();
	}

	if (SnapshotPropertyHandleHierarchy.IsValid())
	{
		SnapshotPropertyHandleHierarchy = nullptr;
	}
	if (WorldPropertyHandleHierarchy.IsValid())
	{
		WorldPropertyHandleHierarchy = nullptr;
	}
}

FLevelSnapshotsEditorResultsRow::FLevelSnapshotsEditorResultsRow(
	const FText InDisplayName,                                     
	const ELevelSnapshotsEditorResultsRowType InRowType, const ECheckBoxState StartingWidgetCheckboxState, 
	const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView, const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow)
{
	check(InResultsView.IsValid());
	ResultsViewPtr = InResultsView;
	
	DisplayName = InDisplayName;
	RowType = InRowType;
	SetWidgetCheckedState(StartingWidgetCheckboxState);

	if (InDirectParentRow.IsValid())
	{
		DirectParentRow = InDirectParentRow;
	}
}

void FLevelSnapshotsEditorResultsRow::InitHeaderRow(const ELevelSnapshotsEditorResultsTreeViewHeaderType InHeaderType,
	const TArray<FText>& InColumns)
{
	HeaderType = InHeaderType;
	HeaderColumns = InColumns;

	// Set visibility values to false for header rows so they hide correctly when they have no visible children
	bDoesRowMatchSearchTerms = false;

	ApplyRowStateMemoryIfAvailable();
}

void FLevelSnapshotsEditorResultsRow::InitAddedActorRow(AActor* InAddedActor)
{
	WorldObject = InAddedActor;

	ApplyRowStateMemoryIfAvailable();
}

void FLevelSnapshotsEditorResultsRow::InitRemovedActorRow(const FSoftObjectPath& InRemovedActorPath)
{
	RemovedActorPath = InRemovedActorPath;

	ApplyRowStateMemoryIfAvailable();
}

void FLevelSnapshotsEditorResultsRow::InitActorRow(
	AActor* InSnapshotActor, AActor* InWorldActor)
{
	SnapshotObject = InSnapshotActor;
	WorldObject = InWorldActor;

	ApplyRowStateMemoryIfAvailable();
}

void FLevelSnapshotsEditorResultsRow::InitObjectRow(
	UObject* InSnapshotObject, UObject* InWorldObject,
	const TWeakPtr<FRowGeneratorInfo>& InSnapshotRowGenerator,
	const TWeakPtr<FRowGeneratorInfo>& InWorldRowGenerator)
{
	SnapshotObject = InSnapshotObject;
	WorldObject = InWorldObject;
	SnapshotRowGeneratorInfo = InSnapshotRowGenerator;
	WorldRowGeneratorInfo = InWorldRowGenerator;

	ApplyRowStateMemoryIfAvailable();
}

void FLevelSnapshotsEditorResultsRow::InitPropertyRow(
	const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InContainingObjectGroup,
	const TSharedPtr<FPropertyHandleHierarchy>& InSnapshotHierarchy, const TSharedPtr<FPropertyHandleHierarchy>& InWorldHandleHierarchy,
	const bool bNewIsCounterpartValueSame)
{
	ContainingObjectGroup = InContainingObjectGroup;
	SnapshotPropertyHandleHierarchy = InSnapshotHierarchy;
	WorldPropertyHandleHierarchy = InWorldHandleHierarchy;
	bIsCounterpartValueSame = bNewIsCounterpartValueSame;

	ApplyRowStateMemoryIfAvailable();
}

void FLevelSnapshotsEditorResultsRow::ApplyRowStateMemoryIfAvailable()
{
	FLevelSnapshotsEditorResultsRowStateMemory RowStateMemory;
	const TSharedPtr<SLevelSnapshotsEditorResults>& ResultsPinned = ResultsViewPtr.Pin();
	
	if (ResultsPinned->FindRowStateMemoryByPath(GetOrGenerateRowPath(), RowStateMemory))
	{
		if (DoesRowRepresentGroup())
		{
			if (RowStateMemory.bIsExpanded)
			{
				ResultsPinned->SetTreeViewItemExpanded(SharedThis(this), true);
			}
		}

		SetWidgetCheckedState(RowStateMemory.WidgetCheckedState);
	}
}

const FString& FLevelSnapshotsEditorResultsRow::GetOrGenerateRowPath()
{
	if (RowPath.IsEmpty())
	{
		RowPath = GetDisplayName().ToString();

		if (DirectParentRow.IsValid())
		{
			RowPath = DirectParentRow.Pin()->GetOrGenerateRowPath() + "." + RowPath;
		}
	}

	return RowPath;
}

void FLevelSnapshotsEditorResultsRow::GenerateActorGroupChildren(FPropertySelectionMap& PropertySelectionMap)
{
	struct FLocalPropertyLooper
	{
		static TArray<TFieldPath<FProperty>> LoopOverProperties(
			const TWeakPtr<FRowGeneratorInfo>& InSnapshotRowGeneratorInfo, const TWeakPtr<FRowGeneratorInfo>& InWorldRowGeneratorInfo,
			const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow, const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter,
			const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView)
		{
			TSharedPtr<FPropertyHandleHierarchy> SnapshotHandleHierarchy = nullptr;
			TSharedPtr<FPropertyHandleHierarchy> WorldHandleHierarchy = nullptr;

			if (InSnapshotRowGeneratorInfo.IsValid())
			{
				SnapshotHandleHierarchy = BuildPropertyHandleHierarchy(InSnapshotRowGeneratorInfo);
			}

			if (InWorldRowGeneratorInfo.IsValid())
			{
				WorldHandleHierarchy = BuildPropertyHandleHierarchy(InWorldRowGeneratorInfo);
			}

			TArray<TFieldPath<FProperty>> PropertyRowsGenerated;

			// We start with World Hierarchy because it's more likely that the user wants to update existing actors than add/delete snapshot ones
			if (WorldHandleHierarchy.IsValid())
			{
				// Don't bother with the first FPropertyHandleHierarchy because that's a dummy node to contain the rest
				for (int32 ChildIndex = 0; ChildIndex < WorldHandleHierarchy->DirectChildren.Num(); ChildIndex++)
				{
					const TSharedRef<FPropertyHandleHierarchy>& ChildHierarchy = WorldHandleHierarchy->DirectChildren[ChildIndex];

					LoopOverHandleHierarchiesAndCreateRowHierarchy(
							ChildHierarchy, InDirectParentRow, PropertiesThatPassFilter, PropertyRowsGenerated, InResultsView, SnapshotHandleHierarchy);
				}
			}

			return PropertyRowsGenerated;
		}

		/* Do not pass in the base hierarchy, only children of the base hierarchy. */
		static void LoopOverHandleHierarchiesAndCreateRowHierarchy(const TWeakPtr<FPropertyHandleHierarchy>& InHierarchy,
			const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow, const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter,
			TArray<TFieldPath<FProperty>>& PropertyRowsGenerated, const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView, const TWeakPtr<FPropertyHandleHierarchy>& InHierarchyToSearchForCounterparts = nullptr)
		{
			if (!ensure(InHierarchy.IsValid()) || !ensure(InDirectParentRow.IsValid()))
			{
				return;
			}

			const TSharedPtr<IPropertyHandle>& Handle = InHierarchy.Pin()->Handle;

			// No asserts for handle or property because PropertyRowGenerator assumes there will be a details layout pointer but this scenario doesn't create one
			// Asserting would cause needless debugging breaks every time a snapshot is chosen
			if (!Handle->IsValidHandle())
			{
				return;
			}

			FProperty* Property = Handle->GetProperty();
			if (!Property)
			{
				return;
			}

			const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType InRowType = 
				FLevelSnapshotsEditorResultsRow::DetermineRowTypeFromProperty(Property, Handle->IsCustomized());
			if (!ensure(InRowType != FLevelSnapshotsEditorResultsRow::None))
			{
				return;
			}

			const TFieldPath<FProperty> PropertyField(Property);

			const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType ParentRowType = InDirectParentRow.Pin()->GetRowType();
			const bool bIsParentRowContainer =
				(ParentRowType == FLevelSnapshotsEditorResultsRow::CollectionGroup || ParentRowType == FLevelSnapshotsEditorResultsRow::StructGroup);
			const bool bIsPropertyFilteredOut = !bIsParentRowContainer && !PropertiesThatPassFilter.Contains(PropertyField);
			if (bIsPropertyFilteredOut) { return; }

			bool bIsCounterpartValueSame = false;

			TWeakPtr<FPropertyHandleHierarchy> FoundHierarchy = nullptr;
			
			if (InHierarchyToSearchForCounterparts.IsValid())
			{
				bool bFoundMatch = false;
				FoundHierarchy = FindCorrespondingHandle(
					InHierarchy.Pin()->ParentPropertyName, InHierarchy.Pin()->PropertyName, InHierarchyToSearchForCounterparts, bFoundMatch);

				if (bFoundMatch && FoundHierarchy.IsValid() && FoundHierarchy.Pin()->Handle.IsValid() &&
					(InRowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap || 
					InRowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInSetOrArray || 
					InRowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInStruct))
				{
					FString ValueA;
					FString ValueB;

					Handle->GetValueAsFormattedString(ValueA);
					FoundHierarchy.Pin()->Handle->GetValueAsFormattedString(ValueB);

					bIsCounterpartValueSame = ValueA.Equals(ValueB);
				}
			}

			if (!bIsCounterpartValueSame)
			{
				const ECheckBoxState StartingCheckedState = (bIsPropertyFilteredOut || bIsCounterpartValueSame) ? 
					ECheckBoxState::Unchecked : InDirectParentRow.IsValid() ? InDirectParentRow.Pin()->GetWidgetCheckedState() : ECheckBoxState::Checked;

				const FText DisplayName = Handle->GetPropertyDisplayName();

				// Create property
				FLevelSnapshotsEditorResultsRowPtr NewProperty = MakeShared<FLevelSnapshotsEditorResultsRow>(DisplayName, InRowType, StartingCheckedState, 
					InResultsView, InDirectParentRow);

				const TWeakPtr<FLevelSnapshotsEditorResultsRow>& ContainingObjectGroup = InDirectParentRow;

				NewProperty->InitPropertyRow(ContainingObjectGroup, 
						FoundHierarchy.IsValid() ? FoundHierarchy.Pin() : nullptr, 
						InHierarchy.IsValid() ? InHierarchy.Pin() : nullptr, bIsCounterpartValueSame);

				for (int32 ChildIndex = 0; ChildIndex < InHierarchy.Pin()->DirectChildren.Num(); ChildIndex++)
				{
					const TSharedRef<FPropertyHandleHierarchy>& ChildHierarchy = InHierarchy.Pin()->DirectChildren[ChildIndex];

					LoopOverHandleHierarchiesAndCreateRowHierarchy(ChildHierarchy, NewProperty, PropertiesThatPassFilter, PropertyRowsGenerated, InResultsView, InHierarchyToSearchForCounterparts);
				}

				if (NewProperty->DoesRowRepresentGroup() && !NewProperty->GetChildRows().Num())
				{
					// No valid children, destroy group
					NewProperty.Reset();
				}
				else
				{
					InDirectParentRow.Pin()->AddToChildRows(NewProperty);

					PropertyRowsGenerated.Add(PropertyField);
				}
			}
		}

		/* Finds a hierarchy entry recursively */
		static TWeakPtr<FPropertyHandleHierarchy> FindCorrespondingHandle(const FString& InParentHandleDisplayName, const FString& InTargetHandleDisplayName,
			const TWeakPtr<FPropertyHandleHierarchy>& HierarchyToSearch, bool& bFoundMatch)
		{
			if (!ensureMsgf(HierarchyToSearch.IsValid(), TEXT("FindCorrespondingHandle: HierarchyToSearch was not valid")))
			{
				return nullptr;
			}

			TWeakPtr<FPropertyHandleHierarchy> OutHierarchy = nullptr;

			for (TSharedRef<FPropertyHandleHierarchy> ChildHierarchy : HierarchyToSearch.Pin()->DirectChildren)
			{
				const bool bIsSameParentName = ChildHierarchy->ParentPropertyName.Equals(InParentHandleDisplayName);
				const bool bIsSamePropertyName = ChildHierarchy->PropertyName.Equals(InTargetHandleDisplayName);

				if (bIsSamePropertyName && bIsSameParentName)
				{
					OutHierarchy = ChildHierarchy;
					bFoundMatch = true;
					break;
				}


				if (bFoundMatch)
				{
					break;
				}
				else
				{
					if (ChildHierarchy->DirectChildren.Num() > 0)
					{
						OutHierarchy = FindCorrespondingHandle(InParentHandleDisplayName, InTargetHandleDisplayName, ChildHierarchy, bFoundMatch);
					}
				}
			}

			return OutHierarchy;
		}

		/* A helper function called by BuildPropertyHandleHierarchy */
		static void CreatePropertyHandleHierarchyChildrenRecursively(
			const TSharedRef<IDetailTreeNode>& InNode, const TWeakPtr<FPropertyHandleHierarchy>& InParentHierarchy)
		{
			if (!ensureMsgf(InParentHierarchy.IsValid(), TEXT("CreatePropertyHandleHierarchyChildrenRecursively: InParentHierarchy was not valid. Check to see that InParentHierarchy is valid before calling this method.")))
			{
				return;
			}

			TWeakPtr<FPropertyHandleHierarchy> HierarchyToPass = InParentHierarchy;

			const EDetailNodeType NodeType = InNode->GetNodeType();

			if (NodeType == EDetailNodeType::Item)
			{
				TSharedPtr<IPropertyHandle> Handle;

				// If the handle already exists then we should just go get it
				if (InNode->GetRow().IsValid() && InNode->GetRow()->GetPropertyHandle().IsValid())
				{
					Handle = InNode->GetRow()->GetPropertyHandle();
				}
				else // Otherwise let's try to create it
				{
					Handle = InNode->CreatePropertyHandle();
				}

				if (Handle.IsValid())
				{
					const TSharedRef<FPropertyHandleHierarchy> NewHierarchy = MakeShared<FPropertyHandleHierarchy>(InNode, Handle);

					HierarchyToPass = NewHierarchy;

					InParentHierarchy.Pin()->DirectChildren.Add(NewHierarchy);
				}
			}

			TArray<TSharedRef<IDetailTreeNode>> NodeChildren;
			InNode->GetChildren(NodeChildren);

			for (const TSharedRef<IDetailTreeNode>& ChildNode : NodeChildren)
			{
				CreatePropertyHandleHierarchyChildrenRecursively(ChildNode, HierarchyToPass);
			}
		}

		/* Creates a node tree of all property handles created by PRG. The first node is a dummy node to contain all children. */
		static TSharedPtr<FPropertyHandleHierarchy> BuildPropertyHandleHierarchy(const TWeakPtr<FRowGeneratorInfo>& InRowGenerator)
		{
			check(InRowGenerator.IsValid());

			// Create a base hierarchy with dummy info and no handle
			TSharedRef<FPropertyHandleHierarchy> ReturnHierarchy = MakeShared<FPropertyHandleHierarchy>(nullptr, nullptr);

			if (InRowGenerator.Pin()->GetGeneratorObject().IsValid())
			{
				for (const TSharedRef<IDetailTreeNode>& Node : InRowGenerator.Pin()->GetGeneratorObject().Pin()->GetRootTreeNodes())
				{
					CreatePropertyHandleHierarchyChildrenRecursively(Node, ReturnHierarchy);
				}
			}

			return ReturnHierarchy;
		}
	};
	
	struct FLocalComponentLooper
	{
		struct FComponentHierarchy
		{
			FComponentHierarchy(USceneComponent* InComponent)
				: Component(InComponent) {};

			const TWeakObjectPtr<USceneComponent> Component;
			TArray<TSharedRef<FComponentHierarchy>> DirectChildren;
		};

		static UActorComponent* FindCounterpartComponent(const UActorComponent* ComponentToMatch, const TSet<UActorComponent*>& InCounterpartComponents)
		{
			if (!ensure(ComponentToMatch) || !ensure(InCounterpartComponents.Num() > 0))
			{
				return nullptr;
			}

			const TArray<UActorComponent*>::ElementType* FoundComponent = Algo::FindByPredicate(InCounterpartComponents, 
				[&ComponentToMatch](UActorComponent* ComponentInLoop)
				{
					const bool bIsSameClass = ComponentInLoop->IsA(ComponentToMatch->GetClass());
					const bool bIsSameName = ComponentInLoop->GetName().Equals(ComponentToMatch->GetName());
					return bIsSameClass && bIsSameName;
				});

			if (FoundComponent)
			{
				return *FoundComponent;
			}
			else
			{
				return nullptr;
			}
		}

		static int32 CreateNewHierarchyStructInLoop(const AActor* InActor, USceneComponent* SceneComponent, TArray<TWeakPtr<FComponentHierarchy>>& AllHierarchies)
		{
			check(InActor);
			check(SceneComponent);
			
			const TSharedRef<FComponentHierarchy> NewHierarchy = MakeShared<FComponentHierarchy>(SceneComponent);

			const int32 ReturnValue = AllHierarchies.Add(NewHierarchy);

			USceneComponent* ParentComponent = SceneComponent->GetAttachParent();

			if (ParentComponent)
			{
				int32 IndexOfParentHierarchy = AllHierarchies.IndexOfByPredicate(
					[&ParentComponent](const TWeakPtr<FComponentHierarchy>& Hierarchy)
					{
						return Hierarchy.Pin()->Component == ParentComponent;
					});

				if (IndexOfParentHierarchy == -1)
				{
					IndexOfParentHierarchy = CreateNewHierarchyStructInLoop(InActor, ParentComponent, AllHierarchies);
				}

				AllHierarchies[IndexOfParentHierarchy].Pin()->DirectChildren.Add(NewHierarchy);
			}

			return ReturnValue;
		}

		/* Creates a node tree of all scene components in an actor. Only scene components can have children. Non-scene actor components do not */
		static TSharedRef<FComponentHierarchy> BuildComponentHierarchy(const AActor* InActor, TArray<UActorComponent*>& OutNonSceneComponents)
		{
			check(InActor);

			TSharedRef<FComponentHierarchy> ReturnHierarchy = MakeShared<FComponentHierarchy>(InActor->GetRootComponent());

			// A flat representation of the hierarchy used for searching the hierarchy more easily
			TArray<TWeakPtr<FComponentHierarchy>> AllHierarchies;
			AllHierarchies.Add(ReturnHierarchy);

			TSet<UActorComponent*> AllActorComponents = InActor->GetComponents();

			for (UActorComponent* Component : AllActorComponents)
			{
				if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
				{
					const bool ComponentContained = AllHierarchies.ContainsByPredicate(
						[&SceneComponent](const TWeakPtr<FComponentHierarchy>& Hierarchy)
						{
							return Hierarchy.Pin()->Component == SceneComponent;
						});

					if (!ComponentContained)
					{
						CreateNewHierarchyStructInLoop(InActor, SceneComponent, AllHierarchies);
					}
				}
				else
				{
					OutNonSceneComponents.Add(Component);
				}
			}

			return ReturnHierarchy;
		}

		static void BuildNestedSceneComponentRowsRecursively(const TWeakPtr<FComponentHierarchy>& InHierarchy, const TSet<UActorComponent*>& InCounterpartComponents,
			FPropertyEditorModule& PropertyEditorModule, const FPropertySelectionMap& PropertySelectionMap, 
			const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow, const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView)
		{
			struct Local
			{
				static void CheckComponentsForVisiblePropertiesRecursively(const TWeakPtr<FComponentHierarchy>& InHierarchy, const FPropertySelectionMap& PropertySelectionMap, bool& bHasVisibleComponents)
				{
					USceneComponent* CurrentComponent = InHierarchy.Pin()->Component.Get();
					
					const FPropertySelection* PropertySelection = PropertySelectionMap.GetSelectedProperties(CurrentComponent);

					bHasVisibleComponents = PropertySelection ? true : false;

					if (!bHasVisibleComponents)
					{
						for (const TSharedRef<FComponentHierarchy>& Child : InHierarchy.Pin()->DirectChildren)
						{
							CheckComponentsForVisiblePropertiesRecursively(Child, PropertySelectionMap, bHasVisibleComponents);

							if (bHasVisibleComponents)
							{
								break;
							}
						}
					}
				}
			};
			
			check(InHierarchy.IsValid());

			bool bShouldCreateComponentRow = false;

			// If this specific component doesn't have properties to display, we need to check the child components recursively
			Local::CheckComponentsForVisiblePropertiesRecursively(InHierarchy, PropertySelectionMap, bShouldCreateComponentRow);

			if (bShouldCreateComponentRow)
			{
				USceneComponent* CurrentComponent = InHierarchy.Pin()->Component.Get();
				
				if (ensureAlwaysMsgf(CurrentComponent,
					TEXT("%hs: CurrentComponent was nullptr. Please check the InHierarchy for valid component."), __FUNCTION__))
				{
					const FPropertySelection* PropertySelection = PropertySelectionMap.GetSelectedProperties(CurrentComponent);

					const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter =
						PropertySelection ? PropertySelection->GetSelectedLeafProperties() : TArray<TFieldPath<FProperty>>();

					const TWeakPtr<FLevelSnapshotsEditorResultsRow>& ComponentPropertyAsRow =
						BuildComponentRow(
							CurrentComponent, InCounterpartComponents, PropertyEditorModule, PropertiesThatPassFilter, InDirectParentRow, InResultsView);

					if (ensureAlwaysMsgf(ComponentPropertyAsRow.IsValid(),
						TEXT("%hs: ComponentPropertyAsRow for component '%s' was not valid but code paths hould not return null value."),
						__FUNCTION__, *CurrentComponent->GetName()))
					{
						for (const TSharedRef<FComponentHierarchy>& ChildHierarchy : InHierarchy.Pin()->DirectChildren)
						{
							BuildNestedSceneComponentRowsRecursively(
								ChildHierarchy, InCounterpartComponents, PropertyEditorModule, PropertySelectionMap, ComponentPropertyAsRow, InResultsView);
						}
					}
				}
			}
		}

		static TWeakPtr<FLevelSnapshotsEditorResultsRow> BuildComponentRow(UActorComponent* InComponent, const TSet<UActorComponent*>& InCounterpartComponents,
			FPropertyEditorModule& PropertyEditorModule, const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter, 
			const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow, const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView)
		{
			check(InComponent);

			UActorComponent* CounterpartComponent = FindCounterpartComponent(InComponent, InCounterpartComponents);

			// Create group
			FLevelSnapshotsEditorResultsRowPtr NewComponentGroup = MakeShared<FLevelSnapshotsEditorResultsRow>(
				FText::FromString(InComponent->GetName()), ComponentGroup,
				InDirectParentRow.IsValid() ? InDirectParentRow.Pin()->GetWidgetCheckedState() : ECheckBoxState::Checked, InResultsView, InDirectParentRow);

			// Create Row Generators for object and counterpart
			const TWeakPtr<FRowGeneratorInfo>& RowGeneratorInfo = InResultsView.Pin()->RegisterRowGenerator(NewComponentGroup, ObjectType_World, PropertyEditorModule);
			
			TWeakPtr<FRowGeneratorInfo> CounterpartRowGeneratorInfo = nullptr;

			RowGeneratorInfo.Pin()->GetGeneratorObject().Pin()->SetObjects({ InComponent });

			if (CounterpartComponent)
			{
				CounterpartRowGeneratorInfo = InResultsView.Pin()->RegisterRowGenerator(NewComponentGroup, ObjectType_Snapshot, PropertyEditorModule);
				
				CounterpartRowGeneratorInfo.Pin()->GetGeneratorObject().Pin()->SetObjects({ CounterpartComponent });
			}
			
			NewComponentGroup->InitObjectRow(InComponent, CounterpartComponent, CounterpartRowGeneratorInfo, RowGeneratorInfo);

			const TArray<TFieldPath<FProperty>> PropertyRowsGenerated = FLocalPropertyLooper::LoopOverProperties(CounterpartRowGeneratorInfo, RowGeneratorInfo, NewComponentGroup, PropertiesThatPassFilter, InResultsView);

			// Generate fallback rows for properties not supported by PropertyRowGenerator
			for (TFieldPath<FProperty> FieldPath : PropertiesThatPassFilter)
			{
				if (!PropertyRowsGenerated.Contains(FieldPath))
				{
					UE_LOG(LogLevelSnapshots, Warning, TEXT("Unsupported Component Property found named '%s' with FieldPath: %s"), *FieldPath->GetAuthoredName(), *FieldPath.ToString());
				}
			}

			InDirectParentRow.Pin()->InsertChildRowAtIndex(NewComponentGroup);
				
			return NewComponentGroup;
		}
	};

	check(ResultsViewPtr.IsValid());

	ChildRows.Empty();
	
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	AActor* SnapshotActorLocal = nullptr;
	AActor* WorldActorLocal = nullptr;
	
	if (SnapshotObject.IsValid())
	{
		SnapshotActorLocal = Cast<AActor>(SnapshotObject.Get());
		SnapshotRowGeneratorInfo = ResultsViewPtr.Pin()->RegisterRowGenerator(SharedThis(this), ObjectType_Snapshot, PropertyEditorModule);
		SnapshotRowGeneratorInfo.Pin()->GetGeneratorObject().Pin()->SetObjects({ SnapshotActorLocal });
	}

	if (WorldObject.IsValid())
	{
		WorldActorLocal = Cast<AActor>(WorldObject.Get());
		WorldRowGeneratorInfo = ResultsViewPtr.Pin()->RegisterRowGenerator(SharedThis(this), ObjectType_World, PropertyEditorModule);
		WorldRowGeneratorInfo.Pin()->GetGeneratorObject().Pin()->SetObjects({ WorldActorLocal });
	}

	// Iterate over components
	TArray<UActorComponent*> WorldActorNonSceneComponents;
	const TSharedRef<FLocalComponentLooper::FComponentHierarchy> WorldComponentHierarchy = 
		FLocalComponentLooper::BuildComponentHierarchy(WorldActorLocal, WorldActorNonSceneComponents);

	TSet<UActorComponent*> CounterpartComponents;

	if (SnapshotActorLocal)
	{
		CounterpartComponents = SnapshotActorLocal->GetComponents();
	}

	// Non-scene actor components cannot be nested and have no children or parents, so we'll add them separately
	for (UActorComponent* WorldComponent : WorldActorNonSceneComponents)
	{
		if (WorldComponent)
		{
			if (const FPropertySelection* PropertySelection = PropertySelectionMap.GetSelectedProperties(WorldComponent))
			{
				// Get remaining properties after filter
				if (!PropertySelection->IsEmpty())
				{
					FLocalComponentLooper::BuildComponentRow(WorldComponent, CounterpartComponents,
						PropertyEditorModule, PropertySelection->GetSelectedLeafProperties(), SharedThis(this), ResultsViewPtr);
				}
			}
		}
	}

	if (WorldComponentHierarchy->Component != nullptr) // Some Actors have no components, like World Settings
	{
		FLocalComponentLooper::BuildNestedSceneComponentRowsRecursively(WorldComponentHierarchy, CounterpartComponents,
			PropertyEditorModule, PropertySelectionMap, SharedThis(this), ResultsViewPtr);
	}
	
	if (const FPropertySelection* PropertySelection = PropertySelectionMap.GetSelectedProperties(GetWorldObject()))
	{
		if (!PropertySelection->IsEmpty())
		{
			const TArray<TFieldPath<FProperty>>& PropertyRowsGenerated = FLocalPropertyLooper::LoopOverProperties(
				SnapshotRowGeneratorInfo,
				WorldRowGeneratorInfo, 
				SharedThis(this), PropertySelection->GetSelectedLeafProperties(), ResultsViewPtr);
			
			// Generate fallback rows for properties not supported by 
			for (TFieldPath<FProperty> FieldPath : PropertySelection->GetSelectedLeafProperties())
			{
				if (!PropertyRowsGenerated.Contains(FieldPath))
				{
					UE_LOG(LogLevelSnapshots, Warning, TEXT("Unsupported Actor Property found named '%s' with FieldPath: %s"), *FieldPath->GetAuthoredName(), *FieldPath.ToString());
				}
			}
		}
	}

	SetHasGeneratedChildren(true);

	// Remove cached search terms now that the actor group has child rows to search
	SetCachedSearchTerms("");

	// Apply Search
	check(ResultsViewPtr.IsValid());
	
	if (const TSharedPtr<SLevelSnapshotsEditorResults>& PinnedResults = ResultsViewPtr.Pin())
	{
		PinnedResults->ExecuteResultsViewSearchOnSpecifiedActors(PinnedResults->GetSearchStringFromSearchInputField(), { SharedThis(this) });
	}
}

bool FLevelSnapshotsEditorResultsRow::DoesRowRepresentGroup() const
{
	const TArray<ELevelSnapshotsEditorResultsRowType> GroupTypes =
	{
		TreeViewHeader,
		ActorGroup,
		ComponentGroup,
		SubObjectGroup,
		StructGroup,
		CollectionGroup
	};
	
	return GroupTypes.Contains(GetRowType());
}

bool FLevelSnapshotsEditorResultsRow::DoesRowRepresentObject() const
{
	const TArray<ELevelSnapshotsEditorResultsRowType> GroupTypes =
	{
		ActorGroup,
		ComponentGroup,
		SubObjectGroup,
		AddedActor,
		RemovedActor
	};

	return GroupTypes.Contains(GetRowType());
}

FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType FLevelSnapshotsEditorResultsRow::GetRowType() const
{
	return RowType;
}

FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType FLevelSnapshotsEditorResultsRow::DetermineRowTypeFromProperty(FProperty* InProperty, const bool bIsCustomized)
{
	if (!InProperty)
	{
		return None;
	}

	ELevelSnapshotsEditorResultsRowType ReturnRowType = SingleProperty;

	if (bIsCustomized)
	{
		return ReturnRowType;
	}
	
	if (FPropertyInfoHelpers::IsPropertyContainer(InProperty))
	{
		if (FPropertyInfoHelpers::IsPropertyCollection(InProperty))
		{
			ReturnRowType = CollectionGroup;
		}
		else
		{
			ReturnRowType = StructGroup;
		}
	}
	else if (FPropertyInfoHelpers::IsPropertyComponentFast(InProperty))
	{
		ReturnRowType = ComponentGroup;
	}
	else if (FPropertyInfoHelpers::IsPropertySubObject(InProperty))
	{
		ReturnRowType = SubObjectGroup;
	}
	else // Single Property. If it's in a collection it needs a custom widget.
	{
		if (FPropertyInfoHelpers::IsPropertyInContainer(InProperty))
		{
			if (FPropertyInfoHelpers::IsPropertyInStruct(InProperty))
			{
				ReturnRowType = SinglePropertyInStruct;
			}
			else if (FPropertyInfoHelpers::IsPropertyInMap(InProperty))
			{
				ReturnRowType = SinglePropertyInMap;
			}
			else
			{
				ReturnRowType = SinglePropertyInSetOrArray;
			}
		}
	}

	return ReturnRowType;
}

const TArray<FText>& FLevelSnapshotsEditorResultsRow::GetHeaderColumns() const
{
	return HeaderColumns;
}

FText FLevelSnapshotsEditorResultsRow::GetDisplayName() const
{
	if (GetRowType() == TreeViewHeader && HeaderColumns.Num() > 0)
	{
		return HeaderColumns[0];
	}
	
	return DisplayName;
}

void FLevelSnapshotsEditorResultsRow::SetDisplayName(const FText InDisplayName)
{
	DisplayName = InDisplayName;
}

const FSlateBrush* FLevelSnapshotsEditorResultsRow::GetIconBrush() const
{
	if (RowType == ComponentGroup)
	{		
		ELevelSnapshotsObjectType ObjectType;
		UObject* RowObject = GetFirstValidObject(ObjectType);
		
		if (UActorComponent* AsComponent = Cast<UActorComponent>(RowObject))
		{
			return FSlateIconFinder::FindIconBrushForClass(AsComponent->GetClass(), TEXT("SCS.Component"));
		}
	}
	else if (RowType == ActorGroup)
	{		
		ELevelSnapshotsObjectType ObjectType;
		UObject* RowObject = GetFirstValidObject(ObjectType);
		
		if (AActor* AsActor = Cast<AActor>(RowObject))
		{
			FName IconName = AsActor->GetCustomIconName();
			if (IconName == NAME_None)
			{
				IconName = AsActor->GetClass()->GetFName();
			}

			return FClassIconFinder::FindIconForActor(AsActor);
		}
	}

	return nullptr;
}

const TArray<FLevelSnapshotsEditorResultsRowPtr>& FLevelSnapshotsEditorResultsRow::GetChildRows() const
{
	return ChildRows;
}

int32 FLevelSnapshotsEditorResultsRow::GetChildCount() const
{
	return ChildRows.Num();
}

void FLevelSnapshotsEditorResultsRow::SetChildRows(const TArray<FLevelSnapshotsEditorResultsRowPtr>& InChildRows)
{
	ChildRows = InChildRows;
}

void FLevelSnapshotsEditorResultsRow::AddToChildRows(const FLevelSnapshotsEditorResultsRowPtr& InRow)
{
	ChildRows.Add(InRow);
}

void FLevelSnapshotsEditorResultsRow::InsertChildRowAtIndex(const FLevelSnapshotsEditorResultsRowPtr& InRow, const int32 AtIndex)
{
	ChildRows.Insert(InRow, AtIndex);
}

bool FLevelSnapshotsEditorResultsRow::GetIsTreeViewItemExpanded() const
{
	return DoesRowRepresentGroup() && bIsTreeViewItemExpanded;
}

void FLevelSnapshotsEditorResultsRow::SetIsTreeViewItemExpanded(const bool bNewExpanded)
{
	bIsTreeViewItemExpanded = bNewExpanded;
}

uint8 FLevelSnapshotsEditorResultsRow::GetChildDepth() const
{
	return ChildDepth;
}

void FLevelSnapshotsEditorResultsRow::SetChildDepth(const uint8 InDepth)
{
	ChildDepth = InDepth;
}

TWeakPtr<FLevelSnapshotsEditorResultsRow> FLevelSnapshotsEditorResultsRow::GetDirectParentRow() const
{
	return DirectParentRow;
}

void FLevelSnapshotsEditorResultsRow::SetDirectParentRow(const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow)
{
	DirectParentRow = InDirectParentRow;
}

TWeakPtr<FLevelSnapshotsEditorResultsRow> FLevelSnapshotsEditorResultsRow::GetParentRowAtTopOfHierarchy()
{
	TWeakPtr<FLevelSnapshotsEditorResultsRow> TopOfHierarchy(SharedThis(this));

	while (TopOfHierarchy.Pin()->GetDirectParentRow().IsValid())
	{
		TopOfHierarchy = TopOfHierarchy.Pin()->GetDirectParentRow();
	}

	return TopOfHierarchy;
}

TWeakPtr<FLevelSnapshotsEditorResultsRow> FLevelSnapshotsEditorResultsRow::GetContainingObjectGroup() const
{
	return ContainingObjectGroup;
}

bool FLevelSnapshotsEditorResultsRow::GetHasGeneratedChildren() const
{
	return bHasGeneratedChildren;
}

void FLevelSnapshotsEditorResultsRow::SetHasGeneratedChildren(const bool bNewGenerated)
{
	bHasGeneratedChildren = bNewGenerated;
}

bool FLevelSnapshotsEditorResultsRow::MatchSearchTokensToSearchTerms(const TArray<FString> InTokens, const bool bMatchAnyTokens)
{
	bool bMatchFound = false;

	if (InTokens.Num() == 0) // If the search is cleared we'll consider the row to pass search
	{
		bMatchFound = true;
	}
	else
	{
		const FString& SearchTerms = GetOrCacheSearchTerms();

		for (const FString& Token : InTokens)
		{
			if (SearchTerms.Contains(Token))
			{
				bMatchFound = true;

				if (bMatchAnyTokens)
				{
					break;
				}
			}
			else
			{
				if (!bMatchAnyTokens)
				{
					bMatchFound = false;
					break;
				}
			}
		}
	}

	SetDoesRowMatchSearchTerms(bMatchFound);

	return bMatchFound;
}

void FLevelSnapshotsEditorResultsRow::ExecuteSearchOnChildNodes(const FString& SearchString) const
{
	TArray<FString> Tokens;

	SearchString.ParseIntoArray(Tokens, TEXT(" "), true);

	ExecuteSearchOnChildNodes(Tokens);
}

void FLevelSnapshotsEditorResultsRow::ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const
{
	for (const FLevelSnapshotsEditorResultsRowPtr& ChildRow : GetChildRows())
	{
		if (!ensure(ChildRow.IsValid()))
		{
			continue;
		}

		if (ChildRow->DoesRowRepresentGroup())
		{
			const bool bGroupMatch = ChildRow->MatchSearchTokensToSearchTerms(Tokens);

			if (bGroupMatch)
			{
				// If the group name matches then we pass an empty string to search child nodes since we want them all to be visible
				ChildRow->ExecuteSearchOnChildNodes("");
			}
			else
			{
				// Otherwise we iterate over all child nodes to determine which should and should not be visible
				ChildRow->ExecuteSearchOnChildNodes(Tokens);
			}
		}
		else
		{
			ChildRow->MatchSearchTokensToSearchTerms(Tokens);
		}
	}
}

UObject* FLevelSnapshotsEditorResultsRow::GetSnapshotObject() const
{
	if (SnapshotObject.IsValid())
	{
		return SnapshotObject.Get();
	}

	return nullptr;
}

UObject* FLevelSnapshotsEditorResultsRow::GetWorldObject() const
{
	if (WorldObject.IsValid())
	{
		return WorldObject.Get();
	}
	
	return nullptr;
}

UObject* FLevelSnapshotsEditorResultsRow::GetFirstValidObject(ELevelSnapshotsObjectType& ReturnedType) const
{
	if (UObject* WorldObjectLocal = GetWorldObject())
	{
		ReturnedType = ObjectType_World;
		return WorldObjectLocal;
	}
	else if (UObject* SnapshotActorLocal = GetSnapshotObject())
	{
		ReturnedType = ObjectType_Snapshot;
		return SnapshotActorLocal;
	}

	ReturnedType = ObjectType_None;
	return nullptr;
}

FSoftObjectPath FLevelSnapshotsEditorResultsRow::GetObjectPath() const
{
	if (GetRowType() == RemovedActor)
	{
		return RemovedActorPath;
	}
	else if (GetWorldObject())
	{
		return FSoftObjectPath(GetWorldObject());
	}

	return nullptr;
}

FProperty* FLevelSnapshotsEditorResultsRow::GetProperty() const
{
	TSharedPtr<IPropertyHandle> FirstHandle;
	GetFirstValidPropertyHandle(FirstHandle);

	if (FirstHandle.IsValid())
	{
		if (FProperty* Property = FirstHandle->GetProperty())
		{
			return Property;
		}
	}
	
	return nullptr;
}

FLevelSnapshotPropertyChain FLevelSnapshotsEditorResultsRow::GetPropertyChain() const
{
	struct Local
	{
		static void RecursiveCreateChain(const FLevelSnapshotsEditorResultsRow& This, FLevelSnapshotPropertyChain& Result)
		{
			const TWeakPtr<FLevelSnapshotsEditorResultsRow>& Parent = This.GetDirectParentRow();
			
			if (Parent.IsValid())
			{
				if (Parent.Pin()->GetProperty())
				{
					RecursiveCreateChain(*Parent.Pin(), Result);
				}
			}

			if (FProperty* Property = This.GetProperty())
			{
				Result.AppendInline(Property);
			}
		}
	};

	FLevelSnapshotPropertyChain Result;
	Local::RecursiveCreateChain(*this, Result);
	return Result;
}

TSharedPtr<IDetailTreeNode> FLevelSnapshotsEditorResultsRow::GetSnapshotPropertyNode() const
{
	return SnapshotPropertyHandleHierarchy.IsValid() ? SnapshotPropertyHandleHierarchy->Node : nullptr;
}

TSharedPtr<IDetailTreeNode> FLevelSnapshotsEditorResultsRow::GetWorldPropertyNode() const
{
	return WorldPropertyHandleHierarchy.IsValid() ? WorldPropertyHandleHierarchy->Node : nullptr;
}

ELevelSnapshotsObjectType FLevelSnapshotsEditorResultsRow::GetFirstValidPropertyNode(
	TSharedPtr<IDetailTreeNode>& OutNode) const
{
	if (const TSharedPtr<IDetailTreeNode>& WorldNode = GetWorldPropertyNode())
	{
		OutNode = WorldNode;
		return ObjectType_World;
	}
	else if (const TSharedPtr<IDetailTreeNode>& SnapshotNode = GetSnapshotPropertyNode())
	{
		OutNode = SnapshotNode;
		return ObjectType_Snapshot;
	}

	return ObjectType_None;
}

TSharedPtr<IPropertyHandle> FLevelSnapshotsEditorResultsRow::GetSnapshotPropertyHandle() const
{
	return SnapshotPropertyHandleHierarchy.IsValid() ? SnapshotPropertyHandleHierarchy->Handle : nullptr;
}

TSharedPtr<IPropertyHandle> FLevelSnapshotsEditorResultsRow::GetWorldPropertyHandle() const
{
	return WorldPropertyHandleHierarchy.IsValid() ? WorldPropertyHandleHierarchy->Handle : nullptr;
}

ELevelSnapshotsObjectType FLevelSnapshotsEditorResultsRow::GetFirstValidPropertyHandle(TSharedPtr<IPropertyHandle>& OutHandle) const
{
	if (const TSharedPtr<IPropertyHandle>& WorldHandle = GetWorldPropertyHandle())
	{
		OutHandle = WorldHandle;
		return ObjectType_World;
	}
	else if (const TSharedPtr<IPropertyHandle>& SnapshotHandle = GetSnapshotPropertyHandle())
	{
		OutHandle = SnapshotHandle;
		return ObjectType_Snapshot;
	}

	return ObjectType_None;
}

bool FLevelSnapshotsEditorResultsRow::GetIsCounterpartValueSame() const
{
	return bIsCounterpartValueSame;
}

void FLevelSnapshotsEditorResultsRow::SetIsCounterpartValueSame(const bool bIsValueSame)
{
	bIsCounterpartValueSame = bIsValueSame;
}

ECheckBoxState FLevelSnapshotsEditorResultsRow::GetWidgetCheckedState() const
{
	return WidgetCheckedState;
}

void FLevelSnapshotsEditorResultsRow::SetWidgetCheckedState(const ECheckBoxState NewState, const bool bShouldUpdateHierarchyCheckedStates)
{
	WidgetCheckedState = NewState;

	if (bShouldUpdateHierarchyCheckedStates)
	{
		// Set Children to same checked state
		for (const FLevelSnapshotsEditorResultsRowPtr& ChildRow : GetChildRows())
		{
			if (!ChildRow.IsValid())
			{
				continue;
			}

			ChildRow->SetWidgetCheckedState(NewState);
		}
		
		EvaluateAndSetAllParentGroupCheckedStates();
	}

	const ELevelSnapshotsEditorResultsRowType RowTypeLocal = GetRowType();
	
	if ((RowTypeLocal == ActorGroup || RowTypeLocal == AddedActor || RowTypeLocal == RemovedActor) && ResultsViewPtr.IsValid())
	{
		ResultsViewPtr.Pin()->UpdateSnapshotInformationText();
		ResultsViewPtr.Pin()->RefreshScroll();
	}
}

bool FLevelSnapshotsEditorResultsRow::GetIsNodeChecked() const
{
	return GetWidgetCheckedState() == ECheckBoxState::Checked ? true : false;
}

void FLevelSnapshotsEditorResultsRow::SetIsNodeChecked(const bool bNewChecked, const bool bShouldUpdateHierarchyCheckedStates)
{
	SetWidgetCheckedState(bNewChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked, bShouldUpdateHierarchyCheckedStates);
}

bool FLevelSnapshotsEditorResultsRow::HasVisibleChildren() const
{	
	bool bVisibleChildFound = false;

	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ChildRow : GetChildRows())
	{
		if (!ChildRow.IsValid())
		{
			continue;
		}

		if (ChildRow->ShouldRowBeVisible())
		{
			bVisibleChildFound = true;

			break;
		}
	}

	return bVisibleChildFound;
}

bool FLevelSnapshotsEditorResultsRow::HasCheckedChildren() const
{
	bool bCheckedChildFound = false;

	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ChildRow : GetChildRows())
	{
		if (!ChildRow.IsValid())
		{
			continue;
		}

		bCheckedChildFound = ChildRow->GetIsNodeChecked() || ChildRow->HasCheckedChildren();

		if (bCheckedChildFound)
		{
			break;
		}
	}

	return bCheckedChildFound;
}

bool FLevelSnapshotsEditorResultsRow::HasUncheckedChildren() const
{
	bool bUncheckedChildFound = false;

	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ChildRow : GetChildRows())
	{
		bUncheckedChildFound = !ChildRow->GetIsNodeChecked() || ChildRow->HasUncheckedChildren();

		if (bUncheckedChildFound)
		{
			break;
		}
	}

	return bUncheckedChildFound;
}

bool FLevelSnapshotsEditorResultsRow::HasChangedChildren() const
{
	bool bChangedChildFound = false;

	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ChildRow : GetChildRows())
	{
		if (!ChildRow.IsValid())
		{
			continue;
		}

		bChangedChildFound = !ChildRow->GetIsCounterpartValueSame() || ChildRow->HasChangedChildren();

		if (bChangedChildFound)
		{
			break;
		}
	}

	return bChangedChildFound;
}

bool FLevelSnapshotsEditorResultsRow::ShouldRowBeVisible() const
{
	const bool bShowUnselectedRows = ResultsViewPtr.IsValid() ? ResultsViewPtr.Pin()->GetShowUnselectedRows() : true;
	const bool bShouldBeVisibleBasedOnCheckedState = bShowUnselectedRows ? true : GetWidgetCheckedState() != ECheckBoxState::Unchecked;
	return bShouldBeVisibleBasedOnCheckedState && (GetDoesRowMatchSearchTerms() || HasVisibleChildren());
}

EVisibility FLevelSnapshotsEditorResultsRow::GetDesiredVisibility() const
{
	return ShouldRowBeVisible() ? EVisibility::Visible : EVisibility::Collapsed;
}

const FString& FLevelSnapshotsEditorResultsRow::GetOrCacheSearchTerms()
{
	if (CachedSearchTerms.IsEmpty())
	{
		SetCachedSearchTerms(GetDisplayName().ToString());
	}
	
	return CachedSearchTerms;
}

void FLevelSnapshotsEditorResultsRow::SetCachedSearchTerms(const FString& InTerms)
{
	CachedSearchTerms = InTerms;
}

bool FLevelSnapshotsEditorResultsRow::GetDoesRowMatchSearchTerms() const
{
	return bDoesRowMatchSearchTerms;
}

void FLevelSnapshotsEditorResultsRow::SetDoesRowMatchSearchTerms(const bool bNewMatch)
{
	bDoesRowMatchSearchTerms = bNewMatch;
}

void FLevelSnapshotsEditorResultsRow::GetAllCheckedChildProperties(TArray<FLevelSnapshotsEditorResultsRowPtr>& CheckedSinglePropertyNodeArray) const
{
	if (HasCheckedChildren())
	{
		for (const FLevelSnapshotsEditorResultsRowPtr& ChildRow : GetChildRows())
		{		
			if (!ChildRow.IsValid())
			{
				continue;
			}

			const ELevelSnapshotsEditorResultsRowType ChildRowType = ChildRow->GetRowType();

			if ((ChildRowType == SingleProperty || ChildRowType == CollectionGroup) && ChildRow->GetIsNodeChecked())
			{
				CheckedSinglePropertyNodeArray.Add(ChildRow);
			}

			if (ChildRowType == StructGroup)
			{
				CheckedSinglePropertyNodeArray.Add(ChildRow);
				
				ChildRow->GetAllCheckedChildProperties(CheckedSinglePropertyNodeArray);
			}
		}
	}
}

void FLevelSnapshotsEditorResultsRow::GetAllUncheckedChildProperties(
	TArray<FLevelSnapshotsEditorResultsRowPtr>& UncheckedSinglePropertyNodeArray) const
{
	if (HasUncheckedChildren())
	{
		for (const FLevelSnapshotsEditorResultsRowPtr& ChildRow : GetChildRows())
		{
			if (!ChildRow.IsValid())
			{
				continue;
			}
			
			const ELevelSnapshotsEditorResultsRowType ChildRowType = ChildRow->GetRowType();

			if ((ChildRowType == SingleProperty || ChildRowType == CollectionGroup) && !ChildRow->GetIsNodeChecked())
			{
				UncheckedSinglePropertyNodeArray.Add(ChildRow);
			}

			if (ChildRowType == StructGroup)
			{
				if (!ChildRow->GetIsNodeChecked())
				{
					UncheckedSinglePropertyNodeArray.Add(ChildRow);
				}

				ChildRow->GetAllUncheckedChildProperties(UncheckedSinglePropertyNodeArray);
			}
		}
	}
}

void FLevelSnapshotsEditorResultsRow::EvaluateAndSetAllParentGroupCheckedStates() const
{
	TWeakPtr<FLevelSnapshotsEditorResultsRow> ParentRow = GetDirectParentRow();

	ECheckBoxState NewWidgetCheckedState = ECheckBoxState::Unchecked;

	while (ParentRow.IsValid())
	{
		const FLevelSnapshotsEditorResultsRowPtr& PinnedParent = ParentRow.Pin();
		
		if (PinnedParent->DoesRowRepresentGroup())
		{
			if (NewWidgetCheckedState != ECheckBoxState::Undetermined)
			{
				const bool bHasCheckedChildren = PinnedParent->HasCheckedChildren();
				const bool bHasUncheckedChildren = PinnedParent->HasUncheckedChildren();

				if (!bHasCheckedChildren && bHasUncheckedChildren)
				{
					NewWidgetCheckedState = ECheckBoxState::Unchecked;
				}
				else if (bHasCheckedChildren && !bHasUncheckedChildren)
				{
					NewWidgetCheckedState = ECheckBoxState::Checked;
				}
				else
				{
					NewWidgetCheckedState = ECheckBoxState::Undetermined;
				}
			}

			PinnedParent->SetWidgetCheckedState(NewWidgetCheckedState);
		}

		ParentRow = PinnedParent->GetDirectParentRow();
	}
}

void SLevelSnapshotsEditorResults::Construct(const FArguments& InArgs, ULevelSnapshotsEditorData* InEditorData)
{
	if (!ensure(InEditorData))
	{
		return;
	}
	EditorDataPtr = InEditorData;

	DefaultNameText = LOCTEXT("LevelSnapshots", "Level Snapshots");

	OnActiveSnapshotChangedHandle = InEditorData->OnActiveSnapshotChanged.AddSP(this, &SLevelSnapshotsEditorResults::OnSnapshotSelected);

	OnRefreshResultsHandle = InEditorData->OnRefreshResults.AddSP(this, &SLevelSnapshotsEditorResults::RefreshResults);

	OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddLambda([this](const FString& FileName, bool bAsTemplate)
    {
		FlushMemory(false);
		UpdateSnapshotInformationText();
    });

	FMenuBuilder ShowOptionsMenuBuilder = BuildShowOptionsMenu();

	ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5.f, 10.f)
			.HAlign(HAlign_Fill)
			[
				SNew(SHorizontalBox)
			
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FLevelSnapshotsEditorStyle::GetBrush(TEXT("LevelSnapshots.ToolbarButton")))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(SelectedSnapshotNamePtr, STextBlock)
					.Text(DefaultNameText)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				]
			]	
			
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Top)
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				[
					SAssignNew(ResultsSearchBoxPtr, SSearchBox)
					.HintText(LOCTEXT("LevelSnapshotsEditorResults_SearchHintText", "Search actors, components, properties..."))
					.OnTextChanged_Raw(this, &SLevelSnapshotsEditorResults::OnResultsViewSearchTextChanged)
				]

				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(SComboButton)
					.ContentPadding(0)
					.ForegroundColor(FSlateColor::UseForeground())
					.ButtonStyle(FEditorStyle::Get(), "ToggleButton")
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ViewOptions")))
					.MenuContent()
					[
						ShowOptionsMenuBuilder.MakeWidget()
					]
					.ButtonContent()
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("GenericViewButton"))
					]
				]
			]

			+ SVerticalBox::Slot()
			[
				SNew(SOverlay)
				
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.Padding(2.0f, 2.0f, 2.0f, 2.0f)
				[
					SAssignNew(TreeViewPtr, STreeView<FLevelSnapshotsEditorResultsRowPtr>)
					.SelectionMode(ESelectionMode::None)
					.TreeItemsSource(&TreeViewRootHeaderObjects)
					.OnGenerateRow_Lambda([this](FLevelSnapshotsEditorResultsRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
						{
							check(Row.IsValid());
						
							return SNew(STableRow<FLevelSnapshotsEditorResultsRowPtr>, OwnerTable)
								[
									SNew(SLevelSnapshotsEditorResultsRow, Row, SplitterManagerPtr)
								]
								.Visibility_Raw(Row.Get(), &FLevelSnapshotsEditorResultsRow::GetDesiredVisibility);
						})
					.OnGetChildren_Raw(this, &SLevelSnapshotsEditorResults::OnGetRowChildren)
					.OnExpansionChanged_Raw(this, &SLevelSnapshotsEditorResults::OnRowChildExpansionChange)
					.Visibility_Lambda([this]()
						{
							return this->DoesTreeViewHaveVisibleChildren() ? EVisibility::Visible : EVisibility::Collapsed;
						})
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.Padding(2.0f, 24.0f, 2.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LevelSnapshotsEditorResults_NoResults", "No results to show. Try selecting a snapshot, changing your active filters, or clearing any active search."))
					.Visibility_Lambda([this]()
						{
							return DoesTreeViewHaveVisibleChildren() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
						})
				]
			]

			+SVerticalBox::Slot()
			.VAlign(VAlign_Bottom)
			.AutoHeight()
			[
				SNew(SBorder)
				.HAlign(HAlign_Fill)
				.BorderBackgroundColor(FLinearColor::Black)
				[
					// Snapshot Information Text
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.Padding(FMargin(5.f, 5.f))
					[
						SAssignNew(InfoTextBox, SVerticalBox)
						.Visibility_Lambda([this]()
						{
							return EditorDataPtr.IsValid() && EditorDataPtr->GetSelectedWorld() && EditorDataPtr->GetActiveSnapshot() && TreeViewRootHeaderObjects.Num() ? 
								EVisibility::HitTestInvisible : EVisibility::Hidden;
						})

						+SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(SelectedActorCountText, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
							.Justification(ETextJustify::Right)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(MiscActorCountText, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.Justification(ETextJustify::Right)
						]
					]

					// Apply to World
					+SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.Padding(5.f, 2.f)
						[
							SNew(SButton)
							.IsEnabled_Lambda([this]()
							{
								return EditorDataPtr.IsValid() && EditorDataPtr->GetSelectedWorld() && EditorDataPtr->GetActiveSnapshot() && TreeViewRootHeaderObjects.Num();
							})
							.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
							.ForegroundColor(FSlateColor::UseForeground())
							.OnClicked_Raw(this, &SLevelSnapshotsEditorResults::OnClickApplyToWorld)
							[
								SNew(STextBlock)
								.Justification(ETextJustify::Center)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
								.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f))
								.ShadowOffset(FVector2D(1, 1))
								.ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f))
								.Text(LOCTEXT("RestoreLevelSnapshot", "Restore Level Snapshot"))
							]
						]
				]
			]
		];
}

SLevelSnapshotsEditorResults::~SLevelSnapshotsEditorResults()
{
	if (EditorDataPtr.IsValid())
	{
		EditorDataPtr->OnActiveSnapshotChanged.Remove(OnActiveSnapshotChangedHandle);
		EditorDataPtr->OnRefreshResults.Remove(OnRefreshResultsHandle);
		EditorDataPtr.Reset();
	}
	
	OnActiveSnapshotChangedHandle.Reset();
	OnRefreshResultsHandle.Reset();
	
	FEditorDelegates::OnMapOpened.Remove(OnMapOpenedDelegateHandle);
	OnMapOpenedDelegateHandle.Reset();
	
	ResultsSearchBoxPtr.Reset();
	ResultsBoxContainerPtr.Reset();

	SplitterManagerPtr.Reset();

	SelectedSnapshotNamePtr.Reset();

	SelectedActorCountText.Reset();
	MiscActorCountText.Reset();

	DummyRow.Reset();

	FlushMemory(false);

	TreeViewPtr.Reset();
}

FMenuBuilder SLevelSnapshotsEditorResults::BuildShowOptionsMenu()
{
	FMenuBuilder ShowOptionsMenuBuilder = FMenuBuilder(true, nullptr);

	ShowOptionsMenuBuilder.AddMenuEntry(
		LOCTEXT("ShowUnselectedRows", "Show Unselected Rows"),
		LOCTEXT("ShowUnselectedRows_Tooltip", "If false, unselected rows will be hidden from view."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
				SetShowUnselectedRows(!GetShowUnselectedRows());
				}),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SLevelSnapshotsEditorResults::GetShowUnselectedRows)
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	ShowOptionsMenuBuilder.AddMenuEntry(
		LOCTEXT("CollapseAll", "Collapse All"),
		LOCTEXT("LevelSnapshotsResultsView_CollapseAll_Tooltip", "Collapse all expanded actor groups in the Modified Actors list."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
				SetAllActorGroupsCollapsed();
				})
		),
		NAME_None,
		EUserInterfaceActionType::Button
	);

	return ShowOptionsMenuBuilder;
}

void SLevelSnapshotsEditorResults::SetShowFilteredRows(const bool bNewSetting)
{
	bShowFilteredActors = bNewSetting;
}

void SLevelSnapshotsEditorResults::SetShowUnselectedRows(const bool bNewSetting)
{
	bShowUnselectedActors = bNewSetting;
}

bool SLevelSnapshotsEditorResults::GetShowFilteredRows() const
{
	return bShowFilteredActors;
}

bool SLevelSnapshotsEditorResults::GetShowUnselectedRows() const
{
	return bShowUnselectedActors;
}

void SLevelSnapshotsEditorResults::FlushMemory(const bool bShouldKeepMemoryAllocated)
{
	if (bShouldKeepMemoryAllocated)
	{
		TreeViewRootHeaderObjects.Reset();
		TreeViewModifiedActorGroupObjects.Reset();
		TreeViewAddedActorGroupObjects.Reset();
		TreeViewRemovedActorGroupObjects.Reset();
	}
	else
	{
		TreeViewRootHeaderObjects.Empty();
		TreeViewModifiedActorGroupObjects.Empty();
		TreeViewAddedActorGroupObjects.Empty();
		TreeViewRemovedActorGroupObjects.Empty();
	}

	CleanUpGenerators(bShouldKeepMemoryAllocated);
}

TOptional<ULevelSnapshot*> SLevelSnapshotsEditorResults::GetSelectedLevelSnapshot() const
{
	return ensure(EditorDataPtr.IsValid()) ? EditorDataPtr->GetActiveSnapshot() : TOptional<ULevelSnapshot*>();
}

void SLevelSnapshotsEditorResults::OnSnapshotSelected(const TOptional<ULevelSnapshot*>& InLevelSnapshot)
{	
	if (InLevelSnapshot.IsSet() && InLevelSnapshot.GetValue())
	{
		UpdateSnapshotNameText(InLevelSnapshot);
		
		GenerateTreeView(true);
	}
}

void SLevelSnapshotsEditorResults::RefreshResults()
{
	GenerateTreeView(false);
}

FReply SLevelSnapshotsEditorResults::OnClickApplyToWorld()
{
	if (!ensure(EditorDataPtr.IsValid()))
	{
		FReply::Handled();
	}

	const TOptional<ULevelSnapshot*> ActiveLevelSnapshot = EditorDataPtr->GetActiveSnapshot();
	if (ActiveLevelSnapshot.IsSet())
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("OnClickApplyToWorld"), STAT_LevelSnapshots, STATGROUP_LevelSnapshots);
		{
			// Measure how long it takes to get all selected properties from UI
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("BuildSelectionSetFromSelectedProperties"), STAT_BuildSelectionSetFromSelectedProperties, STATGROUP_LevelSnapshots);
			BuildSelectionSetFromSelectedPropertiesInEachActorGroup();
		}

		UWorld* World = EditorDataPtr->GetSelectedWorld();
		ActiveLevelSnapshot.GetValue()->ApplySnapshotToWorld(World, EditorDataPtr->GetFilterResults()->GetPropertiesToRollback());

		SetAllActorGroupsCollapsed();

		RefreshResults();
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("SelectSnapshotFirst", "Select a snapshot first."));
		Info.ExpireDuration = 5.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	return FReply::Handled();
}

void SLevelSnapshotsEditorResults::UpdateSnapshotNameText(const TOptional<ULevelSnapshot*>& InLevelSnapshot) const
{
	if (SelectedSnapshotNamePtr.IsValid())
	{
		FText SnapshotName = DefaultNameText;
		
		if (InLevelSnapshot.IsSet())
		{
			ULevelSnapshot* Snapshot = InLevelSnapshot.GetValue();
			
			FName ReturnedName = Snapshot->GetSnapshotName();
			if (ReturnedName == "None")
			{
				// If a bespoke snapshot name is not provided then we'll use the asset name. Usually these names are the same.
				ReturnedName = Snapshot->GetFName();
			}

			SnapshotName = FText::FromName(ReturnedName);
		}
			
		SelectedSnapshotNamePtr->SetText(SnapshotName);
	}
}

void SLevelSnapshotsEditorResults::UpdateSnapshotInformationText()
{
	check(EditorDataPtr.IsValid());
	
	int32 SelectedModifiedActorCount = 0;
	const int32 TotalPassingModifiedActorCount = TreeViewModifiedActorGroupObjects.Num();

	int32 SelectedAddedActorCount = 0;
	int32 SelectedRemovedActorCount = 0;
	
	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ActorGroup : TreeViewModifiedActorGroupObjects)
	{
		if (ActorGroup->GetWidgetCheckedState() != ECheckBoxState::Unchecked)
		{
			SelectedModifiedActorCount++;
		}
	}

	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ActorGroup : TreeViewAddedActorGroupObjects)
	{
		if (ActorGroup->GetWidgetCheckedState() != ECheckBoxState::Unchecked)
		{
			SelectedAddedActorCount++;
		}
	}

	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ActorGroup : TreeViewRemovedActorGroupObjects)
	{
		if (ActorGroup->GetWidgetCheckedState() != ECheckBoxState::Unchecked)
		{
			SelectedRemovedActorCount++;
		}
	}

	if (SelectedActorCountText.IsValid())
	{
		SelectedActorCountText->SetText(FText::Format(LOCTEXT("ResultsRestoreInfoFormatSelectedModifiedActorCount", "{0} actor(s) (of {1} in snapshot) will be restored"),
			FText::AsNumber(SelectedModifiedActorCount), FText::AsNumber(TotalPassingModifiedActorCount)));
	}

	if (MiscActorCountText.IsValid())
	{
		MiscActorCountText->SetText(FText::Format(LOCTEXT("ResultsRestoreInfoFormatSelectedAddedRemovedActorCounts", "{0} actors will be recreated, {1} will be removed"),
			FText::AsNumber(SelectedRemovedActorCount), FText::AsNumber(SelectedAddedActorCount)));
	}
}

void SLevelSnapshotsEditorResults::RefreshScroll() const
{
	TreeViewPtr->RequestListRefresh();
}

void SLevelSnapshotsEditorResults::BuildSelectionSetFromSelectedPropertiesInEachActorGroup()
{
	if (!ensure(GetEditorDataPtr()))
	{
		return;
	}

	struct Local
	{
		static void RemoveUndesirablePropertiesFromSelectionMapRecursively(
			FPropertySelectionMap& SelectionMap, const FLevelSnapshotsEditorResultsRowPtr& Group)
		{
			if (!ensureMsgf(Group->DoesRowRepresentObject() && Group->GetWorldObject(),
				TEXT("AddAllChildPropertiesInObjectGroupToSelectionSetRecursively: Group does not represent an object. Group name: %s"), *Group->GetDisplayName().ToString()))
			{
				return;
			}

			// We only want to check component or subobject groups or actor groups which have had their children generated already
			if (Group->GetRowType() == FLevelSnapshotsEditorResultsRow::ActorGroup && !Group->GetHasGeneratedChildren())
			{
				return;
			}

			// We only want to check groups which have unchecked children and remove properties which are unchecked
			if (!Group->HasUncheckedChildren())
			{
				return;
			}

			const FPropertySelection* PropertySelection = SelectionMap.GetSelectedProperties(Group->GetWorldObject());

			if (!PropertySelection)
			{
				return;
			}

			// Make a copy of the property selection. If a node is unchecked, we'll remove it from the copy.
			FPropertySelection CheckedNodeFieldPaths = *PropertySelection;
			SelectionMap.RemoveObjectPropertiesFromMap(Group->GetWorldObject());

			TArray<FLevelSnapshotsEditorResultsRowPtr> UncheckedChildPropertyNodes;
			
			for (const FLevelSnapshotsEditorResultsRowPtr& ChildRow : Group->GetChildRows())
			{
				if (!ChildRow.IsValid())
				{
					continue;
				}

				const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType ChildRowType = ChildRow->GetRowType();

				if (ChildRow->DoesRowRepresentObject())
				{
					RemoveUndesirablePropertiesFromSelectionMapRecursively(SelectionMap, ChildRow);
				}
				else if ((ChildRowType == FLevelSnapshotsEditorResultsRow::SingleProperty || ChildRowType == FLevelSnapshotsEditorResultsRow::CollectionGroup) && 
					!ChildRow->GetIsNodeChecked())
				{
					UncheckedChildPropertyNodes.Add(ChildRow);
				}
				else if (ChildRowType == FLevelSnapshotsEditorResultsRow::StructGroup)
				{
					if (!ChildRow->GetIsNodeChecked())
					{
						UncheckedChildPropertyNodes.Add(ChildRow);
					}
					
					ChildRow->GetAllUncheckedChildProperties(UncheckedChildPropertyNodes);
				}
			}

			for (const FLevelSnapshotsEditorResultsRowPtr& ChildRow : UncheckedChildPropertyNodes)
			{
				if (ChildRow.IsValid())
				{
					FLevelSnapshotPropertyChain Chain = ChildRow->GetPropertyChain();
					
					CheckedNodeFieldPaths.RemoveProperty(&Chain, ChildRow->GetProperty());
				}
			}

			if (CheckedNodeFieldPaths.GetSelectedLeafProperties().Num())
			{
				SelectionMap.AddObjectProperties(Group->GetWorldObject(), CheckedNodeFieldPaths);
			}
		}
	};

	FPropertySelectionMap PropertySelectionMap = FilterListData.GetModifiedActorsSelectedProperties_AllowedByFilter();

	// Modified actors
	for (const FLevelSnapshotsEditorResultsRowPtr& Group : TreeViewModifiedActorGroupObjects)
	{
		if (Group.IsValid())
		{
			if (Group->GetWidgetCheckedState() == ECheckBoxState::Unchecked)
			{
				// Remove from PropertyMap
				if (AActor* WorldActor = Cast<AActor>(Group->GetWorldObject()))
				{
					PropertySelectionMap.RemoveObjectPropertiesFromMap(WorldActor);

					for (UActorComponent* Component : WorldActor->GetComponents())
					{
						PropertySelectionMap.RemoveObjectPropertiesFromMap(Component);
					}
				}
			}
			else
			{
				Local::RemoveUndesirablePropertiesFromSelectionMapRecursively(PropertySelectionMap, Group);
			}
		}
	}

	// Added actors
	for (const FLevelSnapshotsEditorResultsRowPtr& Group : TreeViewAddedActorGroupObjects)
	{
		if (Group.IsValid())
		{
			if (Group->GetWidgetCheckedState() != ECheckBoxState::Unchecked)
			{
				// Add to PropertyMap
				if (AActor* WorldActor = Cast<AActor>(Group->GetWorldObject()))
				{
					PropertySelectionMap.AddNewActorToDespawn(WorldActor);
				}
			}
		}
	}

	// Removed actors
	for (const FLevelSnapshotsEditorResultsRowPtr& Group : TreeViewRemovedActorGroupObjects)
	{
		if (Group.IsValid())
		{
			if (Group->GetWidgetCheckedState() != ECheckBoxState::Unchecked)
			{
				// Add to PropertyMap
				PropertySelectionMap.AddDeletedActorToRespawn(Group->GetObjectPath());
			}
		}
	}

	GetEditorDataPtr()->GetFilterResults()->SetPropertiesToRollback(PropertySelectionMap);
}

FString SLevelSnapshotsEditorResults::GetSearchStringFromSearchInputField() const
{
	return ensureAlwaysMsgf(ResultsSearchBoxPtr.IsValid(), TEXT("%hs: ResultsSearchBoxPtr is not valid. Check to make sure it was created."), __FUNCTION__)
	? ResultsSearchBoxPtr->GetText().ToString() : "";
}

ULevelSnapshotsEditorData* SLevelSnapshotsEditorResults::GetEditorDataPtr() const
{
	return EditorDataPtr.IsValid() ? EditorDataPtr.Get() : nullptr;
}

FPropertyRowGeneratorArgs SLevelSnapshotsEditorResults::GetLevelSnapshotsAppropriatePropertyRowGeneratorArgs()
{
	FPropertyRowGeneratorArgs Args;
	Args.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Show;
	Args.bAllowMultipleTopLevelObjects = false;
	Args.bShouldShowHiddenProperties = true;
	Args.bAllowEditingClassDefaultObjects = true;

	return Args;
}

TWeakPtr<FRowGeneratorInfo> SLevelSnapshotsEditorResults::RegisterRowGenerator(
	const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InBoundObject,
	const ELevelSnapshotsObjectType InGeneratorType,
	FPropertyEditorModule& PropertyEditorModule)
{
	// Since we must keep many PRG objects alive in order to access the handle data, validating the nodes each tick is very taxing.
	// We can override the validation with a lambda since the validation function in PRG is not necessary for our implementation

	auto ValidationLambda = ([](const FRootPropertyNodeList& PropertyNodeList) { return true; });
	
	const TSharedRef<IPropertyRowGenerator>& RowGeneratorObject = 
		PropertyEditorModule.CreatePropertyRowGenerator(GetLevelSnapshotsAppropriatePropertyRowGeneratorArgs());

	RowGeneratorObject->SetCustomValidatePropertyNodesFunction(FOnValidatePropertyRowGeneratorNodes::CreateLambda(MoveTemp(ValidationLambda)));
	
	const TSharedRef<FRowGeneratorInfo> NewGeneratorInfo = MakeShared<FRowGeneratorInfo>(FRowGeneratorInfo(InBoundObject, InGeneratorType, RowGeneratorObject));

	RegisteredRowGenerators.Add(NewGeneratorInfo);

	return NewGeneratorInfo;
}

void SLevelSnapshotsEditorResults::CleanUpGenerators(const bool bShouldKeepMemoryAllocated)
{
	for (TSharedPtr<FRowGeneratorInfo>& Generator : RegisteredRowGenerators)
	{		
		if (Generator.IsValid())
		{
			Generator->FlushReferences();
			Generator.Reset();			
		}
	}

	if (bShouldKeepMemoryAllocated)
	{
		RegisteredRowGenerators.Reset();
	}
	else
	{
		RegisteredRowGenerators.Empty();
	}
}

bool SLevelSnapshotsEditorResults::FindRowStateMemoryByPath(const FString& InPath, FLevelSnapshotsEditorResultsRowStateMemory& OutRowStateMemory)
{
	if (RowStateMemory.Num())
	{
		const AlgoImpl::TRangePointerType<TSet<TSharedPtr<FLevelSnapshotsEditorResultsRowStateMemory>>>::Type& FindResult =
			Algo::FindByPredicate(RowStateMemory, [&InPath](const TSharedPtr<FLevelSnapshotsEditorResultsRowStateMemory>& InRowStateMemory)
			{
				return InRowStateMemory->PathToRow.Equals(InPath);
			});

		if (FindResult)
		{
			OutRowStateMemory = *FindResult->Get();
			return true;
		}
	}

	return false;
}

void SLevelSnapshotsEditorResults::AddRowStateToRowStateMemory(
	const TSharedPtr<FLevelSnapshotsEditorResultsRowStateMemory> InRowStateMemory)
{
	RowStateMemory.Add(InRowStateMemory);
}

void SLevelSnapshotsEditorResults::GenerateRowStateMemoryRecursively()
{
	struct Local
	{
		static void GenerateRowStateMemory(const TSharedRef<SLevelSnapshotsEditorResults>& InResultsView, const TSharedPtr<FLevelSnapshotsEditorResultsRow>& Row)
		{
			if (!Row.IsValid())
			{
				return;
			}
			
			InResultsView->AddRowStateToRowStateMemory(
				MakeShared<FLevelSnapshotsEditorResultsRowStateMemory>(Row->GetOrGenerateRowPath(), Row->GetIsTreeViewItemExpanded(), Row->GetWidgetCheckedState()));

			for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ChildRow : Row->GetChildRows())
			{
				GenerateRowStateMemory(InResultsView, ChildRow);
			}
		}
	};
	
	// Reset() rather than Empty() because it's likely we'll use a similar amount of memory
	RowStateMemory.Reset();

	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& Row : TreeViewRootHeaderObjects)
	{
		Local::GenerateRowStateMemory(SharedThis(this), Row);
	}
}

FLevelSnapshotsEditorResultsRowPtr& SLevelSnapshotsEditorResults::GetOrCreateDummyRow()
{
	if (!DummyRow.IsValid())
	{
		DummyRow = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(
				FText::FromString("Dummy"), FLevelSnapshotsEditorResultsRow::None, ECheckBoxState::Undetermined, SharedThis(this)));
	}
	
	return DummyRow;
}

void SLevelSnapshotsEditorResults::GenerateTreeView(const bool bSnapshotHasChanged)
{	
	if (!ensure(EditorDataPtr.IsValid()) || !ensure(TreeViewPtr.IsValid()))
	{
		return;
	}

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("GenerateTreeView"), STAT_GenerateTreeView, STATGROUP_LevelSnapshots);

	if (bSnapshotHasChanged)
	{
		RowStateMemory.Empty();
	}
	else
	{
		GenerateRowStateMemoryRecursively();
	}
	
	FlushMemory(!bSnapshotHasChanged);
	
	UFilteredResults* FilteredResults = EditorDataPtr->GetFilterResults(); 
	const TWeakObjectPtr<ULevelSnapshotFilter>& UserFilters = FilteredResults->GetUserFilters();

	FilteredResults->UpdateFilteredResults();

	FilterListData = FilteredResults->GetFilteredData();
	
	SplitterManagerPtr = MakeShared<FLevelSnapshotsEditorResultsSplitterManager>(FLevelSnapshotsEditorResultsSplitterManager());

	// Create root headers
	if (FilterListData.GetModifiedActors_AllowedByFilter().Num())
	{
		FLevelSnapshotsEditorResultsRowPtr ModifiedActorsHeader = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::GetEmpty(), FLevelSnapshotsEditorResultsRow::TreeViewHeader, ECheckBoxState::Checked, SharedThis(this)));
		ModifiedActorsHeader->InitHeaderRow(FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsTreeViewHeaderType::HeaderType_ModifiedActors, {
			LOCTEXT("ColumnName_ModifiedActors", "Modified Actors"),
			LOCTEXT("ColumnName_CurrentValue", "Current Value"),
			LOCTEXT("ColumnName_ValueToRestore", "Value to Restore")
			});

		if (GenerateTreeViewChildren_ModifiedActors(ModifiedActorsHeader, UserFilters.Get()))
		{
			TreeViewRootHeaderObjects.Add(ModifiedActorsHeader);
		}
		else
		{
			ModifiedActorsHeader.Reset();
		}
	}

	if (FilterListData.GetAddedWorldActors_AllowedByFilter().Num())
	{
		FLevelSnapshotsEditorResultsRowPtr AddedActorsHeader = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::GetEmpty(), FLevelSnapshotsEditorResultsRow::TreeViewHeader, ECheckBoxState::Checked, SharedThis(this)));
		AddedActorsHeader->InitHeaderRow(FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsTreeViewHeaderType::HeaderType_AddedActors, 
			{ LOCTEXT("ColumnName_ActorsToRemove", "Actors to Remove") });

		if (GenerateTreeViewChildren_AddedActors(AddedActorsHeader))
		{
			TreeViewRootHeaderObjects.Add(AddedActorsHeader);
		}
		else
		{
			AddedActorsHeader.Reset();
		}
	}
	
	if (FilterListData.GetRemovedOriginalActorPaths_AllowedByFilter().Num())
	{
		FLevelSnapshotsEditorResultsRowPtr RemovedActorsHeader = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::GetEmpty(), FLevelSnapshotsEditorResultsRow::TreeViewHeader, ECheckBoxState::Checked, SharedThis(this)));
		RemovedActorsHeader->InitHeaderRow(FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsTreeViewHeaderType::HeaderType_RemovedActors, 
			{ LOCTEXT("ColumnName_ActorsToAdd", "Actors to Add") });

		if (GenerateTreeViewChildren_RemovedActors(RemovedActorsHeader))
		{
			TreeViewRootHeaderObjects.Add(RemovedActorsHeader);
		}
		else
		{
			RemovedActorsHeader.Reset();
		}
	}

	TreeViewPtr->RequestListRefresh();
	UpdateSnapshotInformationText();

	// Apply last search
	ExecuteResultsViewSearchOnAllActors(GetSearchStringFromSearchInputField());
}

bool SLevelSnapshotsEditorResults::GenerateTreeViewChildren_ModifiedActors(FLevelSnapshotsEditorResultsRowPtr ModifiedActorsHeader, ULevelSnapshotFilter* UserFilters)
{
	check(ModifiedActorsHeader);
	
	const TSet<TWeakObjectPtr<AActor>>& ActorsToConsider = FilterListData.GetModifiedActors_AllowedByFilter();

	TSet<FSoftObjectPath> EvaluatedObjects;

	for (const TWeakObjectPtr<AActor>& WeakWorldActor : ActorsToConsider)
	{
		if (!ensure(WeakWorldActor.IsValid()))
		{
			continue;
		}

		AActor* WorldActor = WeakWorldActor.Get();

		if (!ensure(WorldActor))
		{
			continue;
		}

		if (!FSnapshotRestorability::IsActorDesirableForCapture(WorldActor))
		{
			continue;
		}

		const int32 KeyCountBeforeFilter = FilterListData.GetModifiedActorsSelectedProperties_AllowedByFilter().GetKeyCount();

		// Get remaining properties after filter
		if (UserFilters)
		{
			FilterListData.ApplyFilterToFindSelectedProperties(WorldActor, UserFilters);
		}

		const FPropertySelectionMap& ModifiedSelectedActors = FilterListData.GetModifiedActorsSelectedProperties_AllowedByFilter();
		const int32 KeyCountAfterFilter = ModifiedSelectedActors.GetKeyCount();
		const int32 KeyCountDifference = KeyCountAfterFilter - KeyCountBeforeFilter;

		// If keys have been added, then this actor or its children have properties that pass the filter
		const bool bDoesActorHavePropertiesAfterFilter = KeyCountDifference > 0;

		if (!bDoesActorHavePropertiesAfterFilter)
		{
			continue;
		}

		TWeakObjectPtr<AActor> WeakSnapshotActor = FilterListData.GetSnapshotCounterpartFor(WorldActor);

		const FString& ActorName = WorldActor->GetActorLabel();

		// Create group
		FLevelSnapshotsEditorResultsRowPtr NewActorGroup = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(
				FText::FromString(ActorName), FLevelSnapshotsEditorResultsRow::ActorGroup, ECheckBoxState::Checked, SharedThis(this), ModifiedActorsHeader));
		
		NewActorGroup->InitActorRow(WeakSnapshotActor.IsValid() ? WeakSnapshotActor.Get() : nullptr, WorldActor);

		ModifiedActorsHeader->AddToChildRows(NewActorGroup);
		
		TreeViewModifiedActorGroupObjects.Add(NewActorGroup);

		// Cache search terms using the desired leaf properties for each object newly added to ModifiedActorsSelectedProperties this loop
		FString NewCachedSearchTerms = WorldActor->GetHumanReadableName();

		if (const FPropertySelection* ActorProperties = ModifiedSelectedActors.GetSelectedProperties(WorldActor))
		{
			for (const TFieldPath<FProperty>& LeafProperty : ActorProperties->GetSelectedLeafProperties())
			{
				NewCachedSearchTerms += " " + LeafProperty.ToString();
			}
		}

		for (UActorComponent* Component : WorldActor->GetComponents())
		{
			if (const FPropertySelection* ComponentProperties = ModifiedSelectedActors.GetSelectedProperties(Component))
			{
				NewCachedSearchTerms += " " + Component->GetReadableName();
				for (const TFieldPath<FProperty>& LeafProperty : ComponentProperties->GetSelectedLeafProperties())
				{
					NewCachedSearchTerms += " " + LeafProperty.ToString();
				}
			}
		}
		
		NewActorGroup->SetCachedSearchTerms(NewCachedSearchTerms);
	}

	return TreeViewModifiedActorGroupObjects.Num() > 0;
}

bool SLevelSnapshotsEditorResults::GenerateTreeViewChildren_AddedActors(FLevelSnapshotsEditorResultsRowPtr AddedActorsHeader)
{
	check(AddedActorsHeader);
	
	for (const TWeakObjectPtr<AActor>& Actor : FilterListData.GetAddedWorldActors_AllowedByFilter())
	{
		if (!Actor.IsValid())
		{
			continue;
		}
		
		// Create group
		FLevelSnapshotsEditorResultsRowPtr NewActorRow = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(
				FText::FromString(Actor.Get()->GetName()), FLevelSnapshotsEditorResultsRow::AddedActor, ECheckBoxState::Checked, SharedThis(this), AddedActorsHeader));
		NewActorRow->InitAddedActorRow(Actor.Get());

		AddedActorsHeader->AddToChildRows(NewActorRow);

		TreeViewAddedActorGroupObjects.Add(NewActorRow);
	}

	return TreeViewAddedActorGroupObjects.Num() > 0;
}

bool SLevelSnapshotsEditorResults::GenerateTreeViewChildren_RemovedActors(FLevelSnapshotsEditorResultsRowPtr RemovedActorsHeader)
{
	check(RemovedActorsHeader);
	
	for (const FSoftObjectPath& ActorPath : FilterListData.GetRemovedOriginalActorPaths_AllowedByFilter())
	{
		FString ActorName = ActorPath.GetSubPathString().IsEmpty() ? ActorPath.GetAssetName() : ActorPath.GetSubPathString();

		if (ActorName.Contains("."))
		{
			ActorName = ActorName.Right(ActorName.Len() - ActorName.Find(".", ESearchCase::IgnoreCase, ESearchDir::FromEnd) - 1);
		}
		
		// Create group
		FLevelSnapshotsEditorResultsRowPtr NewActorRow = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(
				FText::FromString(ActorName), FLevelSnapshotsEditorResultsRow::RemovedActor, ECheckBoxState::Checked, SharedThis(this), RemovedActorsHeader));
		NewActorRow->InitRemovedActorRow(ActorPath);

		RemovedActorsHeader->AddToChildRows(NewActorRow);

		TreeViewRemovedActorGroupObjects.Add(NewActorRow);
	}

	return TreeViewRemovedActorGroupObjects.Num() > 0;
}

FReply SLevelSnapshotsEditorResults::SetAllActorGroupsCollapsed()
{
	if (TreeViewPtr.IsValid())
	{
		for (const FLevelSnapshotsEditorResultsRowPtr& RootRow : TreeViewModifiedActorGroupObjects)
		{
			if (!RootRow.IsValid())
			{
				continue;
			}
			
			TreeViewPtr->SetItemExpansion(RootRow, false);
			RootRow->SetIsTreeViewItemExpanded(false);
		}
	}

	return FReply::Handled();
}

void SLevelSnapshotsEditorResults::OnResultsViewSearchTextChanged(const FText& Text) const
{
	ExecuteResultsViewSearchOnAllActors(Text.ToString());
}

void SLevelSnapshotsEditorResults::ExecuteResultsViewSearchOnAllActors(const FString& SearchString) const
{
	// Consider all rows for search except the header rows
	ExecuteResultsViewSearchOnSpecifiedActors(SearchString, TreeViewModifiedActorGroupObjects);
	ExecuteResultsViewSearchOnSpecifiedActors(SearchString, TreeViewAddedActorGroupObjects);
	ExecuteResultsViewSearchOnSpecifiedActors(SearchString, TreeViewRemovedActorGroupObjects);
}

void SLevelSnapshotsEditorResults::ExecuteResultsViewSearchOnSpecifiedActors(
	const FString& SearchString, const TArray<TSharedPtr<FLevelSnapshotsEditorResultsRow>>& ActorRowsToConsider) const
{
	TArray<FString> Tokens;
	
	// unquoted search equivalent to a match-any-of search
	SearchString.ParseIntoArray(Tokens, TEXT(" "), true);
	
	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ChildRow : ActorRowsToConsider)
	{
		if (!ensure(ChildRow.IsValid()))
		{
			continue;
		}
		
		const bool bGroupMatch = ChildRow->MatchSearchTokensToSearchTerms(Tokens);
		
		// If the group name matches then we pass in an empty string so all child nodes are visible.
		// If the name doesn't match, then we need to evaluate each child.
		ChildRow->ExecuteSearchOnChildNodes(bGroupMatch ? "" : SearchString);
	}
}

bool SLevelSnapshotsEditorResults::DoesTreeViewHaveVisibleChildren() const
{
	if (TreeViewPtr.IsValid())
	{
		for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& Header : TreeViewRootHeaderObjects)
		{
			const EVisibility HeaderVisibility = Header->GetDesiredVisibility();
			
			if (HeaderVisibility != EVisibility::Hidden && HeaderVisibility != EVisibility::Collapsed)
			{
				return true;
			}
		}
	}
	
	return false;
}

void SLevelSnapshotsEditorResults::SetTreeViewItemExpanded(const TSharedPtr<FLevelSnapshotsEditorResultsRow>& RowToExpand, const bool bNewExpansion) const
{
	if (TreeViewPtr.IsValid())
	{
		TreeViewPtr->SetItemExpansion(RowToExpand, bNewExpansion);
	}
}

void SLevelSnapshotsEditorResults::OnGetRowChildren(FLevelSnapshotsEditorResultsRowPtr Row, TArray<FLevelSnapshotsEditorResultsRowPtr>& OutChildren)
{
	if (Row.IsValid())
	{
		if (Row->GetHasGeneratedChildren() || Row->GetRowType() != FLevelSnapshotsEditorResultsRow::ActorGroup)
		{
			OutChildren = Row->GetChildRows();
		}
		else
		{
			if (Row->GetIsTreeViewItemExpanded())
			{
				FPropertySelectionMap PropertySelectionMap = FilterListData.GetModifiedActorsSelectedProperties_AllowedByFilter();
				Row->GenerateActorGroupChildren(PropertySelectionMap);

				OutChildren = Row->GetChildRows();
			}
			else
			{
				OutChildren.Add(GetOrCreateDummyRow());
			}
		}
	}
}

void SLevelSnapshotsEditorResults::OnRowChildExpansionChange(FLevelSnapshotsEditorResultsRowPtr Row, const bool bIsExpanded) const
{
	if (Row.IsValid())
	{
		Row->SetIsTreeViewItemExpanded(bIsExpanded);
	}
}

void SLevelSnapshotsEditorResultsRow::Construct(const FArguments& InArgs, const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InRow, const FLevelSnapshotsEditorResultsSplitterManagerPtr& InSplitterManagerPtr)
{	
	check(InRow.IsValid());

	Item = InRow;

	FLevelSnapshotsEditorResultsRowPtr PinnedItem = Item.Pin();
	
	SplitterManagerPtr = InSplitterManagerPtr;
	check(SplitterManagerPtr.IsValid());
	
	TSharedPtr<IPropertyHandle> ItemHandle;
	PinnedItem->GetFirstValidPropertyHandle(ItemHandle);
	const bool bHasValidHandle = ItemHandle.IsValid() && ItemHandle->IsValidHandle();
	
	const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType RowType = PinnedItem->GetRowType();
	const FText DisplayText = RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap ? FText::GetEmpty() : PinnedItem->GetDisplayName();

	FText Tooltip;

	const bool bIsHeaderRow = RowType == FLevelSnapshotsEditorResultsRow::TreeViewHeader;
	const bool bIsAddedOrRemovedActorRow = RowType == FLevelSnapshotsEditorResultsRow::AddedActor || RowType == FLevelSnapshotsEditorResultsRow::RemovedActor;
	const bool bIsSinglePropertyInCollection = 
		RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap || RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInSetOrArray;

	const bool bDoesRowNeedSplitter = (RowType == FLevelSnapshotsEditorResultsRow::TreeViewHeader && PinnedItem->GetHeaderColumns().Num() > 1) ||
		(!bIsAddedOrRemovedActorRow && !PinnedItem->DoesRowRepresentGroup());
	
	if (bIsSinglePropertyInCollection)
	{
		Tooltip = LOCTEXT("CollectionDisclaimer", "Individual members of collections cannot be selected. The whole collection will be restored.");
	}
	else if (RowType == FLevelSnapshotsEditorResultsRow::ComponentGroup)
	{
		Tooltip = LOCTEXT("ComponentOrderDisclaimer", "Please note that component order reflects the order in the world, not the snapshot. LevelSnapshots does not alter component order.");
	}
	else
	{
		Tooltip = bHasValidHandle ? ItemHandle->GetToolTipText() : PinnedItem->GetDisplayName();
	}

	int32 IndentationDepth = 0;
	TWeakPtr<FLevelSnapshotsEditorResultsRow> ParentRow = PinnedItem->GetDirectParentRow();
	while (ParentRow.IsValid())
	{
		IndentationDepth++;
		ParentRow = ParentRow.Pin()->GetDirectParentRow();
	}
	PinnedItem->SetChildDepth(IndentationDepth);

	TSharedPtr<SBorder> BorderPtr;

	ChildSlot
	[
		SNew(SBox)
		.Padding(FMargin(5,2))
		[
			SAssignNew(BorderPtr, SBorder)
			.ToolTipText(Tooltip)
			.Padding(FMargin(0, 5))
			.BorderImage_Lambda([RowType]()
				{
					switch (RowType)
					{							
						case FLevelSnapshotsEditorResultsRow::ActorGroup:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.ActorGroupBorder");

						case FLevelSnapshotsEditorResultsRow::AddedActor:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.ActorGroupBorder");

						case FLevelSnapshotsEditorResultsRow::RemovedActor:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.ActorGroupBorder");

						case FLevelSnapshotsEditorResultsRow::TreeViewHeader:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.HeaderRowBorder");

						default:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.DefaultBorder");
					}
				})
		]
	];

	// Create name and checkbox

	TSharedRef<SHorizontalBox> BasicRowWidgets = SNew(SHorizontalBox);

	BasicRowWidgets->AddSlot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Left)
	.AutoWidth()
	.Padding(5.f, 2.f)
	[
		SNew(SCheckBox)
		.Visibility(bIsSinglePropertyInCollection ? EVisibility::Hidden : EVisibility::Visible)
		.IsChecked_Raw(PinnedItem.Get(), &FLevelSnapshotsEditorResultsRow::GetWidgetCheckedState)
		.OnCheckStateChanged_Raw(PinnedItem.Get(), &FLevelSnapshotsEditorResultsRow::SetWidgetCheckedState, true)
	];

	if (PinnedItem->DoesRowRepresentObject())
	{
		if (const FSlateBrush* RowIcon = PinnedItem->GetIconBrush())
		{
			BasicRowWidgets->AddSlot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.AutoWidth()
			.Padding(0.f, 2.f, 5.f, 2.f)
			[
				SNew(SImage).Image(RowIcon)
			];
		}
	}

	BasicRowWidgets->AddSlot()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Left)
	.AutoWidth()
	[
		SNew(STextBlock).Text(DisplayText)
	];

	// Create value widgets
	
	if (bDoesRowNeedSplitter)
	{
		SAssignNew(SplitterPtr, SSplitter)
		.Style(FEditorStyle::Get(), "DetailsView.Splitter")
		.PhysicalSplitterHandleSize(bDoesRowNeedSplitter ? 5.0f : 0.f)
		.HitDetectionSplitterHandleSize(bDoesRowNeedSplitter ? 5.0f : 0.f);

		SplitterPtr->AddSlot()
		.OnSlotResized(this, &SLevelSnapshotsEditorResultsRow::SetNameColumnSize)
		.Value(this, &SLevelSnapshotsEditorResultsRow::GetNameColumnSize)
		[
			BasicRowWidgets
		];
	
		// Splitter Slot 1
		int32 SlotIndex = 1;
		
		TSharedPtr<SWidget> WorldChildWidget;

		bool bIsWorldChildWidgetCustomized = false;

		if (const TSharedPtr<IPropertyHandle>& WorldPropertyHandle = PinnedItem->GetWorldPropertyHandle())
		{
			if (WorldPropertyHandle->IsCustomized())
			{
				bIsWorldChildWidgetCustomized = true;
				
				if (const TSharedPtr<IDetailTreeNode>& Node = PinnedItem->GetWorldPropertyNode())
				{
					FNodeWidgets Widgets = Node->CreateNodeWidgets();

					WorldChildWidget = Widgets.WholeRowWidget.IsValid() ? Widgets.WholeRowWidget : Widgets.ValueWidget;
				}
			}
			else if (RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap)
			{
				TSharedRef<SSplitter> Splitter = SNew(SSplitter).ResizeMode(ESplitterResizeMode::FixedPosition);

				const TSharedPtr<IPropertyHandle> KeyHandle = WorldPropertyHandle->GetKeyHandle();

				if (KeyHandle.IsValid() && KeyHandle->IsValidHandle())
				{
					Splitter->AddSlot()[KeyHandle->CreatePropertyValueWidget(false)];
				}

				Splitter->AddSlot()[WorldPropertyHandle->CreatePropertyValueWidget(false)];

				WorldChildWidget = Splitter;
			}
			else
			{
				WorldChildWidget = WorldPropertyHandle->CreatePropertyValueWidget(false);
			}
		}
		else
		{
			if (bIsHeaderRow && PinnedItem->GetHeaderColumns().Num() > SlotIndex)
			{
				WorldChildWidget = SNew(STextBlock).Text(PinnedItem->GetHeaderColumns()[SlotIndex]);
			}
			else if (bIsAddedOrRemovedActorRow || PinnedItem->DoesRowRepresentGroup())
			{
				WorldChildWidget = SNullWidget::NullWidget;
			}
		}

		if (!WorldChildWidget.IsValid())
		{
			WorldChildWidget = 
				SNew(STextBlock)
				.Text(LOCTEXT("LevelSnapshotsEditorResults_NoWorldPropertyFound", "No World property found"));
		}

		WorldChildWidget->SetEnabled(bIsHeaderRow);
		WorldChildWidget->SetCanTick(bIsHeaderRow);

		TSharedPtr<SWidget> FinalWorldWidget = SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2, 0))
			[
				WorldChildWidget.ToSharedRef()
			];

		if (!bIsWorldChildWidgetCustomized)
		{
			FinalWorldWidget = SNew(SInvalidationPanel)
				[
					FinalWorldWidget.ToSharedRef()
				];
		}

		SplitterPtr->AddSlot()
		.OnSlotResized(this, &SLevelSnapshotsEditorResultsRow::SetWorldColumnSize)
		.Value(this, &SLevelSnapshotsEditorResultsRow::GetWorldColumnSize)
		[
			FinalWorldWidget.ToSharedRef()
		];

		// Splitter Slot 2
		SlotIndex = 2;
		
		TSharedPtr<SWidget> SnapshotChildWidget;
		
		bool bIsSnapshotChildWidgetCustomized = false;
		
		if (const TSharedPtr<IPropertyHandle>& SnapshotPropertyHandle = PinnedItem->GetSnapshotPropertyHandle())
		{
			if (SnapshotPropertyHandle->IsCustomized())
			{
				bIsSnapshotChildWidgetCustomized = true;
				
				if (const TSharedPtr<IDetailTreeNode>& Node = PinnedItem->GetSnapshotPropertyNode())
				{
					FNodeWidgets Widgets = Node->CreateNodeWidgets();

					SnapshotChildWidget = Widgets.WholeRowWidget.IsValid() ? Widgets.WholeRowWidget : Widgets.ValueWidget;
				}
			}
			else if (RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap)
			{
				TSharedRef<SSplitter> Splitter = SNew(SSplitter).ResizeMode(ESplitterResizeMode::FixedPosition);

				const TSharedPtr<IPropertyHandle> KeyHandle = SnapshotPropertyHandle->GetKeyHandle();

				if (KeyHandle.IsValid() && KeyHandle->IsValidHandle())
				{
					Splitter->AddSlot()[KeyHandle->CreatePropertyValueWidget(false)];
				}

				Splitter->AddSlot()[SnapshotPropertyHandle->CreatePropertyValueWidget(false)];

				SnapshotChildWidget = Splitter;
			}
			else
			{
				SnapshotChildWidget = SnapshotPropertyHandle->CreatePropertyValueWidget(false);
			}
		}
		else
		{
			if (bIsHeaderRow && PinnedItem->GetHeaderColumns().Num() > SlotIndex)
			{
				SnapshotChildWidget = SNew(STextBlock).Text(PinnedItem->GetHeaderColumns()[SlotIndex]);
			}
			else if (bIsAddedOrRemovedActorRow || PinnedItem->DoesRowRepresentGroup())
			{
				SnapshotChildWidget = SNullWidget::NullWidget;
			}
		}

		if (!SnapshotChildWidget.IsValid())
		{
			SnapshotChildWidget = 
				SNew(STextBlock)
				.Text(LOCTEXT("LevelSnapshotsEditorResults_NoSnapshotPropertyFound", "No snapshot property found"));
		}

		SnapshotChildWidget->SetEnabled(bIsHeaderRow);
		SnapshotChildWidget->SetCanTick(bIsHeaderRow);

		TSharedPtr<SWidget> FinalSnapshotWidget = SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(FMargin(2, 0))
			[
				SnapshotChildWidget.ToSharedRef()
			];

		if (!bIsSnapshotChildWidgetCustomized)
		{
			FinalSnapshotWidget = SNew(SInvalidationPanel)
				[
					FinalSnapshotWidget.ToSharedRef()
				];
		}

		SplitterPtr->AddSlot()
		.OnSlotResized(this, &SLevelSnapshotsEditorResultsRow::SetSnapshotColumnSize)
		.Value(this, &SLevelSnapshotsEditorResultsRow::GetSnapshotColumnSize)
		[
			FinalSnapshotWidget.ToSharedRef()
		];
		
		BorderPtr->SetContent(SplitterPtr.ToSharedRef());
	}
	else
	{
		BorderPtr->SetContent(BasicRowWidgets);
	}
}

SLevelSnapshotsEditorResultsRow::~SLevelSnapshotsEditorResultsRow()
{
	// Remove delegate bindings

	// Unbind event to the splitter being resized first
	if (SplitterPtr.IsValid())
	{
		for (int32 SplitterSlotCount = 0; SplitterSlotCount < SplitterPtr->GetChildren()->Num(); SplitterSlotCount++)
		{
			SplitterPtr->SlotAt(SplitterSlotCount).OnSlotResized().Unbind();
		}
	}

	SplitterPtr.Reset();
	SplitterManagerPtr.Reset();
}

float SLevelSnapshotsEditorResultsRow::GetNameColumnSize() const
{
	return SplitterManagerPtr->NameColumnWidth;;
}

float SLevelSnapshotsEditorResultsRow::GetSnapshotColumnSize() const
{
	return SplitterManagerPtr->SnapshotPropertyColumnWidth;
}

float SLevelSnapshotsEditorResultsRow::GetWorldColumnSize() const
{
	return SplitterManagerPtr->WorldObjectPropertyColumnWidth;
}

void SLevelSnapshotsEditorResultsRow::SetNameColumnSize(const float InWidth) const
{
	SplitterManagerPtr->NameColumnWidth = InWidth;
}

void SLevelSnapshotsEditorResultsRow::SetSnapshotColumnSize(const float InWidth) const
{
	SplitterManagerPtr->SnapshotPropertyColumnWidth = InWidth;
}

void SLevelSnapshotsEditorResultsRow::SetWorldColumnSize(const float InWidth) const
{
	SplitterManagerPtr->WorldObjectPropertyColumnWidth = InWidth;
}

#undef LOCTEXT_NAMESPACE
