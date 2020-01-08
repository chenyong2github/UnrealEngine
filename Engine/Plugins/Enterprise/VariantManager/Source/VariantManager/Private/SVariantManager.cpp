// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVariantManager.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "GameFramework/Actor.h"
#include "Editor.h"
#include "EditorFontGlyphs.h"
#include "Engine/Selection.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/ActorDragDropGraphEdOp.h"
#include "DragAndDrop/ClassDragDropOp.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "VariantManager.h"
#include "LevelVariantSets.h"
#include "VariantSet.h"
#include "Variant.h"
#include "FunctionCaller.h"
#include "VariantManagerClipboard.h"
#include "VariantManagerEditorCommands.h"
#include "DisplayNodes/VariantManagerDisplayNode.h"
#include "DisplayNodes/VariantManagerActorNode.h"
#include "DisplayNodes/VariantManagerVariantNode.h"
#include "DisplayNodes/VariantManagerVariantSetNode.h"
#include "DisplayNodes/VariantManagerPropertyNode.h"
#include "DisplayNodes/VariantManagerStructPropertyNode.h"
#include "DisplayNodes/VariantManagerEnumPropertyNode.h"
#include "DisplayNodes/VariantManagerStringPropertyNode.h"
#include "DisplayNodes/VariantManagerFunctionPropertyNode.h"
#include "DisplayNodes/VariantManagerOptionPropertyNode.h"
#include "SVariantManagerNodeTreeView.h"
#include "SVariantManagerActorListView.h"
#include "SVariantManagerTableRow.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ObjectTools.h"
#include "PropertyValue.h"
#include "ScopedTransaction.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CString.h"
#include "VariantManagerSelection.h"
#include "Widgets/Input/SButton.h"
#include "Misc/ITransaction.h"
#include "Modules/ModuleManager.h"
#include "LevelEditor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "CapturableProperty.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "VariantManagerUtils.h"
#include "SwitchActor.h"
#include "Brushes/SlateImageBrush.h"

#define LOCTEXT_NAMESPACE "SVariantManager"

FSplitterValues::FSplitterValues(FString& InSerialized)
{
	TArray<FString> SplitString;
	InSerialized.ParseIntoArray(SplitString, TEXT(";"));

	if (SplitString.Num() != 4)
	{
		return;
	}

	VariantColumn = FCString::Atof(*SplitString[0]);
	ActorColumn = FCString::Atof(*SplitString[1]);
	PropertyNameColumn = FCString::Atof(*SplitString[2]);
	PropertyValueColumn = FCString::Atof(*SplitString[3]);
}

FString FSplitterValues::ToString()
{
	return FString::SanitizeFloat(VariantColumn) + TEXT(";") +
		   FString::SanitizeFloat(ActorColumn) + TEXT(";") +
		   FString::SanitizeFloat(PropertyNameColumn) + TEXT(";") +
		   FString::SanitizeFloat(PropertyValueColumn);
}

TSharedRef<SWidget> SVariantManager::MakeAddButton()
{
	return SNew(SButton)
	.OnClicked(this, &SVariantManager::OnAddVariantSetClicked)
	.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
	.Content()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "NormalText.Important")
			.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
			.Text(FEditorFontGlyphs::Plus)
		]
		+ SHorizontalBox::Slot()
		.Padding(4, 0, 0, 0)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "NormalText.Important")
			.Text(LOCTEXT("VariantSetPlusText", "Variant Set"))
		]
	];
}

TSharedRef<ITableRow> SVariantManager::MakeCapturedPropertyRow(TSharedPtr<FVariantManagerPropertyNode> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SVariantManagerTableRow, OwnerTable, StaticCastSharedPtr<FVariantManagerDisplayNode>(Item).ToSharedRef());
}

TSharedPtr<SWidget> SVariantManager::OnPropertyListContextMenuOpening()
{
	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	CapturedPropertyListView->GetSelectedItems(SelectedNodes);

	if (SelectedNodes.Num() > 0)
	{
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, GetPropertyListCommandBindings());

		SelectedNodes[0]->BuildContextMenu(MenuBuilder);

		return MenuBuilder.MakeWidget();
	}

	return nullptr;
}

void SVariantManager::Construct(const FArguments& InArgs, TSharedRef<FVariantManager> InVariantManager)
{
	VariantManagerPtr = InVariantManager;

	bAutoCaptureProperties = false;

	CreateCommandBindings();

	const float CommonPadding = 3.f;
	const float CommonHeaderMaxHeight = 26.0f;

	SAssignNew(NodeTreeView, SVariantManagerNodeTreeView, InVariantManager->GetNodeTree());

	SAssignNew(ActorListView, SVariantManagerActorListView, InVariantManager)
		.ListItemsSource(&DisplayedActors);

	FSplitterValues SplitterValues;
	FString SplitterValuesString;
	if (GConfig->GetString(TEXT("VariantManager"), TEXT("MainSplitterValues"), SplitterValuesString, GEditorPerProjectIni))
	{
		SplitterValues = FSplitterValues(SplitterValuesString);
	}

	RightPropertyColumnWidth = SplitterValues.PropertyValueColumn / (SplitterValues.PropertyValueColumn + SplitterValues.PropertyNameColumn);
	ColumnSizeData.LeftColumnWidth = TAttribute<float>(this, &SVariantManager::OnGetLeftColumnWidth);
	ColumnSizeData.RightColumnWidth = TAttribute<float>(this, &SVariantManager::OnGetRightColumnWidth);
	ColumnSizeData.OnWidthChanged = SSplitter::FOnSlotResized::CreateSP(this, &SVariantManager::OnSetColumnWidth);

	InVariantManager->GetSelection().GetOnOutlinerNodeSelectionChanged().AddSP(this, &SVariantManager::RefreshActorList);
	InVariantManager->GetSelection().GetOnActorNodeSelectionChanged().AddSP(this, &SVariantManager::OnActorNodeSelectionChanged);

	// Subscribe to when objects are modified so that we can auto-resolve when components/array properties are added/removed/renamed
	OnObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &SVariantManager::OnObjectTransacted);
	OnObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &SVariantManager::OnObjectPropertyChanged);
	OnBeginPieHandle = FEditorDelegates::BeginPIE.AddRaw(this, &SVariantManager::OnPieEvent);
	OnEndPieHandle = FEditorDelegates::EndPIE.AddRaw(this, &SVariantManager::OnPieEvent);

	// Do this so that if we recompile a function caller changing a function name we'll rebuild our nodes to display the
	// new names
	OnBlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddSP(this, &SVariantManager::OnBlueprintCompiled);

	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		OnMapChangedHandle = LevelEditorModule.OnMapChanged().AddSP(this, &SVariantManager::OnMapChanged);
	}

	RecordButtonBrush = MakeShared<FSlateImageBrush>(FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/CA_Record.png"), FVector2D(24.0f, 24.0f));

	ChildSlot
	[
		SAssignNew(MainSplitter, SSplitter)
		.Orientation(Orient_Horizontal)
		// VariantSet/Variant column
		+SSplitter::Slot()
		.Value(SplitterValues.VariantColumn)
		[
			SNew(SVerticalBox)

			// +VariantSets button and search
			+SVerticalBox::Slot()
			.MaxHeight(CommonHeaderMaxHeight)
			.AutoHeight()
			.Padding(FMargin(CommonPadding, CommonPadding, 0.0f, CommonPadding))
			[
				SNew(SBox)
				.HeightOverride(CommonHeaderMaxHeight)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, CommonPadding+2.0f, 1.f))
					.AutoWidth()
					[
						MakeAddButton()
					]

					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, CommonPadding+2.0f, 0.f))
					.AutoWidth()
					.MaxWidth(CommonHeaderMaxHeight) // square aspect ratio
					[
						SNew(SBox)
						.HeightOverride(CommonHeaderMaxHeight - 8.0f) // These so that it matches the height of the search box
						.WidthOverride(CommonHeaderMaxHeight - 8.0f)
						[
							SNew(SCheckBox)
							.Style(FCoreStyle::Get(), "ToggleButtonCheckbox")
							.ToolTipText(LOCTEXT("AutoCaptureTooltip", "Enable or disable auto-capture properties"))
							.IsChecked_Lambda([&]()
							{
								return bAutoCaptureProperties? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([&](const ECheckBoxState NewState)
							{
								bAutoCaptureProperties = NewState == ECheckBoxState::Checked;
							})
							[
								SNew(SBox)
								.Padding(FMargin(0.0f, 2.0, 2.0f, 2.0)) // Extra padding on the right because ToggleButtonCheckboxes always nudges the image to the right
								[
									SNew(SImage)
									.Image(RecordButtonBrush.Get())
								]
							]
						]
					]

					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SSearchBox)
						.HintText(LOCTEXT("VariantManagerFilterText", "Filter"))
						.OnTextChanged(this, &SVariantManager::OnOutlinerSearchChanged)
					]
				]
			]

			+SVerticalBox::Slot()
			.Padding(FMargin(CommonPadding, 0.0f, 0.0f, CommonPadding))
			.VAlign(VAlign_Fill)
			.FillHeight(1.0f)
			[
				SNew(SScrollBorder, NodeTreeView.ToSharedRef())
				[
					SNew(SBox) // Very important to prevent the tree from expanding freely
					[
						NodeTreeView.ToSharedRef()
					]
				]
			]
		]

		// Actor column
		+ SSplitter::Slot()
		.Value(SplitterValues.ActorColumn)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.MaxHeight(CommonHeaderMaxHeight)
			.AutoHeight()
			.Padding(FMargin(0.0f, CommonPadding, 0.0f, CommonPadding))
			[
				SNew(SBox)
				.HeightOverride(CommonHeaderMaxHeight)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "NormalText.Important")
					.Text(LOCTEXT("ActorsText", "Actors"))
				]
			]

			+ SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, CommonPadding))
			.VAlign(VAlign_Fill)
			.FillHeight(1.0f)
			[
				ActorListView.ToSharedRef()
			]
		]

		// Properties column
		+ SSplitter::Slot()
		.Value(SplitterValues.PropertyNameColumn + SplitterValues.PropertyValueColumn)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.MaxHeight(CommonHeaderMaxHeight)
			.AutoHeight()
			.Padding(0.0f, CommonPadding, CommonPadding, CommonPadding)
			[
				// Headers
				SNew(SSplitter)
				.Orientation(Orient_Horizontal)
				+ SSplitter::Slot()
				.Value(ColumnSizeData.LeftColumnWidth)
				.OnSlotResized(SSplitter::FOnSlotResized::CreateLambda([](float InNewWidth)
				{
					//This has to be bound or the splitter will take it upon itself to determine the size
					//We do nothing here because it is handled by the column size data
				}))
				[
					SNew(SBox)
					.HeightOverride(CommonHeaderMaxHeight)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Text(LOCTEXT("PropertiesText", "Properties"))
					]
				]
				+ SSplitter::Slot()
				.Value(ColumnSizeData.RightColumnWidth)
				.OnSlotResized(ColumnSizeData.OnWidthChanged)
				[
					SNew(SBox)
					.HeightOverride(CommonHeaderMaxHeight)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Text(LOCTEXT("PropertiesValuesText", "Values"))
					]
				]
			]
			+ SVerticalBox::Slot()
			.Padding(0.0f, 0.0f, CommonPadding, CommonPadding)
			.VAlign(VAlign_Fill)
			.FillHeight(1.0f)
			[
				SAssignNew(CapturedPropertyListView, SListView<TSharedPtr<FVariantManagerPropertyNode>>)
				.SelectionMode(ESelectionMode::Single)
				.ListItemsSource(&DisplayedPropertyNodes)
				.OnContextMenuOpening(this, &SVariantManager::OnPropertyListContextMenuOpening)
				.OnGenerateRow(this, &SVariantManager::MakeCapturedPropertyRow)
				.Visibility(EVisibility::Visible)
			]
		]
	];

	RefreshVariantTree();
}

SVariantManager::~SVariantManager()
{
	FCoreUObjectDelegates::OnObjectTransacted.RemoveAll(this);

	GEditor->OnBlueprintCompiled().Remove(OnBlueprintCompiledHandle);
	OnBlueprintCompiledHandle.Reset();

	FCoreUObjectDelegates::OnObjectTransacted.Remove(OnObjectTransactedHandle);
	OnObjectTransactedHandle.Reset();

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnObjectPropertyChangedHandle);
	OnObjectPropertyChangedHandle.Reset();

	FEditorDelegates::BeginPIE.Remove(OnBeginPieHandle);
	OnBeginPieHandle.Reset();

	FEditorDelegates::EndPIE.Remove(OnEndPieHandle);
	OnEndPieHandle.Reset();

	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.OnMapChanged().Remove(OnMapChangedHandle);
		OnMapChangedHandle.Reset();
	}

	// Save splitter layout
	{
		FChildren* Slots = MainSplitter->GetChildren();
		if (Slots->Num() == 3)
		{
			FSplitterValues Values;
			Values.VariantColumn = MainSplitter->SlotAt(0).SizeValue.Get();
			Values.ActorColumn = MainSplitter->SlotAt(1).SizeValue.Get();
			float PropertyCombo = MainSplitter->SlotAt(2).SizeValue.Get();
			Values.PropertyNameColumn = PropertyCombo * OnGetLeftColumnWidth();
			Values.PropertyValueColumn = PropertyCombo * OnGetRightColumnWidth();

			GConfig->SetString(TEXT("VariantManager"), TEXT("MainSplitterValues"), *Values.ToString(), GEditorPerProjectIni);
		}
	}
}

void SVariantManager::CreateCommandBindings()
{
	VariantTreeCommandBindings = MakeShareable(new FUICommandList);
	ActorListCommandBindings = MakeShareable(new FUICommandList);
	PropertyListCommandBindings = MakeShareable(new FUICommandList);

	VariantTreeCommandBindings->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &SVariantManager::CutSelectionVariantTree),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanCutVariantTree)
	);

	VariantTreeCommandBindings->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SVariantManager::CopySelectionVariantTree),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanCopyVariantTree)
	);

	VariantTreeCommandBindings->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SVariantManager::PasteSelectionVariantTree),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanPasteVariantTree)
	);

	VariantTreeCommandBindings->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SVariantManager::DeleteSelectionVariantTree),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanDeleteVariantTree)
	);

	VariantTreeCommandBindings->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SVariantManager::DuplicateSelectionVariantTree),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanDuplicateVariantTree)
	);

	VariantTreeCommandBindings->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SVariantManager::RenameSelectionVariantTree),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanRenameVariantTree)
	);

	VariantTreeCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().AddVariantSetCommand,
		FExecuteAction::CreateSP(this, &SVariantManager::CreateNewVariantSet),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanCreateNewVariantSet)
	);

	VariantTreeCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().SwitchOnSelectedVariantCommand,
		FExecuteAction::CreateSP(this, &SVariantManager::SwitchOnSelectedVariant),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanSwitchOnVariant)
	);

	VariantTreeCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().CreateThumbnailVariantCommand,
		FExecuteAction::CreateSP(this, &SVariantManager::CreateThumbnail),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanCreateThumbnail)
	);

	VariantTreeCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().ClearThumbnailVariantCommand,
		FExecuteAction::CreateSP(this, &SVariantManager::ClearThumbnail),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanClearThumbnail)
	);

	// This command is added to both lists so that we can add actors by right clicking on variant
	// nodes or by right clicking on the actor list with a variant node selected
	VariantTreeCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().AddSelectedActorsCommand,
		FExecuteAction::CreateSP(this, &SVariantManager::AddEditorSelectedActorsToVariant),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanAddEditorSelectedActorsToVariant)
	);

	ActorListCommandBindings->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateSP(this, &SVariantManager::CutSelectionActorList),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanCutActorList)
	);

	ActorListCommandBindings->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateSP(this, &SVariantManager::CopySelectionActorList),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanCopyActorList)
	);

	ActorListCommandBindings->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateSP(this, &SVariantManager::PasteSelectionActorList),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanPasteActorList)
	);

	ActorListCommandBindings->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SVariantManager::DeleteSelectionActorList),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanDeleteActorList)
	);

	ActorListCommandBindings->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateSP(this, &SVariantManager::DuplicateSelectionActorList),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanDuplicateActorList)
	);

	ActorListCommandBindings->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SVariantManager::RenameSelectionActorList),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanRenameActorList)
	);

	// This command is added to both lists so that we can add actors by right clicking on variant
	// nodes or by right clicking on the actor list with a variant node selected
	ActorListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().AddSelectedActorsCommand,
		FExecuteAction::CreateSP(this, &SVariantManager::AddEditorSelectedActorsToVariant),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanAddEditorSelectedActorsToVariant)
	);

	ActorListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().AddPropertyCaptures,
		FExecuteAction::CreateSP(this, &SVariantManager::CaptureNewPropertiesFromSelectedActors),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanCaptureNewPropertiesFromSelectedActors)
	);

	ActorListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().AddFunction,
		FExecuteAction::CreateSP(this, &SVariantManager::AddFunctionCaller),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanAddFunctionCaller)
	);

	ActorListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().RemoveActorBindings,
		FExecuteAction::CreateSP(this, &SVariantManager::RemoveActorBindings),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanRemoveActorBindings)
	);

	PropertyListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().ApplyProperty,
		FExecuteAction::CreateSP(this, &SVariantManager::ApplyProperty),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanApplyProperty)
	);

	PropertyListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().RecordProperty,
		FExecuteAction::CreateSP(this, &SVariantManager::RecordProperty),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanRecordProperty)
	);

	PropertyListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().RemoveCapture,
		FExecuteAction::CreateSP(this, &SVariantManager::RemoveCapture),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanRemoveCapture)
	);

	PropertyListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().CallFunction,
		FExecuteAction::CreateSP(this, &SVariantManager::CallDirectorFunction),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanCallDirectorFunction)
	);

	PropertyListCommandBindings->MapAction(
		FVariantManagerEditorCommands::Get().RemoveFunction,
		FExecuteAction::CreateSP(this, &SVariantManager::RemoveDirectorFunctionCaller),
		FCanExecuteAction::CreateSP(this, &SVariantManager::CanRemoveDirectorFunctionCaller)
	);
}

void SVariantManager::AddEditorSelectedActorsToVariant()
{
	TArray<AActor*> Actors;
	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator SelectedObject(*Selection); SelectedObject; ++SelectedObject)
	{
		if (AActor* SelectedActor = Cast<AActor>(*SelectedObject))
		{
			Actors.Add(SelectedActor);
		}
	}

	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>>& Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	TArray<UVariant*> SelectedVariants;
	for (const TSharedRef<FVariantManagerDisplayNode>& Node : Nodes)
	{
		if (Node->GetType() == EVariantManagerNodeType::Variant)
		{
			TSharedPtr<FVariantManagerVariantNode> SomeNodeAsVariant = StaticCastSharedRef<FVariantManagerVariantNode>(Node);

			if (SomeNodeAsVariant.IsValid())
			{
				SelectedVariants.Add(&SomeNodeAsVariant->GetVariant());
			}
		}
	}

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("AddEditorSelectedActorsToVariantTransaction", "Add {0} actor {0}|plural(one=binding,other=bindings) to {1} {1}|plural(one=variant,other=variants)"),
		Actors.Num(),
		SelectedVariants.Num()
	));

	VariantManager->CreateObjectBindingsAndCaptures(Actors, SelectedVariants);

	RefreshActorList();
}

bool SVariantManager::CanAddEditorSelectedActorsToVariant()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>>& Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	// Get all selected variants
	TArray<UVariant*> SelectedVariants;
	for (const TSharedRef<FVariantManagerDisplayNode>& Node : Nodes)
	{
		if (Node->GetType() == EVariantManagerNodeType::Variant)
		{
			TSharedPtr<FVariantManagerVariantNode> SomeNodeAsVariant = StaticCastSharedRef<FVariantManagerVariantNode>(Node);
			if (SomeNodeAsVariant.IsValid())
			{
				SelectedVariants.Add(&SomeNodeAsVariant->GetVariant());
			}
		}
	}

	// Get actors selected in the editor
	TArray<TWeakObjectPtr<AActor>> SelectedActors;
	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator SelectedObject(*Selection); SelectedObject; ++SelectedObject)
	{
		AActor* SelectedActor = Cast<AActor>(*SelectedObject);
		if (SelectedActor)
		{
			SelectedActors.Add(SelectedActor);
		}
	}

	// See if we can add at least one new binding to at least one of the selected variants
	for (UVariant* Var : SelectedVariants)
	{
		TArray<TWeakObjectPtr<AActor>> ActorsWeCanAdd;
		VariantManager->CanAddActorsToVariant(SelectedActors, Var, ActorsWeCanAdd);

		if (ActorsWeCanAdd.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

void SVariantManager::CreateNewVariantSet()
{
	TSharedPtr<FVariantManager> VarMan = VariantManagerPtr.Pin();
	if (VarMan.IsValid())
	{
		VarMan->CreateVariantSet(VarMan->GetCurrentLevelVariantSets());

		RefreshVariantTree();
	}
}

bool SVariantManager::CanCreateNewVariantSet()
{
	return true;
}

// Used both by copy and cut
void CopySelectionVariantTreeInternal(TSharedPtr<FVariantManager> VariantManager, TArray<UVariant*>& InCopiedVariants, TArray<UVariantSet*>& InCopiedVariantSets)
{
	FVariantManagerClipboard::Empty();

	// Keep track of variant duplication so that we can transfer thumbnails later
	// We could use the clipboard arrays for this, but this does not make
	// any assumptions about how the clipboard stores its stuff
	TArray<UVariant*> OriginalVariants;
	TArray<UVariant*> NewVariants;

	// Add copies of our stuff to the clipboard
	for (UVariantSet* VariantSet : InCopiedVariantSets)
	{
		UVariantSet* NewVariantSet = DuplicateObject(VariantSet, nullptr);
		FVariantManagerClipboard::Push(NewVariantSet);

		OriginalVariants.Append(VariantSet->GetVariants());
		NewVariants.Append(NewVariantSet->GetVariants());
	}
	for (UVariant* Variant : InCopiedVariants)
	{
		// Don't copy variants those parents are also copied
		if (InCopiedVariantSets.Contains(Variant->GetParent()))
		{
			continue;
		}

		// Transient package here because our Outer might be deleted while we're in the clipboard
		UVariant* NewVariant = DuplicateObject(Variant, nullptr);
		FVariantManagerClipboard::Push(NewVariant);

		OriginalVariants.Add(Variant);
		NewVariants.Add(NewVariant);
	}

	VariantManager->CopyVariantThumbnails(NewVariants, OriginalVariants);
}

void SVariantManager::CutSelectionVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	if (!VariantManager.IsValid())
	{
		return;
	}

	TArray<UVariant*> CopiedVariants;
	TArray<UVariantSet*> CopiedVariantSets;
	VariantManager->GetSelection().GetSelectedVariantsAndVariantSets(CopiedVariants, CopiedVariantSets);

	CopySelectionVariantTreeInternal(VariantManager, CopiedVariants, CopiedVariantSets);

	// Don't capture CopySelection in the transaction buffer because if we undo we kind of expect
	// our cut stuff to still be in the clipboard
	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("CutSelectionVariantTreeTransaction", "Cut {0} {0}|plural(one=variant,other=variants) and {1} variant {1}|plural(one=set,other=sets)"),
		CopiedVariants.Num(),
		CopiedVariantSets.Num()
		));


	VariantManager->RemoveVariantsFromParent(CopiedVariants);
	VariantManager->RemoveVariantSetsFromParent(CopiedVariantSets);

	RefreshVariantTree();
	RefreshActorList();
}

void SVariantManager::CopySelectionVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	if (!VariantManager.IsValid())
	{
		return;
	}

	TArray<UVariant*> CopiedVariants;
	TArray<UVariantSet*> CopiedVariantSets;
	VariantManager->GetSelection().GetSelectedVariantsAndVariantSets(CopiedVariants, CopiedVariantSets);

	CopySelectionVariantTreeInternal(VariantManager, CopiedVariants, CopiedVariantSets);
}

void SVariantManager::PasteSelectionVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	if (!VariantManager.IsValid())
	{
		return;
	}

	const TSet<TSharedRef<FVariantManagerDisplayNode>> SelectedNodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();
	ULevelVariantSets* LevelVarSets = VariantManager->GetCurrentLevelVariantSets();

	// Keep track of variant duplication so that we can transfer thumbnails later
	// We could use the clipboard arrays for this, but this does not make
	// any assumptions about how the clipboard stores its stuff
	TArray<UVariant*> OriginalVariants;
	TArray<UVariant*> NewVariants;

	const TArray<TStrongObjectPtr<UVariantSet>>& CopiedVariantSets = FVariantManagerClipboard::GetVariantSets();
	const TArray<TStrongObjectPtr<UVariant>>& CopiedVariants = FVariantManagerClipboard::GetVariants();

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("PasteSelectionVariantTreeTransaction", "Paste {0} {0}|plural(one=variant,other=variants) and {1} variant {1}|plural(one=set,other=sets)"),
		CopiedVariants.Num(),
		CopiedVariantSets.Num()
		));

	// Paste variant sets onto the tree, regardless of where we clicked
	TArray<UVariantSet*> VarSetsToAdd;
	for (const TStrongObjectPtr<UVariantSet>& CopiedVarSet : CopiedVariantSets)
	{
		// Duplicate objects since we'll maintain this in the clipboard
		UVariantSet* NewVariantSet = DuplicateObject(CopiedVarSet.Get(), nullptr);
		VarSetsToAdd.Add(NewVariantSet);

		OriginalVariants.Append(CopiedVarSet.Get()->GetVariants());
		NewVariants.Append(NewVariantSet->GetVariants());
	}
	VariantManager->AddVariantSets(VarSetsToAdd, LevelVarSets);

	// Add our copied variants to either the first varset we find, or create a new one
	if (CopiedVariants.Num() > 0)
	{
		TSharedPtr<FVariantManagerVariantSetNode> FirstVarSetNodeWeFound(nullptr);

		// See if we have a variant set node selected
		for (const TSharedRef<FVariantManagerDisplayNode>& SelectedNode : SelectedNodes)
		{
			if (SelectedNode->GetType() == EVariantManagerNodeType::VariantSet)
			{
				FirstVarSetNodeWeFound = StaticCastSharedRef<FVariantManagerVariantSetNode>(SelectedNode);
				if (FirstVarSetNodeWeFound.IsValid())
				{
					break;
				}
			}
		}

		// If not, but we have selected a variant, pick its variant set so that we can paste
		// the copied variants as siblings
		for (const TSharedRef<FVariantManagerDisplayNode>& SelectedNode : SelectedNodes)
		{
			if (SelectedNode->GetType() == EVariantManagerNodeType::Variant)
			{
				TSharedPtr<FVariantManagerVariantNode> SomeVariantNode = StaticCastSharedRef<FVariantManagerVariantNode>(SelectedNode);
				if (SomeVariantNode.IsValid())
				{
					FirstVarSetNodeWeFound = StaticCastSharedPtr<FVariantManagerVariantSetNode>(SomeVariantNode->GetParent());
				}
			}
		}

		UVariantSet* TargetVarSet = nullptr;

		// If we still have nowhere to place our copied variants, create a new variant set
		if (FirstVarSetNodeWeFound.IsValid())
		{
			TargetVarSet = &FirstVarSetNodeWeFound->GetVariantSet();
		}
		if (TargetVarSet == nullptr)
		{
			TargetVarSet = VariantManager->CreateVariantSet(LevelVarSets);
		}

		// Actually paste our copied variants
		TArray<UVariant*> VariantsToAdd;
		for (const TStrongObjectPtr<UVariant>& CopiedVariant : CopiedVariants)
		{
			// Make sure that if we pasted our parent variant set (which will already have CopiedVariant), we
			// don't do it again. We do this check on copy/cut, but its better to be safe
			UVariantSet* ParentVariantSet = CopiedVariant.Get()->GetParent();
			if (CopiedVariantSets.FindByPredicate([ParentVariantSet](const TStrongObjectPtr<UVariantSet>& VarSet)
			{
				return VarSet.Get() == ParentVariantSet;
			}))
			{
				continue;
			}

			// Duplicate objects since we'll maintain this in the clipboard
			UVariant* NewVariant = DuplicateObject(CopiedVariant.Get(), nullptr);
			VariantsToAdd.Add(NewVariant);

			OriginalVariants.Add(CopiedVariant.Get());
			NewVariants.Add(NewVariant);
		}
		VariantManager->AddVariants(VariantsToAdd, TargetVarSet);
	}

	VariantManager->CopyVariantThumbnails(NewVariants, OriginalVariants);

	RefreshVariantTree();
	RefreshActorList(); // For example if we paste a variant within an empty, selected variant set. We need to show the actors of the new variant
}

void SVariantManager::DeleteSelectionVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	if (!VariantManager.IsValid())
	{
		return;
	}

	TArray<UVariant*> VariantsToDelete;
	TArray<UVariantSet*> VariantSetsToDelete;
	VariantManager->GetSelection().GetSelectedVariantsAndVariantSets(VariantsToDelete, VariantSetsToDelete);

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("DeleteSelectionVariantTreeTransaction", "Delete {0} {0}|plural(one=variant,other=variants) and {1} variant {1}|plural(one=set,other=sets)"),
		VariantsToDelete.Num(),
		VariantSetsToDelete.Num()
		));

	VariantManager->RemoveVariantsFromParent(VariantsToDelete);
	VariantManager->RemoveVariantSetsFromParent(VariantSetsToDelete);

	RefreshVariantTree();
	RefreshActorList();
}

void SVariantManager::DuplicateSelectionVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	if (!VariantManager.IsValid())
	{
		return;
	}

	ULevelVariantSets* LevelVarSets = VariantManager->GetCurrentLevelVariantSets();

	// Collect all variants and variant sets that we selected
	TArray<UVariant*> VariantsToDuplicate;
	TArray<UVariantSet*> VariantSetsToDuplicate;
	VariantManager->GetSelection().GetSelectedVariantsAndVariantSets(VariantsToDuplicate, VariantSetsToDuplicate);

	// Keep track of variant duplication so that we can transfer thumbnails later
	// We could use the clipboard arrays for this, but this does not make
	// any assumptions about how the clipboard stores its stuff
	TArray<UVariant*> OriginalVariants;
	TArray<UVariant*> NewVariants;

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("DuplicateSelectionVariantTreeTransaction", "Duplicate {0} {0}|plural(one=variant,other=variants) and {1} variant {1}|plural(one=set,other=sets)"),
		VariantsToDuplicate.Num(),
		VariantSetsToDuplicate.Num()
		));

	// Duplicate variants
	for (UVariant* Variant : VariantsToDuplicate)
	{
		UVariantSet* ParentVariantSet = Variant->GetParent();
		if (VariantSetsToDuplicate.Contains(ParentVariantSet))
		{
			continue;
		}

		UVariant* NewVariant = DuplicateObject(Variant, nullptr);

		OriginalVariants.Add(Variant);
		NewVariants.Add(NewVariant);

		// Add individually because we might have different parents
		TArray<UVariant*> VariantsToAdd({NewVariant});
		VariantManager->AddVariants(VariantsToAdd, ParentVariantSet);
	}

	// Duplicate variant sets
	TArray<UVariantSet*> VarSetsToAdd;
	for (UVariantSet* VariantSet : VariantSetsToDuplicate)
	{
		UVariantSet* NewVariantSet = DuplicateObject(VariantSet, nullptr);

		OriginalVariants.Append(VariantSet->GetVariants());
		NewVariants.Append(NewVariantSet->GetVariants());

		VarSetsToAdd.Add(NewVariantSet);
	}
	VariantManager->AddVariantSets(VarSetsToAdd, LevelVarSets);

	VariantManager->CopyVariantThumbnails(NewVariants, OriginalVariants);

	RefreshVariantTree();
}

void SVariantManager::RenameSelectionVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>> Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	for (const TSharedRef<FVariantManagerDisplayNode>& SomeNode : Nodes)
	{
		SomeNode->StartRenaming();
	}
}

bool SVariantManager::CanCutVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>> Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	return Nodes.Num() > 0;
}

bool SVariantManager::CanCopyVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>> Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	return Nodes.Num() > 0;
}

bool SVariantManager::CanPasteVariantTree()
{
	return (FVariantManagerClipboard::GetVariants().Num() + FVariantManagerClipboard::GetVariantSets().Num()) > 0;
}

bool SVariantManager::CanDeleteVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>> Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	return Nodes.Num() > 0;
}

bool SVariantManager::CanDuplicateVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>> Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	return Nodes.Num() > 0;
}

bool SVariantManager::CanRenameVariantTree()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>> Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	for (const TSharedRef<FVariantManagerDisplayNode>& SomeNode : Nodes)
	{
		if (!SomeNode->IsReadOnly())
		{
			return true;
		}
	}

	return false;
}

void SVariantManager::CutSelectionActorList()
{

}

void SVariantManager::CopySelectionActorList()
{

}

void SVariantManager::PasteSelectionActorList()
{

}

void SVariantManager::DeleteSelectionActorList()
{

}

void SVariantManager::DuplicateSelectionActorList()
{

}

void SVariantManager::RenameSelectionActorList()
{

}

bool SVariantManager::CanCutActorList()
{
	return true;
}

bool SVariantManager::CanCopyActorList()
{
	return true;
}

bool SVariantManager::CanPasteActorList()
{
	return true;
}

bool SVariantManager::CanDeleteActorList()
{
	return true;
}

bool SVariantManager::CanDuplicateActorList()
{
	return true;
}

bool SVariantManager::CanRenameActorList()
{
	return true;
}

void SVariantManager::SwitchOnSelectedVariant()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	if (!VariantManager.IsValid())
	{
		return;
	}

	TArray<TSharedRef<FVariantManagerDisplayNode>> SelectedNodes = VariantManager->GetSelection().GetSelectedOutlinerNodes().Array();
	NodeTreeView->SortAsDisplayed(SelectedNodes);

	for (const TSharedRef<FVariantManagerDisplayNode>& Node : SelectedNodes)
	{
		if (Node->GetType() == EVariantManagerNodeType::Variant)
		{
			TSharedPtr<FVariantManagerVariantNode> VarNode = StaticCastSharedRef<FVariantManagerVariantNode>(Node);

			UVariant* Variant = &VarNode->GetVariant();

			SwitchOnVariant(Variant);
		}
	}
}

void SVariantManager::CreateThumbnail()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	if (!VariantManager.IsValid())
	{
		return;
	}

	const TSet<TSharedRef<FVariantManagerDisplayNode>>& Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	FViewport* Viewport = GEditor->GetActiveViewport();

	if ( ensure(GCurrentLevelEditingViewportClient) && ensure(Viewport) )
	{
		TArray<FAssetData> SelectedAssets;
		for (const TSharedRef<FVariantManagerDisplayNode>& Node : Nodes)
		{
			if (Node->GetType() == EVariantManagerNodeType::Variant)
			{
				TSharedPtr<FVariantManagerVariantNode> NodeAsVariant = StaticCastSharedRef<FVariantManagerVariantNode>(Node);
				if (NodeAsVariant.IsValid())
				{
					UVariant* Variant = &NodeAsVariant->GetVariant();

					SelectedAssets.Emplace(Variant);
				}
			}
		}

		//have to re-render the requested viewport
		FLevelEditorViewportClient* OldViewportClient = GCurrentLevelEditingViewportClient;
		//remove selection box around client during render
		GCurrentLevelEditingViewportClient = nullptr;
		Viewport->Draw();

		IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
		ContentBrowser.CaptureThumbnailFromViewport(Viewport, SelectedAssets);

		//redraw viewport to have the yellow highlight again
		GCurrentLevelEditingViewportClient = OldViewportClient;
		Viewport->Draw();
	}

	RefreshVariantTree();
}

void SVariantManager::ClearThumbnail()
{
	TSharedPtr<FVariantManager> VarMan = VariantManagerPtr.Pin();
	if (!VarMan.IsValid())
	{
		return;
	}

	TArray<UVariant*> SelectedVariants;
	TArray<UVariantSet*> SelectedVariantSets;
	VarMan->GetSelection().GetSelectedVariantsAndVariantSets(SelectedVariants, SelectedVariantSets);

	for (UVariant* Variant : SelectedVariants)
	{
		UPackage* VariantPackage = Variant->GetOutermost();

		ThumbnailTools::CacheEmptyThumbnail(Variant->GetFullName(), VariantPackage);

		VariantPackage->MarkPackageDirty();
		Variant->PostEditChange();
	}

	RefreshVariantTree();
}

bool SVariantManager::CanSwitchOnVariant()
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	const TSet<TSharedRef<FVariantManagerDisplayNode>>& Nodes = VariantManager->GetSelection().GetSelectedOutlinerNodes();

	int32 NumVariants = 0;

	for (const TSharedRef<FVariantManagerDisplayNode>& SomeNode : Nodes)
	{
		if (SomeNode->GetType() == EVariantManagerNodeType::Variant)
		{
			TSharedPtr<FVariantManagerVariantNode> SomeNodeAsVariant = StaticCastSharedRef<FVariantManagerVariantNode>(SomeNode);

			if (SomeNodeAsVariant.IsValid())
			{
				NumVariants += 1;
			}
		}
	}

	return NumVariants >= 1;
}

bool SVariantManager::CanCreateThumbnail()
{
	return true;
}

bool SVariantManager::CanClearThumbnail()
{
	return true;
}

void SVariantManager::CaptureNewPropertiesFromSelectedActors()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (PinnedVariantManager.IsValid())
	{
		FVariantManagerSelection& Selection = PinnedVariantManager->GetSelection();
		const TSet<TSharedRef<FVariantManagerActorNode>>& SelectedActorNodes = Selection.GetSelectedActorNodes();

		TArray<UVariantObjectBinding*> SelectedBindings;
		for (const TSharedRef<FVariantManagerActorNode>& ActorNode : SelectedActorNodes)
		{
			UVariantObjectBinding* Binding = ActorNode->GetObjectBinding().Get();
			if (Binding)
			{
				SelectedBindings.Add(Binding);
			}
		}

		int32 NumBindings = SelectedBindings.Num();

		FScopedTransaction Transaction(FText::Format(
			LOCTEXT("ActorNodeCaptureNewPropertiesTransaction", "Capture new properties for {0} actor {0}|plural(one=binding,other=bindings)"),
			NumBindings
			));

		PinnedVariantManager->CaptureNewProperties(SelectedBindings);
		PinnedVariantManager->GetVariantManagerWidget()->RefreshPropertyList();
	}
}

bool SVariantManager::CanCaptureNewPropertiesFromSelectedActors()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (PinnedVariantManager.IsValid())
	{
		FVariantManagerSelection& Selection = PinnedVariantManager->GetSelection();
		const TSet<TSharedRef<FVariantManagerActorNode>>& SelectedActorNodes = Selection.GetSelectedActorNodes();

		return SelectedActorNodes.Num() > 0;
	}

	return false;
}

void SVariantManager::AddFunctionCaller()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}

	FVariantManagerSelection& Selection = PinnedVariantManager->GetSelection();
	const TSet<TSharedRef<FVariantManagerActorNode>>& SelectedActorNodes = Selection.GetSelectedActorNodes();
	if (SelectedActorNodes.Num() < 1)
	{
		return;
	}

	int32 NumNewCallers = SelectedActorNodes.Num();
	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("AddFunctionCaller", "Created {0} new function {0}|plural(one=caller,other=callers)"),
		NumNewCallers
	));

	for (const TSharedPtr<FVariantManagerActorNode>& Node : SelectedActorNodes)
	{
		UVariantObjectBinding* Binding = Node->GetObjectBinding().Get();
		if (Binding)
		{
			PinnedVariantManager->CreateFunctionCaller({Binding});
		}
	}

	RefreshPropertyList();
}

bool SVariantManager::CanAddFunctionCaller()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return false;
	}

	FVariantManagerSelection& Selection = PinnedVariantManager->GetSelection();
	const TSet<TSharedRef<FVariantManagerActorNode>>& SelectedActorNodes = Selection.GetSelectedActorNodes();

	return SelectedActorNodes.Num() > 0;
}

void SVariantManager::RemoveActorBindings()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (PinnedVariantManager.IsValid())
	{
		FVariantManagerSelection& Selection = PinnedVariantManager->GetSelection();
		const TSet<TSharedRef<FVariantManagerActorNode>>& SelectedActorNodes = Selection.GetSelectedActorNodes();

		TArray<UVariantObjectBinding*> SelectedBindings;
		for (const TSharedRef<FVariantManagerActorNode>& ActorNode : SelectedActorNodes)
		{
			UVariantObjectBinding* Binding = ActorNode->GetObjectBinding().Get();
			if (Binding)
			{
				SelectedBindings.Add(Binding);
			}
		}

		int32 NumBindings = SelectedBindings.Num();

		FScopedTransaction Transaction(FText::Format(
			LOCTEXT("ActorNodeRemoveTransaction", "Remove {0} actor {0}|plural(one=binding,other=bindings)"),
			NumBindings
		));

		PinnedVariantManager->RemoveObjectBindingsFromParent(SelectedBindings);
		PinnedVariantManager->GetVariantManagerWidget()->RefreshActorList();
	}
}

bool SVariantManager::CanRemoveActorBindings()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (PinnedVariantManager.IsValid())
	{
		FVariantManagerSelection& Selection = PinnedVariantManager->GetSelection();
		const TSet<TSharedRef<FVariantManagerActorNode>>& SelectedActorNodes = Selection.GetSelectedActorNodes();

		return SelectedActorNodes.Num() > 0;
	}

	return false;
}

void SVariantManager::ApplyProperty()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);
	if (NumNodes != 1)
	{
		return;
	}

	const TArray<TWeakObjectPtr<UPropertyValue>>& PropValues = SelectedNodes[0]->GetPropertyValues();
	if (PropValues.Num() < 1 || !PropValues[0].IsValid())
	{
		return;
	}

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ApplyPropertyTransaction", "Apply recorded data for property '{0}'"),
		FText::FromString(PropValues[0].Get()->GetLeafDisplayString())));

	for (const TWeakObjectPtr<UPropertyValue>& WeakPropValue : PropValues)
	{
		if (!WeakPropValue.IsValid())
		{
			continue;
		}
		PinnedVariantManager->ApplyProperty(WeakPropValue.Get());
	}

	// Trick to force the viewport gizmos to also update, even though our selection
	// will remain the same
	GEditor->NoteSelectionChange();
}

void SVariantManager::RecordProperty()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);
	if (NumNodes != 1)
	{
		return;
	}

	const TArray<TWeakObjectPtr<UPropertyValue>>& PropValues = SelectedNodes[0]->GetPropertyValues();
	if (PropValues.Num() < 1 || !PropValues[0].IsValid())
	{
		return;
	}

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("ApplyPropertyTransaction", "Apply recorded data for property '{0}'"),
		FText::FromString(PropValues[0].Get()->GetLeafDisplayString())));

	for (const TWeakObjectPtr<UPropertyValue>& WeakPropValue : PropValues)
	{
		if (!WeakPropValue.IsValid())
		{
			continue;
		}
		PinnedVariantManager->RecordProperty(WeakPropValue.Get());
	}

	RefreshPropertyList();
}

void SVariantManager::RemoveCapture()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);
	if (NumNodes != 1)
	{
		return;
	}

	TArray<UPropertyValue*> PropValuesToRemove;

	const TArray<TWeakObjectPtr<UPropertyValue>>& PropValues = SelectedNodes[0]->GetPropertyValues();
	for (const TWeakObjectPtr<UPropertyValue>& WeakPropValue : PropValues)
	{
		if (!WeakPropValue.IsValid())
		{
			continue;
		}
		PropValuesToRemove.Add(WeakPropValue.Get());
	}

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("RemoveCaptureTransaction", "Remove {0} property {0}|plural(one=capture,other=captures)"),
		PropValuesToRemove.Num()));

	PinnedVariantManager->RemovePropertyCapturesFromParent(PropValuesToRemove);

	RefreshPropertyList();
}

void SVariantManager::CallDirectorFunction()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);
	if (NumNodes != 1)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("TriggerEventTransaction", "Trigger a captured event"));

	for (const TSharedPtr<FVariantManagerPropertyNode>& Node : SelectedNodes)
	{
		if (Node->GetType() == EVariantManagerNodeType::Function)
		{
			TSharedPtr<FVariantManagerFunctionPropertyNode> FunctionNode = StaticCastSharedPtr<FVariantManagerFunctionPropertyNode>(Node);
			if (FunctionNode.IsValid())
			{
				FName FunctionName = FunctionNode->GetFunctionCaller().FunctionName;
				UVariantObjectBinding* FunctionTarget = FunctionNode->GetObjectBinding().Get();
				PinnedVariantManager->CallDirectorFunction(FunctionName, FunctionTarget);
			}
		}
	}
}

void SVariantManager::RemoveDirectorFunctionCaller()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}

	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);

	TMap<UVariantObjectBinding*, TArray<FFunctionCaller*>> FunctionCallers;
	int32 NumCallersWellRemove = 0;

	for (const TSharedPtr<FVariantManagerPropertyNode>& Node : SelectedNodes)
	{
		if (Node->GetType() == EVariantManagerNodeType::Function)
		{
			TSharedPtr<FVariantManagerFunctionPropertyNode> FunctionNode = StaticCastSharedPtr<FVariantManagerFunctionPropertyNode>(Node);
			if (FunctionNode.IsValid())
			{
				TArray<FFunctionCaller*>& Callers = FunctionCallers.FindOrAdd(FunctionNode->GetObjectBinding().Get());
				Callers.Add(&FunctionNode->GetFunctionCaller());

				NumCallersWellRemove++;
			}
		}
	}

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("RemoveCallersTransaction", "Remove {0} function {0}|plural(one=caller,other=callers)"),
		NumCallersWellRemove));

	for (auto& Pair : FunctionCallers)
	{
		UVariantObjectBinding* Binding = Pair.Key;
		const TArray<FFunctionCaller*>& Callers = Pair.Value;

		PinnedVariantManager->RemoveFunctionCallers(Callers, Binding);
	}

	RefreshPropertyList();
}

bool SVariantManager::CanApplyProperty()
{
	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);
	if (NumNodes != 1)
	{
		return false;
	}

	const TArray<TWeakObjectPtr<UPropertyValue>>& PropValues = SelectedNodes[0]->GetPropertyValues();
	if (PropValues.Num() < 1 || !PropValues[0].IsValid())
	{
		return false;
	}

	return true;
}

bool SVariantManager::CanRecordProperty()
{
	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);
	if (NumNodes != 1)
	{
		return false;
	}

	const TArray<TWeakObjectPtr<UPropertyValue>>& PropValues = SelectedNodes[0]->GetPropertyValues();
	if (PropValues.Num() < 1 || !PropValues[0].IsValid())
	{
		return false;
	}

	return true;
}

bool SVariantManager::CanRemoveCapture()
{
	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);
	if (NumNodes != 1)
	{
		return false;
	}

	const TArray<TWeakObjectPtr<UPropertyValue>>& PropValues = SelectedNodes[0]->GetPropertyValues();
	if (PropValues.Num() < 1 || !PropValues[0].IsValid())
	{
		return false;
	}

	return true;
}

bool SVariantManager::CanCallDirectorFunction()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return false;
	}

	TArray<TSharedPtr<FVariantManagerPropertyNode>> SelectedNodes;
	int32 NumNodes = CapturedPropertyListView->GetSelectedItems(SelectedNodes);
	if (NumNodes != 1)
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("TriggerEventTransaction", "Trigger a captured event"));

	for (const TSharedPtr<FVariantManagerPropertyNode>& Node : SelectedNodes)
	{
		if (Node->GetType() == EVariantManagerNodeType::Function)
		{
			TSharedPtr<FVariantManagerFunctionPropertyNode> FunctionNode = StaticCastSharedPtr<FVariantManagerFunctionPropertyNode>(Node);
			if (FunctionNode.IsValid())
			{
				FFunctionCaller& Caller = FunctionNode->GetFunctionCaller();
				return Caller.IsValidFunction(Caller.GetFunctionEntry());
			}
		}
	}

	return false;
}

bool SVariantManager::CanRemoveDirectorFunctionCaller()
{
	return true;
}

void SVariantManager::SwitchOnVariant(UVariant* Variant)
{
	if (Variant == nullptr)
	{
		return;
	}

	FScopedTransaction Transaction(FText::Format(
		LOCTEXT("SwitchOnVariantTransaction", "Switch on variant '{0}'"),
		Variant->GetDisplayText()));

	bool bSomeFailedToResolve = false;
	for (const UVariantObjectBinding* Binding : Variant->GetBindings())
	{
		if (!Binding->GetObject())
		{
			bSomeFailedToResolve = true;
			break;
		}
	}
	if (bSomeFailedToResolve)
	{
		FNotificationInfo Error(FText::Format(LOCTEXT("UnresolvedActorsOnSwitchOnNotification", "Switched-on Variant '{0}' contains unresolved actor bindings!"),
			Variant->GetDisplayText()));
		Error.ExpireDuration = 5.0f;
		Error.bFireAndForget = true;
		Error.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Error"));
		FSlateNotificationManager::Get().AddNotification(Error);
	}

	Variant->SwitchOn();

	// Trick to force the viewport gizmos to also update, even though our selection
	// may remain the same
	GEditor->NoteSelectionChange();
}

void SVariantManager::SortDisplayNodes(TArray<TSharedRef<FVariantManagerDisplayNode>>& DisplayNodes)
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}

	struct NodeAndDisplayIndex
	{
		TSharedRef<FVariantManagerDisplayNode> Node;
		int32 DisplayIndex;
	};

	TArray<NodeAndDisplayIndex> SortedNodes;
	SortedNodes.Reserve(DisplayNodes.Num());

	for (TSharedRef<FVariantManagerDisplayNode>& DisplayNode : DisplayNodes)
	{
		int32 Index = INDEX_NONE;

		switch (DisplayNode->GetType())
		{
		case EVariantManagerNodeType::Actor:
			Index = DisplayedActors.Find(DisplayNode);
			break;
		case EVariantManagerNodeType::Variant:
		case EVariantManagerNodeType::VariantSet:
			Index = NodeTreeView->GetDisplayIndexOfNode(DisplayNode);
			break;
		default:
			break;
		}

		SortedNodes.Add({DisplayNode, Index});
	}

	SortedNodes.Sort([](const NodeAndDisplayIndex& A, const NodeAndDisplayIndex& B)
	{
		return A.DisplayIndex < B.DisplayIndex;
	});

	DisplayNodes.Empty(DisplayNodes.Num());
	for (const NodeAndDisplayIndex& SortedNode : SortedNodes)
	{
		DisplayNodes.Add(SortedNode.Node);
	}
}

// Utility that scans the passed in display nodes and returns all the contained variants and variant sets
void GetVariantsAndVariantSetsFromNodes(const TArray<TSharedRef<FVariantManagerDisplayNode>>& InNodes, TSet<UVariant*>& OutVariants, TSet<UVariantSet*>& OutVariantSets)
{
	for (const TSharedRef<FVariantManagerDisplayNode>& DisplayNode : InNodes)
	{
		if (DisplayNode->GetType() == EVariantManagerNodeType::Variant)
		{
			TSharedPtr<FVariantManagerVariantNode> DisplayNodeAsVarNode = StaticCastSharedRef<FVariantManagerVariantNode>(DisplayNode);
			if (DisplayNodeAsVarNode.IsValid())
			{
				OutVariants.Add(&DisplayNodeAsVarNode->GetVariant());
				continue;
			}
		}
		else if (DisplayNode->GetType() == EVariantManagerNodeType::VariantSet)
		{
			TSharedPtr<FVariantManagerVariantSetNode> DisplayNodeAsVarSetNode = StaticCastSharedRef<FVariantManagerVariantSetNode>(DisplayNode);
			if (DisplayNodeAsVarSetNode.IsValid())
			{
				OutVariantSets.Add(&DisplayNodeAsVarSetNode->GetVariantSet());
				continue;
			}
		}
	}
}

void SVariantManager::OnActorNodeSelectionChanged()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (PinnedVariantManager.IsValid())
	{
		const TSet<TSharedRef<FVariantManagerActorNode>>& SelectedActorNodes = PinnedVariantManager->GetSelection().GetSelectedActorNodes();

		GEditor->SelectNone(true, true);

		for (const TSharedRef<FVariantManagerActorNode>& ActorNode : SelectedActorNodes)
		{
			TWeakObjectPtr<UVariantObjectBinding> Binding = ActorNode->GetObjectBinding();
			if (Binding.IsValid())
			{
				if (AActor* SelectedActor = Cast<AActor>(Binding->GetObject()))
				{
					GEditor->SelectActor(SelectedActor, true, true);
				}
			}
		}
	}

	RefreshPropertyList();
}

void SVariantManager::RefreshVariantTree()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}
	FVariantManagerSelection& Selection = PinnedVariantManager->GetSelection();
	TSet<FString>& SelectedNodePaths = Selection.GetSelectedNodePaths();

	// Store previous selection
	for (const TSharedRef<FVariantManagerDisplayNode>& DisplayNode : Selection.GetSelectedOutlinerNodes())
	{
		if (DisplayNode->GetType() == EVariantManagerNodeType::VariantSet)
		{
			TSharedPtr<FVariantManagerVariantSetNode> DisplayNodeAsVarSetNode = StaticCastSharedRef<FVariantManagerVariantSetNode>(DisplayNode);
			if (DisplayNodeAsVarSetNode.IsValid())
			{
				SelectedNodePaths.Add(DisplayNodeAsVarSetNode->GetVariantSet().GetPathName());
			}
		}
		else if (DisplayNode->GetType() == EVariantManagerNodeType::Variant)
		{
			TSharedPtr<FVariantManagerVariantNode> DisplayNodeAsVarNode = StaticCastSharedRef<FVariantManagerVariantNode>(DisplayNode);
			if (DisplayNodeAsVarNode.IsValid())
			{
				SelectedNodePaths.Add(DisplayNodeAsVarNode->GetVariant().GetPathName());
			}
		}
	}

	// Store selected UVariant and UVariantSets so that we can re-select them after the rebuild if we can
	TSet<UVariant*> OldSelectedVariants;
	TSet<UVariantSet*> OldSelectedVariantSets;
	GetVariantsAndVariantSetsFromNodes(Selection.GetSelectedOutlinerNodes().Array(), OldSelectedVariants, OldSelectedVariantSets);

	Selection.SuspendBroadcast();
	Selection.EmptySelectedOutlinerNodes();

	PinnedVariantManager->GetNodeTree()->Update();

	// Restore the selection state.
	for (const TSharedRef<FVariantManagerDisplayNode>& DisplayNode : PinnedVariantManager->GetNodeTree()->GetRootNodes())
	{
		if (DisplayNode->GetType() == EVariantManagerNodeType::VariantSet)
		{
			TSharedPtr<FVariantManagerVariantSetNode> DisplayNodeAsVarSetNode = StaticCastSharedRef<FVariantManagerVariantSetNode>(DisplayNode);
			if (DisplayNodeAsVarSetNode.IsValid())
			{
				if (SelectedNodePaths.Contains(DisplayNodeAsVarSetNode->GetVariantSet().GetPathName()))
				{
					Selection.AddToSelection(DisplayNode);
				}

				for (const TSharedRef<FVariantManagerDisplayNode>& ChildDisplayNode : DisplayNodeAsVarSetNode->GetChildNodes())
				{
					if (ChildDisplayNode->GetType() == EVariantManagerNodeType::Variant)
					{
						TSharedPtr<FVariantManagerVariantNode> ChildDisplayNodeAsVarNode = StaticCastSharedRef<FVariantManagerVariantNode>(ChildDisplayNode);
						if (ChildDisplayNodeAsVarNode.IsValid())
						{
							if (SelectedNodePaths.Contains(ChildDisplayNodeAsVarNode->GetVariant().GetPathName()))
							{
								Selection.AddToSelection(ChildDisplayNode);
							}
						}
					}
				}
			}
		}
	}

	// Do this now or else we might have dangling paths that will be randomly selected when we replace a node
	SelectedNodePaths.Empty();

	NodeTreeView->UpdateTreeViewFromSelection();
	NodeTreeView->Refresh();
	Selection.ResumeBroadcast();
}

void SVariantManager::RefreshActorList()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (!PinnedVariantManager.IsValid())
	{
		return;
	}
	FVariantManagerSelection& Selection = PinnedVariantManager->GetSelection();
	TSet<FString>& SelectedNodePaths = Selection.GetSelectedNodePaths();

	// Store previous actor selection
	for (const TSharedRef<FVariantManagerActorNode>& SelectedActorNode : Selection.GetSelectedActorNodes())
	{
		SelectedNodePaths.Add(SelectedActorNode->GetObjectBinding()->GetPathName());
	}

	// Rebuild list of FVariantManagerActorNode
	{
		CachedDisplayedActorPaths.Empty();

		// Get all unique variants in order (in case we selected a variant and its variant set)
		TArray<UVariant*> SelectedVariants;
		for (const TSharedRef<FVariantManagerDisplayNode>& Node : Selection.GetSelectedOutlinerNodes())
		{
			if (Node->GetType() == EVariantManagerNodeType::VariantSet)
			{
				TSharedRef<FVariantManagerVariantSetNode> NodeAsVarSet = StaticCastSharedRef<FVariantManagerVariantSetNode>(Node);
				const TArray<UVariant*>& Variants = NodeAsVarSet->GetVariantSet().GetVariants();

				for (UVariant* Variant : Variants)
				{
					SelectedVariants.AddUnique(Variant);
				}
			}
			else if (Node->GetType() == EVariantManagerNodeType::Variant)
			{
				SelectedVariants.AddUnique(&StaticCastSharedRef<FVariantManagerVariantNode>(Node)->GetVariant());
			}
		}

		// Get all bindings to use, in order (but allowing repeats because we might have selected two variants
		// with bindings to the same actor)
		TArray<UVariantObjectBinding*> TargetBindings;
		for (UVariant* Variant : SelectedVariants)
		{
			const TArray<UVariantObjectBinding*>& Bindings = Variant->GetBindings();
			if (Bindings.Num() > 0)
			{
				TargetBindings.Append(Bindings);
				TargetBindings.Add(nullptr); // nullptrs will be converted to spacers
			}
		}

		DisplayedActors.Empty();
		for (UVariantObjectBinding* Binding : TargetBindings)
		{
			if (Binding == nullptr)
			{
				DisplayedActors.Add(MakeShareable(new FVariantManagerDisplayNode(nullptr, nullptr)));
			}
			else
			{
				DisplayedActors.Add(MakeShareable(new FVariantManagerActorNode(Binding, nullptr, VariantManagerPtr)));
				CachedDisplayedActorPaths.Add(Binding->GetObjectPath());
			}
		}
	}

	// Restore actor selection
	Selection.SuspendBroadcast();
	Selection.EmptySelectedActorNodes();
	CachedSelectedActorPaths.Empty();

	for (const TSharedRef<FVariantManagerDisplayNode>& DisplayedNode : DisplayedActors)
	{
		if (DisplayedNode->GetType() == EVariantManagerNodeType::Actor)
		{
			const TSharedRef<FVariantManagerActorNode>& DisplayedActor = StaticCastSharedRef<FVariantManagerActorNode>(DisplayedNode);

			TWeakObjectPtr<UVariantObjectBinding> Binding = DisplayedActor->GetObjectBinding();
			if (Binding.IsValid() && SelectedNodePaths.Contains(Binding->GetPathName()))
			{
				Selection.AddActorNodeToSelection(DisplayedActor);
				CachedSelectedActorPaths.Add(Binding->GetObjectPath());
			}
		}
	}

	SelectedNodePaths.Empty();

	// Select the FVariantManagerSelection items in the SListView
	ActorListView->UpdateListViewFromSelection();
	ActorListView->RebuildList();
	Selection.ResumeBroadcast();

	// We might be still selecting a binding to the same actor, but we need to update
	// the captured properties, because we might select a different variant now, so the captured
	// properties could be different
	RefreshPropertyList();
}

void SVariantManager::RefreshPropertyList()
{
	FVariantManagerSelection& Selection = VariantManagerPtr.Pin()->GetSelection();

	TArray<UPropertyValue*> NewCapturedProps;
	TArray<UVariantObjectBinding*> SelectedBindings;

	for (const TSharedRef<FVariantManagerActorNode>& Node : Selection.GetSelectedActorNodes())
	{
		// Ignore unresolved actor bindings
		UVariantObjectBinding* Binding = Node->GetObjectBinding().Get();
		if (Binding == nullptr || Binding->GetObject() == nullptr)
		{
			continue;
		}

		NewCapturedProps.Append(Binding->GetCapturedProperties());
		SelectedBindings.Add(Binding);
	}

	// Group properties by PathHash
	TMap<uint32, TArray<UPropertyValue*>> PropsByHash;
	for (UPropertyValue* NewCapturedProp : NewCapturedProps)
	{
		uint32 Hash = NewCapturedProp->GetPropertyPathHash();
		TArray<UPropertyValue*>& Props = PropsByHash.FindOrAdd(Hash);

		Props.Add(NewCapturedProp);
	}

	DisplayedPropertyNodes.Empty();
	for (auto HashToPropArray : PropsByHash)
	{
		TArray<UPropertyValue*>& Props = HashToPropArray.Value;
		if (Props.Num() < 1)
		{
			continue;
		}

		UPropertyValue* FirstProp = Props[0];

		// Attempts to resolve first so that we can fetch the objects below
		FirstProp->Resolve();

		UStruct* Struct = FirstProp->GetStructPropertyStruct();
		UEnum* Enum = FirstProp->GetEnumPropertyEnum();

		if (Struct)
		{
			DisplayedPropertyNodes.Add(MakeShareable(new FVariantManagerStructPropertyNode(Props, VariantManagerPtr)));
		}
		else if (Enum)
		{
			DisplayedPropertyNodes.Add(MakeShareable(new FVariantManagerEnumPropertyNode(Props, VariantManagerPtr)));
		}
		else if (FirstProp->GetPropertyClass()->IsChildOf(FStrProperty::StaticClass()) ||
				 FirstProp->GetPropertyClass()->IsChildOf(FNameProperty::StaticClass()) ||
				 FirstProp->GetPropertyClass()->IsChildOf(FTextProperty::StaticClass()))
		{
			DisplayedPropertyNodes.Add(MakeShareable(new FVariantManagerStringPropertyNode(Props, VariantManagerPtr)));
		}
		else if (FirstProp->GetPropCategory() == EPropertyValueCategory::Option)
		{
			DisplayedPropertyNodes.Add(MakeShareable(new FVariantManagerOptionPropertyNode(Props, VariantManagerPtr)));
		}
		else
		{
			DisplayedPropertyNodes.Add(MakeShareable(new FVariantManagerPropertyNode(Props, VariantManagerPtr)));
		}
	}

	//TODO @Speed
	DisplayedPropertyNodes.Sort([](const TSharedPtr<FVariantManagerPropertyNode>& A, const TSharedPtr<FVariantManagerPropertyNode>& B)
	{
		return A->GetDisplayName().ToString() < B->GetDisplayName().ToString();
	});

	// Add a node for each function caller
	for (UVariantObjectBinding* Binding : SelectedBindings)
	{
		for (FFunctionCaller& Caller : Binding->GetFunctionCallers())
		{
			DisplayedPropertyNodes.Add(MakeShareable(new FVariantManagerFunctionPropertyNode(Binding, Caller, VariantManagerPtr)));
		}
	}

	CapturedPropertyListView->RequestListRefresh();
}

void SVariantManager::UpdatePropertyDefaults()
{
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (PinnedVariantManager.IsValid())
	{
		if (ULevelVariantSets* LVS = PinnedVariantManager->GetCurrentLevelVariantSets())
		{
			for (UVariantSet* VarSet : LVS->GetVariantSets())
			{
				for (UVariant* Var : VarSet->GetVariants())
				{
					for (UVariantObjectBinding* Binding : Var->GetBindings())
					{
						for (UPropertyValue* Prop : Binding->GetCapturedProperties())
						{
							Prop->ClearDefaultValue();
						}
					}
				}
			}
		}
	}
}

void SVariantManager::OnBlueprintCompiled()
{
	RefreshPropertyList();

	// We might have changed the default value for a blueprint component or actor
	UpdatePropertyDefaults();
}

void SVariantManager::OnMapChanged(UWorld* World, EMapChangeType MapChangeType)
{
	CachedAllActorPaths.Empty();
	RefreshActorList();
}

void SVariantManager::OnOutlinerSearchChanged(const FText& Filter)
{
	TSharedPtr<FVariantManager> VariantManager = VariantManagerPtr.Pin();
	if (VariantManager.IsValid())
	{
		const FString& FilterString = Filter.ToString();

		VariantManager->GetNodeTree()->FilterNodes(FilterString);
		NodeTreeView->Refresh();
	}
}

void SVariantManager::OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
}

void SVariantManager::OnDragLeave( const FDragDropEvent& DragDropEvent )
{
}

FReply SVariantManager::OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	//bool bIsDragSupported = false;

	//TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	//if (Operation.IsValid() && (
	//	Operation->IsOfType<FAssetDragDropOp>() ||
	//	Operation->IsOfType<FClassDragDropOp>() ||
	//	Operation->IsOfType<FActorDragDropGraphEdOp>() ) )
	//{
	//	bIsDragSupported = true;
	//}

	//return bIsDragSupported ? FReply::Handled() : FReply::Unhandled();
	return FReply::Unhandled();
}

FReply SVariantManager::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	//bool bWasDropHandled = false;

	//TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();

	//if (Operation.IsValid() )
	//{
	//	if ( Operation->IsOfType<FAssetDragDropOp>() )
	//	{
	//		const auto& DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>( Operation );

	//		//OnAssetsDropped( *DragDropOp );
	//		bWasDropHandled = true;
	//	}
	//	else if( Operation->IsOfType<FClassDragDropOp>() )
	//	{
	//		const auto& DragDropOp = StaticCastSharedPtr<FClassDragDropOp>( Operation );

	//		//OnClassesDropped( *DragDropOp );
	//		bWasDropHandled = true;
	//	}
	//	else if( Operation->IsOfType<FActorDragDropGraphEdOp>() )
	//	{
	//		const auto& DragDropOp = StaticCastSharedPtr<FActorDragDropGraphEdOp>( Operation );

	//		//OnActorsDropped( *DragDropOp );
	//		bWasDropHandled = true;
	//	}
	//}

	//return bWasDropHandled ? FReply::Handled() : FReply::Unhandled();
	return FReply::Unhandled();
}

FReply SVariantManager::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	// A toolkit tab is active, so direct all command processing to it
	if(VariantTreeCommandBindings->ProcessCommandBindings( InKeyEvent ))
	{
		return FReply::Handled();
	}

	const FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Enter)
	{
		SwitchOnSelectedVariant();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SVariantManager::OnFocusChanging( const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent )
{
	// if (NewWidgetPath.ContainsWidget(AsShared()))
	// {
	// 	OnReceivedFocus.ExecuteIfBound();
	// }
}

FReply SVariantManager::OnAddVariantSetClicked()
{
	FScopedTransaction Transaction(LOCTEXT("AddVariantSetTransaction", "Create a new variant set"));

	CreateNewVariantSet();
	return FReply::Handled();
}

// Tries capturing and recording new data for the property at PropertyPath for TargetActor, into whatever Variants we have selected.
// Will return true if it created or updated a UPropertyValue
bool AutoCaptureProperty(FVariantManager* VarMan, AActor* TargetActor, const FString& PropertyPath, const FProperty* Property)
{
	// Transient actors are generated temporarily while dragging actors into the level. Once the
	// mouse is released, another non-transient actor is instantiated
	if (!VarMan || !TargetActor || TargetActor->HasAnyFlags(RF_Transient) || PropertyPath.IsEmpty())
	{
		return false;
	}

	// Get selected variants
	TArray<UVariant*> SelectedVariants;
	TArray<UVariantSet*> SelectedVariantSets;
	VarMan->GetSelection().GetSelectedVariantsAndVariantSets(SelectedVariants, SelectedVariantSets);
	if (SelectedVariants.Num() < 1)
	{
		return false;
	}

	// Create/get bindings
	TArray<AActor*> TargetActorArr{TargetActor};
	TArray<UVariantObjectBinding*> Bindings = VarMan->CreateObjectBindings(TargetActorArr, SelectedVariants);
	if (Bindings.Num() < 1)
	{
		return false;
	}

	// Create property captures
	TArray<TSharedPtr<FCapturableProperty>> OutProps;
	VarMan->GetCapturableProperties(TargetActorArr, OutProps, PropertyPath);
	TArray<UPropertyValue*> CreatedProps = VarMan->CreatePropertyCaptures(OutProps, {Bindings}, true);

	// UPropertyValue always contains the Inner for array properties, but the event that
	// calls this function only provides the outer
	FProperty* FilterProperty = const_cast<FProperty*>(Property);
	if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FilterProperty = ArrayProp->Inner;
	}

	// Update property captures
	for (UVariantObjectBinding* Binding : Bindings)
	{
		for (UPropertyValue* PropertyValue : Binding->GetCapturedProperties())
		{
			if (FilterProperty && PropertyValue->ContainsProperty(FilterProperty))
			{
				PropertyValue->RecordDataFromResolvedObject();
			}
		}
	}

	return true;
}

namespace SVariantManagerUtils
{
	// Returns the paths of all the actors bound to variants of this LVS
	TSet<FString> GetAllActorPaths(const ULevelVariantSets* LVS)
	{
		if (LVS == nullptr)
		{
			return {};
		}

		TSet<FString> Result;

		for (const UVariantSet* VarSet : LVS->GetVariantSets())
		{
			if (VarSet == nullptr)
			{
				continue;
			}

			for (const UVariant* Var : VarSet->GetVariants())
			{
				if (Var == nullptr)
				{
					continue;
				}

				for (const UVariantObjectBinding* Binding : Var->GetBindings())
				{
					if (Binding == nullptr)
					{
						continue;
					}

					// Need to do this instead of just asking the binding for its
					// path because we need the paths fixed up for PIE, if that is the case
					if (UObject* Actor = Binding->GetObject())
					{
						Result.Add(Actor->GetPathName());
					}
				}
			}
		}

		return Result;
	}

	enum class EObjectType : uint8
	{
		None = 0,
		PropertyValue = 1,
		VariantObjectBinding = 2,
		Variant = 4,
		VariantSet = 8,
		LevelVariantSets = 16,
	};
	ENUM_CLASS_FLAGS(EObjectType);

	EObjectType GetObjectType(UObject* Object)
	{
		if (Object == nullptr)
		{
			return EObjectType::None;
		}

		UClass* ObjectClass = Object->GetClass();
		if (ObjectClass->IsChildOf(UPropertyValue::StaticClass()))
		{
			return EObjectType::PropertyValue;
		}
		else if (ObjectClass->IsChildOf(UVariantObjectBinding::StaticClass()))
		{
			return EObjectType::VariantObjectBinding;
		}
		else if (ObjectClass->IsChildOf(UVariant::StaticClass()))
		{
			return EObjectType::Variant;
		}
		else if (ObjectClass->IsChildOf(UVariantSet::StaticClass()))
		{
			return EObjectType::VariantSet;
		}
		else if (ObjectClass->IsChildOf(ULevelVariantSets::StaticClass()))
		{
			return EObjectType::LevelVariantSets;
		}

		return EObjectType::None;
	}
}

void SVariantManager::OnObjectTransacted(UObject* Object, const class FTransactionObjectEvent& Event)
{
	// We fully redraw the variant manager when undoing/redoing, so we can just worry about finalized here
	if (Event.GetEventType() != ETransactionObjectEventType::Finalized)
	{
		return;
	}

	if (Object == nullptr)
	{
		return;
	}

	SVariantManagerUtils::EObjectType ObjectType = SVariantManagerUtils::GetObjectType(Object);

	// Variants may have changed 'active' state
	if(EnumHasAnyFlags(ObjectType, SVariantManagerUtils::EObjectType::PropertyValue |
								   SVariantManagerUtils::EObjectType::VariantObjectBinding |
								   SVariantManagerUtils::EObjectType::Variant))
	{
		RefreshVariantTree();
	}

	// Set of all bound actors may have changed
	if (EnumHasAnyFlags(ObjectType, SVariantManagerUtils::EObjectType::VariantObjectBinding |
									SVariantManagerUtils::EObjectType::Variant |
									SVariantManagerUtils::EObjectType::VariantSet |
									SVariantManagerUtils::EObjectType::LevelVariantSets))
	{
		CachedAllActorPaths.Empty();
	}

	AActor* TargetActor = Cast<AActor>(Object);
	if (TargetActor == nullptr)
	{
		if (UActorComponent* ObjectAsActorComponent = Cast<UActorComponent>(Object))
		{
			TargetActor = ObjectAsActorComponent->GetOwner();
		}
	}

	// Actor or an actor's component has transacted
	if (TargetActor != nullptr)
	{
		FString ActorPath = TargetActor->GetPathName();

		// When we switch a SwitchActor, only the child actors will transact, so we
		// have to manually check if this transaction was a switch actor switch
		FString ParentActorPath;
		if (ASwitchActor* SwitchActorParent = Cast<ASwitchActor>(TargetActor->GetAttachParentActor()))
		{
			if (Object->IsA(USceneComponent::StaticClass()) && Event.GetChangedProperties().Contains(USceneComponent::GetVisiblePropertyName()))
			{
				ParentActorPath = SwitchActorParent->GetPathName();
				bool bSwitchWasCapturedAlready = CachedDisplayedActorPaths.Contains(ParentActorPath);

				// Annoyingly we have to handle switch actor auto-capture in here, as it doesn't have
				// any 'property' to trigger OnObjectPropertyChanged
				if (bAutoCaptureProperties)
				{
					TSharedPtr<FVariantManager> PinnedVarMan = VariantManagerPtr.Pin();
					bool bDidSomething = AutoCaptureProperty(PinnedVarMan.Get(), SwitchActorParent, TEXT("Selected Option"), nullptr);

					if (bDidSomething && !bSwitchWasCapturedAlready)
					{
						RefreshActorList();
					}
				}
			}
		}

		// Recorded values may be out of date, so we would need to show the "Record" button (aka dirty
		// property indicator)
		if (CachedSelectedActorPaths.Contains(ActorPath) || CachedSelectedActorPaths.Contains(ParentActorPath))
		{
			RefreshPropertyList();
		}

		// Make sure this cache is built
		if (CachedAllActorPaths.Num() == 0)
		{
			TSharedPtr<FVariantManager> VarMan = VariantManagerPtr.Pin();
			if (VarMan.IsValid())
			{
				TSet<FString> DiscoveredActorPaths = SVariantManagerUtils::GetAllActorPaths(VarMan->GetCurrentLevelVariantSets());
				CachedAllActorPaths = MoveTemp(DiscoveredActorPaths);
			}
		}

		// If the actor transacted, properties may not be current and so variants may not be active anymore
		if (CachedAllActorPaths.Contains(ActorPath) || CachedAllActorPaths.Contains(ParentActorPath))
		{
			RefreshVariantTree();
		}
	}
}

void SVariantManager::OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event)
{
	if (!bAutoCaptureProperties || !Object || !Event.Property)
	{
		return;
	}

	AActor* TargetActor = nullptr;
	FString PropertyPath;

	bool bIsStructProperty = Event.MemberProperty && Event.MemberProperty->IsA(FStructProperty::StaticClass());
	bool bIsBuiltIn = bIsStructProperty && FVariantManagerUtils::IsBuiltInStructProperty(Event.MemberProperty);

	// We don't want to capture just the X component of a RelativeLocation property, but we want to capture
	// the ISO property of a FPostProcessSettings StructProperty
	FProperty* Prop = bIsBuiltIn? Event.MemberProperty : Event.Property;

	// Fetch TargetActor
	USceneComponent* ObjAsSceneComp = Cast<USceneComponent>(Object);
	if (ObjAsSceneComp)
	{
		TargetActor = Cast<AActor>(ObjAsSceneComp->GetOwner());
	}
	else if (UActorComponent* ObjAsActorComp = Cast<UActorComponent>(Object))
	{
		TargetActor = ObjAsActorComp->GetTypedOuter<AActor>();
		PropertyPath = ObjAsActorComp->GetName() + PATH_DELIMITER;
	}
	else
	{
		TargetActor = Cast<AActor>(Object);
	}

	if (!TargetActor)
	{
		return;
	}

	// We need to check if its a blueprint actor or not, as we handle blueprint root component
	// names a little bit differently
	bool bIsBlueprintGeneratedClass = ((UObject*)TargetActor->GetClass())->IsA(UBlueprintGeneratedClass::StaticClass());

	// Build the property path with the components, if we're nested in one
	while (ObjAsSceneComp)
	{
		USceneComponent* AttachParent = ObjAsSceneComp->GetAttachParent();
		FString ComponentName;

		// We're some form of root component
		if (AttachParent == nullptr || AttachParent->GetOwner() != TargetActor)
		{
			if (bIsBlueprintGeneratedClass)
			{
				// Users can rename of the root component for a blueprint generated class, so lets
				// use that
				ComponentName = ObjAsSceneComp->GetName();
			}
			else
			{
				// Users can't rename root components, and their actual names are always
				// something like StaticMeshComponent0 or LightComponent0 (even if its class is a
				// UPointLightComponent). Getting the class display name matches how the Variant Manager
				// behaves
				ComponentName = ObjAsSceneComp->GetClass()->GetDisplayNameText().ToString();
			}
			ObjAsSceneComp = nullptr;
		}
		else
		{
			ComponentName = ObjAsSceneComp->GetName();
			ObjAsSceneComp = AttachParent;
		}

		PropertyPath = ComponentName + PATH_DELIMITER + PropertyPath;
	}

	// If we're a non-built in struct property, build the path with the categories
	// like the propertycapturer would have done (this is mostly to manage Post Process Volume properties)
	if (bIsStructProperty && !bIsBuiltIn)
	{
		// Add 'Settings /'
		PropertyPath += Event.MemberProperty->GetDisplayNameText().ToString() + PATH_DELIMITER;

		FString Category = Prop->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty())
		{
			Category = Category.Replace(TEXT("|"), PATH_DELIMITER);
			// Add 'Lens / Camera /'
			PropertyPath += Category + PATH_DELIMITER;
		}
	}

	FString PropertyName = Prop->GetDisplayNameText().ToString();
	TArray<FString> PropertyPaths;
	static const TSet<FString> ProxyPropertyPaths{ TEXT("Relative Location"), TEXT("Relative Rotation"), TEXT("Relative Scale 3D") };

	// We capture as just 'Materials' in the Variant Manager UI, instead of 'Override Materials'
	// Override Materials doesn't work like a regular FArrayProperty, we need to use GetNumMaterials
	if (Prop == FVariantManagerUtils::GetOverrideMaterialsProperty())
	{
		if (UStaticMeshComponent* ObjAsComp = Cast<UStaticMeshComponent>(Object))
		{
			for (int32 Index = 0; Index < ObjAsComp->GetNumMaterials(); ++Index)
			{
				// 'Static Mesh Component / Material' + '[0]'
				PropertyPaths.Add(PropertyPath + FString::Printf(TEXT("Material[%d]"), Index));
			}
		}
	}
	// Generate one path for each array position. Because the event doesn't tell us which array
	// element that fired it, we must capture all positions of the array
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Object));
		for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
		{
			// 'Static Mesh Component / ' + 'Tags[0]'
			PropertyPaths.Add(PropertyPath + FString::Printf(TEXT("%s[%d]"), *PropertyName, Index));
		}
	}
	// Some properties are reported as from the actor, but really they are just proxies for the root component
	// The Variant Manager doesn't capture these, only showing the root component versions, so we need to tweak the path
	else if (PropertyPath.IsEmpty() && ProxyPropertyPaths.Contains(PropertyName))
	{
		FString RootComponentName = bIsBlueprintGeneratedClass ? TargetActor->GetRootComponent()->GetName() :
									TargetActor->GetRootComponent()->GetClass()->GetDisplayNameText().ToString();

		// 'Static Mesh Component' + ' / ' + 'Relative Location'
		PropertyPaths.Add(RootComponentName + PATH_DELIMITER + PropertyName);
	}
	else
	{
		PropertyPaths.Add(PropertyPath + PropertyName);
	}

	bool bUpdatedSomething = false;
	TSharedPtr<FVariantManager> PinnedVarMan = VariantManagerPtr.Pin();
	for (const FString& SomePropertyPath : PropertyPaths)
	{
		bUpdatedSomething |= AutoCaptureProperty(PinnedVarMan.Get(), TargetActor, SomePropertyPath, Prop);
	}

	if (bUpdatedSomething)
	{
		RefreshActorList();
	}
}

void SVariantManager::OnPieEvent(bool bIsSimulating)
{
	// We must forcebly clear these, because during PIE the actors/components remain
	// alive in the editor world, meaning UPropertyValues::HasValidResolve() will return true.
	// Ideally they would subscribe to that event themselves, but that would require
	// VariantManagerContent depend on the Editor module
	TSharedPtr<FVariantManager> PinnedVariantManager = VariantManagerPtr.Pin();
	if (PinnedVariantManager.IsValid())
	{
		if (ULevelVariantSets* LVS = PinnedVariantManager->GetCurrentLevelVariantSets())
		{
			for (UVariantSet* VarSet : LVS->GetVariantSets())
			{
				for (UVariant* Var : VarSet->GetVariants())
				{
					for (UVariantObjectBinding* Binding : Var->GetBindings())
					{
						for (UPropertyValue* Prop : Binding->GetCapturedProperties())
						{
							Prop->ClearLastResolve();
						}
					}
				}
			}
		}
	}

	CachedAllActorPaths.Empty();
	RefreshActorList();
}

#undef LOCTEXT_NAMESPACE

