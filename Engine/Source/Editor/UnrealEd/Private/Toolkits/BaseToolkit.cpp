// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/BaseToolkit.h"
#include "EdModeInteractiveToolsContext.h"
#include "Toolkits/ToolkitManager.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"
#include "IDetailsView.h"
#include "InteractiveToolManager.h"
#include "Editor.h"
#include "InteractiveTool.h"
#include "Tools/UEdMode.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EdMode.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "BaseToolkit"

FBaseToolkit::FBaseToolkit()
	: ToolkitMode( EToolkitMode::Standalone ),
	  ToolkitCommands( new FUICommandList() )
{
	WorkspaceMenuCategory = FWorkspaceItem::NewGroup(LOCTEXT("WorkspaceMenu_BaseToolkit", "Toolkit"));
}


FBaseToolkit::~FBaseToolkit()
{

	EditorModeManager.Reset();
}



bool FBaseToolkit::IsWorldCentricAssetEditor() const
{
	return ToolkitMode == EToolkitMode::WorldCentric;
}



bool FBaseToolkit::IsHosted() const
{
	return ToolkitHost.IsValid();
}


const TSharedRef< IToolkitHost > FBaseToolkit::GetToolkitHost() const
{
	return ToolkitHost.Pin().ToSharedRef();
}

FName FBaseToolkit::GetToolkitContextFName() const
{
	return GetToolkitFName();
}


bool FBaseToolkit::ProcessCommandBindings( const FKeyEvent& InKeyEvent ) const
{
	if( ToolkitCommands->ProcessCommandBindings( InKeyEvent ) )
	{
		return true;
	}
	
	return false;
}

FString FBaseToolkit::GetTabPrefix() const
{
	if( IsWorldCentricAssetEditor() )
	{
		return GetWorldCentricTabPrefix();
	}
	else
	{
		return TEXT( "" );
	}
}


FLinearColor FBaseToolkit::GetTabColorScale() const
{
	return IsWorldCentricAssetEditor() ? GetWorldCentricTabColorScale() : FLinearColor( 0, 0, 0, 0 );
}

void FBaseToolkit::CreateEditorModeManager()
{
}

void FBaseToolkit::BringToolkitToFront()
{
	if( ensure( ToolkitHost.IsValid() ) )
	{
		// Bring the host window to front
		ToolkitHost.Pin()->BringToFront();
		// Tell the toolkit its been brought to the fore - give it a chance to update anything it needs to
		ToolkitBroughtToFront();
	}
}

TSharedPtr<class SWidget> FBaseToolkit::GetInlineContent() const
{
	return TSharedPtr<class SWidget>();
}


bool FBaseToolkit::IsBlueprintEditor() const
{
	return false;
}

FEditorModeTools& FBaseToolkit::GetEditorModeManager() const
{
	if (IsWorldCentricAssetEditor() && IsHosted())
	{
		return GetToolkitHost()->GetEditorModeManager();
	}

	check(EditorModeManager.IsValid());
	return *EditorModeManager.Get();
}

#undef LOCTEXT_NAMESPACE


void FModeToolkit::Init(const TSharedPtr< class IToolkitHost >& InitToolkitHost)
{
	Init(InitToolkitHost, TWeakObjectPtr<UEdMode>());
}

void FModeToolkit::Init(const TSharedPtr< class IToolkitHost >& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	check( InitToolkitHost.IsValid() );

	ToolkitMode = EToolkitMode::Type::Standalone;
	ToolkitHost = InitToolkitHost;
	OwningEditorMode = InOwningMode;
	EditorModeManager = InitToolkitHost->GetEditorModeManager().AsShared();

	UInteractiveToolManager* ToolManager = GetEditorModeManager().GetInteractiveToolsContext()->ToolManager;
	ToolManager->OnToolStarted.AddSP(this, &FModeToolkit::OnToolStarted);
	ToolManager->OnToolEnded.AddSP(this, &FModeToolkit::OnToolEnded);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	{
		FDetailsViewArgs ModeDetailsViewArgs;
		ModeDetailsViewArgs.bAllowSearch = false;
		ModeDetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		ModeDetailsViewArgs.bHideSelectionTip = true;
		ModeDetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
		ModeDetailsViewArgs.bShowOptions = false;
		ModeDetailsViewArgs.bAllowMultipleTopLevelObjects = true;

		ModeDetailsView = PropertyEditorModule.CreateDetailView(ModeDetailsViewArgs);
	}

	{
		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = true;

		DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	}

	FToolkitManager::Get().RegisterNewToolkit(SharedThis(this));
}


FModeToolkit::~FModeToolkit()
{
	UInteractiveToolManager* ToolManager = GetEditorModeManager().GetInteractiveToolsContext()->ToolManager;
	ToolManager->OnToolStarted.RemoveAll(this);
	ToolManager->OnToolEnded.RemoveAll(this);

	OwningEditorMode.Reset();
}

FName FModeToolkit::GetToolkitFName() const
{
	return FName("EditorModeToolkit");
}

FText FModeToolkit::GetBaseToolkitName() const
{
	return NSLOCTEXT("EditorModeToolkit", "DisplayName", "EditorMode Tool");
}

FString FModeToolkit::GetWorldCentricTabPrefix() const
{
	return FString();
}

bool FModeToolkit::IsAssetEditor() const
{
	return false;
}

const TArray< UObject* >* FModeToolkit::GetObjectsCurrentlyBeingEdited() const
{
	return NULL;
}

FLinearColor FModeToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor();
}



bool FModeToolkit::CanStartTool(const FString& ToolTypeIdentifier)
{
	if (!OwningEditorMode.IsValid())
	{
		return false;
	}

	UInteractiveToolManager* Manager = OwningEditorMode->GetToolManager();

	return (Manager->HasActiveTool(EToolSide::Left) == false) &&
		(Manager->CanActivateTool(EToolSide::Left, ToolTypeIdentifier) == true);
}

bool FModeToolkit::CanAcceptActiveTool()
{
	if (!OwningEditorMode.IsValid())
	{
		return false;
	}

	return OwningEditorMode->GetToolManager()->CanAcceptActiveTool(EToolSide::Left);
}

bool FModeToolkit::CanCancelActiveTool()
{
	if (!OwningEditorMode.IsValid())
	{
		return false;
	}

	return OwningEditorMode->GetToolManager()->CanCancelActiveTool(EToolSide::Left);
}

bool FModeToolkit::CanCompleteActiveTool()
{
	if (!OwningEditorMode.IsValid())
	{
		return false;
	}

	return OwningEditorMode->GetToolManager()->HasActiveTool(EToolSide::Left) && CanCancelActiveTool() == false;
}


void FModeToolkit::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	// Update properties panel
	if (!OwningEditorMode.IsValid())
	{
		return;
	}

	UInteractiveTool* CurTool = OwningEditorMode->GetToolManager()->GetActiveTool(EToolSide::Left);
	if (CurTool)
	{
		DetailsView->SetObjects(CurTool->GetToolProperties());
	}
}

void FModeToolkit::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	DetailsView->SetObject(nullptr);
}

class FEdMode* FModeToolkit::GetEditorMode() const
{
	return nullptr; 
}

FText FModeToolkit::GetEditorModeDisplayName() const
{
	if (FEdMode* EdMode = GetEditorMode())
	{
		return EdMode->GetModeInfo().Name;
	}
	else if (OwningEditorMode.IsValid())
	{
		return OwningEditorMode->GetModeInfo().Name;
	}

	return FText::GetEmpty();
}

FSlateIcon FModeToolkit::GetEditorModeIcon() const
{
	if (FEdMode* EdMode = GetEditorMode())
	{
		return EdMode->GetModeInfo().IconBrush;
	}
	else if (OwningEditorMode.IsValid())
	{
		return OwningEditorMode->GetModeInfo().IconBrush;
	}

	return FSlateIcon();
}

TWeakObjectPtr<UEdMode> FModeToolkit::GetScriptableEditorMode() const
{
	return OwningEditorMode;
}

TSharedPtr<SWidget> FModeToolkit::GetInlineContent() const
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ModeDetailsView.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		[
			DetailsView.ToSharedRef()
		];
}

void FModeToolkit::BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder)
{
	if (!OwningEditorMode.IsValid())
	{
		return;
	}

	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> CommandLists = OwningEditorMode->GetModeCommands();
	TArray<TSharedPtr<FUICommandInfo>>* CurrentCommandListPtr = CommandLists.Find(PaletteName);
	if (CurrentCommandListPtr)
	{
		TArray<TSharedPtr<FUICommandInfo>> CurrentCommandList = *CurrentCommandListPtr;
		for (TSharedPtr<FUICommandInfo> Command : CurrentCommandList)
		{
			ToolbarBuilder.AddToolBarButton(Command);
		}

	}
}

FName FModeToolkit::GetCurrentPalette() const
{
	return CurrentPaletteName;
}

void FModeToolkit::SetCurrentPalette(FName InPalette)
{
	CurrentPaletteName = InPalette;
	OnToolPaletteChanged(CurrentPaletteName);
	OnPaletteChangedDelegate.Broadcast(InPalette);
}

void FModeToolkit::SetModeSettingsObject(UObject* InSettingsObject)
{
	ModeDetailsView->SetObject(InSettingsObject);
}
