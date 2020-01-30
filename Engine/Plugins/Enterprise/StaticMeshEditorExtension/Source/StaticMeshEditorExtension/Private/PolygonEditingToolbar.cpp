// Copyright Epic Games, Inc. All Rights Reserved.

#include "PolygonEditingToolbar.h"
#include "PolygonSelectionTool.h"
#include "MeshEditingContext.h"

#include "OverlayComponent.h"
#include "WireframeMeshComponent.h"
#include "MeshEditorSelectionModifiers.h"
#include "MeshEditorStyle.h"

#include "AssignMaterial.h"
#include "DeleteMeshElement.h"
#include "EditableMesh.h"
#include "EditableMeshFactory.h"
#include "EditableMeshTypes.h"
#include "EditorViewportClient.h"
#include "EditorModeManager.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "FlipPolygon.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Interfaces/IPluginManager.h"
#include "IStaticMeshEditor.h"
#include "IViewportInteractionModule.h"
#include "Materials/Material.h"
#include "MeshEditorCommands.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"
#include "StaticMeshEditorModule.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "UnifyNormals.h"
#include "ViewportWorldInteraction.h"
#include "Widgets/SWidget.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "StaticMeshEditorExtensionToolbar"

DEFINE_LOG_CATEGORY_STATIC(LogStaticMeshEditorExtension, Log, All);

namespace FPolygonEditingCommandsUtil
{
	template< typename InMeshEditorCommand >
	const TSharedPtr<FUICommandInfo>& GetUICommandInfo()
	{
		static TSharedPtr<FUICommandInfo> UICommandInfoInvalid;

		TArray<UMeshEditorCommand*> MeshEditorCommandList = MeshEditorCommands::Get();
		for (UMeshEditorCommand* MeshEditorCommand : MeshEditorCommandList)
		{
			if (MeshEditorCommand->IsA(InMeshEditorCommand::StaticClass()))
			{
				return MeshEditorCommand->GetUICommandInfo();
			}
		}

		return UICommandInfoInvalid;
	}

	template< typename InMeshEditorCommand >
	FUIAction MakeUIAction(IMeshEditorModeUIContract* Contract)
	{
		TArray<UMeshEditorCommand*> MeshEditorCommandList = MeshEditorCommands::Get();
		for (UMeshEditorCommand* MeshEditorCommand : MeshEditorCommandList)
		{
			if (MeshEditorCommand->IsA(InMeshEditorCommand::StaticClass()))
			{
				return MeshEditorCommand->MakeUIAction(*Contract);
			}
		}

		return FUIAction();
	}
}

//
// Helper class to manage changes made on the editable mesh associated with the polygon editing toolbar
// The goal is to have FChange::HasExpired to return false when the editable mesh is not reachable anymore.
class FMeshEditingChange : public FSwapChange
{

public:

	/** Constructor */
	explicit FMeshEditingChange(TSharedPtr<FPolygonEditingToolbar> InToolbar, TUniquePtr<FChange>&& InBaseChange)
		: Toolbar(InToolbar)
		, BaseChange(MoveTemp(InBaseChange))
	{
		ensure(Toolbar.IsValid());
	}

	// Parent class overrides
	virtual EChangeStyle GetChangeType() override
	{
		return Toolbar.IsValid() && BaseChange.IsValid() ? BaseChange->GetChangeType() : EChangeStyle::InPlaceSwap;
	}

	virtual TUniquePtr<FChange> Execute(UObject* Object) override
	{
		if(Toolbar.IsValid() && BaseChange.IsValid())
		{
			TUniquePtr<FMeshEditingChange> ExecutedChange( new FMeshEditingChange(Toolbar.Pin(), BaseChange->Execute(Object)) );
			return ExecutedChange;
		}

		return nullptr;
	}

	virtual void Apply( UObject* Object ) override
	{
		if(Toolbar.IsValid() && BaseChange.IsValid())
		{
			BaseChange->Apply(Object);
		}
	}

	virtual void Revert( UObject* Object ) override
	{
		if(Toolbar.IsValid() && BaseChange.IsValid())
		{
			BaseChange->Revert(Object);
		}
	}

	/**
	 * @remark Will return false when the StaticMesh editor is closed and the associated editable mesh is released
	 */
	virtual bool HasExpired( UObject* Object ) const
	{ 
		return !Toolbar.IsValid();
	}

	virtual FString ToString() const override
	{
		return Toolbar.IsValid() && BaseChange.IsValid() ? BaseChange->ToString() : FString();
	}

	virtual void PrintToLog(class FFeedbackContext& FeedbackContext, const int32 IndentLevel = 0) override
	{
		if(Toolbar.IsValid() && BaseChange.IsValid())
		{
			BaseChange->PrintToLog(FeedbackContext, IndentLevel);
		}
	}

private:
	TWeakPtr<FPolygonEditingToolbar> Toolbar;
	TUniquePtr<FChange> BaseChange;

private:
	// Non-copyable
	FMeshEditingChange( const FMeshEditingChange& ) = delete;
	FMeshEditingChange& operator=( const FMeshEditingChange& ) = delete;
};


#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FPolygonEditingToolbarStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )

class FPolygonEditingToolbarStyle
{
public:

	static void Initialize()
	{
		if( StyleSet.IsValid() )
		{
			return;
		}

		StyleSet = MakeShared<FSlateStyleSet>( GetStyleSetName() );

		StyleSet->SetContentRoot( FPaths::EngineContentDir() / TEXT( "Editor/Slate") );
		StyleSet->SetCoreContentRoot( FPaths::EngineContentDir() / TEXT( "Slate" ) );

		const FVector2D Icon20x20( 20.0f, 20.0f );
		const FVector2D Icon40x40( 40.0f, 40.0f );

		// Icons for the mode panel tabs
		StyleSet->Set( "MeshEditorPolygonMode.EditMode", new IMAGE_PLUGIN_BRUSH( "Icons/EditMode", Icon40x40 ) );
		StyleSet->Set( "MeshEditorPolygonMode.EditMode.Small", new IMAGE_PLUGIN_BRUSH( "Icons/EditMode", Icon20x20) );
		StyleSet->Set( "MeshEditorPolygonMode.EditMode.Selected", new IMAGE_PLUGIN_BRUSH( "Icons/EditMode", Icon40x40 ) );
		StyleSet->Set( "MeshEditorPolygonMode.EditMode.Selected.Small", new IMAGE_PLUGIN_BRUSH( "Icons/EditMode", Icon20x20) );

		StyleSet->Set("MeshEditorPolygonMode.IncludeBackfaces", new IMAGE_PLUGIN_BRUSH("Icons/IncludeBackfaces", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.IncludeBackfaces.Small", new IMAGE_PLUGIN_BRUSH("Icons/IncludeBackfaces", Icon20x20));
		StyleSet->Set("MeshEditorPolygonMode.IncludeBackfaces.Selected", new IMAGE_PLUGIN_BRUSH("Icons/IncludeBackfaces", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.IncludeBackfaces.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/IncludeBackfaces", Icon20x20));

		StyleSet->Set("MeshEditorPolygonMode.ExpandSelection", new IMAGE_PLUGIN_BRUSH("Icons/ExpandSelection", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.ExpandSelection.Small", new IMAGE_PLUGIN_BRUSH("Icons/ExpandSelection", Icon20x20));
		StyleSet->Set("MeshEditorPolygonMode.ExpandSelection.Selected", new IMAGE_PLUGIN_BRUSH("Icons/ExpandSelection", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.ExpandSelection.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/ExpandSelection", Icon20x20));

		StyleSet->Set("MeshEditorPolygonMode.ShrinkSelection", new IMAGE_PLUGIN_BRUSH("Icons/ShrinkSelection", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.ShrinkSelection.Small", new IMAGE_PLUGIN_BRUSH("Icons/ShrinkSelection", Icon20x20));
		StyleSet->Set("MeshEditorPolygonMode.ShrinkSelection.Selected", new IMAGE_PLUGIN_BRUSH("Icons/ShrinkSelection", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.ShrinkSelection.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/ShrinkSelection", Icon20x20));

		StyleSet->Set("MeshEditorPolygonMode.Defeaturing", new IMAGE_PLUGIN_BRUSH("Icons/Defeaturing", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.Defeaturing.Small", new IMAGE_PLUGIN_BRUSH("Icons/Defeaturing", Icon20x20));
		StyleSet->Set("MeshEditorPolygonMode.Defeaturing.Selected", new IMAGE_PLUGIN_BRUSH("Icons/Defeaturing", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.Defeaturing.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/Defeaturing", Icon20x20));

		StyleSet->Set("MeshEditorPolygonMode.Jacketing", new IMAGE_PLUGIN_BRUSH("Icons/Jacketing", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.Jacketing.Small", new IMAGE_PLUGIN_BRUSH("Icons/Jacketing", Icon20x20));
		StyleSet->Set("MeshEditorPolygonMode.Jacketing.Selected", new IMAGE_PLUGIN_BRUSH("Icons/Jacketing", Icon40x40));
		StyleSet->Set("MeshEditorPolygonMode.Jacketing.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/Jacketing", Icon20x20));

		FSlateStyleRegistry::RegisterSlateStyle( *StyleSet.Get() );
	}

	static void Shutdown()
	{
		if( StyleSet.IsValid() )
		{
			FSlateStyleRegistry::UnRegisterSlateStyle( *StyleSet.Get() );
			ensure( StyleSet.IsUnique() );
			StyleSet.Reset();
		}
	}

	static TSharedPtr<ISlateStyle> Get() { return StyleSet; }

	static FName GetStyleSetName()
	{
		static FName StyleName( "PolygonEditingToolbarStyle" );
		return StyleName;
	}

	static FString InContent( const FString& RelativePath, const ANSICHAR* Extension )
	{
		static FString ContentDir = IPluginManager::Get().FindPlugin( TEXT( "StaticMeshEditorExtension" ) )->GetContentDir();
		return ( ContentDir / RelativePath ) + Extension;
	}


private:

	static TSharedPtr<FSlateStyleSet> StyleSet;
};

#undef IMAGE_PLUGIN_BRUSH

TSharedPtr<FSlateStyleSet> FPolygonEditingToolbarStyle::StyleSet = nullptr;

FPolygonEditingCommands::FPolygonEditingCommands()
	: TCommands<FPolygonEditingCommands>(
		"MeshEditorPolygonMode",
		LOCTEXT( "FPolygonEditingCommands", "Static Mesh Editor Polygon Edit Commands" ),
		"StaticMeshEditor", // @todo: Put MainFrame here!!!!
		FPolygonEditingToolbarStyle::GetStyleSetName()	)
{
}

void FPolygonEditingCommands::RegisterCommands()
{
	UI_COMMAND(EditMode, "Edit Mode", "Toggle edit mode on/off.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::E, false, true, false, false ));
	UI_COMMAND(IncludeBackfaces, "Backfaces", "Include backfaces in selection.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::B, true, true, false, false));
	UI_COMMAND(ExpandSelection, "+", "Expand the selection of polygons to add neighboring polygons.", EUserInterfaceActionType::Button, FInputChord(EKeys::Add, false, true, false, false));
	UI_COMMAND(ShrinkSelection, "-", "Shrink the selection of polygons to remove boundary polygons.", EUserInterfaceActionType::Button, FInputChord(EKeys::Subtract, false, true, false, false));
	UI_COMMAND(Defeaturing, "Defeaturing", "Defeaturing", EUserInterfaceActionType::Button, FInputChord(EKeys::D, false, true, false, false ));
}

FPolygonEditingToolbar::FPolygonEditingToolbar()
	: bIsEditing(false)
	, bIncludeBackfaces(false)
	, StaticMeshEditor(nullptr)
	, StaticMesh(nullptr)
	, PolygonSelectionTool(nullptr)
	, bDeleteCommandOverriden(false)
	, bTransactionsRecorded(false)
{
	PolygonToolbarProxyObject = TStrongObjectPtr<UPolygonToolbarProxyObject>(NewObject<UPolygonToolbarProxyObject>());
	PolygonToolbarProxyObject->Owner = this;
}

FPolygonEditingToolbar::~FPolygonEditingToolbar()
{
	PolygonToolbarProxyObject->Owner = nullptr;

	if (StaticMesh != nullptr)
	{
		// Stop any on-going editing
		if (bIsEditing)
		{
			// Set invalid context on selection tool
			GetPolygonSelectionToolPtr()->SetContext(TSharedPtr<FMeshEditingUIContext>());

			// Deactivate editing context
			EditingContext->Deactivate();
		}

		// Unregister to changes made to the StaticMesh
		StaticMesh->GetOnMeshChanged().RemoveAll(this);

		// Remove editable meshes related to static mesh from cache
		FEditableMeshCache::Get().RemoveObject(StaticMesh);

		if(bTransactionsRecorded)
		{
			UE_LOG(LogStaticMeshEditorExtension, Warning, TEXT("Mesh editing operations made on static mesh %s have been nullified. Undoing those mesh editing operations won't have any effect."), *StaticMesh->GetName());
		}
	}

	bIsEditing = false;

	// Delete editing context
	EditingContext.Reset();

	PolygonSelectionTool.Reset();

	StaticMeshEditor = nullptr;

	StaticMesh = nullptr;
}

void FPolygonEditingToolbar::BindCommands(const TSharedPtr<FUICommandList> CommandList)
{
	BoundCommandList = CommandList;

	// Initialize style set associated with MeshEditor plugin
	FMeshEditorStyle::Initialize();

	FPolygonEditingToolbarStyle::Initialize();

	// Register commands local to toolbar
	FPolygonEditingCommands::Register();

	// Register commands available in MeshEditor
	// Note: Order below is important as element specific commands depend on common commands
	FMeshEditorCommonCommands::Register();
	FMeshEditorAnyElementCommands::Register();
	FMeshEditorPolygonCommands::Register();
	FMeshEditorSelectionModifiers::Register();

	const FPolygonEditingCommands& PolygonEditingCommands = FPolygonEditingCommands::Get();

	CommandList->MapAction(
		PolygonEditingCommands.EditMode,
		FExecuteAction::CreateSP(this, &FPolygonEditingToolbar::OnToggleEditMode),
		FCanExecuteAction(),
		FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::IsEditModeChecked)
	);

	CommandList->MapAction(
		PolygonEditingCommands.IncludeBackfaces,
		FExecuteAction::CreateSP(this, &FPolygonEditingToolbar::OnIncludeBackfaces),
		FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::IsEditModeChecked),
		FIsActionChecked::CreateSP(this, &FPolygonEditingToolbar::IsIncludeBackfacesChecked)
	);

	CommandList->MapAction(
		PolygonEditingCommands.ExpandSelection,
		FExecuteAction::CreateSP(this, &FPolygonEditingToolbar::OnExpandSelection),
		FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::HasSelectedElement),
		FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::IsEditModeChecked)
	);

	CommandList->MapAction(
		PolygonEditingCommands.ShrinkSelection,
		FExecuteAction::CreateSP(this, &FPolygonEditingToolbar::OnShrinkSelection),
		FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::HasSelectedElement),
		FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::IsEditModeChecked)
	);

	CommandList->MapAction(
		PolygonEditingCommands.Defeaturing,
		FExecuteAction::CreateSP(this, &FPolygonEditingToolbar::OnDefeaturing),
		FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::IsEditModeUnchecked)
	);

	// Back up the generic delete action for dynamic binding
	const FUIAction* DeleteAction = CommandList->GetActionForCommand(FGenericCommands::Get().Delete);
	if (DeleteAction)
	{
		GenericDeleteAction = *DeleteAction;
	}

	CommandList->MapAction(
		FPolygonEditingCommandsUtil::GetUICommandInfo<UDeleteMeshElementCommand>(),
		FPolygonEditingCommandsUtil::MakeUIAction<UDeleteMeshElementCommand>(this)
	);

	FPolygonEditingCommandsUtil::GetUICommandInfo<UFlipPolygonCommand>()->SetActiveChord(FInputChord(EKeys::F, true, false, false, false ), EMultipleKeyBindingIndex::Primary);
	CommandList->MapAction(
		FPolygonEditingCommandsUtil::GetUICommandInfo<UFlipPolygonCommand>(),
		FPolygonEditingCommandsUtil::MakeUIAction<UFlipPolygonCommand>(this)
	);

	FPolygonEditingCommandsUtil::GetUICommandInfo<UAssignMaterialCommand>()->SetActiveChord(FInputChord(EKeys::M, true, false, false, false ), EMultipleKeyBindingIndex::Primary);
	CommandList->MapAction(
		FPolygonEditingCommandsUtil::GetUICommandInfo<UAssignMaterialCommand>(),
		FPolygonEditingCommandsUtil::MakeUIAction<UAssignMaterialCommand>(this)
	);

	FPolygonEditingCommandsUtil::GetUICommandInfo<UUnifyNormalsCommand>()->SetActiveChord(FInputChord(EKeys::U, true, false, false, false), EMultipleKeyBindingIndex::Primary);
	CommandList->MapAction(
		FPolygonEditingCommandsUtil::GetUICommandInfo<UUnifyNormalsCommand>(),
		FPolygonEditingCommandsUtil::MakeUIAction<UUnifyNormalsCommand>(this)
	);
}

bool FPolygonEditingToolbar::Initialize(UStaticMesh* InStaticMesh, const TSharedRef<FUICommandList> CommandList)
{
	// Take a hold on the StaticMesh Editor hosting this toolbar
	IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(InStaticMesh, false);
	if (!EditorInstance || !EditorInstance->GetEditorName().ToString().Contains(TEXT("StaticMeshEditor")))
	{
		return false;
	}

	StaticMesh = InStaticMesh;

	StaticMeshEditor = static_cast<IStaticMeshEditor*>(EditorInstance);

	StaticMeshEditor->SetSecondaryToolbarDisplayName(LOCTEXT("FPolygonEditingToolbarDisplayName", "Mesh Editing"));

	// Create editing context
	EditingContext = MakeShared< FMeshEditingUIContext >( StaticMeshEditor->GetStaticMeshComponent() );

	BindCommands(CommandList);

	UpdateEditableLODs();

	// Register to changes made to FRawMesh of StaticMesh
	InStaticMesh->GetOnMeshChanged().AddRaw(this, &FPolygonEditingToolbar::OnMeshChanged);

	return true;
}

void FPolygonEditingToolbar::PopulateToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> CommandList)
{
	ToolbarBuilder.BeginSection("PolygonSelection");
	{
		// Add invisible widget used to detect closure of hosting static mesh editor
		ToolbarBuilder.AddWidget(SNew(SToolbarWidget<FPolygonEditingToolbar>).EditingToolbar(SharedThis(this)), NAME_None);
		
		ToolbarBuilder.AddToolBarButton(FPolygonEditingCommands::Get().EditMode);

		const TArray< UMeshEditorSelectionModifier* >& ModifierSet = MeshEditorSelectionModifiers::Get();

		if (ModifierSet.Num() > 0)
		{
			ToolbarBuilder.AddToolBarButton(
				FUIAction(
					FExecuteAction(),
					FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::IsEditModeChecked)
				),
				NAME_None,
				TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateLambda([this]() { return GetSelectionModeCommand()->GetLabel(); })),
				TAttribute< FText >::Create(TAttribute< FText >::FGetter::CreateLambda([this]() { return GetSelectionModeCommand()->GetDescription(); })),
				TAttribute< FSlateIcon >::Create(TAttribute< FSlateIcon >::FGetter::CreateLambda([this]() { return GetSelectionModeCommand()->GetIcon(); }))
			);

			ToolbarBuilder.AddComboButton(
				FUIAction(
					FExecuteAction(),
					FCanExecuteAction::CreateSP(this, &FPolygonEditingToolbar::IsEditModeChecked)
				),
				FOnGetContent::CreateSP(this, &FPolygonEditingToolbar::CreateSelectionMenu, CommandList),
				FText(),
				LOCTEXT( "MeshEditorExtensionSelectionMenuToolTip", "Polygon Selection Mode" ),
				FSlateIcon(),
				true
			);
		}

		ToolbarBuilder.AddToolBarButton(FPolygonEditingCommands::Get().IncludeBackfaces);
	}
	ToolbarBuilder.EndSection();

	ToolbarBuilder.BeginSection("PolygonEditing");
	{
		ToolbarBuilder.AddToolBarButton(FPolygonEditingCommandsUtil::GetUICommandInfo<UDeleteMeshElementCommand>());
		ToolbarBuilder.AddToolBarButton(FPolygonEditingCommandsUtil::GetUICommandInfo<UFlipPolygonCommand>());
		ToolbarBuilder.AddToolBarButton(FPolygonEditingCommandsUtil::GetUICommandInfo<UAssignMaterialCommand>());
		ToolbarBuilder.AddToolBarButton(FPolygonEditingCommandsUtil::GetUICommandInfo<UUnifyNormalsCommand>());
	}
	ToolbarBuilder.EndSection();

	if ( IsMeshProcessingAvailable() )
	{
		ToolbarBuilder.BeginSection("MeshProcessing");
		{
			ToolbarBuilder.AddToolBarButton(FPolygonEditingCommands::Get().Defeaturing);
		}
		ToolbarBuilder.EndSection();
	}
}

void FPolygonEditingToolbar::CreateToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> CommandList, UStaticMesh* InStaticMesh)
{
	TSharedPtr<FPolygonEditingToolbar> PolygonEditingToolbar = MakeShareable(new FPolygonEditingToolbar());

	if (!PolygonEditingToolbar->Initialize(InStaticMesh, CommandList))
	{
		return;
	}

	PolygonEditingToolbar->PopulateToolbar(ToolbarBuilder, CommandList);
}

TSharedRef<SWidget> FPolygonEditingToolbar::CreateSelectionMenu(const TSharedRef<FUICommandList> CommandList)
{
	FMenuBuilder MenuBuilder( true, CommandList );

	FUIAction MenuAction;

	// Loop on all selection modifiers and add them to the selection mode menu
	const TArray< UMeshEditorSelectionModifier* >& ModifierSet = MeshEditorSelectionModifiers::Get();
	for (UMeshEditorSelectionModifier* SelectionModifier : ModifierSet)
	{
		MenuAction.ExecuteAction.BindSP( this, &FPolygonEditingToolbar::SetSelectionMode, SelectionModifier->GetSelectionModifierName());
		MenuAction.GetActionCheckState.BindSP( this, &FPolygonEditingToolbar::GetSelectionModeCheckState, SelectionModifier->GetSelectionModifierName());

		const TSharedPtr<FUICommandInfo>& UICommandInfo = SelectionModifier->GetUICommandInfo();

		MenuBuilder.AddMenuEntry(
			UICommandInfo->GetLabel(),
			UICommandInfo->GetDescription(),
			UICommandInfo->GetIcon(),
			MenuAction,
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}

	return MenuBuilder.MakeWidget();
}

void FPolygonEditingToolbar::OnIncludeBackfaces()
{
	bIncludeBackfaces = !bIncludeBackfaces;

	if (PolygonSelectionTool.IsValid())
	{
		GetPolygonSelectionToolPtr()->SetIncludeBackfaces(bIncludeBackfaces);
	}
}

void FPolygonEditingToolbar::OnToggleEditMode()
{
	if (StaticMeshEditor == nullptr)
	{
		return;
	}

	// Activate edit mode
	if (!bIsEditing)
	{
		bIsEditing = true;

		// If static mesh editor is set on 'LOD Auto' or non-editable LOD, ask user to select LOD 0
		// Note: EditableLODs contains at least 'Auto' and 'LOD0'. No check is done if only those two are available to the user because we know we are working on an editable mesh.
		if (EditableLODs.Num() > 2 && !(EditableLODs.IsValidIndex(StaticMeshEditor->GetCurrentLODLevel()) && EditableLODs[StaticMeshEditor->GetCurrentLODLevel()]))
		{
			if (StaticMeshEditor->GetCurrentLODLevel() == 0)
			{
				FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("FPolygonEditingToolbarNoLODAuto", "Cannot edit mesh when 'LOD Auto' is selected.\nPlease select LOD 0.") );
			}
			else
			{
				FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("FPolygonEditingToolbarBadLOD", "Selected LOD cannot be edited.\nPlease select LOD 0.") );
			}

			bIsEditing = false;

			return;
		}

		FEditorViewportClient& ViewportClient = StaticMeshEditor->GetViewportClient();

		EditingContext->Activate(ViewportClient, StaticMeshEditor->GetCurrentLODIndex());

		FEditorModeTools* ModeTools = ViewportClient.GetModeTools();
		ModeTools->ActivateMode(FPolygonSelectionTool::EM_PolygonSelection);

		PolygonSelectionTool = ModeTools->GetActiveMode(FPolygonSelectionTool::EM_PolygonSelection)->AsShared();
		check(PolygonSelectionTool.IsValid());

		GetPolygonSelectionToolPtr()->SetContext(EditingContext);

		StaticMeshEditor->RegisterOnSelectedLODChanged(FOnSelectedLODChanged::CreateSP(this, &FPolygonEditingToolbar::OnLODModelChanged), false);

		OnObjectReimportedHandle = GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddRaw(this, &FPolygonEditingToolbar::OnObjectReimported);
	}
	else
	{
		ExitEditMode();

		// Regenerate rendering data if static mesh has reduction settings even if none of the percentages has changed
		// The regeneration is required because the rendering data were replaced when activating the edit mode
		// In edit mode, the full mesh (no reduction applied) is edited
		// Consequently, the rendering data must be restored.
		FMeshReductionSettings& ReductionSettings =	StaticMesh->GetSourceModel(StaticMeshEditor->GetCurrentLODIndex()).ReductionSettings;
		if (ReductionSettings.PercentTriangles != 1.0f || ReductionSettings.PercentVertices != 1.0f)
		{
			StaticMesh->Build(true);
		}
	}
}

bool FPolygonEditingToolbar::IsEditModeChecked()
{
	return bIsEditing;
}

bool FPolygonEditingToolbar::IsIncludeBackfacesChecked()
{
	return bIncludeBackfaces;
}

void FPolygonEditingToolbar::OnExpandSelection()
{
	if (EditingContext.IsValid())
	{
		EditingContext->ExpandPolygonSelection();
	}
}

void FPolygonEditingToolbar::OnShrinkSelection()
{
	if (EditingContext.IsValid())
	{
		EditingContext->ShrinkPolygonSelection();
	}
}

bool FPolygonEditingToolbar::HasSelectedElement() const
{
	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid())
	{
		return EditingContext->GetSelectedElements(EEditableMeshElementType::Polygon).Num() > 0;
	}

	return false;
}

void FPolygonEditingToolbar::SetSelectionMode(FName InSelectionMode)
{
	if (PolygonSelectionTool.IsValid())
	{
		GetPolygonSelectionToolPtr()->SetSelectionModeName(InSelectionMode);
	}
}

ECheckBoxState FPolygonEditingToolbar::GetSelectionModeCheckState(FName InSelectionMode)
{
	if (PolygonSelectionTool.IsValid())
	{
		return GetPolygonSelectionToolPtr()->GetSelectionModeName() == InSelectionMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	return ECheckBoxState::Unchecked;
}

const TSharedPtr<FUICommandInfo> FPolygonEditingToolbar::GetSelectionModeCommand()
{
	const TArray< UMeshEditorSelectionModifier* >& ModifierSet = MeshEditorSelectionModifiers::Get();
	FName SelectionMode = ModifierSet[0]->GetSelectionModifierName();

	if (PolygonSelectionTool.IsValid())
	{
		SelectionMode = GetPolygonSelectionToolPtr()->GetSelectionModeName();

		for (UMeshEditorSelectionModifier* SelectionModifier : ModifierSet)
		{
			if (SelectionModifier->GetSelectionModifierName() == SelectionMode)
			{
				return SelectionModifier->GetUICommandInfo();
			}
		}
	}

	return ModifierSet[0]->GetUICommandInfo();
}

void FPolygonEditingToolbar::OnLODModelChanged()
{
	// If static mesh editor is set on 'LOD Auto' with more than one LOD or on a non-editable LOD, exit polygon editing
	if (!EditableLODs.IsValidIndex(StaticMeshEditor->GetCurrentLODLevel()) || !EditableLODs[StaticMeshEditor->GetCurrentLODLevel()])
	{
		if (StaticMeshEditor->GetCurrentLODLevel() == 0 && !EditableLODs[0])
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FPolygonEditingToolbarExitEdit_LODAutoNotEditable", "LOD Auto is not editable on a static mesh with more than one LOD.\nExiting Edit Mode."));
		}
		else
		{
			FMessageDialog::Open( EAppMsgType::Ok, LOCTEXT("FPolygonEditingToolbarExitEdit_NonEditableLOD", "Non editable LOD has been selected.\nExiting Edit Mode.") );
		}
		OnToggleEditMode();
		return;
	}

	EditingContext->SetLODIndex(StaticMeshEditor->GetCurrentLODIndex());
}

void FPolygonEditingToolbar::OnMeshChanged()
{
	// Update cached editable mesh
	if (EditingContext.IsValid())
	{
		EditingContext->OnMeshChanged();
	}

	// Update list of editable LODs
	UpdateEditableLODs();

	if (bIsEditing)
	{
		// Check if current selected LOD is still editable
		// Note that EditableLODIndex is shifted by 1 EditableLODs since index 0 in EditableLODs is the LOD Auto
		int32 EditableLODIndex = StaticMeshEditor->GetCurrentLODIndex() + 1;
		if (!EditableLODs.IsValidIndex(EditableLODIndex) || !EditableLODs[EditableLODIndex])
		{
			if (StaticMeshEditor->GetCurrentLODLevel() == 0 && !EditableLODs[0])
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FPolygonEditingToolbarExitEdit_LODAutoNotEditable", "LOD Auto is not editable on a static mesh with more than one LOD.\nExiting Edit Mode."));
			}
			else
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("FPolygonEditingToolbarExitEdit_LODNoLongerEditable", "Selected LOD is not editable anymore.\nExiting Edit Mode."));
			}
			OnToggleEditMode();
		}
	}
}

void FPolygonEditingToolbar::UpdateEditableLODs()
{
	if (StaticMesh == nullptr)
	{
		return;
	}

	EditableLODs.Empty();

	// Build list of valid LODs
	EditableLODs.AddUninitialized(StaticMesh->GetNumSourceModels() + 1);

	// 'LOD Auto' is not a valid selection except in case of only 1 LOD, then it is equivalent to LOD 0
	EditableLODs[0] = StaticMesh->GetNumSourceModels() <= 1;

	// LOD 0 is assumed to be a valid selection
	EditableLODs[1] = true;

	for (int32 Index = 1; Index < StaticMesh->GetNumSourceModels(); ++Index)
	{
		const FMeshReductionSettings& ReductionSettings = StaticMesh->GetSourceModel(Index).ReductionSettings;

		// LOD is not good for editing if automatically built from a base LOD
		// Note: See logic to compute bUseReduction in FStaticMeshBuilder::Build, StaticMeshBuilder.cpp
		EditableLODs[Index+1] = !(ReductionSettings.PercentTriangles < 1.0f || ReductionSettings.MaxDeviation > 0.0f);
	}
}

void FPolygonEditingToolbar::TrackUndo(UObject* Object, TUniquePtr<FChange> RevertChange)
{
	if (Object != nullptr && RevertChange.IsValid())
	{
		// Verify an FScopedTransaction is wrapping this call
		// The only exception is in Simulate mode, where Undo is not allowed.
		check( GUndo != nullptr || GEditor == nullptr || GEditor->bIsSimulatingInEditor );
		if( GUndo != nullptr )
		{
			if (UEditableMesh* EditableMesh = Cast<UEditableMesh>(Object))
			{
				// Create custom FChange object and add it to current transaction
				TUniquePtr<FMeshEditingChange> Change( new FMeshEditingChange( AsShared(), MoveTemp( RevertChange ) ) );
				GUndo->StoreUndo( Object, MoveTemp( Change ) );
				bTransactionsRecorded = true;
			}
		}
	}
}

const UEditableMesh* FPolygonEditingToolbar::FindEditableMesh(UPrimitiveComponent& Component, const FEditableMeshSubMeshAddress& SubMeshAddress) const
{
	if (EditingContext->GetEditableMesh() != nullptr &&  EditingContext->GetEditableMesh() == FEditableMeshCache::Get().FindEditableMesh(Component, SubMeshAddress))
	{
		return EditingContext->GetEditableMesh();
	}
	
	return nullptr;
}

void FPolygonEditingToolbar::SelectMeshElements(const TArray<FMeshElement>& MeshElementsToSelect)
{
	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid())
	{
		FSelectOrDeselectMeshElementsChangeInput ChangeInput;
		ChangeInput.MeshElementsToSelect = MeshElementsToSelect;
		TrackUndo(PolygonToolbarProxyObject.Get(), FSelectOrDeselectMeshElementsChange(ChangeInput).Execute(PolygonToolbarProxyObject.Get()));
	}
}

void FPolygonEditingToolbar::DeselectAllMeshElements()
{
	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid())
	{
		EditingContext->ClearSelectedElements();
	}
}

void FPolygonEditingToolbar::DeselectMeshElements(const TArray<FMeshElement>& MeshElementsToDeselect)
{
	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid())
	{
		FSelectOrDeselectMeshElementsChangeInput ChangeInput;
		ChangeInput.MeshElementsToDeselect = MeshElementsToDeselect;
		TrackUndo(PolygonToolbarProxyObject.Get(), FSelectOrDeselectMeshElementsChange(ChangeInput).Execute(PolygonToolbarProxyObject.Get()));
	}
}

void FPolygonEditingToolbar::GetSelectedMeshesAndElements(EEditableMeshElementType ElementType, TMap<UEditableMesh*,TArray<FMeshElement>>& OutMeshesAndElements)
{
	OutMeshesAndElements.Reset();

	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid())
	{
		TArray<FMeshElement> SelectedMeshElements = EditingContext->GetSelectedElements(ElementType);
		OutMeshesAndElements.FindOrAdd(EditingContext->GetEditableMesh()) = SelectedMeshElements;
	}
}

bool FPolygonEditingToolbar::IsMeshElementSelected(const FMeshElement MeshElement) const
{
	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid())
	{
		return EditingContext->IsSelected(MeshElement);
	}

	return false;
}

bool FPolygonEditingToolbar::IsMeshElementTypeSelected(EEditableMeshElementType ElementType) const
{
	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid())
	{
		return EditingContext->IsMeshElementTypeSelected(ElementType);
	}

	return false;
}

EEditableMeshElementType FPolygonEditingToolbar::GetSelectedMeshElementType() const
{
	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid() && EditingContext->IsMeshElementTypeSelected(EEditableMeshElementType::Any) )
	{
		ToggleDynamicBindings(true);

		return EEditableMeshElementType::Any;
	}

	ToggleDynamicBindings(false);

	return EEditableMeshElementType::Invalid;
}

const TArray<UEditableMesh*>& FPolygonEditingToolbar::GetSelectedEditableMeshes()
{
	static TArray<UEditableMesh*> EditableMeshes;

	EditableMeshes.Reset();

	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid() && EditingContext->IsMeshElementTypeSelected(EEditableMeshElementType::Any) )
	{
		EditableMeshes.Add(EditingContext->GetEditableMesh());
	}

	return EditableMeshes;
}

const TArray<UEditableMesh*>& FPolygonEditingToolbar::GetSelectedEditableMeshes() const
{
	static TArray<UEditableMesh*> EditableMeshes;

	EditableMeshes.Reset();

	if (bIsEditing && EditingContext.IsValid() && EditingContext->IsValid() && EditingContext->IsMeshElementTypeSelected(EEditableMeshElementType::Any))
	{
		EditableMeshes.Add(EditingContext->GetEditableMesh());
	}

	return EditableMeshes;
}

void FPolygonEditingToolbar::ToggleDynamicBindings(bool bOverrideDeleteCommand) const
{
	if (bDeleteCommandOverriden == bOverrideDeleteCommand)
	{
		return;
	}

	// Toggle the generic delete command to let the toolbar delete command through (they are both mapped to the Delete key)
	if (bOverrideDeleteCommand)
	{
		BoundCommandList->UnmapAction(FGenericCommands::Get().Delete);
		bDeleteCommandOverriden = true;
	}
	else
	{
		BoundCommandList->MapAction(FGenericCommands::Get().Delete, GenericDeleteAction);
		bDeleteCommandOverriden = false;
	}
}

TUniquePtr<FChange> FPolygonEditingToolbar::FSelectOrDeselectMeshElementsChange::Execute(UObject* Object)
{
	UPolygonToolbarProxyObject* PolygonToolbarProxy = CastChecked<UPolygonToolbarProxyObject>(Object);
	if (!PolygonToolbarProxy->Owner)
	{
		// User can undo after closing the StaticMeshEditor, in which case, the owning PolygonEditingToolbar will have been destroyed
		return nullptr;
	}

	FPolygonEditingToolbar& PolygonEditingToolbar = *PolygonToolbarProxy->Owner;

	// Get the current element selection mode
	const EEditableMeshElementType CurrentElementSelectionMode = PolygonEditingToolbar.GetMeshElementSelectionMode();

	// Back up the current selection so we can restore it on undo
	FCompoundChangeInput CompoundRevertInput;

	FSelectOrDeselectMeshElementsChangeInput RevertInput;
	RevertInput.MeshElementsToSelect = Input.MeshElementsToDeselect;
	RevertInput.MeshElementsToDeselect = Input.MeshElementsToSelect;
	CompoundRevertInput.Subchanges.Add(MakeUnique<FSelectOrDeselectMeshElementsChange>(MoveTemp(RevertInput)));

	const double CurrentRealTime = FSlateApplication::Get().GetCurrentTime();

	if (PolygonEditingToolbar.IsEditModeChecked())
	{
		PolygonEditingToolbar.EditingContext->RemoveElementsFromSelection(Input.MeshElementsToDeselect);

		if (Input.MeshElementsToSelect.Num() > 0)
		{
			TArray<FMeshElement> MeshElementsToSelect;

			// Make sure they're all the same type.
			EEditableMeshElementType ElementTypeToSelect = Input.MeshElementsToSelect[0].ElementAddress.ElementType;
			for (FMeshElement& MeshElementToSelect : Input.MeshElementsToSelect)
			{
				check(MeshElementToSelect.ElementAddress.ElementType == ElementTypeToSelect);
			}

			for (FMeshElement& MeshElementToSelect : Input.MeshElementsToSelect)
			{
				if (MeshElementToSelect.IsValidMeshElement())
				{
					if (CurrentElementSelectionMode == EEditableMeshElementType::Any ||
						MeshElementToSelect.ElementAddress.ElementType == CurrentElementSelectionMode)
					{
						const UEditableMesh* EditableMesh = PolygonEditingToolbar.FindEditableMesh(*MeshElementToSelect.Component, MeshElementToSelect.ElementAddress.SubMeshAddress);
						if (EditableMesh != nullptr)
						{
							if (MeshElementToSelect.IsElementIDValid(EditableMesh))
							{
								FMeshElement& MeshElement = MeshElementsToSelect.Add_GetRef(MeshElementToSelect);
								MeshElement.LastSelectTime = CurrentRealTime;
							}
						}
					}
				}
			}

			PolygonEditingToolbar.EditingContext->AddElementsToSelection(MeshElementsToSelect);
		}
	}

	return MakeUnique<FCompoundChange>(MoveTemp(CompoundRevertInput));
}


FString FPolygonEditingToolbar::FSelectOrDeselectMeshElementsChange::ToString() const
{
	return FString::Printf(
		TEXT("Select or Deselect Mesh Elements [MeshElementsToSelect:%s, MeshElementsToDeselect:%s]"),
		*LogHelpers::ArrayToString(Input.MeshElementsToSelect),
		*LogHelpers::ArrayToString(Input.MeshElementsToDeselect));
}

void FPolygonEditingToolbar::OnObjectReimported(UObject* InObject)
{
	if (StaticMesh && Cast<UStaticMesh>(InObject) == StaticMesh)
	{
		ExitEditMode();
	}
}

void FPolygonEditingToolbar::ExitEditMode()
{
	if (!bIsEditing)
	{
		return;
	}

	bIsEditing = false;

	GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.Remove(OnObjectReimportedHandle);

	ToggleDynamicBindings(false);

	StaticMeshEditor->UnRegisterOnSelectedLODChanged(this);

	GetPolygonSelectionToolPtr()->SetContext(TSharedPtr<FMeshEditingUIContext>());
	PolygonSelectionTool.Reset();

	FEditorModeTools* ModeTools = StaticMeshEditor->GetViewportClient().GetModeTools();
	ModeTools->DeactivateMode(FPolygonSelectionTool::EM_PolygonSelection);

	EditingContext->Deactivate();
}

#undef LOCTEXT_NAMESPACE