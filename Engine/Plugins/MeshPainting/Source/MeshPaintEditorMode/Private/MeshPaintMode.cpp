// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshPaintMode.h"
#include "MeshPaintModeCommands.h"
#include "MeshPaintModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Framework/Commands/UICommandList.h"
#include "MeshPaintModeSettings.h"
#include "Framework/Commands/Commands.h"
#include "MultiSelectionTool.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolsContext.h"
#include "Components/MeshComponent.h"
#include "Dialogs/Dialogs.h"
#include "Components/StaticMeshComponent.h"
#include "MeshPaintHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "ScopedTransaction.h"
#include "PackageTools.h"
#include "MeshPaintHelpers.h"
#include "Engine/Selection.h"
#include "MeshPaintHelpers.h"
#include "MeshPaintAdapterFactory.h"
#include "MeshPaintModeHelpers.h"
#include "MeshSelect.h"


#define LOCTEXT_NAMESPACE "MeshPaintMode"

FName UMeshPaintMode::MeshPaintMode_Color = FName(TEXT("Color"));
FName UMeshPaintMode::MeshPaintMode_Weights = FName(TEXT("Weights"));
FName UMeshPaintMode::MeshPaintMode_Texture = FName(TEXT("Texture"));

UMeshVertexPaintingToolProperties* UMeshPaintMode::GetVertexToolProperties()
{
	if (GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")
		&& GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")->GetToolManager()->GetActiveTool(EToolSide::Mouse))
	{
		TArray<UObject*> PropertyArray = GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")->GetToolManager()->GetActiveTool(EToolSide::Mouse)->GetToolProperties();
		for(UObject* Property : PropertyArray)
		{
			if (UMeshVertexPaintingToolProperties* VertexProperties = Cast<UMeshVertexPaintingToolProperties>(Property))
			{
				return VertexProperties;
			}
		}
	}
	return nullptr;
}

UMeshColorPaintingToolProperties* UMeshPaintMode::GetColorToolProperties()
{
	if (GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")
		&& GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")->GetToolManager()->GetActiveTool(EToolSide::Mouse))
	{
		TArray<UObject*> PropertyArray = GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")->GetToolManager()->GetActiveTool(EToolSide::Mouse)->GetToolProperties();
		for (UObject* Property : PropertyArray)
		{
			if (UMeshColorPaintingToolProperties* ColorProperties = Cast<UMeshColorPaintingToolProperties>(Property))
			{
				return ColorProperties;
			}
		}
	}
	return nullptr;
}

UMeshWeightPaintingToolProperties* UMeshPaintMode::GetWeightToolProperties()
{
	if (GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")
		&& GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")->GetToolManager()->GetActiveTool(EToolSide::Mouse))
	{
		TArray<UObject*> PropertyArray = GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode")->GetToolManager()->GetActiveTool(EToolSide::Mouse)->GetToolProperties();
		for (UObject* Property : PropertyArray)
		{
			if (UMeshWeightPaintingToolProperties* WeightProperties = Cast<UMeshWeightPaintingToolProperties>(Property))
			{
				return WeightProperties;
			}
		}
	}
	return nullptr;
}

UMeshPaintMode* UMeshPaintMode::GetMeshPaintMode()
{
	return Cast<UMeshPaintMode>(GLevelEditorModeTools().GetActiveScriptableMode("MeshPaintMode"));
}

UMeshPaintMode::UMeshPaintMode()
	:Super()
{
	SettingsClass = UMeshPaintModeSettings::StaticClass();
	ToolsContextClass = UMeshToolsContext::StaticClass();
	CurrentPaletteName = MeshPaintMode_Color;
}

void UMeshPaintMode::Enter()
{
	Super::Enter();
	GEditor->OnEditorClose().AddUObject(this, &UMeshPaintMode::OnResetViewMode);
	ModeSettings = Cast<UMeshPaintModeSettings>(SettingsObject);
	FMeshPaintEditorModeCommands ToolManagerCommands = FMeshPaintEditorModeCommands::Get();
	RegisterTool(ToolManagerCommands.Select, TEXT("MeshClickTool"), NewObject<UMeshClickToolBuilder>());
	RegisterTool(ToolManagerCommands.ColorPaint, TEXT("ColorBrushTool"), NewObject<UMeshColorPaintingToolBuilder>());
	RegisterTool(ToolManagerCommands.WeightPaint, TEXT("WeightBrushTool"), NewObject<UMeshWeightPaintingToolBuilder>());

	UpdateSelectedMeshes();

	ActivateDefaultTool();
}

void UMeshPaintMode::Exit()
{
	GEditor->OnEditorClose().RemoveAll(this);
	OnResetViewMode();
	const FMeshPaintEditorModeCommands& Commands = FMeshPaintEditorModeCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();
	for (const TSharedPtr<const FUICommandInfo> Action : Commands.Commands[UMeshPaintMode::MeshPaintMode_Color])
	{
		CommandList->UnmapAction(Action);
	}
	for (const TSharedPtr<const FUICommandInfo> Action : Commands.Commands[UMeshPaintMode::MeshPaintMode_Weights])
	{
		CommandList->UnmapAction(Action);
	}
	for (const TSharedPtr<const FUICommandInfo> Action : Commands.Commands[UMeshPaintMode::MeshPaintMode_Weights])
	{
		CommandList->UnmapAction(Action);
	}
	Super::Exit();
}

void UMeshPaintMode::CreateToolkit()
{
	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FMeshPaintModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}

	// Register UI commands
	BindCommands();

	Super::CreateToolkit();
}

void UMeshPaintMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	UEdMode::Tick(ViewportClient, DeltaTime);

	if (ViewportClient->IsPerspective())
	{
		// Make sure perspective viewports are still set to real-time
		UMeshPaintModeHelpers::SetRealtimeViewport(true);

		// Set viewport show flags		
		UMeshPaintModeHelpers::SetViewportColorMode(ModeSettings->ColorViewMode, ViewportClient);
	}


	if (bRecacheVertexDataSize)
	{
		UpdateCachedVertexDataSize();
	}
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UMeshPaintMode::GetModeCommands() const
{
	return FMeshPaintEditorModeCommands::GetCommands();
}

void UMeshPaintMode::BindCommands()
{
	const FMeshPaintEditorModeCommands& Commands = FMeshPaintEditorModeCommands::Get();
	const TSharedRef<FUICommandList>& CommandList = Toolkit->GetToolkitCommands();

	auto IsInValidPaintMode = [this]() -> bool { return (GetVertexToolProperties() != nullptr) && Cast<UMeshToolManager>(GetToolManager())->SelectionContainsValidAdapters(); };
	CommandList->MapAction(Commands.Fill, 
		FUIAction(FExecuteAction::CreateUObject(this, &UMeshPaintMode::FillWithVertexColor),
		FCanExecuteAction::CreateLambda(IsInValidPaintMode)));

	CommandList->MapAction(Commands.Propagate,
		FUIAction(
			FExecuteAction::CreateUObject(this, &UMeshPaintMode::PropagateVertexColorsToAsset),
			FCanExecuteAction::CreateUObject(this, &UMeshPaintMode::CanPropagateVertexColors)
		));

	auto IsAValidMeshComponentSelected = [this]() -> bool { return (GetSelectedComponents<UMeshComponent>().Num() == 1) && Cast<UMeshToolManager>(GetToolManager())->SelectionContainsValidAdapters(); };
	CommandList->MapAction(Commands.Import,
		FUIAction(
			FExecuteAction::CreateUObject(this, &UMeshPaintMode::ImportVertexColors),
			FCanExecuteAction::CreateLambda(IsAValidMeshComponentSelected)
		));

	CommandList->MapAction(Commands.Save,
		FUIAction(
			FExecuteAction::CreateUObject(this, &UMeshPaintMode::SavePaintedAssets),
			FCanExecuteAction::CreateUObject(this, &UMeshPaintMode::CanSaveMeshPackages)
		));

	CommandList->MapAction(Commands.Copy,
		FUIAction(
			FExecuteAction::CreateUObject(this, &UMeshPaintMode::CopyVertexColors),
			FCanExecuteAction::CreateUObject(this, &UMeshPaintMode::CanCopyInstanceVertexColors)
		));

	CommandList->MapAction(Commands.Paste,
		FUIAction(
			FExecuteAction::CreateUObject(this, &UMeshPaintMode::PasteVertexColors),
			FCanExecuteAction::CreateUObject(this, &UMeshPaintMode::CanPasteInstanceVertexColors)
		));

	CommandList->MapAction(Commands.Remove,
		FUIAction(
			FExecuteAction::CreateUObject(this, &UMeshPaintMode::RemoveVertexColors),
			FCanExecuteAction::CreateUObject(this, &UMeshPaintMode::CanRemoveInstanceColors)
		));

	CommandList->MapAction(Commands.Fix,
		FUIAction(
			FExecuteAction::CreateUObject(this, &UMeshPaintMode::FixVertexColors),
			FCanExecuteAction::CreateUObject(this, &UMeshPaintMode::DoesRequireVertexColorsFixup)
		));

	CommandList->MapAction(Commands.PropagateVertexColorsToLODs,
		FUIAction(
			FExecuteAction::CreateUObject(this, &UMeshPaintMode::PropagateVertexColorsToLODs),
			FCanExecuteAction::CreateUObject(this, &UMeshPaintMode::CanPropagateVertexColorsToLODs)
		));

}




void UMeshPaintMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FMeshPaintingToolActionCommands::UpdateToolCommandBinding(Tool, ToolCommandList, false);

	if (UMeshVertexPaintingTool* VertexPaintingTool = Cast<UMeshVertexPaintingTool>(GetToolManager()->GetActiveTool(EToolSide::Left)))
	{
		VertexPaintingTool->OnPaintingFinished().BindUObject(this, &UMeshPaintMode::UpdateCachedVertexDataSize);
	}
}

void UMeshPaintMode::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	FMeshPaintingToolActionCommands::UpdateToolCommandBinding(Tool, ToolCommandList, true);
	// First update your bindings, then call the base behavior
	Super::OnToolEnded(Manager, Tool);
}

bool UMeshPaintMode::InputAxis(FEditorViewportClient* InViewportClient, FViewport* Viewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime)
{
	bool bSelectingOnly = (GetToolManager()->GetActiveTool(EToolSide::Mouse)) ? GetToolManager()->GetActiveTool(EToolSide::Mouse)->IsA<USingleClickTool>() : false;

	// If we are painting (not selecting), don't allow panning with the mouse.
	return !bSelectingOnly && !InViewportClient->IsAltPressed();
}

void UMeshPaintMode::ActorSelectionChangeNotify()
{
	UpdateSelectedMeshes();
}

void UMeshPaintMode::UpdateSelectedMeshes()
{
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		MeshToolManager->ResetState();
		const TArray<UMeshComponent*> CurrentMeshComponents = GetSelectedComponents<UMeshComponent>();
		MeshToolManager->AddSelectedMeshComponents(CurrentMeshComponents);
		MeshToolManager->bNeedsRecache = true;
	}
	bRecacheVertexDataSize = true;
}

void UMeshPaintMode::FillWithVertexColor()
{
	FScopedTransaction Transaction(LOCTEXT("LevelMeshPainter_TransactionFillInstColors", "Filling Per-Instance Vertex Colors"));
	const TArray<UMeshComponent*> MeshComponents = GetSelectedComponents<UMeshComponent>();

	static const bool bConvertSRGB = false;
	FColor FillColor = FColor::White;
	FColor MaskColor = FColor::White;

	if (GetToolManager()->GetActiveTool(EToolSide::Mouse)->IsA<UMeshWeightPaintingTool>())
	{
		FillColor = UMeshPaintingToolset::GenerateColorForTextureWeight((int32)GetWeightToolProperties()->TextureWeightType, (int32)GetWeightToolProperties()->PaintTextureWeightIndex).ToFColor(bConvertSRGB);
	}
	else if (UMeshColorPaintingToolProperties* ColorProperties = GetColorToolProperties())
	{
		FillColor = ColorProperties->PaintColor.ToFColor(bConvertSRGB);
		MaskColor.R = ColorProperties->bWriteRed ? 255 : 0;
		MaskColor.G = ColorProperties->bWriteGreen ? 255 : 0;
		MaskColor.B = ColorProperties->bWriteBlue ? 255 : 0;
		MaskColor.A = ColorProperties->bWriteAlpha ? 255 : 0;
	}

	TUniquePtr< FComponentReregisterContext > ComponentReregisterContext;
	/** Fill each mesh component with the given vertex color */
	for (UMeshComponent* Component : MeshComponents)
	{
		checkf(Component != nullptr, TEXT("Invalid Mesh Component"));
		Component->Modify();
		ComponentReregisterContext = MakeUnique<FComponentReregisterContext>(Component);

		TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = Cast<UMeshToolManager>(GetToolManager())->GetAdapterForComponent(Component);
		if (MeshAdapter)
		{
			MeshAdapter->PreEdit();
		}

		if (UMeshColorPaintingToolProperties* ColorProperties = UMeshPaintMode::GetColorToolProperties())
		{
			const bool bPaintOnSpecificLOD = ColorProperties ? ColorProperties->bPaintOnSpecificLOD : false;

			if (Component->IsA<UStaticMeshComponent>())
			{
				UMeshPaintingToolset::FillStaticMeshVertexColors(Cast<UStaticMeshComponent>(Component), bPaintOnSpecificLOD ? ColorProperties->LODIndex : -1, FillColor, MaskColor);
			}
			else if (Component->IsA<USkeletalMeshComponent>())
			{
				UMeshPaintingToolset::FillSkeletalMeshVertexColors(Cast<USkeletalMeshComponent>(Component), bPaintOnSpecificLOD ? ColorProperties->LODIndex : -1, FillColor, MaskColor);
			}
		}

		if (MeshAdapter)
		{
			MeshAdapter->PostEdit();
		}
	}
}


void UMeshPaintMode::PropagateVertexColorsToAsset()
{
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	FSuppressableWarningDialog::FSetupInfo SetupInfo(LOCTEXT("PushInstanceVertexColorsPrompt_Message", "Copying the instance vertex colors to the source mesh will replace any of the source mesh's pre-existing vertex colors and affect every instance of the source mesh."),
		LOCTEXT("PushInstanceVertexColorsPrompt_Title", "Warning: Copying vertex data overwrites all instances"), "Warning_PushInstanceVertexColorsPrompt");

	SetupInfo.ConfirmText = LOCTEXT("PushInstanceVertexColorsPrompt_ConfirmText", "Continue");
	SetupInfo.CancelText = LOCTEXT("PushInstanceVertexColorsPrompt_CancelText", "Abort");
	SetupInfo.CheckBoxText = LOCTEXT("PushInstanceVertexColorsPrompt_CheckBoxText", "Always copy vertex colors without prompting");

	FSuppressableWarningDialog VertexColorCopyWarning(SetupInfo);

	// Prompt the user to see if they really want to push the vert colors to the source mesh and to explain
	// the ramifications of doing so. This uses a suppressible dialog so that the user has the choice to always ignore the warning.
	if (VertexColorCopyWarning.ShowModal() != FSuppressableWarningDialog::Cancel)
	{
		FScopedTransaction Transaction(LOCTEXT("LevelMeshPainter_TransactionPropogateColors", "Propagating Vertex Colors To Source Meshes"));
		UMeshPaintModeHelpers::PropagateVertexColors(StaticMeshComponents);
	}
}

bool UMeshPaintMode::CanPropagateVertexColors() const
{
	// Check whether or not our selected Static Mesh Components contain instance based vertex colors (only these can be propagated to the base mesh)
	int32 NumInstanceVertexColorBytes = 0;

	TArray<UStaticMesh*> StaticMeshes;
	TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	return UMeshPaintModeHelpers::CanPropagateVertexColors(StaticMeshComponents, StaticMeshes, NumInstanceVertexColorBytes);

}

void UMeshPaintMode::ImportVertexColors()
{
	const TArray<UMeshComponent*> MeshComponents = GetSelectedComponents<UMeshComponent>();
	if (MeshComponents.Num() == 1)
	{
		/** Import vertex color to single selected mesh component */
		FScopedTransaction Transaction(LOCTEXT("LevelMeshPainter_TransactionImportColors", "Importing Vertex Colors From Texture"));
		UMeshPaintModeHelpers::ImportVertexColorsFromTexture(MeshComponents[0]);
	}
}

void UMeshPaintMode::SavePaintedAssets()
{
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	const TArray<USkeletalMeshComponent*> SkeletalMeshComponents = GetSelectedComponents<USkeletalMeshComponent>();

	/** Try and save outstanding dirty packages for currently selected mesh components */
	TArray<UObject*> ObjectsToSave;
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		if (StaticMeshComponent && StaticMeshComponent->GetStaticMesh())
		{
			ObjectsToSave.Add(StaticMeshComponent->GetStaticMesh());
		}
	}

	for (USkeletalMeshComponent* SkeletalMeshComponent : SkeletalMeshComponents)
	{
		if (SkeletalMeshComponent && SkeletalMeshComponent->SkeletalMesh)
		{
			ObjectsToSave.Add(SkeletalMeshComponent->SkeletalMesh);
		}
	}

	if (ObjectsToSave.Num() > 0)
	{
		UPackageTools::SavePackagesForObjects(ObjectsToSave);
	}
}

bool UMeshPaintMode::CanSaveMeshPackages() const
{
	// Check whether or not any of our selected mesh components contain mesh objects which require saving
	TArray<UMeshComponent*> Components = GetSelectedComponents<UMeshComponent>();

	bool bValid = false;

	for (UMeshComponent* Component : Components)
	{
		UObject* Object = nullptr;
		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
		{
			Object = StaticMeshComponent->GetStaticMesh();
		}
		else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Component))
		{
			Object = SkeletalMeshComponent->SkeletalMesh;
		}

		if (Object != nullptr && Object->GetOutermost()->IsDirty())
		{
			bValid = true;
			break;
		}
	}

	return bValid;
}

bool UMeshPaintMode::CanRemoveInstanceColors() const
{
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	int32 PaintingMeshLODIndex = 0;
	if (UMeshColorPaintingToolProperties* ColorProperties = UMeshPaintMode::GetColorToolProperties())
	{
		PaintingMeshLODIndex = ColorProperties->bPaintOnSpecificLOD ? ColorProperties->LODIndex : 0;
	}
	int32 NumValidMeshes = 0;
	// Retrieve per instance vertex color information (only valid if the component contains actual instance vertex colors)
	for (UStaticMeshComponent* Component : StaticMeshComponents)
	{
		if (Component != nullptr && Component->GetStaticMesh() != nullptr && Component->GetStaticMesh()->GetNumLODs() > (int32)PaintingMeshLODIndex)
		{
			uint32 BufferSize = UMeshPaintingToolset::GetVertexColorBufferSize(Component, PaintingMeshLODIndex, true);

			if (BufferSize > 0)
			{
				++NumValidMeshes;
			}
		}
	}

	return (NumValidMeshes != 0);
}

bool UMeshPaintMode::CanPasteInstanceVertexColors() const
{
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	const TArray<FPerComponentVertexColorData> CopiedColorsByComponent = Cast<UMeshToolManager>(GetToolManager())->GetCopiedColorsByComponent();
	return UMeshPaintModeHelpers::CanPasteInstanceVertexColors(StaticMeshComponents, CopiedColorsByComponent);
}

bool UMeshPaintMode::CanCopyInstanceVertexColors() const
{
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	int32 PaintingMeshLODIndex = 0;
	if (UMeshColorPaintingToolProperties* ColorProperties = UMeshPaintMode::GetColorToolProperties())
	{
		PaintingMeshLODIndex = ColorProperties->bPaintOnSpecificLOD ? ColorProperties->LODIndex : 0;
	}

	return UMeshPaintModeHelpers::CanCopyInstanceVertexColors(StaticMeshComponents, PaintingMeshLODIndex);

}

bool UMeshPaintMode::CanPropagateVertexColorsToLODs() const
{
	bool bPaintOnSpecificLOD = false;
	if (UMeshColorPaintingToolProperties* ColorProperties = UMeshPaintMode::GetColorToolProperties())
	{
		bPaintOnSpecificLOD = ColorProperties ? ColorProperties->bPaintOnSpecificLOD : false;
	}
	// Can propagate when the mesh contains per-lod vertex colors or when we are not painting to a specific lod
	const bool bSelectionContainsPerLODColors = Cast<UMeshToolManager>(GetToolManager())->SelectionContainsPerLODColors();
	return bSelectionContainsPerLODColors || bPaintOnSpecificLOD;
}

void UMeshPaintMode::CopyVertexColors()
{
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	TArray<FPerComponentVertexColorData> CopiedColorsByComponent;
	UMeshPaintModeHelpers::CopyVertexColors(StaticMeshComponents, CopiedColorsByComponent);
	Cast<UMeshToolManager>(GetToolManager())->SetCopiedColorsByComponent(CopiedColorsByComponent);

}

void UMeshPaintMode::PasteVertexColors()
{
	FScopedTransaction Transaction(LOCTEXT("LevelMeshPainter_TransactionPasteInstColors", "Pasting Per-Instance Vertex Colors"));
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	TArray<FPerComponentVertexColorData> CopiedColorsByComponent = Cast<UMeshToolManager>(GetToolManager())->GetCopiedColorsByComponent();
	UMeshPaintModeHelpers::PasteVertexColors(StaticMeshComponents, CopiedColorsByComponent);
	UpdateCachedVertexDataSize();
}

void UMeshPaintMode::FixVertexColors()
{
	FScopedTransaction Transaction(LOCTEXT("LevelMeshPainter_TransactionFixInstColors", "Fixing Per-Instance Vertex Colors"));
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	for (UStaticMeshComponent* Component : StaticMeshComponents)
	{
		Component->FixupOverrideColorsIfNecessary();
	}
}

bool UMeshPaintMode::DoesRequireVertexColorsFixup() const
{
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	bool bAnyMeshNeedsFixing = false;
	/** Check if there are any static mesh components which require fixing */
	for (UStaticMeshComponent* Component : StaticMeshComponents)
	{
		bAnyMeshNeedsFixing |= Component->RequiresOverrideVertexColorsFixup();
	}

	return bAnyMeshNeedsFixing;
}

void UMeshPaintMode::RemoveVertexColors()
{
	FScopedTransaction Transaction(LOCTEXT("LevelMeshPainter_TransactionRemoveInstColors", "Removing Per-Instance Vertex Colors"));
	const TArray<UStaticMeshComponent*> StaticMeshComponents = GetSelectedComponents<UStaticMeshComponent>();
	for (UStaticMeshComponent* Component : StaticMeshComponents)
	{
		UMeshPaintingToolset::RemoveComponentInstanceVertexColors(Component);
	}

	UpdateCachedVertexDataSize();
}


void UMeshPaintMode::PropagateVertexColorsToLODs()
{
	//Only show the lost data warning if there is actually some data to lose
	bool bAbortChange = false;
	UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager());
	TArray<UMeshComponent*> PaintableComponents = MeshToolManager->GetPaintableMeshComponents();
	const bool bSelectionContainsPerLODColors = MeshToolManager->SelectionContainsPerLODColors();
	if (bSelectionContainsPerLODColors)
	{
		//Warn the user they will lose custom painting data
		FSuppressableWarningDialog::FSetupInfo SetupInfo(LOCTEXT("LooseLowersLODsVertexColorsPrompt_Message", "Propagating Vertex Colors from LOD0 to all lower LODs. This mean all lower LODs custom vertex painting will be lost."),
			LOCTEXT("LooseLowersLODsVertexColorsPrompt_Title", "Warning: Lowers LODs custom vertex painting will be lost!"), "Warning_LooseLowersLODsVertexColorsPrompt");

		SetupInfo.ConfirmText = LOCTEXT("LooseLowersLODsVertexColorsPrompt_ConfirmText", "Continue");
		SetupInfo.CancelText = LOCTEXT("LooseLowersLODsVertexColorsPrompt_CancelText", "Abort");
		SetupInfo.CheckBoxText = LOCTEXT("LooseLowersLODsVertexColorsPrompt_CheckBoxText", "Always copy vertex colors without prompting");

		FSuppressableWarningDialog LooseLowersLODsVertexColorsWarning(SetupInfo);
	
		// Prompt the user to see if they really want to propagate the base lod vert colors to the lowers LODs.
		if (LooseLowersLODsVertexColorsWarning.ShowModal() == FSuppressableWarningDialog::Cancel)
		{
			bAbortChange = true;
		}
		else
		{
			// Reset the state flag as we'll be removing all per-lod colors 
			MeshToolManager->ClearSelectionLODColors();
			UMeshPaintModeHelpers::RemovePerLODColors(PaintableComponents);
		}
	}

	//The user cancel the change, avoid changing the value
	if (bAbortChange)
	{
		return;
	}

	for (UMeshComponent* SelectedComponent : PaintableComponents)
	{
		if (SelectedComponent)
		{
			TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter = Cast<UMeshToolManager>(GetToolManager())->GetAdapterForComponent(SelectedComponent);
			UMeshPaintingToolset::ApplyVertexColorsToAllLODs(*MeshAdapter, SelectedComponent);
			FComponentReregisterContext ReregisterContext(SelectedComponent);
		}
	}

	MeshToolManager->Refresh();
}

template<typename ComponentClass>
TArray<ComponentClass*> UMeshPaintMode::GetSelectedComponents() const
{
	TArray<ComponentClass*> Components;
	FToolBuilderState SelectionState;
	GetToolManager()->GetContextQueriesAPI()->GetCurrentSelectionState(SelectionState);
//	if (PaintSettings->PaintMode == EPaintMode::Textures)
	{

		for (int32 SelectionIndex = 0; SelectionIndex < SelectionState.SelectedComponents.Num(); ++SelectionIndex)
		{
			ComponentClass* SelectedComponent = Cast<ComponentClass>(SelectionState.SelectedComponents[SelectionIndex]);
			if (SelectedComponent)
			{
				Components.AddUnique(SelectedComponent);
			}
		}
	}

	if (Components.Num() == 0)
	{
		for (int32 SelectionIndex = 0; SelectionIndex < SelectionState.SelectedActors.Num(); ++SelectionIndex)
		{
			AActor* SelectedActor = Cast<AActor>(SelectionState.SelectedActors[SelectionIndex]);
			if (SelectedActor)
			{
				TInlineComponentArray<ComponentClass*> ActorComponents;
				SelectedActor->GetComponents(ActorComponents);
				for (ComponentClass* Component : ActorComponents)
				{
					Components.AddUnique(Component);
				}
			}
		}
	}

	return Components;
}

template TArray<UStaticMeshComponent*> UMeshPaintMode::GetSelectedComponents<UStaticMeshComponent>() const;
template TArray<USkeletalMeshComponent*> UMeshPaintMode::GetSelectedComponents<USkeletalMeshComponent>() const;
template TArray<UMeshComponent*> UMeshPaintMode::GetSelectedComponents<UMeshComponent>() const;




void UMeshPaintMode::UpdateCachedVertexDataSize()
{
	CachedVertexDataSize = 0;

	const bool bInstance = true;
	if (UMeshToolManager* MeshToolManager = Cast<UMeshToolManager>(GetToolManager()))
	{
		for (UMeshComponent* SelectedComponent : MeshToolManager->GetPaintableMeshComponents())
		{
			int32 NumLODs = UMeshPaintingToolset::GetNumberOfLODs(SelectedComponent);
			for (int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex)
			{
				CachedVertexDataSize += UMeshPaintingToolset::GetVertexColorBufferSize(SelectedComponent, LODIndex, bInstance);
			}
		}
	}
}



void UMeshPaintMode::ActivateDefaultTool()
{
	if (CurrentPaletteName == UMeshPaintMode::MeshPaintMode_Color)
	{
		ToolsContext->StartTool(EToolSide::Mouse, TEXT("MeshClickTool"));
	}
	if (CurrentPaletteName == UMeshPaintMode::MeshPaintMode_Weights)
	{
		ToolsContext->StartTool(EToolSide::Mouse, TEXT("MeshClickTool"));
	}
	if (CurrentPaletteName == UMeshPaintMode::MeshPaintMode_Weights)
	{

	}
}

void UMeshPaintMode::UpdateOnPaletteChange()
{
	UpdateSelectedMeshes();
}

void UMeshPaintMode::OnResetViewMode()
{
	// Reset viewport color mode for all active viewports
	for (FEditorViewportClient* ViewportClient : GEditor->GetAllViewportClients())
	{
		if (!ViewportClient || ViewportClient->GetModeTools() != GetModeManager())
		{
			continue;
		}

		UMeshPaintModeHelpers::SetViewportColorMode(EMeshPaintDataColorViewMode::Normal, ViewportClient);
	}
}


#undef LOCTEXT_NAMESPACE

