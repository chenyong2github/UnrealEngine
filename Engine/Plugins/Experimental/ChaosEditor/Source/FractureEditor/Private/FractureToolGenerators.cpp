// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureToolGenerators.h"

#include "Editor.h"
#include "ScopedTransaction.h"
#include "Engine/Selection.h"
#include "SCreateAssetFromObject.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Layers/LayersSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Misc/Change.h"
#include "ChangeTransactor.h"

#include "FractureModeSettings.h"

#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionConversion.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "FractureToolContext.h"

#define LOCTEXT_NAMESPACE "FractureToolGenerators"

// Creates an undo/redo action that (un)registers and object with the Asset Registry.
// Upon undo this causes the object to be unregistered and as a result be removed from
// Content Browsers.
class FAssetRegistrationChange final : public FCommandChange
{
public:
	void Apply(UObject* Object) override
	{
		FAssetRegistryModule::AssetCreated(Object);
	}
	void Revert(UObject* Object) override
	{
		FAssetRegistryModule::AssetDeleted(Object);
	}

	FString ToString() const override
	{
		return TEXT("Asset registry from " LOCTEXT_NAMESPACE);
	}
};

FText UFractureToolGenerateAsset::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolGenerateAsset", "New"));
}

FText UFractureToolGenerateAsset::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolGenerateAssetTooltip", "Generate a Geometry Collection Asset from selected Static Meshes and/or Geometry Collections."));
}

FSlateIcon UFractureToolGenerateAsset::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.GenerateAsset");
}

void UFractureToolGenerateAsset::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "GenerateAsset", "New", "Generate a new Geometry Collection Asset from the selected Static Meshes and/or Geometry Collections. Geometry Collections are assets that support fracture.", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->GenerateAsset = UICommandInfo;
}

bool UFractureToolGenerateAsset::CanExecute() const
{
	return (IsStaticMeshSelected() || IsGeometryCollectionSelected());
}

void UFractureToolGenerateAsset::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	Toolkit = InToolkit;
	
	USelection* SelectionSet = GEditor->GetSelectedActors();

	TArray<AActor*> SelectedActors;
	SelectedActors.Reserve(SelectionSet->Num());

	SelectionSet->GetSelectedObjects(SelectedActors);

	OpenGenerateAssetDialog(SelectedActors);

	// Note: Transaction for undo history is created after the user completes the UI dialog; see OnGenerateAssetPathChosen()
}

void UFractureToolGenerateAsset::OpenGenerateAssetDialog(TArray<AActor*>& Actors)
{
	TSharedPtr<SWindow> PickAssetPathWindow;

	SAssignNew(PickAssetPathWindow, SWindow)
		.Title(LOCTEXT("SelectPath", "Select Path"))
		.ToolTipText(LOCTEXT("SelectPathTooltip", "Select the asset path for your new Geometry Collection"))
		.ClientSize(FVector2D(500, 500));

	// NOTE - the parent window has to completely exist before this one does so the parent gets set properly.
	// This is why we do not just put this in the Contents()[ ... ] of the Window above.
	TSharedPtr<SCreateAssetFromObject> CreateAssetDialog;
	PickAssetPathWindow->SetContent(
		SAssignNew(CreateAssetDialog, SCreateAssetFromObject, PickAssetPathWindow)
		.AssetFilenameSuffix(TEXT("GeometryCollection"))
		.HeadingText(LOCTEXT("CreateGeometryCollection_Heading", "Geometry Collection Name"))
		.CreateButtonText(LOCTEXT("CreateGeometryCollection_ButtonLabel", "Create Geometry Collection"))
		.AssetPath(AssetPath)
		.OnCreateAssetAction(FOnPathChosen::CreateUObject(this, &UFractureToolGenerateAsset::OnGenerateAssetPathChosen, Actors))
	);

	TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
	if (RootWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(PickAssetPathWindow.ToSharedRef(), RootWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(PickAssetPathWindow.ToSharedRef());
	}

}

void UFractureToolGenerateAsset::OnGenerateAssetPathChosen(const FString& InAssetPath, TArray<AActor*> Actors)
{	
	FScopedTransaction Transaction(LOCTEXT("GenerateAsset", "Generate Geometry Collection Asset"));

	//Record the path
	int32 LastSlash = INDEX_NONE;
	if (InAssetPath.FindLastChar('/', LastSlash))
	{
		AssetPath = InAssetPath.Left(LastSlash);
	}

	UGeometryCollectionComponent* GeometryCollectionComponent = nullptr;

	if (Actors.Num() > 0)
	{
		AActor* FirstActor = Actors[0];

		AGeometryCollectionActor* GeometryCollectionActor = Cast<AGeometryCollectionActor>(FirstActor);
		
		GeometryCollectionActor = ConvertActorsToGeometryCollection(InAssetPath, Actors);

		GeometryCollectionComponent = GeometryCollectionActor->GetGeometryCollectionComponent();

		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
		EditBoneColor.SetShowBoneColors(true);

		// Move GC actor to source actors position and remove source actor from scene
		const FVector ActorLocation(FirstActor->GetActorLocation());
		GeometryCollectionActor->SetActorLocation(ActorLocation);

		// Clear selection of mesh actor used to make GC before selecting, will cause details pane to not display geometry collection details.
		GEditor->SelectNone(true, true, false);
		GEditor->SelectActor(GeometryCollectionActor, true, true);

		EditBoneColor.SelectBones(GeometryCollection::ESelectionMode::AllGeometry);

		if (Toolkit.IsValid())
		{
			TSharedPtr<FFractureEditorModeToolkit> SharedToolkit(Toolkit.Pin());
			SharedToolkit->SetOutlinerComponents({ GeometryCollectionComponent });
			SharedToolkit->SetBoneSelection(GeometryCollectionComponent, EditBoneColor.GetSelectedBones(), true);

			SharedToolkit->OnSetLevelViewValue(-1);

			SharedToolkit->RegenerateOutliner();
			SharedToolkit->RegenerateHistogram();

			SharedToolkit->UpdateExplodedVectors(GeometryCollectionComponent);
		}
		
		GeometryCollectionComponent->MarkRenderDynamicDataDirty();
		GeometryCollectionComponent->MarkRenderStateDirty();

		for (AActor* Actor : Actors)
		{
			Actor->Modify();
			Actor->Destroy();
		}
	}
}

AGeometryCollectionActor* UFractureToolGenerateAsset::ConvertActorsToGeometryCollection(const FString& InAssetPath, TArray<AActor*>& Actors)
{
	ensure(Actors.Num() > 0);
	AActor* FirstActor = Actors[0];
	const FString& Name = FirstActor->GetActorLabel();
	const FVector FirstActorLocation(FirstActor->GetActorLocation());


	AGeometryCollectionActor* NewActor = CreateNewGeometryActor(InAssetPath, FTransform(), true);

	FGeometryCollectionEdit GeometryCollectionEdit = NewActor->GetGeometryCollectionComponent()->EditRestCollection(GeometryCollection::EEditUpdate::RestPhysicsDynamic);
	UGeometryCollection* FracturedGeometryCollection = GeometryCollectionEdit.GetRestCollection();
	check(FracturedGeometryCollection);

	for (AActor* Actor : Actors)
	{
		const FTransform ActorTransform(Actor->GetTransform());
		const FVector ActorOffset(Actor->GetActorLocation() - FirstActor->GetActorLocation());

		TArray<UStaticMeshComponent*> StaticMeshComponents;
		Actor->GetComponents<UStaticMeshComponent>(StaticMeshComponents, true);
		for (int32 ii = 0, ni = StaticMeshComponents.Num(); ii < ni; ++ii)
		{
			// We're partial to static mesh components, here
			UStaticMeshComponent* StaticMeshComponent = StaticMeshComponents[ii];
			if (StaticMeshComponent != nullptr)
			{
				UStaticMesh* ComponentStaticMesh = StaticMeshComponent->GetStaticMesh();
				if (ComponentStaticMesh != nullptr)
				{
					// If any of the static meshes have Nanite enabled, also enable on the new geometry collection asset for convenience.
					FracturedGeometryCollection->EnableNanite |= ComponentStaticMesh->NaniteSettings.bEnabled;
				}

				FTransform ComponentTransform(StaticMeshComponent->GetComponentTransform());
				ComponentTransform.SetTranslation((ComponentTransform.GetTranslation() - ActorTransform.GetTranslation()) + ActorOffset);

				// Record the contributing source on the asset.
				FSoftObjectPath SourceSoftObjectPath(ComponentStaticMesh);
				decltype(FGeometryCollectionSource::SourceMaterial) SourceMaterials(StaticMeshComponent->GetMaterials());
				FracturedGeometryCollection->GeometrySource.Add({ SourceSoftObjectPath, ComponentTransform, SourceMaterials });

				FGeometryCollectionEngineConversion::AppendStaticMesh(ComponentStaticMesh, SourceMaterials, ComponentTransform, FracturedGeometryCollection, false);
			}
		}

		TArray<UGeometryCollectionComponent*> GeometryCollectionComponents;
		Actor->GetComponents<UGeometryCollectionComponent>(GeometryCollectionComponents, true);
		for (int32 ii = 0, ni = GeometryCollectionComponents.Num(); ii < ni; ++ii)
		{
			UGeometryCollectionComponent* GeometryCollectionComponent = GeometryCollectionComponents[ii];
			if (GeometryCollectionComponent != nullptr)
			{
				const UGeometryCollection* RestCollection = GeometryCollectionComponent->GetRestCollection();
				if (RestCollection != nullptr)
				{
					// If any of the static meshes have Nanite enabled, also enable on the new geometry collection asset for convenience.
					FracturedGeometryCollection->EnableNanite |= RestCollection->EnableNanite;
				}

				FTransform ComponentTransform(GeometryCollectionComponent->GetComponentTransform());
				ComponentTransform.SetTranslation((ComponentTransform.GetTranslation() - ActorTransform.GetTranslation()) + ActorOffset);

				// Record the contributing source on the asset.
				FSoftObjectPath SourceSoftObjectPath(RestCollection);

				// We're not interested in recording the final material of the collection since it's inevitably the Selection material.
				int32 NumMaterials = GeometryCollectionComponent->GetNumMaterials() - 1;
				TArray<TObjectPtr<UMaterialInterface>> SourceMaterials;
				SourceMaterials.SetNum(NumMaterials);
				for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
				{
					SourceMaterials[MaterialIndex] = GeometryCollectionComponent->GetMaterial(MaterialIndex);
				}
				FracturedGeometryCollection->GeometrySource.Add({ SourceSoftObjectPath, ComponentTransform, SourceMaterials });

				FGeometryCollectionEngineConversion::AppendGeometryCollection(RestCollection, GeometryCollectionComponent, ComponentTransform, FracturedGeometryCollection, false);

			}
		}
	}

	FracturedGeometryCollection->InitializeMaterials();

	AddSingleRootNodeIfRequired(FracturedGeometryCollection);

	if (FracturedGeometryCollection->EnableNanite)
	{
		FracturedGeometryCollection->InvalidateCollection();
		FracturedGeometryCollection->EnsureDataIsCooked(true /* init resources */);
	}

	NewActor->GetGeometryCollectionComponent()->MarkRenderStateDirty();

	// Add and initialize guids
	::GeometryCollection::GenerateTemporaryGuids(FracturedGeometryCollection->GetGeometryCollection().Get(), 0 , true);

	// Update proximity graph
	FGeometryCollectionProximityUtility ProximityUtility(FracturedGeometryCollection->GetGeometryCollection().Get());
	ProximityUtility.UpdateProximity();

	const UFractureModeSettings* ModeSettings = GetDefault<UFractureModeSettings>();
	ModeSettings->ApplyDefaultConvexSettings(*FracturedGeometryCollection->GetGeometryCollection());

	return NewActor;
}

class AGeometryCollectionActor* UFractureToolGenerateAsset::CreateNewGeometryActor(const FString& InAssetPath, const FTransform& Transform, bool AddMaterials /*= false*/)
{
	FString UniquePackageName = InAssetPath;
	FString UniqueAssetName = FPackageName::GetLongPackageAssetName(InAssetPath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(UniquePackageName, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* Package = CreatePackage(*UniquePackageName);
	UGeometryCollection* InGeometryCollection = static_cast<UGeometryCollection*>(NewObject<UGeometryCollection>(Package, UGeometryCollection::StaticClass(), FName(*UniqueAssetName), RF_Transactional | RF_Public | RF_Standalone));
	if(!InGeometryCollection->SizeSpecificData.Num()) InGeometryCollection->SizeSpecificData.Add(FGeometryCollectionSizeSpecificData());

	// Record the creation of the geometry collection so it's removed from the Asset Registry and the Content Browser when undo is called.
	UE::FChangeTransactor Transactor(InGeometryCollection);
	Transactor.OpenTransaction(LOCTEXT("GeometryCollectionAssetRegistration", "Geometry Collection Asset Registration"));
	Transactor.AddTransactionChange<FAssetRegistrationChange>();
	Transactor.CloseTransaction();

	// Create the new Geometry Collection actor
	AGeometryCollectionActor* NewActor = Cast<AGeometryCollectionActor>(AddActor(GetSelectedLevel(), AGeometryCollectionActor::StaticClass()));
	check(NewActor->GetGeometryCollectionComponent());

	// Set the Geometry Collection asset in the new actor
	NewActor->GetGeometryCollectionComponent()->SetRestCollection(InGeometryCollection);
	NewActor->GetGeometryCollectionComponent()->SetPhysMaterialOverride(GEngine->DefaultDestructiblePhysMaterial);

	// copy transform of original static mesh actor to this new actor
	NewActor->SetActorLabel(UniqueAssetName);
	NewActor->SetActorTransform(Transform);

	// Mark relevant stuff dirty
	FAssetRegistryModule::AssetCreated(InGeometryCollection);
	InGeometryCollection->MarkPackageDirty();
	Package->SetDirtyFlag(true);

	return NewActor;
}

ULevel* UFractureToolGenerateAsset::GetSelectedLevel()
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<ULevel*> UniqueLevels;
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			UniqueLevels.AddUnique(Actor->GetLevel());
		}
	}
	check(UniqueLevels.Num() == 1);
	return UniqueLevels[0];
}

AActor* UFractureToolGenerateAsset::AddActor(ULevel* InLevel, UClass* Class)
{
	check(Class);

	UWorld* World = InLevel->OwningWorld;
	ULevel* DesiredLevel = InLevel;

	// Transactionally add the actor.
	AActor* Actor = NULL;
	{
		FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "AddActor", "Add Actor"));

		FActorSpawnParameters SpawnInfo;
		SpawnInfo.OverrideLevel = DesiredLevel;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.ObjectFlags = RF_Transactional;
		const auto Location = FVector(0);
		const auto Rotation = FTransform(FVector(0)).GetRotation().Rotator();
		Actor = World->SpawnActor(Class, &Location, &Rotation, SpawnInfo);

		check(Actor);
		Actor->InvalidateLightingCache();
		Actor->PostEditMove(true);
	}

	// If this actor is part of any layers (set in its default properties), add them into the visible layers list.
	ULayersSubsystem* Layers = GEditor->GetEditorSubsystem<ULayersSubsystem>();
	Layers->SetLayersVisibility(Actor->Layers, true);

	// Clean up.
	Actor->MarkPackageDirty();
	ULevel::LevelDirtiedEvent.Broadcast();

	return Actor;
}


FText UFractureToolResetAsset::GetDisplayText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolReset", "Reset"));
}

FText UFractureToolResetAsset::GetTooltipText() const
{
	return FText(NSLOCTEXT("Fracture", "FractureToolResetTooltip", "Reset Geometry Collections to their initial unfractured states."));
}

FSlateIcon UFractureToolResetAsset::GetToolIcon() const
{
	return FSlateIcon("FractureEditorStyle", "FractureEditor.ResetAsset");
}

void UFractureToolResetAsset::RegisterUICommand(FFractureEditorCommands* BindingContext)
{
	UI_COMMAND_EXT(BindingContext, UICommandInfo, "ResetAsset", "Reset", "Reset selected Geometry Collections to their initial unfractured states.", EUserInterfaceActionType::ToggleButton, FInputChord());
	BindingContext->ResetAsset = UICommandInfo;
}

bool UFractureToolResetAsset::CanExecute() const
{
	return IsGeometryCollectionSelected();
}

void UFractureToolResetAsset::Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit)
{
	if (!InToolkit.IsValid())
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ResetCollection", "Reset Geometry Collection"));

	TSet<UGeometryCollectionComponent*> GeomCompSelection;
	GetSelectedGeometryCollectionComponents(GeomCompSelection);
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeomCompSelection)
	{
		GeometryCollectionComponent->Modify();
		FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
		if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{
				constexpr bool bKeepPreviousMaterials = true; // written as a flag in case we want to make this optional later
				TArray<TObjectPtr<UMaterialInterface>> OldMaterials;
				if (bKeepPreviousMaterials)
				{
					OldMaterials = GeometryCollectionObject->Materials;
				}

				GeometryCollectionObject->Reset();

				// Rebuild Collection from recorded source assets.
				for (const FGeometryCollectionSource& Source : GeometryCollectionObject->GeometrySource)
				{
					const UObject* SourceMesh = Source.SourceGeometryObject.TryLoad();
					if (const UStaticMesh* SourceStaticMesh = Cast<UStaticMesh>(SourceMesh))
					{
						FGeometryCollectionEngineConversion::AppendStaticMesh(SourceStaticMesh, Source.SourceMaterial, Source.LocalTransform, GeometryCollectionObject, false);
					}
					else if (const USkeletalMesh* SourceSkeletalMesh = Cast<USkeletalMesh>(SourceMesh))
					{
						// #todo (bmiller) Once we've settled on the right approach with static meshes, we'll need to apply the same strategy to skeletal mesh reconstruction.
						// FGeometryCollectionConversion::AppendSkeletalMesh(SourceSkeletalMesh, Source.SourceMaterial, Source.LocalTransform, GeometryCollectionObject, false);
					}
					else if (const UGeometryCollection* SourceGeometryCollection = Cast<UGeometryCollection>(SourceMesh))
					{
						FGeometryCollectionEngineConversion::AppendGeometryCollection(SourceGeometryCollection, Source.SourceMaterial, Source.LocalTransform, GeometryCollectionObject, false);
					}
				}

				GeometryCollectionObject->InitializeMaterials();

				if (bKeepPreviousMaterials)
				{
					int32 NewMatNum = GeometryCollectionObject->Materials.Num(), OldMatNum = OldMaterials.Num();
					// if the source asset was changed, number of materials might have changed; only copy to the extent the two arrays match
					int32 NumToCopy = FMath::Min(NewMatNum, OldMatNum);
					for (int32 MatIdx = 0; MatIdx + 1 < NumToCopy; MatIdx++)
					{
						GeometryCollectionObject->Materials[MatIdx] = OldMaterials[MatIdx];
					}
					if (NumToCopy > 0) // copy the selection material
					{
						GeometryCollectionObject->Materials[NewMatNum - 1] = OldMaterials[OldMatNum - 1];
					}
				}
				
				// Update proximity graph
				FGeometryCollectionProximityUtility ProximityUtility(GeometryCollection);
				ProximityUtility.UpdateProximity();

				const UFractureModeSettings* ModeSettings = GetDefault<UFractureModeSettings>();
				ModeSettings->ApplyDefaultConvexSettings(*GeometryCollection);

				FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);
				AddSingleRootNodeIfRequired(GeometryCollectionObject);
				GeometryCollectionComponent->MarkRenderDynamicDataDirty();
				GeometryCollectionComponent->MarkRenderStateDirty();
			}

			GeometryCollectionObject->MarkPackageDirty();
		}
		
		GeometryCollectionComponent->InitializeEmbeddedGeometry();
		
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection(true);
		EditBoneColor.ResetBoneSelection();
		EditBoneColor.ResetHighlightedBones();
	}
	InToolkit.Pin()->OnSetLevelViewValue(-1);
	InToolkit.Pin()->SetOutlinerComponents(GeomCompSelection.Array());
}


#undef LOCTEXT_NAMESPACE
