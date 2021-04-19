// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Results/SLevelSnapshotsEditorResults.h"

#include "Data/FilteredResults.h"
#include "Data/LevelSnapshot.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotSelections.h"
#include "LevelSnapshotsStats.h"
#include "PropertyInfoHelpers.h"
#include "PropertySelection.h"

#include "Algo/Find.h"
#include "EditorStyleSet.h"
#include "GameFramework/Actor.h"
#include "IDetailPropertyRow.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Stats/StatsMisc.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

FLevelSnapshotsEditorResultsRow::~FLevelSnapshotsEditorResultsRow()
{
	ChildRows.Empty();
	
	DirectParentRow.Reset();
	ContainingObjectGroup.Reset();

	SnapshotRowGenerator.Reset();
	WorldRowGenerator.Reset();

	SnapshotPropertyHandle.Reset();
	WorldPropertyHandle.Reset();
}

FLevelSnapshotsEditorResultsRow::FLevelSnapshotsEditorResultsRow(const FText InDisplayName,
	const ELevelSnapshotsEditorResultsRowType InRowType, const ECheckBoxState StartingWidgetCheckboxState,
	const FLevelSnapshotsEditorResultsRowPtr DirectParent)
{
	DisplayName = InDisplayName;
	RowType = InRowType;
	WidgetCheckedState = StartingWidgetCheckboxState;
	DirectParentRow = DirectParent;
}

void FLevelSnapshotsEditorResultsRow::InitActorRow(
	const TWeakObjectPtr<AActor>& InSnapshotActor, const TWeakObjectPtr<AActor>& InWorldActor)
{
	SnapshotActor = InSnapshotActor;
	WorldActor = InWorldActor;
}

void FLevelSnapshotsEditorResultsRow::InitObjectRow(const TSharedPtr<IPropertyRowGenerator> InSnapshotRowGenerator,
                                                            const TSharedPtr<IPropertyRowGenerator> InWorldRowGenerator)
{
	SnapshotRowGenerator = InSnapshotRowGenerator;
	WorldRowGenerator = InWorldRowGenerator;
}

void FLevelSnapshotsEditorResultsRow::InitPropertyRowWithHandles(
	const FLevelSnapshotsEditorResultsRowPtr InContainingObjectGroup,
	TSharedPtr<IPropertyHandle> InSnapshotHandle, TSharedPtr<IPropertyHandle> InWorldHandle, 
	const bool bNewIsCounterpartValueSame, const ELevelSnapshotsWidgetTypeCustomization InWidgetTypeCustomization)
{
	ContainingObjectGroup = InContainingObjectGroup;
	SnapshotPropertyHandle = InSnapshotHandle;
	WorldPropertyHandle = InWorldHandle;
	bIsCounterpartValueSame = bNewIsCounterpartValueSame;
	WidgetTypeCustomization = InWidgetTypeCustomization;
}

void FLevelSnapshotsEditorResultsRow::InitPropertyRowWithObjectsAndProperty(
	const FLevelSnapshotsEditorResultsRowPtr InContainingObjectGroup,
	UObject* InSnapshotObject, UObject* InWorldObject,
	FProperty* InPropertyForCustomization, const ELevelSnapshotsWidgetTypeCustomization InWidgetTypeCustomization)
{
	ContainingObjectGroup = InContainingObjectGroup;
	SnapshotObjectForCustomization = InSnapshotObject;
	WorldObjectForCustomization = InWorldObject;
	PropertyForCustomization = InPropertyForCustomization;
	WidgetTypeCustomization = InWidgetTypeCustomization;
}

void FLevelSnapshotsEditorResultsRow::GenerateActorGroupChildren(FPropertySelectionMap& PropertySelectionMap)
{
	struct FLocalPropertyLooper
	{
		struct FPropertyHandleHierarchy
		{
			FPropertyHandleHierarchy(const TSharedPtr<IPropertyHandle> InHandle)
				: Handle(InHandle)
			{
				const FString& DefaultPropertyName = "Base";

				if (InHandle.IsValid())
				{
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
				else
				{
					PropertyName = DefaultPropertyName;
					ParentPropertyName = DefaultPropertyName;
				}
			}

			// Used to identify counterparts
			FString PropertyName;
			FString ParentPropertyName;

			TSharedPtr<IPropertyHandle> Handle;
			TArray<FPropertyHandleHierarchy*> DirectChildren;
		};

		static TArray<TFieldPath<FProperty>> LoopOverProperties(
			const TSharedPtr<IPropertyRowGenerator> InSnapshotRowGenerator, const TSharedPtr<IPropertyRowGenerator> InWorldRowGenerator, const FLevelSnapshotsEditorResultsRowPtr& InDirectParentRow, const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter)
		{
			FPropertyHandleHierarchy* SnapshotHandleHierarchy = nullptr;
			FPropertyHandleHierarchy* WorldHandleHierarchy = nullptr;

			if (InSnapshotRowGenerator)
			{
				SnapshotHandleHierarchy = BuildPropertyHandleHierarchy(InSnapshotRowGenerator);
			}

			if (InWorldRowGenerator)
			{
				WorldHandleHierarchy = BuildPropertyHandleHierarchy(InWorldRowGenerator);
			}

			TArray<TFieldPath<FProperty>> PropertyRowsGenerated;

			// We start with World Hierarchy because it's more likely that the user wants to update existing actors than add/delete snapshot ones
			if (WorldHandleHierarchy)
			{
				// Don't bother with the first FPropertyHandleHierarchy because that's a dummy node to contain the rest
				for (int32 ChildIndex = 0; ChildIndex < WorldHandleHierarchy->DirectChildren.Num(); ChildIndex++)
				{
					if (FPropertyHandleHierarchy* ChildHierarchy = WorldHandleHierarchy->DirectChildren[ChildIndex])
					{
						LoopOverHandleHierarchiesAndCreateRowHierarchy(
							ChildHierarchy, InDirectParentRow.IsValid() ? InDirectParentRow : nullptr, PropertiesThatPassFilter, PropertyRowsGenerated, SnapshotHandleHierarchy);
					}
				}
			}

			return PropertyRowsGenerated;
		}

		/* Do not pass in the base hierarchy, only immediate children of the base hierarchy. */
		static void LoopOverHandleHierarchiesAndCreateRowHierarchy(FPropertyHandleHierarchy* InHierarchy, const FLevelSnapshotsEditorResultsRowPtr& InDirectParentRow, const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter, TArray<TFieldPath<FProperty>>& PropertyRowsGenerated, FPropertyHandleHierarchy* CounterpartHierarchy = nullptr)
		{
			if (!ensure(InHierarchy) || !ensure(InDirectParentRow.IsValid()))
			{
				return;
			}

			const TSharedPtr<IPropertyHandle>& Handle = InHierarchy->Handle;

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

			const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType InRowType = FLevelSnapshotsEditorResultsRow::DetermineRowTypeFromProperty(Property);
			if (!ensure(InRowType != FLevelSnapshotsEditorResultsRow::None))
			{
				return;
			}

			const TFieldPath<FProperty> PropertyField(Property);

			const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType ParentRowType = InDirectParentRow->GetRowType();
			const bool bIsParentRowContainer =
				(ParentRowType == FLevelSnapshotsEditorResultsRow::CollectionGroup || ParentRowType == FLevelSnapshotsEditorResultsRow::StructGroup);
			const bool bIsPropertyFilteredOut = !bIsParentRowContainer && !PropertiesThatPassFilter.Contains(PropertyField);
			if (bIsPropertyFilteredOut) { return; }

			bool bIsCounterpartValueSame = false;

			TSharedPtr<IPropertyHandle> CounterpartHandle = nullptr;

			if (CounterpartHierarchy)
			{
				bool bFoundMatch = false;
				FindCorrespondingHandle(InHierarchy->ParentPropertyName, InHierarchy->PropertyName, CounterpartHierarchy, CounterpartHandle, bFoundMatch);

				if (bFoundMatch && CounterpartHandle.IsValid() &&
					(InRowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap || InRowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInSetOrArray))
				{
					FString ValueA;
					FString ValueB;

					Handle->GetValueAsFormattedString(ValueA);
					CounterpartHandle->GetValueAsFormattedString(ValueB);

					bIsCounterpartValueSame = ValueA.Equals(ValueB);
				}
			}

			if (!bIsCounterpartValueSame)
			{
				const ECheckBoxState StartingCheckedState = (bIsPropertyFilteredOut || bIsCounterpartValueSame) ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;

				const FText DisplayName = Handle->GetPropertyDisplayName();

				// Create property
				FLevelSnapshotsEditorResultsRow NewProperty(DisplayName, InRowType, StartingCheckedState, InDirectParentRow);

				const FLevelSnapshotsEditorResultsRowPtr& ContainingObjectGroup = InDirectParentRow;

				NewProperty.InitPropertyRowWithHandles(ContainingObjectGroup, CounterpartHandle ? CounterpartHandle : nullptr, Handle ? Handle : nullptr, bIsCounterpartValueSame);

				FLevelSnapshotsEditorResultsRowPtr SharedNewPropertyRow = MakeShared<FLevelSnapshotsEditorResultsRow>(NewProperty);

				for (int32 ChildIndex = 0; ChildIndex < InHierarchy->DirectChildren.Num(); ChildIndex++)
				{
					if (FPropertyHandleHierarchy* ChildHierarchy = InHierarchy->DirectChildren[ChildIndex])
					{
						LoopOverHandleHierarchiesAndCreateRowHierarchy(ChildHierarchy, SharedNewPropertyRow, PropertiesThatPassFilter, PropertyRowsGenerated, CounterpartHierarchy);
					}
				}

				if (SharedNewPropertyRow->DoesRowRepresentGroup() && !SharedNewPropertyRow->GetChildRows().IsValidIndex(0))
				{
					// No valid children, destroy group
					SharedNewPropertyRow.Reset();
				}
				else
				{
					InDirectParentRow->AddToChildRows(SharedNewPropertyRow);

					PropertyRowsGenerated.Add(PropertyField);
				}
			}
		}

		/* Finds a hierarchy entry recursively */
		static void FindCorrespondingHandle(const FString& InParentHandleDisplayName, const FString& InTargetHandleDisplayName, FPropertyHandleHierarchy* HierarchyToSearch, TSharedPtr<IPropertyHandle>& OutHandle, bool& bFoundMatch)
		{
			if (!ensureMsgf(HierarchyToSearch, TEXT("FindCorrespondingHandle: HierarchyToSearch was nullptr")))
			{
				return;
			}

			for (FPropertyHandleHierarchy* ChildHierarchy : HierarchyToSearch->DirectChildren)
			{
				if (ChildHierarchy)
				{
					const bool bIsSameParentName = ChildHierarchy->ParentPropertyName.Equals(InParentHandleDisplayName);
					const bool bIsSamePropertyName = ChildHierarchy->PropertyName.Equals(InTargetHandleDisplayName);

					if (bIsSamePropertyName && bIsSameParentName)
					{
						OutHandle = ChildHierarchy->Handle;
						bFoundMatch = true;
						break;
					}
				}


				if (bFoundMatch)
				{
					break;
				}
				else
				{
					if (ChildHierarchy && ChildHierarchy->DirectChildren.Num() > 0)
					{
						FindCorrespondingHandle(InParentHandleDisplayName, InTargetHandleDisplayName, ChildHierarchy, OutHandle, bFoundMatch);
					}
				}
			}
		}

		/* A helper function called by BuildPropertyHandleHierarchy */
		static void CreatePropertyHandleHierarchyChildrenRecursively(const TSharedRef<IDetailTreeNode>& InNode, FPropertyHandleHierarchy* InParentHierarchy)
		{
			if (!ensureMsgf(InParentHierarchy, TEXT("CreatePropertyHandleHierarchyChildrenRecursively: InParentHierarchy was nullptr. Check to see that InParentHierarchy exists before calling this method.")))
			{
				return;
			}

			FPropertyHandleHierarchy* HierarchyToPass = InParentHierarchy;

			const EDetailNodeType NodeType = InNode->GetNodeType();

			if (NodeType == EDetailNodeType::Item)
			{
				const TSharedPtr<IDetailPropertyRow> Row = InNode->GetRow();
				if (Row.IsValid())
				{
					const TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
					if (Handle.IsValid())
					{
						FPropertyHandleHierarchy* NewHierarchy = new FPropertyHandleHierarchy(Handle);

						HierarchyToPass = NewHierarchy;

						InParentHierarchy->DirectChildren.Add(NewHierarchy);
					}
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
		static FPropertyHandleHierarchy* BuildPropertyHandleHierarchy(const TSharedPtr<IPropertyRowGenerator>& InRowGenerator)
		{
			check(InRowGenerator.IsValid());

			// Create a base hierarchy with dummy info and no handle
			FPropertyHandleHierarchy* ReturnHierarchy = new FPropertyHandleHierarchy(nullptr);

			for (const TSharedRef<IDetailTreeNode>& Node : InRowGenerator->GetRootTreeNodes())
			{
				CreatePropertyHandleHierarchyChildrenRecursively(Node, ReturnHierarchy);
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
			TArray<FComponentHierarchy*> DirectChildren;
		};

		static FPropertyRowGeneratorArgs GetLevelSnapshotsAppropriatePropertyRowGeneratorArgs()
		{
			FPropertyRowGeneratorArgs Args;
			Args.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Show;
			Args.bAllowMultipleTopLevelObjects = false;
			Args.bShouldShowHiddenProperties = true;
			Args.bAllowEditingClassDefaultObjects = true;

			return Args;
		}

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

		static int32 CreateNewHierarchyStructInLoop(const AActor* InActor, USceneComponent* SceneComponent, TArray<FComponentHierarchy*>& AllHierarchies)
		{
			check(InActor);
			check(SceneComponent);
			
			FComponentHierarchy* NewHierarchy = new FComponentHierarchy(SceneComponent);

			const int32 ReturnValue = AllHierarchies.Add(NewHierarchy);

			USceneComponent* ParentComponent = SceneComponent->GetAttachParent();

			if (ParentComponent)
			{
				int32 IndexOfParentHierarchy = AllHierarchies.IndexOfByPredicate(
					[&ParentComponent](FComponentHierarchy* Hierarchy)
					{
						return Hierarchy->Component == ParentComponent;
					});

				if (IndexOfParentHierarchy == -1)
				{
					IndexOfParentHierarchy = CreateNewHierarchyStructInLoop(InActor, ParentComponent, AllHierarchies);
				}

				AllHierarchies[IndexOfParentHierarchy]->DirectChildren.Add(NewHierarchy);
			}

			return ReturnValue;
		}

		/* Creates a node tree of all scene components in an actor. Only scene components can have children. Non-scene actor components do not */
		static FComponentHierarchy* BuildComponentHierarchy(const AActor* InActor, TArray<UActorComponent*>& OutNonSceneComponents)
		{
			check(InActor);

			FComponentHierarchy* ReturnHierarchy = new FComponentHierarchy(InActor->GetRootComponent());

			// A flat representation of the hierarchy used for searching the hierarchy more easily
			TArray<FComponentHierarchy*> AllHierarchies;
			AllHierarchies.Add(ReturnHierarchy);

			TSet<UActorComponent*> AllActorComponents = InActor->GetComponents();

			for (UActorComponent* Component : AllActorComponents)
			{
				if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
				{
					const bool ComponentContained = AllHierarchies.ContainsByPredicate(
						[&SceneComponent](FComponentHierarchy* Hierarchy)
						{
							return Hierarchy->Component == SceneComponent;
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

		static void ForceBuildTransformRows(UActorComponent* InComponent, UActorComponent* CounterpartComponent, 
			const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter, TArray<TFieldPath<FProperty>>& PropertyRowsGenerated, FLevelSnapshotsEditorResultsRowPtr SharedNewGroup)
		{
			struct Local
			{
				static void CreateTransformWidgetObjectForComponentGroup(
					const FText DisplayName, UActorComponent* InComponent, UActorComponent* InCounterpartComponent, FProperty* InProperty, FLevelSnapshotsEditorResultsRowPtr SharedComponentGroup,
					const ELevelSnapshotsWidgetTypeCustomization WidgetType)
				{
					check(InComponent);
					check(InProperty);
					check(SharedComponentGroup.IsValid());
					
					// Create property row
					FLevelSnapshotsEditorResultsRow NewProperty(DisplayName, ELevelSnapshotsEditorResultsRowType::SingleProperty, ECheckBoxState::Checked, SharedComponentGroup);

					const FLevelSnapshotsEditorResultsRowPtr& ContainingObjectGroup = SharedComponentGroup;

					// There is no handle generated for the transform properties so handles are passed in nullptr
					NewProperty.InitPropertyRowWithObjectsAndProperty(ContainingObjectGroup, InCounterpartComponent ? InCounterpartComponent : nullptr, InComponent, InProperty, WidgetType);

					const FLevelSnapshotsEditorResultsRowPtr SharedNewPropertyRow = MakeShared<FLevelSnapshotsEditorResultsRow>(NewProperty);

					SharedComponentGroup->AddToChildRows(SharedNewPropertyRow);
				}
			};

			check(InComponent);
			check(SharedNewGroup.IsValid());
			
			if (InComponent->IsA(USceneComponent::StaticClass()))
			{
				if (FProperty* LocationProperty = FindFProperty<FStructProperty>(InComponent->GetClass(), "RelativeLocation"))
				{
					if (PropertiesThatPassFilter.Contains(LocationProperty))
					{
						Local::CreateTransformWidgetObjectForComponentGroup(
							FText::FromString("Relative Location"), InComponent, CounterpartComponent ? CounterpartComponent : nullptr, LocationProperty, SharedNewGroup, WidgetType_Location);

						PropertyRowsGenerated.Add(LocationProperty);
					}
				}
				else
				{
					UE_LOG(LogLevelSnapshots, Error, TEXT("%hs: No property found on Scene Component (%s) named RelativeLocation"), __FUNCTION__, *InComponent->GetName());
				}

				if (FProperty* RotationProperty = FindFProperty<FStructProperty>(InComponent->GetClass(), "RelativeRotation"))
				{
					if (PropertiesThatPassFilter.Contains(RotationProperty))
					{
						Local::CreateTransformWidgetObjectForComponentGroup(
							FText::FromString("Relative Rotation"), InComponent, CounterpartComponent ? CounterpartComponent : nullptr, RotationProperty, SharedNewGroup, WidgetType_Rotation);

						PropertyRowsGenerated.Add(RotationProperty);
					}
				}
				else
				{
					UE_LOG(LogLevelSnapshots, Error, TEXT("%hs: No property found on Scene Component (%s) named RelativeRotation"), __FUNCTION__, *InComponent->GetName());
				}

				if (FProperty* ScaleProperty = FindFProperty<FStructProperty>(InComponent->GetClass(), "RelativeScale3D"))
				{
					if (PropertiesThatPassFilter.Contains(ScaleProperty))
					{
						Local::CreateTransformWidgetObjectForComponentGroup(
							FText::FromString("Relative Scale"), InComponent, CounterpartComponent ? CounterpartComponent : nullptr, ScaleProperty, SharedNewGroup, WidgetType_Scale3D);

						PropertyRowsGenerated.Add(ScaleProperty);
					}
				}
				else
				{
					UE_LOG(LogLevelSnapshots, Error, TEXT("%hs: No property found on Scene Component (%s) named RelativeScale3D"), __FUNCTION__, *InComponent->GetName());
				}
			}
		}
		
		static void BuildRowForUnsupportedProperty(
			const FText DisplayName, UObject* InWorldObject, UObject* InSnapshotObject, FProperty* InProperty, FLevelSnapshotsEditorResultsRowPtr SharedObjectGroup)
		{
			check(InProperty);
			check(SharedObjectGroup.IsValid());
			
			// Create property row
			FLevelSnapshotsEditorResultsRow NewProperty(DisplayName, ELevelSnapshotsEditorResultsRowType::SingleProperty, ECheckBoxState::Checked, SharedObjectGroup);

			const FLevelSnapshotsEditorResultsRowPtr& ContainingObjectGroup = SharedObjectGroup;

			// There is no handle generated for the transform properties so handles are passed in nullptr
			NewProperty.InitPropertyRowWithObjectsAndProperty(
				ContainingObjectGroup, InSnapshotObject ? InSnapshotObject : nullptr, InWorldObject ? InWorldObject : nullptr, InProperty, ELevelSnapshotsWidgetTypeCustomization::WidgetType_UnsupportedProperty);

			const FLevelSnapshotsEditorResultsRowPtr SharedNewPropertyRow = MakeShared<FLevelSnapshotsEditorResultsRow>(NewProperty);

			SharedObjectGroup->AddToChildRows(SharedNewPropertyRow);
		}

		static void BuildNestedSceneComponentRowsRecursively(const FComponentHierarchy* InHierarchy, const TSet<UActorComponent*>& InCounterpartComponents,
			FPropertyEditorModule& PropertyEditorModule, const FPropertySelectionMap& PropertySelectionMap, const TSharedPtr<FLevelSnapshotsEditorResultsRow>& InDirectParentRow)
		{
			check(InHierarchy);

			USceneComponent* CurrentComponent = InHierarchy->Component.Get();

			if (const FPropertySelection* PropertySelection = PropertySelectionMap.GetSelectedProperties(CurrentComponent))
			{
				// Get remaining properties after filter
				if (!PropertySelection->IsEmpty())
				{
					const FLevelSnapshotsEditorResultsRowPtr ComponentPropertyAsRow =
						BuildComponentRow(
							CurrentComponent, InCounterpartComponents, PropertyEditorModule, PropertySelection->GetSelectedLeafProperties(), InDirectParentRow);

					if (ComponentPropertyAsRow.IsValid())
					{
						for (const FComponentHierarchy* ChildHierarchy : InHierarchy->DirectChildren)
						{
							BuildNestedSceneComponentRowsRecursively(
								ChildHierarchy, InCounterpartComponents, PropertyEditorModule, PropertySelectionMap, ComponentPropertyAsRow);
						}
					}
				}
			}
		}

		static FLevelSnapshotsEditorResultsRowPtr BuildComponentRow(UActorComponent* InComponent, const TSet<UActorComponent*>& InCounterpartComponents,
			FPropertyEditorModule& PropertyEditorModule, const TArray<TFieldPath<FProperty>>& PropertiesThatPassFilter, const FLevelSnapshotsEditorResultsRowPtr& InDirectParentRow)
		{
			check(InComponent);
			
			const FSoftObjectPath& ComponentSoftPath = InComponent;

			check(ComponentSoftPath.IsValid());

			UActorComponent* CounterpartComponent = FindCounterpartComponent(InComponent, InCounterpartComponents);

			// Create Row Generators for object and counterpart
			TSharedPtr<IPropertyRowGenerator> RowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GetLevelSnapshotsAppropriatePropertyRowGeneratorArgs());
			TSharedPtr<IPropertyRowGenerator> CounterpartRowGenerator = nullptr;

			RowGenerator->SetObjects({ InComponent });

			if (CounterpartComponent)
			{
				CounterpartRowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(GetLevelSnapshotsAppropriatePropertyRowGeneratorArgs());
				CounterpartRowGenerator->SetObjects({ CounterpartComponent });
			}

			// Create group
			FLevelSnapshotsEditorResultsRow NewComponentGroup(FText::FromString(InComponent->GetName()), ComponentGroup, ECheckBoxState::Checked, InDirectParentRow);
			NewComponentGroup.InitObjectRow(CounterpartRowGenerator, RowGenerator);

			FLevelSnapshotsEditorResultsRowPtr SharedNewGroup = MakeShared<FLevelSnapshotsEditorResultsRow>(NewComponentGroup);

			TArray<TFieldPath<FProperty>> PropertyRowsGenerated;

			// Add Transform widgets if needed. They should come before other properties.
			ForceBuildTransformRows(InComponent, CounterpartComponent, PropertiesThatPassFilter, PropertyRowsGenerated, SharedNewGroup);

			PropertyRowsGenerated.Append(
				FLocalPropertyLooper::LoopOverProperties(CounterpartRowGenerator, RowGenerator, SharedNewGroup, PropertiesThatPassFilter));

			// Generate fallback rows for properties not supported by PropertyRowGenerator
			for (TFieldPath<FProperty> FieldPath : PropertiesThatPassFilter)
			{
				if (!PropertyRowsGenerated.Contains(FieldPath))
				{
					BuildRowForUnsupportedProperty(
						FText::FromString(FieldPath->GetAuthoredName()), InComponent, CounterpartComponent, FieldPath.Get(), SharedNewGroup);
				}
			}

			if (SharedNewGroup->GetChildRows().IsValidIndex(0))
			{
				InDirectParentRow->InsertChildRowAtIndex(SharedNewGroup);

				return SharedNewGroup;
			}
			else
			{
				// No valid children, destroy group
				SharedNewGroup.Reset();

				return nullptr;
			}
		}
	};

	ChildRows.Empty();
	
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	AActor* SnapshotActorLocal = nullptr;
	AActor* WorldActorLocal = nullptr;
	
	if (SnapshotActor.IsValid())
	{
		SnapshotActorLocal = SnapshotActor.Get();
		SnapshotRowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(FLocalComponentLooper::GetLevelSnapshotsAppropriatePropertyRowGeneratorArgs());
		SnapshotRowGenerator->SetObjects({ SnapshotActorLocal });
	}

	if (WorldActor.IsValid())
	{
		WorldActorLocal = WorldActor.Get();
		WorldRowGenerator = PropertyEditorModule.CreatePropertyRowGenerator(FLocalComponentLooper::GetLevelSnapshotsAppropriatePropertyRowGeneratorArgs());
		WorldRowGenerator->SetObjects({ WorldActor.Get() });
	}

	FLevelSnapshotsEditorResultsRowPtr SharedActorGroup = SharedThis(this);
	SharedActorGroup->InitObjectRow(SnapshotRowGenerator, WorldRowGenerator);

	// Iterate over components
	TArray<UActorComponent*> WorldActorNonSceneComponents;
	const FLocalComponentLooper::FComponentHierarchy* WorldComponentHierarchy = FLocalComponentLooper::BuildComponentHierarchy(WorldActorLocal, WorldActorNonSceneComponents);

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
						PropertyEditorModule, PropertySelection->GetSelectedLeafProperties(), SharedActorGroup);
				}
			}
		}
	}

	if (WorldComponentHierarchy->Component != nullptr) // Some Actors have no components, like World Settings
	{
		FLocalComponentLooper::BuildNestedSceneComponentRowsRecursively(WorldComponentHierarchy, CounterpartComponents,
			PropertyEditorModule, PropertySelectionMap, SharedActorGroup);
	}
	
	if (const FPropertySelection* PropertySelection = PropertySelectionMap.GetSelectedProperties(GetWorldObject()))
	{
		if (!PropertySelection->IsEmpty())
		{
			const TArray<TFieldPath<FProperty>>& PropertyRowsGenerated = FLocalPropertyLooper::LoopOverProperties(
				SnapshotRowGenerator, WorldRowGenerator, SharedActorGroup, PropertySelection->GetSelectedLeafProperties());
			
			// Generate fallback rows for properties not supported by 
			for (TFieldPath<FProperty> FieldPath : PropertySelection->GetSelectedLeafProperties())
			{
				if (!PropertyRowsGenerated.Contains(FieldPath))
				{
					FLocalComponentLooper::BuildRowForUnsupportedProperty(
						FText::FromString(
							FieldPath->GetAuthoredName()), 
						WorldActorLocal ? WorldActorLocal : nullptr, SnapshotActorLocal ? SnapshotActorLocal : nullptr, 
						FieldPath.Get(), SharedActorGroup);
				}
			}
		}
	}

	SetHasGeneratedChildren(true);
}

bool FLevelSnapshotsEditorResultsRow::DoesRowRepresentGroup() const
{
	const ELevelSnapshotsEditorResultsRowType InRowType = GetRowType();

	const bool bIsGroup = (
		InRowType == ActorGroup ||
		InRowType == ComponentGroup ||
		InRowType == SubObjectGroup ||
		InRowType == StructGroup ||
		InRowType == CollectionGroup
		);
	return bIsGroup;
}

bool FLevelSnapshotsEditorResultsRow::DoesRowRepresentObject() const
{
	const ELevelSnapshotsEditorResultsRowType InRowType = GetRowType();

	const bool bIsGroup = (
		InRowType == ActorGroup ||
		InRowType == ComponentGroup ||
		InRowType == SubObjectGroup 
		);
	return bIsGroup;
}

FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType FLevelSnapshotsEditorResultsRow::GetRowType() const
{
	return RowType;
}

FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType FLevelSnapshotsEditorResultsRow::DetermineRowTypeFromProperty(FProperty* InProperty)
{
	if (!InProperty)
	{
		return None;
	}

	ELevelSnapshotsEditorResultsRowType ReturnRowType = SingleProperty;

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

FText FLevelSnapshotsEditorResultsRow::GetDisplayName() const
{
	return DisplayName;
}

void FLevelSnapshotsEditorResultsRow::SetDisplayName(const FText InDisplayName)
{
	DisplayName = InDisplayName;
}

TArray<FLevelSnapshotsEditorResultsRowPtr> FLevelSnapshotsEditorResultsRow::GetChildRows() const
{
	return ChildRows;
}

void FLevelSnapshotsEditorResultsRow::SetChildRows(const TArray<FLevelSnapshotsEditorResultsRowPtr> InChildRows)
{
	ChildRows = InChildRows;
}

void FLevelSnapshotsEditorResultsRow::AddToChildRows(FLevelSnapshotsEditorResultsRowPtr InRow)
{
	ChildRows.Add(InRow);
}

void FLevelSnapshotsEditorResultsRow::InsertChildRowAtIndex(FLevelSnapshotsEditorResultsRowPtr InRow, const int32 AtIndex)
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

FLevelSnapshotsEditorResultsRowPtr FLevelSnapshotsEditorResultsRow::GetDirectParentRow() const
{
	return (DirectParentRow.IsValid() ? DirectParentRow : nullptr);
}

void FLevelSnapshotsEditorResultsRow::SetDirectParentRow(const FLevelSnapshotsEditorResultsRowPtr& InDirectParentRow)
{
	DirectParentRow = InDirectParentRow;
}

TSharedPtr<FLevelSnapshotsEditorResultsRow> FLevelSnapshotsEditorResultsRow::GetParentRowAtTopOfHierarchy()
{
	TSharedPtr<FLevelSnapshotsEditorResultsRow> TopOfHierarchy(this);

	while (TopOfHierarchy->GetDirectParentRow() != nullptr)
	{
		TopOfHierarchy = TopOfHierarchy->GetDirectParentRow();
	}

	return TopOfHierarchy;
}

const FLevelSnapshotsEditorResultsRowPtr& FLevelSnapshotsEditorResultsRow::GetContainingObjectGroup() const
{
	return ContainingObjectGroup;
}

FLevelSnapshotsEditorResultsRow::ELevelSnapshotsWidgetTypeCustomization FLevelSnapshotsEditorResultsRow::GetWidgetTypeCustomization() const
{
	return WidgetTypeCustomization;
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
	if (GetRowType() == ActorGroup)
	{
		if (SnapshotActor.IsValid())
		{
			return SnapshotActor.Get();
		}
	}
	else
	{
		if (SnapshotObjectForCustomization.IsValid())
		{
			return SnapshotObjectForCustomization.Get();
		}
		
		if (const TSharedPtr<IPropertyRowGenerator> RowGenerator = GetSnapshotRowGenerator())
		{
			const TArray<TWeakObjectPtr<UObject>> GeneratorObjects = RowGenerator->GetSelectedObjects();

			if (GeneratorObjects.Num() > 0)
			{
				const TWeakObjectPtr<UObject> SnapshotObject = GeneratorObjects[0];

				if (SnapshotObject.IsValid())
				{
					return SnapshotObject.Get();
				}
			}
		}
	}

	return nullptr;
}

UObject* FLevelSnapshotsEditorResultsRow::GetWorldObject() const
{
	if (GetRowType() == ActorGroup)
	{
		if (WorldActor.IsValid())
		{
			return WorldActor.Get();
		}
	}
	else
	{
		if (WorldObjectForCustomization.IsValid())
		{
			return WorldObjectForCustomization.Get();
		}
		
		if (const TSharedPtr<IPropertyRowGenerator> RowGenerator = GetWorldRowGenerator())
		{
			const TArray<TWeakObjectPtr<UObject>> GeneratorObjects = RowGenerator->GetSelectedObjects();

			if (GeneratorObjects.Num() > 0) // Each generator only ever contains one object in Level Snapshots
			{
				const TWeakObjectPtr<UObject> WorldObject = GeneratorObjects[0];

				if (WorldObject.IsValid())
				{
					return WorldObject.Get();
				}
			}
		}
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
	if (UObject* WorldObject = GetWorldObject())
	{
		return FSoftObjectPath(WorldObject);
	}

	return nullptr;
}

const TSharedPtr<IPropertyRowGenerator>& FLevelSnapshotsEditorResultsRow::GetSnapshotRowGenerator() const
{
	return SnapshotRowGenerator;
}

const TSharedPtr<IPropertyRowGenerator>& FLevelSnapshotsEditorResultsRow::GetWorldRowGenerator() const
{
	return WorldRowGenerator;
}

FProperty* FLevelSnapshotsEditorResultsRow::GetProperty() const
{
	if (GetWidgetTypeCustomization() == WidgetType_NoCustomWidget)
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
	}
	else
	{
		if (PropertyForCustomization->IsValidLowLevel())
		{
			return PropertyForCustomization;
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
			if (FLevelSnapshotsEditorResultsRowPtr Parent = This.GetDirectParentRow())
			{
				RecursiveCreateChain(*Parent.Get(), Result);
				Result.AppendInline(This.GetProperty());
			}
		}
	};

	FLevelSnapshotPropertyChain Result;
	Local::RecursiveCreateChain(*this, Result);
	return Result;
}

const TSharedPtr<IPropertyHandle>& FLevelSnapshotsEditorResultsRow::GetSnapshotPropertyHandle() const
{
	return SnapshotPropertyHandle;
}

const TSharedPtr<IPropertyHandle>& FLevelSnapshotsEditorResultsRow::GetWorldPropertyHandle() const
{
	return WorldPropertyHandle;
}

FLevelSnapshotsEditorResultsRow::ELevelSnapshotsObjectType FLevelSnapshotsEditorResultsRow::GetFirstValidPropertyHandle(TSharedPtr<IPropertyHandle>& OutHandle) const
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

FProperty* FLevelSnapshotsEditorResultsRow::GetPropertyForCustomization() const
{
	return PropertyForCustomization;
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
			const ELevelSnapshotsEditorResultsRowType ChildRowType = ChildRow->GetRowType();
			
			if (!ChildRow.IsValid())
			{
				continue;
			}

			if ((ChildRowType == SingleProperty || ChildRowType == CollectionGroup) && ChildRow->GetIsNodeChecked())
			{
				CheckedSinglePropertyNodeArray.Add(ChildRow);
			}

			if (ChildRowType == StructGroup)
			{
				// todo: remove this block after refactoring field path identifiers 
				CheckedSinglePropertyNodeArray.Add(ChildRow);
				// todo: end of removal block
				
				ChildRow->GetAllCheckedChildProperties(CheckedSinglePropertyNodeArray);
			}
		}
	}
}

void FLevelSnapshotsEditorResultsRow::EvaluateAndSetAllParentGroupCheckedStates() const
{
	TSharedPtr<FLevelSnapshotsEditorResultsRow> ParentRow = GetDirectParentRow();

	ECheckBoxState NewWidgetCheckedState = ECheckBoxState::Unchecked;

	while (ParentRow.IsValid())
	{
		if (ParentRow->DoesRowRepresentGroup())
		{
			if (NewWidgetCheckedState != ECheckBoxState::Undetermined)
			{
				const bool bHasCheckedChildren = ParentRow->HasCheckedChildren();
				const bool bHasUncheckedChildren = ParentRow->HasUncheckedChildren();

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

			ParentRow->WidgetCheckedState = NewWidgetCheckedState;
		}

		ParentRow = ParentRow->GetDirectParentRow();
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

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5.f, 10.f)
			[
				SNew(SHorizontalBox)
			
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllActorGroupsCollapsed)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("LevelSnapshotsEditorResults_CollapseAll", "Collapse All"))
					]
				]
	
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllActorGroupsUnselected)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("LevelSnapshotsEditorResults_UnselectAll", "Unselect All"))
					]
				]
		
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f, 0.f)
				[
					SNew(SButton)
					.VAlign(VAlign_Center)
					.OnClicked(this, &SLevelSnapshotsEditorResults::SetAllActorGroupsSelected)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("LevelSnapshotsEditorResults_SelectAll", "Select All"))
					]	
				]
			]
			
			+ SVerticalBox::Slot()
			.VAlign(VAlign_Top)
			.AutoHeight()
			[
				SAssignNew(ResultsSearchBoxPtr, SSearchBox)
				.HintText(LOCTEXT("LevelSnapshotsEditorResults_SearchHintText", "Search actors, components, properties..."))
				.OnTextChanged_Raw(this, &SLevelSnapshotsEditorResults::OnResultsViewSearchTextChanged)
			]

			+ SVerticalBox::Slot()
			[
				SAssignNew(TreeViewPtr, STreeView<FLevelSnapshotsEditorResultsRowPtr>)
				.SelectionMode(ESelectionMode::None)
				.TreeItemsSource(&TreeViewRootObjects)
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
		];
}

SLevelSnapshotsEditorResults::~SLevelSnapshotsEditorResults()
{
	if (EditorDataPtr.IsValid())
	{
		EditorDataPtr->OnActiveSnapshotChanged.Remove(OnActiveSnapshotChangedHandle);
		EditorDataPtr->OnRefreshResults.Remove(OnRefreshResultsHandle);
	}
	OnActiveSnapshotChangedHandle.Reset();
	OnRefreshResultsHandle.Reset();
	
	ResultsSearchBoxPtr.Reset();
	ResultsBoxContainerPtr.Reset();

	SplitterManagerPtr.Reset();

	TreeViewRootObjects.Empty();
	TreeViewPtr.Reset();
}

TOptional<ULevelSnapshot*> SLevelSnapshotsEditorResults::GetSelectedLevelSnapshot() const
{
	return ensure(EditorDataPtr.IsValid()) ? EditorDataPtr->GetActiveSnapshot() : TOptional<ULevelSnapshot*>();
}

void SLevelSnapshotsEditorResults::OnSnapshotSelected(const TOptional<ULevelSnapshot*>& InLevelSnapshot)
{
	if (InLevelSnapshot.IsSet())
	{
		GenerateTreeView();
	}
}

void SLevelSnapshotsEditorResults::RefreshResults()
{
	GenerateTreeView();
}

void SLevelSnapshotsEditorResults::BuildSelectionSetFromSelectedPropertiesInEachActorGroup()
{
	if (!ensure(GetEditorDataPtr().IsValid()))
	{
		return;
	}

	struct Local
	{
		static void AddAllChildPropertiesInObjectGroupToSelectionSetRecursively(
			FPropertySelectionMap& SelectionMap, const FLevelSnapshotsEditorResultsRowPtr& Group)
		{
			if (!ensureMsgf(Group->DoesRowRepresentObject(), 
				TEXT("AddAllChildPropertiesInObjectGroupToSelectionSetRecursively: Group does not represent an object. Group name: %s"), *Group->GetDisplayName().ToString()))
			{
				return;
			}

			if (Group->GetRowType() == FLevelSnapshotsEditorResultsRow::ActorGroup && !Group->GetHasGeneratedChildren())
			{
				return;
			}

			// If we have generated properties or this isn't an actor group, we must check which properties have been checked
			// Remove it from the map then re-add it if it has checked children
			SelectionMap.RemoveObjectPropertiesFromMap(Group->GetWorldObject());
			
			FPropertySelection CheckedNodeFieldPaths;
			if (Group->HasCheckedChildren())
			{
				TArray<FLevelSnapshotsEditorResultsRowPtr> CheckedChildPropertyNodes;
				
				for (const FLevelSnapshotsEditorResultsRowPtr& ChildRow : Group->GetChildRows())
				{
					if (!ChildRow.IsValid())
					{
						continue;
					}

					const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType ChildRowType = ChildRow->GetRowType();

					if (ChildRow->DoesRowRepresentObject())
					{
						AddAllChildPropertiesInObjectGroupToSelectionSetRecursively(SelectionMap, ChildRow);
					}
					else if ((ChildRowType == FLevelSnapshotsEditorResultsRow::SingleProperty || ChildRowType == FLevelSnapshotsEditorResultsRow::CollectionGroup) && 
						ChildRow->GetIsNodeChecked())
					{
						CheckedChildPropertyNodes.Add(ChildRow);
					}
					else if (ChildRowType == FLevelSnapshotsEditorResultsRow::StructGroup)
					{
						// todo: remove this block after refactoring field path identifiers
						if (ChildRow->HasCheckedChildren())
						{
							CheckedChildPropertyNodes.Add(ChildRow);
							// todo: end of removal block 
							
							ChildRow->GetAllCheckedChildProperties(CheckedChildPropertyNodes);
						}
					}
				}

				for (const FLevelSnapshotsEditorResultsRowPtr& ChildRow : CheckedChildPropertyNodes)
				{
					if (ChildRow.IsValid())
					{
						CheckedNodeFieldPaths.AddProperty(ChildRow->GetPropertyChain());
					}
				}
			}

			SelectionMap.AddObjectProperties(Group->GetWorldObject(), CheckedNodeFieldPaths);
		}
	};

	for (const FLevelSnapshotsEditorResultsRowPtr& Group : TreeViewRootObjects)
	{
		if (Group.IsValid())
		{
			if (Group->GetDesiredVisibility() != EVisibility::Visible || Group->GetWidgetCheckedState() == ECheckBoxState::Unchecked)
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
				Local::AddAllChildPropertiesInObjectGroupToSelectionSetRecursively(PropertySelectionMap, Group);
			}
		}
	}

	GetEditorDataPtr()->GetFilterResults()->SetPropertiesToRollback(PropertySelectionMap);
}

const FString& SLevelSnapshotsEditorResults::GetSearchStringFromSearchInputField() const
{
	return ResultsSearchBoxPtr->GetText().ToString();
}

TWeakObjectPtr<ULevelSnapshotsEditorData> SLevelSnapshotsEditorResults::GetEditorDataPtr() const
{
	return EditorDataPtr;
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
	struct Local
	{
		/* Returns true if the actor group would have any visible properties after filtering. */
		static bool DoesActorContainAnyVisibleProperties(TSet <TWeakObjectPtr<UObject>>& EvaluatedObjects, FFilterListData& FilterListData)
		{
			bool bHasAnyVisibleProperties = false;
			
			const TArray<TWeakObjectPtr<UObject>>& SelectionMapKeys = FilterListData.GetModifiedActorsSelectedProperties().GetKeys();

			for (const TWeakObjectPtr<UObject>& Key : SelectionMapKeys)
			{
				if (!EvaluatedObjects.Contains(Key))
				{
					EvaluatedObjects.Add(Key);

					if (Key.IsValid())
					{
						const FPropertySelection* SelectedProperties = FilterListData.GetModifiedActorsSelectedProperties().GetSelectedProperties(Key.Get());
						if (!bHasAnyVisibleProperties
							&& ensure(SelectedProperties) && !SelectedProperties->IsEmpty())
						{
							bHasAnyVisibleProperties = true;
							break;
						}
					}
				}
			}

			return bHasAnyVisibleProperties;
		}
	};
	
	if (!ensure(EditorDataPtr.IsValid()) || !ensure(TreeViewPtr.IsValid()))
	{
		return;
	}

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("GenerateTreeView"), STAT_GenerateTreeView, STATGROUP_LevelSnapshots);
	
	UFilteredResults* FilteredResults = EditorDataPtr->GetFilterResults();
	const TWeakObjectPtr<ULevelSnapshotFilter>& UserFilters = FilteredResults->GetUserFilters();

	FilteredResults->UpdateFilteredResults();

	FFilterListData& FilterListData = FilteredResults->GetFilteredData();
	const TSet<TWeakObjectPtr<AActor>>& ActorsToConsider = FilterListData.GetModifiedFilteredActors();

	TreeViewRootObjects.Empty();

	TSet <TWeakObjectPtr<UObject>> EvaluatedObjects;
	
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

		if (!ULevelSnapshot::IsActorDesirableForCapture(WorldActor))
		{
			continue;
		}
		
		// Get remaining properties after filter
		if (UserFilters.IsValid() && WorldActor)
		{
			FilterListData.ApplyFilterToFindSelectedProperties(WorldActor, UserFilters.Get());
		}
		
		if (!Local::DoesActorContainAnyVisibleProperties(EvaluatedObjects, FilterListData))
		{
			continue;
		}

		TWeakObjectPtr<AActor> WeakSnapshotActor = FilteredResults->GetFilteredData().GetSnapshotCounterpartFor(WorldActor);
		
		const FString& ActorName = WorldActor->GetActorLabel();

		// Create group
		TSharedRef<FLevelSnapshotsEditorResultsRow> NewActorGroup = MakeShared<FLevelSnapshotsEditorResultsRow>(
			FLevelSnapshotsEditorResultsRow(FText::FromString(ActorName), FLevelSnapshotsEditorResultsRow::ActorGroup, ECheckBoxState::Checked));
		NewActorGroup->InitActorRow(WeakSnapshotActor, WeakWorldActor);

		TreeViewRootObjects.Add(NewActorGroup);
	}

	PropertySelectionMap = FilterListData.GetModifiedActorsSelectedProperties();
	
	SplitterManagerPtr = MakeShared<FLevelSnapshotsEditorResultsSplitterManager>(FLevelSnapshotsEditorResultsSplitterManager());

	TreeViewPtr->RequestListRefresh();
}

FReply SLevelSnapshotsEditorResults::SetAllActorGroupsSelected()
{
	if (TreeViewPtr.IsValid())
	{
		for (const FLevelSnapshotsEditorResultsRowPtr& RootRow : TreeViewRootObjects)
		{
			if (!RootRow.IsValid())
			{
				continue;
			}
			
			RootRow->SetIsNodeChecked(true);
		}
	}

	return FReply::Handled();
}

FReply SLevelSnapshotsEditorResults::SetAllActorGroupsUnselected()
{
	if (TreeViewPtr.IsValid())
	{
		for (const FLevelSnapshotsEditorResultsRowPtr& RootRow : TreeViewRootObjects)
		{
			if (!RootRow.IsValid())
			{
				continue;
			}

			RootRow->SetIsNodeChecked(false);
		}
	}

	return FReply::Handled();
}

FReply SLevelSnapshotsEditorResults::SetAllActorGroupsCollapsed()
{
	if (TreeViewPtr.IsValid())
	{
		for (const FLevelSnapshotsEditorResultsRowPtr& RootRow : TreeViewRootObjects)
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
	
	for (const TSharedPtr<FLevelSnapshotsEditorResultsRow>& ChildRow : TreeViewRootObjects)
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

void SLevelSnapshotsEditorResults::OnRowChildExpansionChange(TSharedPtr<FLevelSnapshotsEditorResultsRow> Row, const bool bIsExpanded)
{
	if (Row.IsValid())
	{
		Row->SetIsTreeViewItemExpanded(bIsExpanded);
	}
}

void SLevelSnapshotsEditorResultsRow::Construct(const FArguments& InArgs, const FLevelSnapshotsEditorResultsRowPtr InRow, const FLevelSnapshotsEditorResultsSplitterManagerPtr& InSplitterManagerPtr)
{
	struct FCustomPropertyWidgetGenerator
	{
		static TSharedPtr<SWidget> GenerateCustomWidget(const FLevelSnapshotsEditorResultsRowPtr Item, const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsObjectType ObjectType)
		{
			const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsWidgetTypeCustomization WidgetType = Item->GetWidgetTypeCustomization();

			UObject* ObjectToPass = nullptr;;

			if (ObjectType == FLevelSnapshotsEditorResultsRow::ObjectType_Snapshot)
			{
				ObjectToPass = Item->GetSnapshotObject();
			}
			else if (ObjectType == FLevelSnapshotsEditorResultsRow::ObjectType_World)
			{
				ObjectToPass = Item->GetWorldObject();
			}

			if (!ObjectToPass)
			{
				return nullptr;
			}
			
			if (WidgetType == FLevelSnapshotsEditorResultsRow::WidgetType_Location)
			{
				return GenerateLocationWidget(ObjectToPass);
			}
			else if (WidgetType == FLevelSnapshotsEditorResultsRow::WidgetType_Rotation)
			{
				return GenerateRotationWidget(ObjectToPass);
			}
			else if (WidgetType == FLevelSnapshotsEditorResultsRow::WidgetType_Scale3D)
			{
				return GenerateScale3dWidget(ObjectToPass);
			}
			else if (WidgetType == FLevelSnapshotsEditorResultsRow::WidgetType_UnsupportedProperty)
			{
				return SNew(STextBlock).Text(FText::FromString("Unsupported Property"));
			}

			return nullptr;
		}
		
		static TSharedPtr<SWidget> GenerateLocationWidget(UObject* InContainingObject)
		{
			const TFunction< TOptional<float>(USceneComponent*)> X_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeLocation().X; };
			const TFunction< TOptional<float>(USceneComponent*)> Y_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeLocation().Y; };
			const TFunction< TOptional<float>(USceneComponent*)> Z_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeLocation().Z; };
			
			return GenerateVectorWidget(InContainingObject, X_Function, Y_Function, Z_Function);
		}

		static TSharedPtr<SWidget> GenerateRotationWidget(UObject* InContainingObject)
		{
			const TFunction< TOptional<float>(USceneComponent*)> X_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeRotation().Roll; };
			const TFunction< TOptional<float>(USceneComponent*)> Y_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeRotation().Pitch; };
			const TFunction< TOptional<float>(USceneComponent*)> Z_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeRotation().Yaw; };

			return GenerateVectorWidget(InContainingObject, X_Function, Y_Function, Z_Function);
		}

		static TSharedPtr<SWidget> GenerateScale3dWidget(UObject* InContainingObject)
		{
			const TFunction<TOptional<float>(USceneComponent*)> X_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeScale3D().X; };
			const TFunction< TOptional<float>(USceneComponent*)> Y_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeScale3D().Y; };
			const TFunction< TOptional<float>(USceneComponent*)> Z_Function = [](USceneComponent* InSceneComponent) { return InSceneComponent->GetRelativeScale3D().Z; };

			return GenerateVectorWidget(InContainingObject, X_Function, Y_Function, Z_Function);
		}

		static TSharedPtr<SWidget> GenerateVectorWidget(UObject* InContainingObject, 
			TFunction< TOptional<float>(USceneComponent*)> X_Function, TFunction< TOptional<float>(USceneComponent*)> Y_Function, TFunction< TOptional<float>(USceneComponent*)> Z_Function)
		{
			if (USceneComponent* AsSceneComponent = Cast<USceneComponent>(InContainingObject))
			{
				return SNew(SVectorInputBox)
					.X_Lambda([AsSceneComponent, X_Function]() { return X_Function(AsSceneComponent); })
					.Y_Lambda([AsSceneComponent, Y_Function]() { return Y_Function(AsSceneComponent); })
					.Z_Lambda([AsSceneComponent, Z_Function]() { return Z_Function(AsSceneComponent); })
					.bColorAxisLabels(true)
					.AllowResponsiveLayout(true)
					.IsEnabled(false)
					.AllowSpin(false);
			}

			return nullptr;
		}
	};
	
	Item = InRow;
	check(Item.IsValid());
	
	SplitterManagerPtr = InSplitterManagerPtr;
	check(SplitterManagerPtr.IsValid());
	
	TSharedPtr<IPropertyHandle> ItemHandle;
	Item->GetFirstValidPropertyHandle(ItemHandle);
	const bool bHasValidHandle = ItemHandle.IsValid() && ItemHandle->IsValidHandle();
	
	const FLevelSnapshotsEditorResultsRow::ELevelSnapshotsEditorResultsRowType RowType = Item->GetRowType();
	const FText DisplayText = RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap ? FText::GetEmpty() : Item->GetDisplayName();
	FText Tooltip;

	const bool bIsSinglePropertyInCollection = RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap || RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInSetOrArray;
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
		Tooltip = bHasValidHandle ? ItemHandle->GetToolTipText() : Item->GetDisplayName();
	}

	int32 IndentationDepth = 0;
	FLevelSnapshotsEditorResultsRowPtr ParentRow = Item->GetDirectParentRow();
	while (ParentRow.IsValid())
	{
		IndentationDepth++;
		ParentRow = ParentRow->GetDirectParentRow();
	}
	Item->SetChildDepth(IndentationDepth);

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
					[
						SNew(SCheckBox)
						.Visibility(bIsSinglePropertyInCollection ? EVisibility::Hidden : EVisibility::Visible)
						.IsChecked_Raw(Item.Get(), &FLevelSnapshotsEditorResultsRow::GetWidgetCheckedState)
						.OnCheckStateChanged_Raw(Item.Get(), &FLevelSnapshotsEditorResultsRow::SetWidgetCheckedState, true)
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

	if (const TSharedPtr<IPropertyHandle>& SnapshotPropertyHandle = Item->GetSnapshotPropertyHandle())
	{
		if (RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap)
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
		if (Item->DoesRowRepresentGroup())
		{
			SnapshotChildWidget = SNullWidget::NullWidget;
		}
		else
		{
			SnapshotChildWidget = FCustomPropertyWidgetGenerator::GenerateCustomWidget(Item, FLevelSnapshotsEditorResultsRow::ObjectType_Snapshot);
		}
	}

	if (!SnapshotChildWidget.IsValid())
	{
		SnapshotChildWidget = 
			SNew(STextBlock)
			.Text(LOCTEXT("LevelSnapshotsEditorResults_NoSnapshotPropertyFound", "No snapshot property found"));
	}

	SnapshotChildWidget->SetEnabled(false);

	SplitterPtr->AddSlot()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2, 0))
		[
			SnapshotChildWidget.ToSharedRef()
		]
	].OnSlotResized_Handler.BindSP(this, &SLevelSnapshotsEditorResultsRow::SetSnapshotColumnSize);

	const auto SlotDelegate1 = TAttribute<float>::FGetter::CreateSP(this, &SLevelSnapshotsEditorResultsRow::GetSplitterSlotSize, 1);;
	SplitterPtr->SlotAt(1).SizeValue.Bind(SlotDelegate1);

	// Splitter Slot 2
	TSharedPtr<SWidget> WorldChildWidget;

	if (const TSharedPtr<IPropertyHandle>& WorldPropertyHandle = Item->GetWorldPropertyHandle())
	{
		if (RowType == FLevelSnapshotsEditorResultsRow::SinglePropertyInMap)
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
		if (Item->DoesRowRepresentGroup())
		{
			WorldChildWidget = SNullWidget::NullWidget;
		}
		else
		{
			WorldChildWidget = FCustomPropertyWidgetGenerator::GenerateCustomWidget(Item, FLevelSnapshotsEditorResultsRow::ObjectType_World);
		}
	}

	if (!WorldChildWidget.IsValid())
	{
		WorldChildWidget = 
			SNew(STextBlock)
			.Text(LOCTEXT("LevelSnapshotsEditorResults_NoWorldPropertyFound", "No World property found"));
	}

	WorldChildWidget->SetEnabled(false);

	SplitterPtr->AddSlot()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.Padding(FMargin(2, 0))
		[
			WorldChildWidget.ToSharedRef()
		]
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
	Item.Reset();
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
