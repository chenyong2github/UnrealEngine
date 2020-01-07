// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBlueprintEditorSelectedDebugObjectWidget.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Widgets/Text/STextBlock.h"
#include "EngineGlobals.h"
#include "Editor.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "EditorStyleSet.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/SToolTip.h"
#include "IDocumentation.h"
#include "SLevelOfDetailBranchNode.h"
#include "Widgets/Input/STextComboBox.h"

#define LOCTEXT_NAMESPACE "KismetToolbar"

static TAutoConsoleVariable<int32> CVarUseFastDebugObjectDiscovery(TEXT("r.UseFastDebugObjectDiscovery"), 1, TEXT("Enable new optimised debug object discovery"));

//////////////////////////////////////////////////////////////////////////
// SBlueprintEditorSelectedDebugObjectWidget

void SBlueprintEditorSelectedDebugObjectWidget::Construct(const FArguments& InArgs, TSharedPtr<FBlueprintEditor> InBlueprintEditor)
{
	BlueprintEditor = InBlueprintEditor;

	GenerateDebugWorldNames(false);
	GenerateDebugObjectNames(false);

	LastObjectObserved = DebugObjects[0];

	DebugWorldsComboBox = SNew(STextComboBox)
		.ButtonStyle(FEditorStyle::Get(), "FlatButton.Light")
		.ToolTip(IDocumentation::Get()->CreateToolTip(
		LOCTEXT("BlueprintDebugWorldTooltip", "Select a world to debug"),
		nullptr,
		TEXT("Shared/Editors/BlueprintEditor/BlueprintDebugger"),
		TEXT("DebugWorld")))
		.OptionsSource(&DebugWorldNames)
		.InitiallySelectedItem(GetDebugWorldName())
		.Visibility(this, &SBlueprintEditorSelectedDebugObjectWidget::IsDebugWorldComboVisible)
		.OnComboBoxOpening(this, &SBlueprintEditorSelectedDebugObjectWidget::GenerateDebugWorldNames, true)
		.OnSelectionChanged(this, &SBlueprintEditorSelectedDebugObjectWidget::DebugWorldSelectionChanged);

	DebugObjectsComboBox = SNew(SComboBox<TSharedPtr<FString>>)
		.ButtonStyle(FEditorStyle::Get(), "FlatButton.Light")
		.ToolTip(IDocumentation::Get()->CreateToolTip(
		LOCTEXT("BlueprintDebugObjectTooltip", "Select an object to debug"),
		nullptr,
		TEXT("Shared/Editors/BlueprintEditor/BlueprintDebugger"),
		TEXT("DebugObject")))
		.OptionsSource(&DebugObjectNames)
		.InitiallySelectedItem(GetDebugObjectName())
		.OnComboBoxOpening(this, &SBlueprintEditorSelectedDebugObjectWidget::GenerateDebugObjectNames, true)
		.OnSelectionChanged(this, &SBlueprintEditorSelectedDebugObjectWidget::DebugObjectSelectionChanged)
		.OnGenerateWidget(this, &SBlueprintEditorSelectedDebugObjectWidget::CreateDebugObjectItemWidget)
		.AddMetaData<FTagMetaData>(TEXT("SelectDebugObjectCobmo"))
		[
			SNew(STextBlock)
			.Text(this, &SBlueprintEditorSelectedDebugObjectWidget::GetSelectedDebugObjectTextLabel)
		];

	ChildSlot
	[
		SNew(SLevelOfDetailBranchNode)
		.UseLowDetailSlot(FMultiBoxSettings::UseSmallToolBarIcons)
		.OnGetActiveDetailSlotContent(this, &SBlueprintEditorSelectedDebugObjectWidget::OnGetActiveDetailSlotContent)
	];
}

void SBlueprintEditorSelectedDebugObjectWidget::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if (GetBlueprintObj())
	{
		if (UObject* Object = GetBlueprintObj()->GetObjectBeingDebugged())
		{
			if (Object != LastObjectObserved.Get())
			{
				// bRestoreSelection attempts to restore the selection by name, 
				// this ensures that if the last object we had selected was 
				// regenerated (spawning a new object), then we select that  
				// again, even if it is technically a different object
				GenerateDebugObjectNames(/*bRestoreSelection =*/true);

				TSharedPtr<FString> NewSelection = DebugObjectsComboBox->GetSelectedItem();
				// just in case that object we want to select is actually in 
				// there (and wasn't caught by bRestoreSelection), then let's 
				// favor that over whatever was picked
				for (int32 Index = 0; Index < DebugObjects.Num(); ++Index)
				{
					if (DebugObjects[Index] == Object)
					{
						NewSelection = DebugObjectNames[Index];
						break;
					}
				}

				if (!NewSelection.IsValid())
				{
					NewSelection = DebugObjectNames[0];
				}

				DebugObjectsComboBox->SetSelectedItem(NewSelection);
				LastObjectObserved = Object;
			}
		}
		else
		{
			LastObjectObserved = nullptr;

			// If the object name is a name (rather than the 'No debug selected' string then regenerate the names (which will reset the combo box) as the object is invalid.
			TSharedPtr<FString> CurrentString = DebugObjectsComboBox->GetSelectedItem();
			if (*CurrentString != GetNoDebugString())
			{
				GenerateDebugObjectNames(false);
			}
		}
	}
}

const FString& SBlueprintEditorSelectedDebugObjectWidget::GetNoDebugString() const
{
	return NSLOCTEXT("BlueprintEditor", "DebugObjectNothingSelected", "No debug object selected").ToString();
}

const FString& SBlueprintEditorSelectedDebugObjectWidget::GetDebugAllWorldsString() const
{
	return NSLOCTEXT("BlueprintEditor", "DebugWorldNothingSelected", "All Worlds").ToString();
}

TSharedRef<SWidget> SBlueprintEditorSelectedDebugObjectWidget::OnGetActiveDetailSlotContent(bool bChangedToHighDetail)
{

	const TSharedRef<SWidget> BrowseButton = PropertyCustomizationHelpers::MakeBrowseButton(FSimpleDelegate::CreateSP(this, &SBlueprintEditorSelectedDebugObjectWidget::SelectedDebugObject_OnClicked));
	BrowseButton->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SBlueprintEditorSelectedDebugObjectWidget::IsSelectDebugObjectButtonVisible)));
	BrowseButton->SetToolTipText(LOCTEXT("DebugSelectActor", "Select this Actor in level"));

	TSharedRef<SWidget> DebugObjectSelectionWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			DebugObjectsComboBox.ToSharedRef()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(2.0f)
		[
			BrowseButton
		];

	if (!bChangedToHighDetail)
	{
		return
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				DebugWorldsComboBox.ToSharedRef()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				DebugObjectSelectionWidget
			];
	}
	else
	{
		return
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.VAlign(VAlign_Bottom)
			[
				// Vertical Layout when using normal size icons
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					DebugWorldsComboBox.ToSharedRef()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					DebugObjectSelectionWidget
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DebugSelectTitle", "Debug Filter"))
			];
	}
}

void SBlueprintEditorSelectedDebugObjectWidget::OnRefresh()
{
	if (GetBlueprintObj())
	{
		GenerateDebugWorldNames(false);
		GenerateDebugObjectNames(false);

		if (DebugObjectsComboBox.IsValid())
		{
			DebugObjectsComboBox->SetSelectedItem(GetDebugWorldName());
			DebugObjectsComboBox->SetSelectedItem(GetDebugObjectName());
		}
	}
}

void SBlueprintEditorSelectedDebugObjectWidget::GenerateDebugWorldNames(bool bRestoreSelection)
{
	DebugWorldNames.Empty();
	DebugWorlds.Empty();

	DebugWorlds.Add(nullptr);
	DebugWorldNames.Add(MakeShareable(new FString(GetDebugAllWorldsString())));

	UWorld* PreviewWorld = BlueprintEditor.Pin()->GetPreviewScene()->GetWorld();

	for (TObjectIterator<UWorld> It; It; ++It)
	{
		UWorld *TestWorld = *It;

		// Include only PIE and worlds that own the persistent level (i.e. non-streaming levels).
		const bool bIsValidDebugWorld = (TestWorld != nullptr)
			&& TestWorld->WorldType == EWorldType::PIE
			&& TestWorld->PersistentLevel != nullptr
			&& TestWorld->PersistentLevel->OwningWorld == TestWorld;

		if (!bIsValidDebugWorld)
		{
			continue;
		}

		ENetMode NetMode = TestWorld->GetNetMode();

		FString WorldName;

		switch (NetMode)
		{
		case NM_Standalone:
			WorldName = NSLOCTEXT("BlueprintEditor", "DebugWorldStandalone", "Standalone").ToString();
			break;

		case NM_ListenServer:
			WorldName = NSLOCTEXT("BlueprintEditor", "DebugWorldListenServer", "Listen Server").ToString();
			break;

		case NM_DedicatedServer:
			WorldName = NSLOCTEXT("BlueprintEditor", "DebugWorldDedicatedServer", "Dedicated Server").ToString();
			break;

		case NM_Client:
			if (FWorldContext* PieContext = GEngine->GetWorldContextFromWorld(TestWorld))
			{
				WorldName = FString::Printf(TEXT("%s %d"), *NSLOCTEXT("BlueprintEditor", "DebugWorldClient", "Client").ToString(), PieContext->PIEInstance - 1);
			}
			break;
		};

		if (!WorldName.IsEmpty())
		{
			if (FWorldContext* PieContext = GEngine->GetWorldContextFromWorld(TestWorld))
			{
				if (!PieContext->CustomDescription.IsEmpty())
				{
					WorldName += TEXT(" ") + PieContext->CustomDescription;
				}
			}

			// DebugWorlds & DebugWorldNames need to be the same size (we expect
			// an index in one to correspond to the other) - DebugWorldNames is
			// what populates the dropdown, so it is the authority (if there's 
			// no name to present, they can't select from DebugWorlds)
			DebugWorlds.Add(TestWorld);
			DebugWorldNames.Add( MakeShareable(new FString(WorldName)) );
		}
	}

	if (DebugWorldsComboBox.IsValid())
	{
		// Attempt to restore the old selection
		if (bRestoreSelection)
		{
			TSharedPtr<FString> CurrentDebugWorld = GetDebugWorldName();
			if (CurrentDebugWorld.IsValid())
			{
				DebugWorldsComboBox->SetSelectedItem(CurrentDebugWorld);
			}
		}

		// Finally ensure we have a valid selection
		TSharedPtr<FString> CurrentSelection = DebugWorldsComboBox->GetSelectedItem();
		if (DebugWorldNames.Find(CurrentSelection) == INDEX_NONE)
		{
			if (DebugWorldNames.Num() > 0)
			{
				DebugWorldsComboBox->SetSelectedItem(DebugWorldNames[0]);
			}
			else
			{
				DebugWorldsComboBox->ClearSelection();
			}
		}

		DebugWorldsComboBox->RefreshOptions();
	}
}

void SBlueprintEditorSelectedDebugObjectWidget::GenerateDebugObjectNames(bool bRestoreSelection)
{
	// Empty the lists of actors and regenerate them
	DebugObjects.Empty();
	DebugObjectNames.Empty();
	DebugObjects.Add(nullptr);
	DebugObjectNames.Add(MakeShareable(new FString(GetNoDebugString())));

	// Grab custom objects that should always be visible, regardless of the world
	TArray<FCustomDebugObject> CustomDebugObjects;
	BlueprintEditor.Pin()->GetCustomDebugObjects(/*inout*/ CustomDebugObjects);

	for (const FCustomDebugObject& Entry : CustomDebugObjects)
	{
		if (Entry.NameOverride.IsEmpty())
		{
			AddDebugObject(Entry.Object);
		}
		else
		{
			AddDebugObjectWithName(Entry.Object, Entry.NameOverride);
		}
	}

	// Check for a specific debug world. If DebugWorld=nullptr we take that as "any PIE world"
	UWorld* DebugWorld = nullptr;
	if (DebugWorldsComboBox.IsValid())
	{
		TSharedPtr<FString> CurrentWorldSelection = DebugWorldsComboBox->GetSelectedItem();
		int32 SelectedIndex = INDEX_NONE;
		for (int32 WorldIdx = 0; WorldIdx < DebugWorldNames.Num(); ++WorldIdx)
		{
			if (DebugWorldNames[WorldIdx].IsValid() && CurrentWorldSelection.IsValid()
				&& (*DebugWorldNames[WorldIdx] == *CurrentWorldSelection))
			{
				SelectedIndex = WorldIdx;
				break;
			}
		}
		if (SelectedIndex > 0 && DebugWorldNames.IsValidIndex(SelectedIndex))
		{
			DebugWorld = DebugWorlds[SelectedIndex].Get();
		}
	}

	UWorld* PreviewWorld = BlueprintEditor.Pin()->GetPreviewScene()->GetWorld();

	if (!BlueprintEditor.Pin()->OnlyShowCustomDebugObjects())
	{
		const bool bModifiedIterator = CVarUseFastDebugObjectDiscovery.GetValueOnGameThread() == 1;
		UClass* BlueprintClass = GetBlueprintObj()->GeneratedClass;

		if (bModifiedIterator && BlueprintClass)
		{
			// Experimental new path for debug object discovery
			TArray<UObject*> BlueprintInstances;
			GetObjectsOfClass(BlueprintClass, BlueprintInstances, true);

			for (auto It = BlueprintInstances.CreateIterator(); It; ++It)
			{
				UObject* TestObject = *It;
				// Skip Blueprint preview objects (don't allow them to be selected for debugging)
				if (PreviewWorld != nullptr && TestObject->IsIn(PreviewWorld))
				{
					continue;
				}

				// check outer chain for pending kill objects
				bool bPendingKill = false;
				UObject* ObjOuter = TestObject;
				do
				{
					bPendingKill = ObjOuter->IsPendingKill();
					ObjOuter = ObjOuter->GetOuter();
				} while (!bPendingKill && ObjOuter != nullptr);

				if (!TestObject->HasAnyFlags(RF_ClassDefaultObject) && !bPendingKill)
				{
					ObjOuter = TestObject;
					UWorld *ObjWorld = nullptr;
					static bool bUseNewWorldCode = false;
					do		// Run through at least once in case the TestObject is a UGameInstance
					{
						UGameInstance *ObjGameInstance = Cast<UGameInstance>(ObjOuter);

						ObjOuter = ObjOuter->GetOuter();
						ObjWorld = ObjGameInstance ? ObjGameInstance->GetWorld() : Cast<UWorld>(ObjOuter);
					} while (ObjWorld == nullptr && ObjOuter != nullptr);

					if (ObjWorld)
					{
						// Make check on owning level (not streaming level)
						if (ObjWorld->PersistentLevel && ObjWorld->PersistentLevel->OwningWorld)
						{
							ObjWorld = ObjWorld->PersistentLevel->OwningWorld;
						}

						// We have a specific debug world and the object isn't in it
						if (DebugWorld && ObjWorld != DebugWorld)
						{
							continue;
						}

						if ((ObjWorld->WorldType == EWorldType::Editor) && (GUnrealEd->GetPIEViewport() == nullptr))
						{
							AddDebugObject(TestObject);
						}
						else if (ObjWorld->WorldType == EWorldType::PIE)
						{
							AddDebugObject(TestObject);
						}
					}
				}
			}
		}
		else
		{
			for (TObjectIterator<UObject> It; It; ++It)
			{
				UObject* TestObject = *It;

				// Skip Blueprint preview objects (don't allow them to be selected for debugging)
				if (PreviewWorld != nullptr && TestObject->IsIn(PreviewWorld))
				{
					continue;
				}

				const bool bPassesFlags = !TestObject->HasAnyFlags(RF_ClassDefaultObject) && !TestObject->IsPendingKill();
				const bool bGeneratedByAnyBlueprint = TestObject->GetClass()->ClassGeneratedBy != nullptr;
				const bool bGeneratedByThisBlueprint = bGeneratedByAnyBlueprint && GetBlueprintObj()->GeneratedClass && TestObject->IsA(GetBlueprintObj()->GeneratedClass);

				if (bPassesFlags && bGeneratedByThisBlueprint)
				{
					UObject *ObjOuter = TestObject;
					UWorld *ObjWorld = nullptr;
					do		// Run through at least once in case the TestObject is a UGameInstance
					{
						UGameInstance *ObjGameInstance = Cast<UGameInstance>(ObjOuter);

						ObjOuter = ObjOuter->GetOuter();
						ObjWorld = ObjGameInstance ? ObjGameInstance->GetWorld() : Cast<UWorld>(ObjOuter);
					} while (ObjWorld == nullptr && ObjOuter != nullptr);

					if (ObjWorld)
					{
						// Make check on owning level (not streaming level)
						if (ObjWorld->PersistentLevel && ObjWorld->PersistentLevel->OwningWorld)
						{
							ObjWorld = ObjWorld->PersistentLevel->OwningWorld;
						}

						// We have a specific debug world and the object isn't in it
						if (DebugWorld && ObjWorld != DebugWorld)
						{
							continue;
						}

						if ((ObjWorld->WorldType == EWorldType::Editor) && (GUnrealEd->GetPIEViewport() == nullptr))
						{
							AddDebugObject(TestObject);
						}
						else if (ObjWorld->WorldType == EWorldType::PIE)
						{
							AddDebugObject(TestObject);
						}
					}
				}
			}
		}
	}

	if (DebugObjectsComboBox.IsValid())
	{
		// Attempt to restore the old selection
		if (bRestoreSelection)
		{
			TSharedPtr<FString> CurrentDebugObject = GetDebugObjectName();
			if (CurrentDebugObject.IsValid())
			{
				DebugObjectsComboBox->SetSelectedItem(CurrentDebugObject);
			}
		}

		// Finally ensure we have a valid selection
		TSharedPtr<FString> CurrentSelection = DebugObjectsComboBox->GetSelectedItem();
		if (DebugObjectNames.Find(CurrentSelection) == INDEX_NONE)
		{
			if (DebugObjectNames.Num() > 0)
			{
				DebugObjectsComboBox->SetSelectedItem(DebugObjectNames[0]);
			}
			else
			{
				DebugObjectsComboBox->ClearSelection();
			}
		}

		DebugObjectsComboBox->RefreshOptions();
	}
}

TSharedPtr<FString> SBlueprintEditorSelectedDebugObjectWidget::GetDebugObjectName() const
{
	check(GetBlueprintObj());
	check(DebugObjects.Num() == DebugObjectNames.Num());
	if (UObject* DebugObj = GetBlueprintObj()->GetObjectBeingDebugged())
	{
		for (int32 ObjectIndex = 0; ObjectIndex < DebugObjects.Num(); ++ObjectIndex)
		{
			if (DebugObjects[ObjectIndex].IsValid() && (DebugObjects[ObjectIndex].Get() == DebugObj))
			{
				return DebugObjectNames[ObjectIndex];
			}
		}
	}

	return DebugObjectNames[0];
}

TSharedPtr<FString> SBlueprintEditorSelectedDebugObjectWidget::GetDebugWorldName() const
{
	check(GetBlueprintObj());
	if (ensure(DebugWorlds.Num() == DebugWorldNames.Num()))
	{
		UWorld* DebugWorld = GetBlueprintObj()->GetWorldBeingDebugged();
		if (DebugWorld != nullptr)
		{
			for (int32 WorldIndex = 0; WorldIndex < DebugWorlds.Num(); ++WorldIndex)
			{
				if (DebugWorlds[WorldIndex].IsValid() && (DebugWorlds[WorldIndex].Get() == DebugWorld))
				{
					return DebugWorldNames[WorldIndex];
				}
			}
		}
	}

	return DebugWorldNames[0];
}

void SBlueprintEditorSelectedDebugObjectWidget::DebugWorldSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection != GetDebugWorldName())
	{
		check(DebugWorlds.Num() == DebugWorldNames.Num());
		for (int32 WorldIdx = 0; WorldIdx < DebugWorldNames.Num(); ++WorldIdx)
		{
			if (DebugWorldNames[WorldIdx] == NewSelection)
			{
				GetBlueprintObj()->SetWorldBeingDebugged(DebugWorlds[WorldIdx].Get());

				GetBlueprintObj()->SetObjectBeingDebugged(nullptr);
				LastObjectObserved.Reset();

				GenerateDebugObjectNames(false);
				break;
			}
		}
	}
}

void SBlueprintEditorSelectedDebugObjectWidget::DebugObjectSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection != GetDebugObjectName())
	{
		check(DebugObjects.Num() == DebugObjectNames.Num());
		for (int32 ObjectIndex = 0; ObjectIndex < DebugObjectNames.Num(); ++ObjectIndex)
		{
			if (DebugObjectNames[ObjectIndex] == NewSelection)
			{
				UObject* DebugObj = DebugObjects[ObjectIndex].Get();
				GetBlueprintObj()->SetObjectBeingDebugged(DebugObj);

				LastObjectObserved = DebugObj;
				break;
			}
		}
	}
}

EVisibility SBlueprintEditorSelectedDebugObjectWidget::IsSelectDebugObjectButtonVisible() const
{
	check(GetBlueprintObj());
	if (UObject* DebugObj = GetBlueprintObj()->GetObjectBeingDebugged())
	{
		if (AActor* Actor = Cast<AActor>(DebugObj))
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

void SBlueprintEditorSelectedDebugObjectWidget::SelectedDebugObject_OnClicked()
{
	if (UObject* DebugObj = GetBlueprintObj()->GetObjectBeingDebugged())
	{
		if (AActor* Actor = Cast<AActor>(DebugObj))
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true, true);
			GUnrealEd->Exec(Actor->GetWorld(), TEXT("CAMERA ALIGN ACTIVEVIEWPORTONLY"));
		}
	}
}

EVisibility SBlueprintEditorSelectedDebugObjectWidget::IsDebugWorldComboVisible() const
{
	if (GEditor->PlayWorld != nullptr)
	{
		int32 LocalWorldCount = 0;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World() != nullptr)
			{
				++LocalWorldCount;
			}
		}

		if (LocalWorldCount > 1)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

FString SBlueprintEditorSelectedDebugObjectWidget::MakeDebugObjectLabel(UObject* TestObject, bool bAddContextIfSelectedInEditor) const
{
	FString CustomLabelFromEditor = BlueprintEditor.Pin()->GetCustomDebugObjectLabel(TestObject);
	if (!CustomLabelFromEditor.IsEmpty())
	{
		return CustomLabelFromEditor;
	}

	auto GetActorLabelStringLambda = [](AActor* InActor, bool bIncludeNetModeSuffix, bool bIncludeSelectedSuffix)
	{
		FString Label = InActor->GetActorLabel();

		FString Context;

		if (bIncludeNetModeSuffix)
		{
			switch (InActor->GetNetMode())
			{
			case ENetMode::NM_Client:
			{
				Context = NSLOCTEXT("BlueprintEditor", "DebugWorldClient", "Client").ToString();

				FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(InActor->GetWorld());
				if (WorldContext != nullptr && WorldContext->PIEInstance > 1)
				{
					Context += TEXT(" ");
					Context += FText::AsNumber(WorldContext->PIEInstance - 1).ToString();
				}
			}
			break;

			case ENetMode::NM_ListenServer:
			case ENetMode::NM_DedicatedServer:
				Context = NSLOCTEXT("BlueprintEditor", "DebugWorldServer", "Server").ToString();
				break;
			}
		}

		if (bIncludeSelectedSuffix && InActor->IsSelected())
		{
			if (!Context.IsEmpty())
			{
				Context += TEXT(" - ");
			}

			Context += NSLOCTEXT("BlueprintEditor", "DebugObjectSelected", "selected").ToString();
		}

		if (!Context.IsEmpty())
		{
			Label = FString::Printf(TEXT("%s (%s)"), *Label, *Context);
		}

		return Label;
	};

	// Include net mode suffix when "All worlds" is selected.
	const bool bIncludeNetModeSuffix = *GetDebugWorldName() == GetDebugAllWorldsString();

	FString Label;
	if (AActor* Actor = Cast<AActor>(TestObject))
	{
		Label = GetActorLabelStringLambda(Actor, bIncludeNetModeSuffix, bAddContextIfSelectedInEditor);
	}
	else
	{
		if (AActor* ParentActor = TestObject->GetTypedOuter<AActor>())
		{
			// This gives the most precision, but is pretty long for the combo box
			//const FString RelativePath = TestObject->GetPathName(/*StopOuter=*/ ParentActor);
			const FString RelativePath = TestObject->GetName();
			Label = FString::Printf(TEXT("%s in %s"), *RelativePath, *GetActorLabelStringLambda(ParentActor, bIncludeNetModeSuffix, bAddContextIfSelectedInEditor));
		}
		else
		{
			Label = TestObject->GetName();
		}
	}

	return Label;
}

void SBlueprintEditorSelectedDebugObjectWidget::AddDebugObject(UObject* TestObject)
{
	FString Label = MakeDebugObjectLabel(TestObject, true);
	AddDebugObjectWithName(TestObject, Label);
}

void SBlueprintEditorSelectedDebugObjectWidget::AddDebugObjectWithName(UObject* TestObject, const FString& TestObjectName)
{
	DebugObjects.Add(TestObject);
	DebugObjectNames.Add(MakeShareable(new FString(TestObjectName)));
}

TSharedRef<SWidget> SBlueprintEditorSelectedDebugObjectWidget::CreateDebugObjectItemWidget(TSharedPtr<FString> InItem)
{
	FString ItemString;

	if (InItem.IsValid())
	{
		ItemString = *InItem;
	}

	return SNew(STextBlock)
		.Text(FText::FromString(*ItemString));
}

FText SBlueprintEditorSelectedDebugObjectWidget::GetSelectedDebugObjectTextLabel() const
{
	FString Label;

	UBlueprint* Blueprint = GetBlueprintObj();
	if (Blueprint != nullptr)
	{
		UObject* DebugObj = Blueprint->GetObjectBeingDebugged();
		if (DebugObj != nullptr)
		{
			// Exclude the editor selection suffix for the combo button's label.
			Label = MakeDebugObjectLabel(DebugObj, false);
		}
	}
		
	if (Label.IsEmpty())
	{
		Label = *GetDebugObjectName();
	}

	return FText::FromString(Label);
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
