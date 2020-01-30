// Copyright Epic Games, Inc. All Rights Reserved. 

#include "FractureToolComponent.h"
#include "Async/ParallelFor.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "EditableMesh.h"
#include "MeshFractureSettings.h"

#include "GeometryCollection/GeometryCollectionActor.h"
#include "Engine/Selection.h"
#include "Editor.h"
#include "EditableMeshFactory.h"
#include "Materials/Material.h"
#include "FractureToolDelegates.h"
#include "EditorSupportDelegates.h"
#include "SceneOutlinerDelegates.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(UFractureToolComponentLogging, NoLogging, All);

bool UFractureToolComponent::bInFractureMode = true;

UFractureToolComponent::UFractureToolComponent(const FObjectInitializer& ObjectInitializer) : ShowBoneColors(true), bInMeshEditorMode(false)
{
}

void UFractureToolComponent::OnRegister()
{
	bInMeshEditorMode = true;
	Super::OnRegister();

	FFractureToolDelegates::Get().OnFractureExpansionEnd.AddUObject(this, &UFractureToolComponent::OnFractureExpansionEnd);
	FFractureToolDelegates::Get().OnFractureExpansionUpdate.AddUObject(this, &UFractureToolComponent::OnFractureExpansionUpdate);
	FFractureToolDelegates::Get().OnVisualizationSettingsChanged.AddUObject(this, &UFractureToolComponent::OnVisualisationSettingsChanged);
	FFractureToolDelegates::Get().OnUpdateExplodedView.AddUObject(this, &UFractureToolComponent::OnUpdateExplodedView);
	FFractureToolDelegates::Get().OnUpdateFractureLevelView.AddUObject(this, &UFractureToolComponent::OnUpdateFractureLevelView);
}


void UFractureToolComponent::OnUnregister()
{
	bInMeshEditorMode = false;
	Super::OnUnregister();

	FFractureToolDelegates::Get().OnFractureExpansionEnd.RemoveAll(this);
	FFractureToolDelegates::Get().OnFractureExpansionUpdate.RemoveAll(this);
	FFractureToolDelegates::Get().OnVisualizationSettingsChanged.RemoveAll(this);
	FFractureToolDelegates::Get().OnUpdateExplodedView.RemoveAll(this);
	FFractureToolDelegates::Get().OnUpdateFractureLevelView.RemoveAll(this);

	LeaveFracturingCleanup();
}

void UFractureToolComponent::OnFractureExpansionEnd()
{
	UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent();
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
		EditBoneColor.SetShowBoneColors(ShowBoneColors & bInMeshEditorMode & bInFractureMode);
	}
}

void UFractureToolComponent::OnFractureExpansionUpdate()
{
	UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent();
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
		EditBoneColor.SetShowBoneColors(ShowBoneColors);
	}
}

void UFractureToolComponent::OnVisualisationSettingsChanged(bool ShowBoneColorsIn)
{
	UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent();
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
		EditBoneColor.SetShowBoneColors(ShowBoneColorsIn);
		ShowBoneColors = ShowBoneColorsIn;
	}
}

void UFractureToolComponent::OnFractureLevelChanged(uint8 ViewLevelIn)
{
	UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent();
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
		EditBoneColor.SetLevelViewMode(ViewLevelIn - 1);

		const UGeometryCollection* RestCollection = GeometryCollectionComponent->GetRestCollection();
		if (RestCollection)
		{
			// Reset the selected bones as previous selection most likely won't make sense after changing the actively viewed level
			UEditableMesh* EditableMesh = Cast<UEditableMesh>(RestCollection->EditableMesh);
			if (EditableMesh)
			{
				EditBoneColor.ResetBoneSelection();
			}
		}
	}
}

void UFractureToolComponent::UpdateBoneState(UPrimitiveComponent* Component)
{
	UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(Component);
	if (GeometryCollectionComponent)
	{
		// this scoped method will refresh bone colors
		FScopedColorEdit EditBoneColor( GeometryCollectionComponent, true );
	}
}

void UFractureToolComponent::SetSelectedBones(UEditableMesh* EditableMesh, int32 BoneSelected, bool Multiselection, bool ShowBoneColorsIn)
{
	UGeometryCollectionComponent* GeometryCollectionComponent = GetGeometryCollectionComponent(EditableMesh);
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();

		if (UGeometryCollection* MeshGeometryCollection = GetGeometryCollection(EditableMesh))
		{
			TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = MeshGeometryCollection->GetGeometryCollection();
			if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
			{

				// has the color mode been toggled
				ShowBoneColors = ShowBoneColorsIn;
				if (EditBoneColor.GetShowBoneColors() != ShowBoneColors)
				{
					EditBoneColor.SetShowBoneColors(ShowBoneColors);
				}
				EditBoneColor.SetEnableBoneSelection(true);
				bool BoneWasAlreadySelected = EditBoneColor.IsBoneSelected(BoneSelected);

				// if multiselect then append new BoneSelected to what is already selected, otherwise we just clear and replace the old selection with BoneSelected
				if (!Multiselection)
				{
					EditBoneColor.ResetBoneSelection();
				}

				// toggle the bone selection
				if (BoneWasAlreadySelected)
				{
					EditBoneColor.ClearSelectedBone(BoneSelected);
				}
				else
				{
					EditBoneColor.AddSelectedBone(BoneSelected);
				}

				// The actual selection made is based on the hierarchy and the view mode
				if (GeometryCollection)
				{
					const TArray<int32>& Selected = EditBoneColor.GetSelectedBones();
					TArray<int32> RevisedSelected;
					TArray<int32> Highlighted;
					FGeometryCollectionClusteringUtility::ContextBasedClusterSelection(GeometryCollection, EditBoneColor.GetViewLevel(), Selected, RevisedSelected, Highlighted);
					EditBoneColor.SetSelectedBones(RevisedSelected);
					EditBoneColor.SetHighlightedBones(Highlighted);

					SceneOutliner::FSceneOutlinerDelegates::Get().OnComponentSelectionChanged.Broadcast(GeometryCollectionComponent);
				}
			}

			FEditorSupportDelegates::RedrawAllViewports.Broadcast();

		}
	}
}


void UFractureToolComponent::OnSelected(UPrimitiveComponent* SelectedComponent)
{
	UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(SelectedComponent);
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();

		EditBoneColor.SetShowBoneColors(ShowBoneColors);
		EditBoneColor.SetEnableBoneSelection(true);
	}
	if (bInMeshEditorMode && bInFractureMode)
	{
		FFractureToolDelegates::Get().OnUpdateExplodedView.Broadcast(static_cast<uint8>(EViewResetType::RESET_TRANSFORMS), static_cast<uint8>(0));
	}
}

void UFractureToolComponent::OnDeselected(UPrimitiveComponent* DeselectedComponent)
{
	UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(DeselectedComponent);
	if (GeometryCollectionComponent)
	{
		FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();

		EditBoneColor.SetShowBoneColors(false);
		EditBoneColor.SetEnableBoneSelection(false);
	}
}

void UFractureToolComponent::OnEnterFractureMode()
{
	bInFractureMode = true;
	for(const AActor* SelectedActor : GetSelectedActors())
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		SelectedActor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			OnSelected(Component);
		}
	}
}

void UFractureToolComponent::OnExitFractureMode()
{
	bInFractureMode = false;
	// find all the selected geometry collections and turn off color rendering mode
	for (const AActor* SelectedActor : GetSelectedActors())
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		SelectedActor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			OnDeselected(Component);
		}
	}
	LeaveFracturingCleanup();
}

TArray<AActor*> UFractureToolComponent::GetSelectedActors() const
{
	USelection* SelectedActors = GEditor->GetSelectedActors();
	TArray<AActor*> Actors;
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (Actor)
		{
			Actors.Add(Actor);
		}
	}
	return Actors;
}

AActor* UFractureToolComponent::GetEditableMeshActor(UEditableMesh* EditableMesh)
{
	AActor* ReturnActor = nullptr;
	const TArray<AActor*>& SelectedActors = GetSelectedActors();

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	for (int32 i = 0; i < SelectedActors.Num(); i++)
	{
		SelectedActors[i]->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			FEditableMeshSubMeshAddress SubMeshAddress = UEditableMeshFactory::MakeSubmeshAddress(Component, 0);

			if (EditableMesh->GetSubMeshAddress() == SubMeshAddress)
			{
				ReturnActor = Component->GetOwner();
				break;
			}
		}
		PrimitiveComponents.Reset();
	}

	return ReturnActor;
}

AActor* UFractureToolComponent::GetEditableMeshActor()
{
	AActor* ReturnActor = nullptr;
	const TArray<AActor*>& SelectedActors = GetSelectedActors();

	for (int32 i = 0; i < SelectedActors.Num(); i++)
	{
		if (UPrimitiveComponent* Component = SelectedActors[i]->FindComponentByClass<UPrimitiveComponent>())
		{
			ReturnActor = Component->GetOwner();
			break;
		}
	}

	return ReturnActor;
}

UGeometryCollectionComponent* UFractureToolComponent::GetGeometryCollectionComponent(UEditableMesh* SourceMesh)
{
	check(SourceMesh);
	UGeometryCollectionComponent* GeometryCollectionComponent = nullptr;
	AActor* Actor = GetEditableMeshActor(SourceMesh);
	AGeometryCollectionActor* GeometryCollectionActor = Cast<AGeometryCollectionActor>(Actor);
	if (GeometryCollectionActor)
	{
		GeometryCollectionComponent = GeometryCollectionActor->GetGeometryCollectionComponent();
	}

	return GeometryCollectionComponent;
}

UGeometryCollectionComponent* UFractureToolComponent::GetGeometryCollectionComponent()
{
	UGeometryCollectionComponent* GeometryCollectionComponent = nullptr;
	AActor* Actor = GetEditableMeshActor();
	AGeometryCollectionActor* GeometryCollectionActor = Cast<AGeometryCollectionActor>(Actor);
	if (GeometryCollectionActor)
	{
		GeometryCollectionComponent = GeometryCollectionActor->GetGeometryCollectionComponent();
	}

	return GeometryCollectionComponent;
}

void UFractureToolComponent::LeaveFracturingCleanup()
{
	TArray<AActor*> ActorList;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGeometryCollectionActor::StaticClass(), ActorList);

	if (ActorList.Num() == 0)
	{
		return;
	}

	float OldExpansion = UMeshFractureSettings::ExplodedViewExpansion;
	UMeshFractureSettings::ExplodedViewExpansion = 0.0f;

	EMeshFractureLevel FractureLevel = static_cast<EMeshFractureLevel>(0);
	EViewResetType ResetType = static_cast<EViewResetType>(0);
	EExplodedViewMode ViewMode = EExplodedViewMode::Linear;

	for (AActor* Actor : ActorList)
	{
		AGeometryCollectionActor* GeometryActor = Cast<AGeometryCollectionActor>(Actor);

		if (GeometryActor)
		{
			// hide the bones
			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
			Actor->GetComponents(PrimitiveComponents);
			for (UPrimitiveComponent* Component : PrimitiveComponents)
			{
				UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(Component);
				if (GeometryCollectionComponent)
				{
					FScopedColorEdit EditBoneColor = GeometryCollectionComponent->EditBoneSelection();
					EditBoneColor.SetShowBoneColors(false);
					EditBoneColor.SetEnableBoneSelection(false);
				}
			}

			check(GeometryActor->GeometryCollectionComponent);
			if (HasExplodedAttributes(GeometryActor))
			{
				ExplodeInLevels(GeometryActor);
			}
			GeometryActor->GeometryCollectionComponent->MarkRenderStateDirty();
		}
	}

	FFractureToolDelegates::Get().OnFractureExpansionEnd.Broadcast();
	FEditorSupportDelegates::RedrawAllViewports.Broadcast();
	UMeshFractureSettings::ExplodedViewExpansion = OldExpansion;
}

UGeometryCollection* UFractureToolComponent::GetGeometryCollection(const UEditableMesh* SourceMesh)
{
	check(SourceMesh);
	const FEditableMeshSubMeshAddress& SubMeshAddress = SourceMesh->GetSubMeshAddress();
	return Cast<UGeometryCollection>(static_cast<UObject*>(SubMeshAddress.MeshObjectPtr));
}

void UFractureToolComponent::OnUpdateFractureLevelView(uint8 FractureLevel)
{
	TArray<AActor*> ActorList = GetSelectedActors();

	for (int ActorIndex = 0; ActorIndex < ActorList.Num(); ActorIndex++)
	{
		AGeometryCollectionActor* GeometryActor = Cast<AGeometryCollectionActor>(ActorList[ActorIndex]);

		if (GeometryActor)
		{
			check(GeometryActor->GeometryCollectionComponent);
			FGeometryCollectionEdit GeometryCollectionEdit = GeometryActor->GeometryCollectionComponent->EditRestCollection();
			UGeometryCollection* GeometryCollection = GeometryCollectionEdit.GetRestCollection();
			check(GeometryCollection);
			GeometryActor->GeometryCollectionComponent->MarkRenderStateDirty();
		}
	}

	OnUpdateExplodedView(static_cast<uint8>(EViewResetType::RESET_TRANSFORMS), FractureLevel);

	// Visualization parameters have been modified
	OnFractureLevelChanged(FractureLevel);
}

void UFractureToolComponent::OnUpdateExplodedView(uint8 ResetTypeIn, uint8 FractureLevelIn) const
{
	EMeshFractureLevel FractureLevel = static_cast<EMeshFractureLevel>(FractureLevelIn);
	EViewResetType ResetType = static_cast<EViewResetType>(ResetTypeIn);
	const TArray<AActor*> ActorList = GetSelectedActors();

	// when viewing individual fracture levels we use straight forward explosion algorithm
	EExplodedViewMode ViewMode = EExplodedViewMode::Linear;

	// when viewing all pieces, let the expansion happen one level at a time
	if (FractureLevel == EMeshFractureLevel::AllLevels)
		ViewMode = EExplodedViewMode::SplitLevels;

	for (int ActorIndex = 0; ActorIndex < ActorList.Num(); ActorIndex++)
	{
		AGeometryCollectionActor* GeometryActor = Cast<AGeometryCollectionActor>(ActorList[ActorIndex]);
		if (GeometryActor)
		{
			check(GeometryActor->GeometryCollectionComponent);
			if (!HasExplodedAttributes(GeometryActor))
				continue;

			switch (ViewMode)
			{
			case EExplodedViewMode::SplitLevels:
				ExplodeInLevels(GeometryActor);
				break;

			case EExplodedViewMode::Linear:
			default:
				ExplodeLinearly(GeometryActor, FractureLevel);
				break;
			}

			GeometryActor->GeometryCollectionComponent->MarkRenderStateDirty();
		}
	}

	if (ResetType == EViewResetType::RESET_ALL)
	{
		// Force an update using the output GeometryCollection which may not have existed before the fracture
		FFractureToolDelegates::Get().OnFractureExpansionEnd.Broadcast();
	}
	else
	{
		// only the transforms will have updated
		FFractureToolDelegates::Get().OnFractureExpansionUpdate.Broadcast();
	}

	FEditorSupportDelegates::RedrawAllViewports.Broadcast();
}

void UFractureToolComponent::ExplodeInLevels(AGeometryCollectionActor* GeometryActor) const
{
	check(GeometryActor->GeometryCollectionComponent);
	FGeometryCollectionEdit GeometryCollectionEdit = GeometryActor->GeometryCollectionComponent->EditRestCollection();
	UGeometryCollection* GeometryCollection = GeometryCollectionEdit.GetRestCollection();

	if (GeometryCollection)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollection->GetGeometryCollection();
		if (FGeometryCollection* Collection = GeometryCollectionPtr.Get())
		{
			float ComponentScaling = CalculateComponentScaling(GeometryActor->GeometryCollectionComponent);  // TODO: This also resets all transforms, but the root transform is not set back to its correct value in the loop below!

			TManagedArray<FTransform> & Transform = Collection->Transform;
			TManagedArray<FVector>& ExplodedVectors = Collection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
			TManagedArray<FTransform>& ExplodedTransforms = Collection->GetAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);
			TManagedArray<int32>& Levels = Collection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);


			int32 NumTransforms = Collection->NumElements(FGeometryCollection::TransformGroup);
			int32 MaxFractureLevel = -1;
			for (int i = 0; i < NumTransforms; i++)
			{
				if (Levels[i] > MaxFractureLevel)
					MaxFractureLevel = Levels[i];
			}

			for (int Level = 1; Level <= MaxFractureLevel; Level++)
			{
				for (int t = 0; t < NumTransforms; t++)
				{
					if (Levels[t] != Level)
						continue;

					int32 FractureLevel = Level - 1;
					if (FractureLevel >= 0)
					{
						if (FractureLevel > 7)
							FractureLevel = 7;

						// smaller chunks appear to explode later than their parents
						float UseVal = FMath::Max(0.0f, UMeshFractureSettings::ExplodedViewExpansion - 0.1f * FractureLevel);

						// due to the fact that the levels break later the overall range gets shorter
						// so compensate for this making the later fragments move farther/faster than the earlier ones
						UseVal *= (0.95f / (1.0f - 0.1f * FractureLevel));

						for (int i = 0; i < FractureLevel; i++)
						{
							UseVal *= UseVal;
						}
						FVector NewPos = ExplodedTransforms[t].GetLocation() + ComponentScaling * ExplodedVectors[t] * UseVal;
						Transform[t].SetLocation(NewPos);
					}
				}
			}
		}
	}
}

void UFractureToolComponent::ExplodeLinearly(AGeometryCollectionActor* GeometryActor, EMeshFractureLevel FractureLevel) const
{
	check(GeometryActor->GeometryCollectionComponent);
	FGeometryCollectionEdit GeometryCollectionEdit = GeometryActor->GeometryCollectionComponent->EditRestCollection();
	UGeometryCollection* GeometryCollection = GeometryCollectionEdit.GetRestCollection();

	if (GeometryCollection)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollection->GetGeometryCollection();
		if (FGeometryCollection* Collection = GeometryCollectionPtr.Get())
		{
			float ComponentScaling = CalculateComponentScaling(GeometryActor->GeometryCollectionComponent);  // TODO: This also resets all transforms, but the root transform is not set back to its correct value in the loop below!

			const TManagedArray<FVector>& ExplodedVectors = Collection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
			const TManagedArray<FTransform>& ExplodedTransforms = Collection->GetAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);

			TManagedArray<FTransform>& Transform = Collection->Transform;
			TManagedArray<int32>& Levels = Collection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);


			int32 NumTransforms = Collection->NumElements(FGeometryCollection::TransformGroup);
			int32 FractureLevelNumber = static_cast<int8>(FractureLevel) - static_cast<int8>(EMeshFractureLevel::Level0);
			int32 MaxFractureLevel = FractureLevelNumber;
			for (int i = 0; i < NumTransforms; i++)
			{
				if (Levels[i] > MaxFractureLevel)
					MaxFractureLevel = Levels[i];
			}

			for (int Level = 1; Level <= MaxFractureLevel; Level++)
			{
				for (int t = 0; t < NumTransforms; t++)
				{
					if (Levels[t] == FractureLevelNumber)
					{
						FVector NewPos = ExplodedTransforms[t].GetLocation() + ComponentScaling * ExplodedVectors[t] * UMeshFractureSettings::ExplodedViewExpansion;
						Transform[t].SetLocation(NewPos);
					}
					else
					{
						FVector NewPos = ExplodedTransforms[t].GetLocation();
						Transform[t].SetLocation(NewPos);
					}
				}
			}
		}
	}
}

float UFractureToolComponent::CalculateComponentScaling(UGeometryCollectionComponent* GeometryCollectionComponent) const
{
	FBoxSphereBounds Bounds;

	check(GeometryCollectionComponent);
	FGeometryCollectionEdit GeometryCollectionEdit = GeometryCollectionComponent->EditRestCollection();
	if (UGeometryCollection* GeometryCollectionObject = GeometryCollectionEdit.GetRestCollection())
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{
			// TODO: The transforms' locations should probably not be reset in here.
			//   And as they do, the root location shouldn't be cleared up since it never gets set back to a correct value after this.
			//   This could cause a shift on the root geometry whose vertices may already have been translated and will no longer align
			//   with its cluster's due to the now missing transform location information.

			// reset the transforms so the component is no longer exploded, otherwise get bounds of exploded state which is a moving target
			TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;

			for (int i = 0; i < Transforms.Num(); i++)
			{
				Transforms[i].SetLocation(FVector::ZeroVector);
			}
			Bounds = GeometryCollectionComponent->CalcBounds(FTransform::Identity);
		}
	}
	return Bounds.SphereRadius * 0.01f * 0.2f;
}

void UFractureToolComponent::ShowGeometry(class UGeometryCollection* GeometryCollectionObject, int Index, bool GeometryVisible, bool IncludeChildren)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
	{
		// #todo: the way the visibility is defined in the GeometryCollection makes this operation really slow - best if the visibility was at bone level

		TManagedArray<int32>& BoneMap = GeometryCollection->BoneMap;
		TManagedArray<FIntVector>&  Indices = GeometryCollection->Indices;
		TManagedArray<bool>&  Visible = GeometryCollection->Visible;


		for (int32 i = 0; i < Indices.Num(); i++)
		{

			if (BoneMap[Indices[i][0]] == Index || (IncludeChildren && BoneMap[Indices[i][0]] > Index))
			{
				Visible[i] = GeometryVisible;
			}
		}
	}
}

bool UFractureToolComponent::HasExplodedAttributes(AGeometryCollectionActor* GeometryActor) const
{
	if (GeometryActor &&  GeometryActor->GeometryCollectionComponent && GeometryActor->GeometryCollectionComponent->GetRestCollection() &&
		GeometryActor->GeometryCollectionComponent->GetRestCollection()->GetGeometryCollection())
	{
		return GeometryActor->GeometryCollectionComponent->GetRestCollection()->GetGeometryCollection()->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup);
	}
	return false;
}
