// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureEditorMode.h"
#include "FractureEditorModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "Framework/Application/SlateApplication.h"
#include "EditorModeManager.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "FractureTool.h"
#include "FractureToolUniform.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionHitProxy.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "GeometryCollection/GeometryCollection.h"
#include "FractureSelectionTools.h"
#include "EditorViewportClient.h"
#include "ScopedTransaction.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"

#define LOCTEXT_NAMESPACE "FFractureEditorModeToolkit"

const FEditorModeID FFractureEditorMode::EM_FractureEditorModeId = TEXT("EM_FractureEditorMode");

FFractureEditorMode::FFractureEditorMode()
{
}

FFractureEditorMode::~FFractureEditorMode()
{

}

void FFractureEditorMode::Enter()
{
	FEdMode::Enter();

	GEditor->RegisterForUndo(this);

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(new FFractureEditorModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnActorSelectionChanged().AddRaw(this, &FFractureEditorMode::OnActorSelectionChanged);

	FCoreUObjectDelegates::OnPackageReloaded.AddSP(this, &FFractureEditorMode::HandlePackageReloaded);
	
	// Get initial geometry component selection from currently selected actors when we enter the mode
	USelection* SelectedActors = GEditor->GetSelectedActors();

	TArray<UObject*> SelectedObjects;
	SelectedActors->GetSelectedObjects(SelectedObjects);

	OnActorSelectionChanged(SelectedObjects, false);

}

void FFractureEditorMode::Exit()
{
	GEditor->UnregisterForUndo(this);

	// Empty the geometry component selection set
	TArray<UObject*> SelectedObjects;
	OnActorSelectionChanged(SelectedObjects, false);

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
	if (LevelEditor)
	{
		LevelEditor->OnActorSelectionChanged().RemoveAll(this);
		LevelEditor->OnMapChanged().RemoveAll( this );
	}

	// Call base Exit method to ensure proper cleanup
	FEdMode::Exit();
}

void FFractureEditorMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(SelectedGeometryComponents);
}

bool FFractureEditorMode::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject *, FTransactionObjectEvent>>& TransactionObjectContexts) const
{
	if (InContext.Context == FractureTransactionContexts::SelectBoneContext)
	{
		return true;
	}

	return false;
}

void FFractureEditorMode::PostUndo(bool bSuccess)
{
	OnUndoRedo();
}

void FFractureEditorMode::PostRedo(bool bSuccess)
{
	OnUndoRedo();
}

void FFractureEditorMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	FFractureEditorModeToolkit* FractureToolkit = (FFractureEditorModeToolkit*)Toolkit.Get();
 
	if (UFractureModalTool* FractureTool = FractureToolkit->GetActiveTool())
	{
		auto Settings = FractureTool->GetSettingsObjects();
		FractureTool->Render(View, Viewport, PDI);
	}


}

bool FFractureEditorMode::UsesToolkits() const
{
	return true;
}

bool FFractureEditorMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{

	bool bHandled = false;
	if( Event == IE_Pressed )
	{
		FModifierKeysState ModifierKeysState = FSlateApplication::Get().GetModifierKeys();
		const TSharedRef<FUICommandList> CommandList = Toolkit.Get()->GetToolkitCommands();
		bHandled = CommandList->ProcessCommandBindings( Key, ModifierKeysState, false );
	}
	return bHandled;
}

bool FFractureEditorMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (HitProxy && HitProxy->IsA(HGeometryCollectionBone::StaticGetType()))
	{
		HGeometryCollectionBone* GeometryCollectionProxy = (HGeometryCollectionBone*)HitProxy;

		if(GeometryCollectionProxy->Component)
		{
			TArray<int32> BoneIndices({GeometryCollectionProxy->BoneIndex});

			FScopedTransaction Transaction(FractureTransactionContexts::SelectBoneContext, LOCTEXT("SelectGeometryCollectionBoneTransaction", "Select Bone"), GeometryCollectionProxy->Component);
			FFractureSelectionTools::ToggleSelectedBones(GeometryCollectionProxy->Component, BoneIndices, !Click.IsControlDown());
			GeometryCollectionProxy->Component->Modify();

			if (Toolkit.IsValid())
			{
				FFractureEditorModeToolkit* FractureToolkit = (FFractureEditorModeToolkit*)Toolkit.Get();
				FractureToolkit->SetBoneSelection(GeometryCollectionProxy->Component, GeometryCollectionProxy->Component->GetSelectedBones(), true);
			}

			return true;
		}
	}

	return false;

}

bool FFractureEditorMode::BoxSelect(FBox& InBox, bool InSelect /*= true*/)
{
	FConvexVolume BoxVolume(GetVolumeFromBox(InBox));
	return FrustumSelect(BoxVolume, nullptr, InSelect);
}

bool FFractureEditorMode::FrustumSelect(const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect /*= true*/)
{
	bool bStrictDragSelection = GetDefault<ULevelEditorViewportSettings>()->bStrictBoxSelection;
	bool bSelectedBones = false;

	if (USelection* SelectedActors = GEditor->GetSelectedActors())
	{
		for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
		{
			AActor* Actor = Cast<AActor>(*Iter);
			TArray<UGeometryCollectionComponent*, TInlineAllocator<1>> GeometryComponents;
			Actor->GetComponents<UGeometryCollectionComponent>(GeometryComponents);

			if (GeometryComponents.Num())
			{ 
				FTransform ActorTransform = Actor->GetTransform();
				FMatrix InvActorMatrix(ActorTransform.ToInverseMatrixWithScale());

				FConvexVolume SelectionFrustum(TranformFrustum(InFrustum, InvActorMatrix));

				TMap<int32, FBox> BoundsToBone;
			
				GetActorGlobalBounds(MakeArrayView(GeometryComponents), BoundsToBone);

				TArray<int32> SelectedBonesArray;

				for (const auto& Bone : BoundsToBone)
				{
					const FBox& TransformedBoneBox = Bone.Value;
					bool bFullyContained = false;
					bool bIntersected = SelectionFrustum.IntersectBox(TransformedBoneBox.GetCenter(), TransformedBoneBox.GetExtent(), bFullyContained);
					if (bIntersected)
					{
						if (!bStrictDragSelection || (bFullyContained && bStrictDragSelection))
						{
							SelectedBonesArray.Add(Bone.Key);
						}
					}
				}

				if (SelectedBonesArray.Num() > 0)
				{
					TInlineComponentArray<UGeometryCollectionComponent*> GeometryCollectionComponents;
					Actor->GetComponents(GeometryCollectionComponents);

					for (UGeometryCollectionComponent* GeometryCollectionComponent : GeometryCollectionComponents)
					{
						FScopedColorEdit ColorEdit = GeometryCollectionComponent->EditBoneSelection();
						ColorEdit.SelectBones(GeometryCollection::ESelectionMode::None);
						ColorEdit.SetSelectedBones(SelectedBonesArray);
						ColorEdit.SetHighlightedBones(SelectedBonesArray);
						bSelectedBones = true;

						FFractureEditorModeToolkit* FractureToolkit = (FFractureEditorModeToolkit*)Toolkit.Get();

						if(FractureToolkit)
						{
							FractureToolkit->SetBoneSelection(GeometryCollectionComponent, ColorEdit.GetSelectedBones(), true);
						}
					}
				}
			}
		}
	}

	return bSelectedBones;
}

bool FFractureEditorMode::ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox) const
{
	UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(PrimitiveComponent);
	if (GeometryCollectionComponent && GeometryCollectionComponent->GetSelectedBones().Num() > 0 && SelectedGeometryComponents.Contains(GeometryCollectionComponent))
	{
		TMap<int32, FBox> BoundsToBoneMap;
		GetActorGlobalBounds(MakeArrayView<UGeometryCollectionComponent*>(&GeometryCollectionComponent, 1), BoundsToBoneMap);

		FBox TotalBoneBox(ForceInit);
		for (int32 BoneIndex : GeometryCollectionComponent->GetSelectedBones())
		{
			if (FBox* LocalBoneBox = BoundsToBoneMap.Find(BoneIndex))
			{
				TotalBoneBox += *LocalBoneBox;
			}
		}

		InOutBox += TotalBoneBox.TransformBy(GeometryCollectionComponent->GetComponentToWorld());;

		CustomOrbitPivot = InOutBox.GetCenter();
		return true;
	}

	return false;
}

bool FFractureEditorMode::GetPivotForOrbit(FVector& OutPivot) const
{
	if (CustomOrbitPivot.IsSet())
	{
		OutPivot = CustomOrbitPivot.GetValue();
	}

	return CustomOrbitPivot.IsSet();
}

void FFractureEditorMode::OnUndoRedo()
{
	for (UGeometryCollectionComponent* SelectedComp : SelectedGeometryComponents)
	{
		// We need to update the bone colors to account for undoing/redoing selection
		bool bForce = true;
		FScopedColorEdit Edit(SelectedComp, bForce);
	}
}

void FFractureEditorMode::OnActorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh)
{
	CustomOrbitPivot.Reset();
	TSet<UGeometryCollectionComponent*> NewGeomSelection;

	int32 ViewLevel = -1;
	if(Toolkit.IsValid())
	{
		ViewLevel = ((FFractureEditorModeToolkit*)Toolkit.Get())->GetLevelViewValue();
	}
	

	// Build new selection set
	for (UObject* ActorObj : NewSelection)
	{
		AActor* Actor = CastChecked<AActor>(ActorObj);
		TArray<UGeometryCollectionComponent*> GeometryCollectionComponents;
		Actor->GetComponents(GeometryCollectionComponents);
		
		for(UGeometryCollectionComponent* GeometryCollectionComponent : GeometryCollectionComponents)
		{
			FScopedColorEdit ShowBoneColorsEdit(GeometryCollectionComponent);
			ShowBoneColorsEdit.SetEnableBoneSelection(true);
			// ShowBoneColorsEdit.SetLevelViewMode(ViewLevel);

			NewGeomSelection.Add(GeometryCollectionComponent);
		}
	}

	// reset state for components no longer selected
	for (UGeometryCollectionComponent* ExistingSelection : SelectedGeometryComponents)
	{
		if (ExistingSelection && !NewGeomSelection.Contains(ExistingSelection))
		{
			// This component is no longer selected, clear any modified state

			FScopedColorEdit ShowBoneColorsEdit(ExistingSelection);
			ShowBoneColorsEdit.SetEnableBoneSelection(false);
		}
	}

	SelectedGeometryComponents = NewGeomSelection.Array();

	if(Toolkit.IsValid())
	{
		FFractureEditorModeToolkit* FractureToolkit = (FFractureEditorModeToolkit*)Toolkit.Get();
		FractureToolkit->SetOutlinerComponents(SelectedGeometryComponents);
	}
}

void FFractureEditorMode::GetActorGlobalBounds(TArrayView<UGeometryCollectionComponent*> GeometryComponents, TMap<int32, FBox> &BoundsToBone) const
{
	for (UGeometryCollectionComponent* GeometryCollectionComponent : GeometryComponents)
	{
		FGeometryCollectionEdit RestCollection = GeometryCollectionComponent->EditRestCollection(GeometryCollection::EEditUpdate::None);
		UGeometryCollection* GeometryCollection = RestCollection.GetRestCollection();

		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollection->GetGeometryCollection();
		FGeometryCollection* OutGeometryCollection = GeometryCollectionPtr.Get();

		const TManagedArray<FTransform>& Transform = OutGeometryCollection->GetAttribute<FTransform>("Transform", FGeometryCollection::TransformGroup);
		const TManagedArray<FBox>& BoundingBox = OutGeometryCollection->GetAttribute<FBox>("BoundingBox", FGeometryCollection::GeometryGroup);
		const TManagedArray<int32>& TransformToGeometryIndex = OutGeometryCollection->GetAttribute<int32>("TransformToGeometryIndex", FGeometryCollection::TransformGroup);

		TManagedArray<FVector>* ExplodedVectorsPtr = OutGeometryCollection->FindAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);

		TArray<FTransform> Transforms;
		GeometryCollectionAlgo::GlobalMatrices(Transform, OutGeometryCollection->Parent, Transforms);

		BoundsToBone.Reset();

		if (ExplodedVectorsPtr)
		{
			TManagedArray<FVector>& ExplodedVectors = *ExplodedVectorsPtr;
			for (int32 Idx = 0, ni = GeometryCollection->NumElements(FGeometryCollection::TransformGroup); Idx < ni; ++Idx)
			{
				if (TransformToGeometryIndex[Idx] > -1)
				{
					const FVector& Offset = ExplodedVectors[Idx];
					const FBox& Bounds = BoundingBox[TransformToGeometryIndex[Idx]];
					BoundsToBone.Add(Idx, Bounds.ShiftBy(Offset).TransformBy(Transforms[Idx]));
				}
			}
		}
		else
		{
			for (int32 Idx = 0, ni = GeometryCollection->NumElements(FGeometryCollection::TransformGroup); Idx < ni; ++Idx)
			{
				if (TransformToGeometryIndex[Idx] > -1)
				{
					BoundsToBone.Add(Idx, BoundingBox[TransformToGeometryIndex[Idx]].TransformBy(Transforms[Idx]));
				}
			}
		}
	}
}

void FFractureEditorMode::SelectionStateChanged()
{

}

void FFractureEditorMode::HandlePackageReloaded(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent)
{
	if (InPackageReloadPhase == EPackageReloadPhase::PostPackageFixup)
	{
		// assemble referenced RestCollections
		TMap<const UGeometryCollection*, UGeometryCollectionComponent*> ReferencedRestCollections;
		for (UGeometryCollectionComponent* ExistingSelection : SelectedGeometryComponents)
		{
			ReferencedRestCollections.Add(TPair<const UGeometryCollection*, UGeometryCollectionComponent*>(ExistingSelection->GetRestCollection(), ExistingSelection));
		}

		// refresh outliner if reloaded package contains a referenced RestCollection
		for (const auto& RepointedObjectPair : InPackageReloadedEvent->GetRepointedObjects())
		{
			if (UGeometryCollection* NewObject = Cast<UGeometryCollection>(RepointedObjectPair.Value))
			{
				if (ReferencedRestCollections.Contains(NewObject))
				{
					if (Toolkit.IsValid())
					{
						FFractureEditorModeToolkit* FractureToolkit = (FFractureEditorModeToolkit*)Toolkit.Get();
						FFractureSelectionTools::ClearSelectedBones(ReferencedRestCollections[NewObject]);
						FractureToolkit->SetOutlinerComponents(SelectedGeometryComponents);
					}
				}
			}
		}
	}
}

FConvexVolume FFractureEditorMode::TranformFrustum(const FConvexVolume& InFrustum, const FMatrix& InMatrix)
{
	FConvexVolume NewFrustum;
	NewFrustum.Planes.Empty(6);

	for (int32 ii = 0, ni = InFrustum.Planes.Num() ; ii < ni ; ++ii)
	{
		NewFrustum.Planes.Add(InFrustum.Planes[ii].TransformBy(InMatrix));
	}

	NewFrustum.Init();

	return NewFrustum;
}

FConvexVolume FFractureEditorMode::GetVolumeFromBox(const FBox &InBox)
{
	FConvexVolume ConvexVolume;
	ConvexVolume.Planes.Empty(6);

	ConvexVolume.Planes.Add(FPlane(FVector::LeftVector, -InBox.Min.Y));
	ConvexVolume.Planes.Add(FPlane(FVector::RightVector, InBox.Max.Y));
	ConvexVolume.Planes.Add(FPlane(FVector::UpVector, InBox.Max.Z));
	ConvexVolume.Planes.Add(FPlane(FVector::DownVector, -InBox.Min.Z));
	ConvexVolume.Planes.Add(FPlane(FVector::ForwardVector, InBox.Max.X));
	ConvexVolume.Planes.Add(FPlane(FVector::BackwardVector, -InBox.Min.X));

	ConvexVolume.Init();

	return ConvexVolume;
}

#undef LOCTEXT_NAMESPACE


