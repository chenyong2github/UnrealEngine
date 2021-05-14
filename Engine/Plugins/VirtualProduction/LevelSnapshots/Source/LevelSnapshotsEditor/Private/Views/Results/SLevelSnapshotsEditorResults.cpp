// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/SLevelSnapshotsEditorResults.h"

#include "Data/FilteredResults.h"
#include "Data/LevelSnapshot.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsStats.h"
#include "PropertyInfoHelpers.h"
#include "PropertySelection.h"

#include "Algo/Find.h"
#include "DebugViewModeHelpers.h"
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
	const TWeakPtr<FLevelSnapshotsEditorResultsRow>& DirectParent)
{
	DisplayName = InDisplayName;
	RowType = InRowType;
	WidgetCheckedState = StartingWidgetCheckboxState;

	if (DirectParent.IsValid())
	{
		DirectParentRow = DirectParent;
	}
}

void FLevelSnapshotsEditorResultsRow::InitHeaderRow(const ELevelSnapshotsEditorResultsTreeViewHeaderType InHeaderType,
	const TArray<FText>& InColumns, const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView)
{
	HeaderType = InHeaderType;
	HeaderColumns = InColumns;
	ResultsViewPtr = InResultsView;
}

void FLevelSnapshotsEditorResultsRow::InitAddedActorRow(AActor* InAddedActor)
{
	WorldObject = InAddedActor;
}

void FLevelSnapshotsEditorResultsRow::InitRemovedActorRow(const FSoftObjectPath& InRemovedActorPath,
                                                          const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView)
{
	RemovedActorPath = InRemovedActorPath;
	ResultsViewPtr = InResultsView;
}

void FLevelSnapshotsEditorResultsRow::InitActorRow(
	AActor* InSnapshotActor, AActor* InWorldActor,
	const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView)
{
	SnapshotObject = InSnapshotActor;
	WorldObject = InWorldActor;
	ResultsViewPtr = InResultsView;
}

void FLevelSnapshotsEditorResultsRow::InitObjectRow(
	UObject* InSnapshotObject, UObject* InWorldObject,
	const TWeakPtr<FRowGeneratorInfo>& InSnapshotRowGenerator,
	const TWeakPtr<FRowGeneratorInfo>& InWorldRowGenerator,
	const TWeakPtr<SLevelSnapshotsEditorResults>& InResultsView)
{
	SnapshotObject = InSnapshotObject;
	WorldObject = InWorldObject;
	SnapshotRowGeneratorInfo = InSnapshotRowGenerator;
	WorldRowGeneratorInfo = InWorldRowGenerator;
	ResultsViewPtr = InResultsView;
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
}

void FLevelSnapshotsEditorResultsRow::GenerateActorGroupChildren(FPropertySelectionMap& PropertySelectionMap)
{
	struct FLocalPropertyLooper
	{
		static TArray<TFieldPath<FProperty>> LoopOverProperties(
			const TWeakPtr<FRowGeneratorInfo>& InSnapshotRowGeneratorInfo, const TWeakPtr<FRowGeneratorInfo>& InWorldRowGeneratorInfo,
			const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow, const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter)
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
							ChildHierarchy, InDirectParentRow, PropertiesThatPassFilter, PropertyRowsGenerated, SnapshotHandleHierarchy);
				}
			}

			return PropertyRowsGenerated;
		}

		/* Do not pass in the base hierarchy, only children of the base hierarchy. */
		static void LoopOverHandleHierarchiesAndCreateRowHierarchy(const TWeakPtr<FPropertyHandleHierarchy>& InHierarchy,
			const TWeakPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow, const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter,
			TArray<TFieldPath<FProperty>>& PropertyRowsGenerated, const TWeakPtr<FPropertyHandleHierarchy>& InHierarchyToSearchForCounterparts = nullptr)
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
					InDirectParentRow);

				const TWeakPtr<FLevelSnapshotsEditorResultsRow>& ContainingObjectGroup = InDirectParentRow;

				NewProperty->InitPropertyRow(ContainingObjectGroup, 
						FoundHierarchy.IsValid() ? FoundHierarchy.Pin() : nullptr, 
						InHierarchy.IsValid() ? InHierarchy.Pin() : nullptr, bIsCounterpartValueSame);

				for (int32 ChildIndex = 0; ChildIndex < InHierarchy.Pin()->DirectChildren.Num(); ChildIndex++)
				{
					const TSharedRef<FPropertyHandleHierarchy>& ChildHierarchy = InHierarchy.Pin()->DirectChildren[ChildIndex];

					LoopOverHandleHierarchiesAndCreateRowHierarchy(ChildHierarchy, NewProperty, PropertiesThatPassFilter, PropertyRowsGenerated, InHierarchyToSearchForCounterparts);
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
				InDirectParentRow.IsValid() ? InDirectParentRow.Pin()->GetWidgetCheckedState() : ECheckBoxState::Checked, InDirectParentRow);

			// Create Row Generators for object and counterpart
			const TWeakPtr<FRowGeneratorInfo>& RowGeneratorInfo = InResultsView.Pin()->RegisterRowGenerator(NewComponentGroup, ObjectType_World, PropertyEditorModule);
			
			TWeakPtr<FRowGeneratorInfo> CounterpartRowGeneratorInfo = nullptr;

			RowGeneratorInfo.Pin()->GetGeneratorObject().Pin()->SetObjects({ InComponent });

			if (CounterpartComponent)
			{
				CounterpartRowGeneratorInfo = InResultsView.Pin()->RegisterRowGenerator(NewComponentGroup, ObjectType_Snapshot, PropertyEditorModule);
				
				CounterpartRowGeneratorInfo.Pin()->GetGeneratorObject().Pin()->SetObjects({ CounterpartComponent });
			}
			
			NewComponentGroup->InitObjectRow(InComponent, CounterpartComponent, CounterpartRowGeneratorInfo, RowGeneratorInfo, InResultsView);

			const TArray<TFieldPath<FProperty>> PropertyRowsGenerated = FLocalPropertyLooper::LoopOverProperties(CounterpartRowGeneratorInfo, RowGeneratorInfo, NewComponentGroup, PropertiesThatPassFilter);

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
				SharedThis(this), PropertySelection->GetSelectedLeafProperties());
			
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

const TArray<FLevelSnapshotsEditorResultsRowPtr>& FLevelSnapshotsEditorResultsRow::GetChildRows() const
{
	return ChildRows;
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
		const FString& SearchTerms = GetSearchTerms();

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

void FLevelSnapshotsEditorResultsRow::SetWidgetCheckedState(const ECheckBoxState NewState, const bool bUserClicked)
{
	WidgetCheckedState = NewState;

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

	if (GetRowType() == ActorGroup && ResultsViewPtr.IsValid())
	{
		ResultsViewPtr.Pin()->UpdateSnapshotInformationText();
	}
}

bool FLevelSnapshotsEditorResultsRow::GetIsNodeChecked() const
{
	return GetWidgetCheckedState() == ECheckBoxState::Checked ? true : false;
}

void FLevelSnapshotsEditorResultsRow::SetIsNodeChecked(const bool bNewChecked)
{
	SetWidgetCheckedState(bNewChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
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
	return GetDoesRowMatchSearchTerms();
}

EVisibility FLevelSnapshotsEditorResultsRow::GetDesiredVisibility() const
{
	return ShouldRowBeVisible() ? EVisibility::Visible : EVisibility::Collapsed;
}

const FString& FLevelSnapshotsEditorResultsRow::GetSearchTerms() const
{
	return GetDisplayName().ToString();
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

			PinnedParent->WidgetCheckedState = NewWidgetCheckedState;
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

	OnActiveSnapshotChangedHandle = InEditorData->OnActiveSnapshotChanged.AddSP(this, &SLevelSnapshotsEditorResults::OnSnapshotSelected);

	OnRefreshResultsHandle = InEditorData->OnRefreshResults.AddSP(this, &SLevelSnapshotsEditorResults::RefreshResults);

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
					.Text(NSLOCTEXT("LevelSnapshots", "LevelSnapshots", "Level Snapshots"))
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
						SNew(SVerticalBox)

						+SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)

							+SHorizontalBox::Slot()
							.AutoWidth()
							[
								SAssignNew(SelectedActorCountText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
								.Justification(ETextJustify::Right)
							]

							+SHorizontalBox::Slot()
							.AutoWidth()
							[
								SAssignNew(TotalActorCountText, STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
								.Justification(ETextJustify::Right)
							]
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
	
	ResultsSearchBoxPtr.Reset();
	ResultsBoxContainerPtr.Reset();

	SplitterManagerPtr.Reset();

	SelectedSnapshotNamePtr.Reset();

	SelectedActorCountText.Reset();
	TotalActorCountText.Reset();
	MiscActorCountText.Reset();

	DummyRow.Reset();

	FlushMemory();

	TreeViewPtr.Reset();
}

FMenuBuilder SLevelSnapshotsEditorResults::BuildShowOptionsMenu()
{
	FMenuBuilder ShowOptionsMenuBuilder = FMenuBuilder(true, nullptr);

	ShowOptionsMenuBuilder.AddMenuEntry(
		LOCTEXT("ShowFilteredActors", "Show Filtered Actors"),
		LOCTEXT("ShowFilteredActors_Tooltip", "If false, only displays actors which have passed the applied filter. Refresh Results to apply."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
				SetShowFilteredActors(!GetShowFilteredActors());
				}),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SLevelSnapshotsEditorResults::GetShowFilteredActors)
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	ShowOptionsMenuBuilder.AddMenuEntry(
		LOCTEXT("ShowUnselectedActors", "Show Unselected Actors"),
		LOCTEXT("ShowUnselectedActors_Tooltip", "If false, unselected actors will be hidden from view."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
				SetShowUnselectedActors(!GetShowUnselectedActors());
				}),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(this, &SLevelSnapshotsEditorResults::GetShowUnselectedActors)
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

void SLevelSnapshotsEditorResults::SetShowFilteredActors(const bool bNewSetting)
{
	bShowFilteredActors = bNewSetting;
}

void SLevelSnapshotsEditorResults::SetShowUnselectedActors(const bool bNewSetting)
{
	bShowUnselectedActors = bNewSetting;
}

bool SLevelSnapshotsEditorResults::GetShowFilteredActors() const
{
	return bShowFilteredActors;
}

bool SLevelSnapshotsEditorResults::GetShowUnselectedActors() const
{
	return bShowUnselectedActors;
}

void SLevelSnapshotsEditorResults::FlushMemory()
{
	TreeViewRootHeaderObjects.Empty();
	TreeViewModifiedActorGroupObjects.Empty();
	TreeViewAddedActorGroupObjects.Empty();
	TreeViewRemovedActorGroupObjects.Empty();

	CleanUpGenerators();
}

TOptional<ULevelSnapshot*> SLevelSnapshotsEditorResults::GetSelectedLevelSnapshot() const
{
	return ensure(EditorDataPtr.IsValid()) ? EditorDataPtr->GetActiveSnapshot() : TOptional<ULevelSnapshot*>();
}

void SLevelSnapshotsEditorResults::OnSnapshotSelected(const TOptional<ULevelSnapshot*>& InLevelSnapshot)
{	
	if (InLevelSnapshot.IsSet())
	{
		ULevelSnapshot* Snapshot = InLevelSnapshot.GetValue();

		if (SelectedSnapshotNamePtr.IsValid())
		{
			FName SnapshotName = Snapshot->GetSnapshotName();
			if (SnapshotName == "None")
			{
				// If a bespoke snapshot name is not provided then we'll use the asset name. Usually these names are the same.
				SnapshotName = Snapshot->GetFName();
			}
			
			SelectedSnapshotNamePtr->SetText(FText::FromName(SnapshotName));
		}
		
		GenerateTreeView();
	}
}

void SLevelSnapshotsEditorResults::RefreshResults()
{
	GenerateTreeView();
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

void SLevelSnapshotsEditorResults::UpdateSnapshotInformationText()
{
	check(EditorDataPtr.IsValid());
	
	int32 SelectedActorCount = 0;
	const int32 TotalPassingActorCount = TreeViewModifiedActorGroupObjects.Num();
	const int32 TotalSceneActorsCount = GetNumActorsInWorld(EditorDataPtr->GetSelectedWorld());
	
	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ActorGroup : TreeViewModifiedActorGroupObjects)
	{
		if (ActorGroup->GetWidgetCheckedState() != ECheckBoxState::Unchecked)
		{
			SelectedActorCount++;
		}
	}

	if (SelectedActorCountText.IsValid())
	{
		SelectedActorCountText->SetText(FText::Format(LOCTEXT("ResultsRestoreInfoFormatSelectedActorCount", "{0} actor(s)"),
			FText::AsNumber(SelectedActorCount)));
	}

	if (TotalActorCountText.IsValid())
	{
		TotalActorCountText->SetText(FText::Format(LOCTEXT("ResultsRestoreInfoFormatTotalActorCount", " (of {0} in snapshot) will be restored"),
			FText::AsNumber(TotalPassingActorCount)));
	}

	if (MiscActorCountText.IsValid())
	{
		MiscActorCountText->SetText(FText::Format(LOCTEXT("ResultsRestoreInfoFormatMiscActorCounts", "{0} filtered, {1} unselected"),
			FText::AsNumber(TotalSceneActorsCount - TotalPassingActorCount), FText::AsNumber(TotalPassingActorCount - SelectedActorCount)));
	}
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

			// Make a copy of the property selection. If a node is unchecked, we'll remove it from the copy.
			FPropertySelection CheckedNodeFieldPaths = *SelectionMap.GetSelectedProperties(Group->GetWorldObject());
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

	FPropertySelectionMap PropertySelectionMap = FilterListData.GetModifiedActorsSelectedProperties();

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

const FString& SLevelSnapshotsEditorResults::GetSearchStringFromSearchInputField() const
{
	return ResultsSearchBoxPtr->GetText().ToString();
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

void SLevelSnapshotsEditorResults::CleanUpGenerators()
{
	for (TSharedPtr<FRowGeneratorInfo>& Generator : RegisteredRowGenerators)
	{		
		if (Generator.IsValid())
		{
			Generator->FlushReferences();
			Generator.Reset();			
		}
	}

	RegisteredRowGenerators.Empty();
}

FLevelSnapshotsEditorResultsRowPtr& SLevelSnapshotsEditorResults::GetOrCreateDummyRow()
{
	if (!DummyRow.IsValid())
	{
		DummyRow = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(
				FText::FromString("Dummy"), FLevelSnapshotsEditorResultsRow::None, ECheckBoxState::Undetermined, nullptr));
	}
	
	return DummyRow;
}

void SLevelSnapshotsEditorResults::GenerateTreeView()
{	
	if (!ensure(EditorDataPtr.IsValid()) || !ensure(TreeViewPtr.IsValid()))
	{
		return;
	}

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("GenerateTreeView"), STAT_GenerateTreeView, STATGROUP_LevelSnapshots);
	
	FlushMemory();
	
	UFilteredResults* FilteredResults = EditorDataPtr->GetFilterResults(); 
	const TWeakObjectPtr<ULevelSnapshotFilter>& UserFilters = FilteredResults->GetUserFilters();

	FilteredResults->UpdateFilteredResults();

	FilterListData = FilteredResults->GetFilteredData();
	
	SplitterManagerPtr = MakeShared<FLevelSnapshotsEditorResultsSplitterManager>(FLevelSnapshotsEditorResultsSplitterManager());

	// Create root headers
	if (FilterListData.GetModifiedFilteredActors().Num())
	{
		FLevelSnapshotsEditorResultsRowPtr ModifiedActorsHeader = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::GetEmpty(), FLevelSnapshotsEditorResultsRow::TreeViewHeader, ECheckBoxState::Checked));
		ModifiedActorsHeader->InitHeaderRow(FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsTreeViewHeaderType::HeaderType_ModifiedActors, {
			LOCTEXT("ColumnName_Actor", "Actor"),
			LOCTEXT("ColumnName_ValueToRestore", "Value to Restore"),
			LOCTEXT("ColumnName_CurrentValue", "Current Value")
			},
			SharedThis(this));

		if (GenerateTreeViewChildren_ModifiedActors(ModifiedActorsHeader, UserFilters.Get()))
		{
			TreeViewRootHeaderObjects.Add(ModifiedActorsHeader);
		}
		else
		{
			ModifiedActorsHeader.Reset();
		}
	}

	if (FilterListData.GetFilteredAddedWorldActors().Num())
	{
		FLevelSnapshotsEditorResultsRowPtr AddedActorsHeader = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::GetEmpty(), FLevelSnapshotsEditorResultsRow::TreeViewHeader, ECheckBoxState::Checked));
		AddedActorsHeader->InitHeaderRow(FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsTreeViewHeaderType::HeaderType_AddedActors, 
			{ LOCTEXT("ColumnName_AddedActors", "Added Actors") }, SharedThis(this));

		if (GenerateTreeViewChildren_AddedActors(AddedActorsHeader))
		{
			TreeViewRootHeaderObjects.Add(AddedActorsHeader);
		}
		else
		{
			AddedActorsHeader.Reset();
		}
	}
	
	if (FilterListData.GetFilteredRemovedOriginalActorPaths().Num())
	{
		FLevelSnapshotsEditorResultsRowPtr RemovedActorsHeader = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::GetEmpty(), FLevelSnapshotsEditorResultsRow::TreeViewHeader, ECheckBoxState::Checked));
		RemovedActorsHeader->InitHeaderRow(FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsTreeViewHeaderType::HeaderType_RemovedActors, 
			{ LOCTEXT("ColumnName_RemovedActors", "Removed Actors") }, SharedThis(this));

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
}

bool SLevelSnapshotsEditorResults::GenerateTreeViewChildren_ModifiedActors(FLevelSnapshotsEditorResultsRowPtr ModifiedActorsHeader, ULevelSnapshotFilter* UserFilters)
{
	check(ModifiedActorsHeader);
	
	const TSet<TWeakObjectPtr<AActor>>& ActorsToConsider = FilterListData.GetModifiedFilteredActors();

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

		const int32 KeyCountBeforeFilter = FilterListData.GetModifiedActorsSelectedProperties().GetKeyCount();

		// Get remaining properties after filter
		if (UserFilters)
		{
			FilterListData.ApplyFilterToFindSelectedProperties(WorldActor, UserFilters);
		}

		const int32 KeyCountAfterFilter = FilterListData.GetModifiedActorsSelectedProperties().GetKeyCount();
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
			FLevelSnapshotsEditorResultsRow(FText::FromString(ActorName), FLevelSnapshotsEditorResultsRow::ActorGroup, ECheckBoxState::Checked, ModifiedActorsHeader));
		NewActorGroup->InitActorRow(WeakSnapshotActor.IsValid() ? WeakSnapshotActor.Get() : nullptr, 
			WeakWorldActor.IsValid() ? WeakWorldActor.Get() : nullptr, SharedThis(this));

		ModifiedActorsHeader->AddToChildRows(NewActorGroup);
		
		TreeViewModifiedActorGroupObjects.Add(NewActorGroup);
	}

	return TreeViewModifiedActorGroupObjects.Num() > 0;
}

bool SLevelSnapshotsEditorResults::GenerateTreeViewChildren_AddedActors(FLevelSnapshotsEditorResultsRowPtr AddedActorsHeader)
{
	check(AddedActorsHeader);
	
	for (const TWeakObjectPtr<AActor>& Actor : FilterListData.GetFilteredAddedWorldActors())
	{
		if (!Actor.IsValid())
		{
			continue;
		}
		
		// Create group
		FLevelSnapshotsEditorResultsRowPtr NewActorRow = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::FromString(Actor.Get()->GetName()), FLevelSnapshotsEditorResultsRow::AddedActor, ECheckBoxState::Checked, AddedActorsHeader));
		NewActorRow->InitAddedActorRow(Actor.Get());

		AddedActorsHeader->AddToChildRows(NewActorRow);

		TreeViewAddedActorGroupObjects.Add(NewActorRow);
	}

	return TreeViewAddedActorGroupObjects.Num() > 0;
}

bool SLevelSnapshotsEditorResults::GenerateTreeViewChildren_RemovedActors(FLevelSnapshotsEditorResultsRowPtr RemovedActorsHeader)
{
	check(RemovedActorsHeader);
	
	for (const FSoftObjectPath& ActorPath : FilterListData.GetFilteredRemovedOriginalActorPaths())
	{
		FString ActorName = ActorPath.GetSubPathString().IsEmpty() ? ActorPath.GetAssetName() : ActorPath.GetSubPathString();

		if (ActorName.Contains("."))
		{
			ActorName = ActorName.Right(ActorName.Len() - ActorName.Find(".", ESearchCase::IgnoreCase, ESearchDir::FromEnd) - 1);
		}
		
		// Create group
		FLevelSnapshotsEditorResultsRowPtr NewActorRow = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::FromString(ActorName), FLevelSnapshotsEditorResultsRow::RemovedActor, ECheckBoxState::Checked, RemovedActorsHeader));
		NewActorRow->InitRemovedActorRow(ActorPath, SharedThis(this));

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

void SLevelSnapshotsEditorResults::OnResultsViewSearchTextChanged(const FText& Text)
{
	ExecuteResultsViewSearch(Text.ToString());
}

void SLevelSnapshotsEditorResults::ExecuteResultsViewSearch(const FString& SearchString)
{
	TArray<FString> Tokens;
	
	// unquoted search equivalent to a match-any-of search
	SearchString.ParseIntoArray(Tokens, TEXT(" "), true);
	
	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ChildRow : TreeViewModifiedActorGroupObjects)
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
				FPropertySelectionMap PropertySelectionMap = FilterListData.GetModifiedActorsSelectedProperties();
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

void SLevelSnapshotsEditorResults::OnRowChildExpansionChange(FLevelSnapshotsEditorResultsRowPtr Row, const bool bIsExpanded)
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
	
	if (bIsSinglePropertyInCollection)
	{
		Tooltip = LOCTEXT("LevelSnapshotsEditorResults_CollectionDisclaimer", "Individual members of collections cannot be selected. The whole collection will be restored.");
	}
	else if (RowType == FLevelSnapshotsEditorResultsRow::ComponentGroup)
	{
		Tooltip = LOCTEXT("LevelSnapshotsEditorResults_ComponentOrderDisclaimer", "Please note that component order reflects the order in the world, not the snapshot. LevelSnapshots does not alter component order.");
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

	ChildSlot
	[
		SNew(SBox)
		.Padding(FMargin(5,2))
		[
			SNew(SBorder)
			.ToolTipText(Tooltip)
			.Padding(FMargin(0, 5))
			.BorderImage_Lambda([RowType]()
				{
					switch (RowType)
					{
						case FLevelSnapshotsEditorResultsRow::CollectionGroup:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.CollectionGroupBorder");

						case FLevelSnapshotsEditorResultsRow::StructGroup:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.StructGroupBorder");
							
						case FLevelSnapshotsEditorResultsRow::ComponentGroup:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.ComponentGroupBorder");

						case FLevelSnapshotsEditorResultsRow::SubObjectGroup:
							return FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.SubObjectGroupBorder");

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
			[
				SAssignNew(SplitterPtr, SSplitter)
				.Style(FEditorStyle::Get(), "DetailsView.Splitter")
				.PhysicalSplitterHandleSize(1.0f)
				.HitDetectionSplitterHandleSize(5.0f)

				+ SSplitter::Slot()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					.AutoWidth()
					.Padding(5.f, 2.f)
					[
						SNew(SCheckBox)
						.Visibility(bIsSinglePropertyInCollection ? EVisibility::Hidden : EVisibility::Visible)
						.IsChecked_Raw(PinnedItem.Get(), &FLevelSnapshotsEditorResultsRow::GetWidgetCheckedState)
						.OnCheckStateChanged_Raw(PinnedItem.Get(), &FLevelSnapshotsEditorResultsRow::SetWidgetCheckedState, true)
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Left)
					[
						SNew(STextBlock)
						.Text(DisplayText)
					]
				]
			]
		]
	];

	// Create value widgets

	// Splitter Slot 0
	SplitterPtr->SlotAt(0).OnSlotResized_Handler.BindSP(this, &SLevelSnapshotsEditorResultsRow::SetNameColumnSize);

	const auto SlotDelegate0 = TAttribute<float>::FGetter::CreateSP(this, &SLevelSnapshotsEditorResultsRow::GetSplitterSlotSize, 0);;
	SplitterPtr->SlotAt(0).SizeValue.Bind(SlotDelegate0);
		
	// Splitter Slot 1
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
		if (bIsHeaderRow && PinnedItem->GetHeaderColumns().Num() > 1)
		{
			SnapshotChildWidget = SNew(STextBlock).Text(PinnedItem->GetHeaderColumns()[1]);
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
	[
		FinalSnapshotWidget.ToSharedRef()
	].OnSlotResized_Handler.BindSP(this, &SLevelSnapshotsEditorResultsRow::SetSnapshotColumnSize);

	const auto SlotDelegate1 = TAttribute<float>::FGetter::CreateSP(this, &SLevelSnapshotsEditorResultsRow::GetSplitterSlotSize, 1);;
	SplitterPtr->SlotAt(1).SizeValue.Bind(SlotDelegate1);

	// Splitter Slot 2
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
		if (bIsHeaderRow && PinnedItem->GetHeaderColumns().Num() > 2)
		{
			WorldChildWidget = SNew(STextBlock).Text(PinnedItem->GetHeaderColumns()[2]);
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
	[
		FinalWorldWidget.ToSharedRef()
	].OnSlotResized_Handler.BindSP(this, &SLevelSnapshotsEditorResultsRow::SetWorldObjectColumnSize);

	const auto SlotDelegate2 = TAttribute<float>::FGetter::CreateSP(this, &SLevelSnapshotsEditorResultsRow::GetSplitterSlotSize, 2);;
	SplitterPtr->SlotAt(2).SizeValue.Bind(SlotDelegate2);
}

SLevelSnapshotsEditorResultsRow::~SLevelSnapshotsEditorResultsRow()
{
	// Remove delegate bindings

	// Unbind event to the splitter being resized first
	if (SplitterPtr.IsValid())
	{
		for (int32 SplitterSlotCount = 0; SplitterSlotCount < SplitterPtr->GetChildren()->Num(); SplitterSlotCount++)
		{
			SplitterPtr->SlotAt(SplitterSlotCount).OnSlotResized_Handler.Unbind();
		}
	}

	SplitterPtr.Reset();
	SplitterManagerPtr.Reset();
}

float SLevelSnapshotsEditorResultsRow::GetSplitterSlotSize(const int32 SlotIndex) const
{
	float SplitterSlotSize = 1.0f;
	
	if (ensure(SplitterPtr.IsValid()))
	{
		switch (SlotIndex)
		{
			default:
				SplitterSlotSize = 1.0f;
				break;

			case 0:
				SplitterSlotSize = SplitterManagerPtr->NameColumnWidth;
				break;

			case 1:
				SplitterSlotSize = SplitterManagerPtr->SnapshotPropertyColumnWidth;
				break;

			case 2:
				SplitterSlotSize = SplitterManagerPtr->WorldObjectPropertyColumnWidth;
				break;
		}
	}

	return SplitterSlotSize;
}

void SLevelSnapshotsEditorResultsRow::SetNameColumnSize(const float InWidth) const
{
	SplitterManagerPtr->NameColumnWidth = InWidth;
}

void SLevelSnapshotsEditorResultsRow::SetSnapshotColumnSize(const float InWidth) const
{
	SplitterManagerPtr->SnapshotPropertyColumnWidth = InWidth;
}

void SLevelSnapshotsEditorResultsRow::SetWorldObjectColumnSize(const float InWidth) const
{
	SplitterManagerPtr->WorldObjectPropertyColumnWidth = InWidth;
}

#undef LOCTEXT_NAMESPACE
