// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollectionCommandCommon.h"
#include "IMeshEditorModeEditingContract.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "PackageTools.h"
#include "Layers/LayersSubsystem.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "EditableMeshFactory.h"
#include "EditorSupportDelegates.h"
#include "SceneOutlinerDelegates.h"
#include "MeshFractureSettings.h"
#include "AssetRegistryModule.h"
// #include "GeometryCollection/GeometryCollectionFactory.h"
#include "GeometryCollection/GeometryCollectionConversion.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
// #include "GeometryCollection/GeometryCollectionFactory.h"
#include "AssetToolsModule.h"
#include "FractureToolDelegates.h"

#define LOCTEXT_NAMESPACE "LogGeometryCommandCommon"

DEFINE_LOG_CATEGORY(LogGeometryCommandCommon);

namespace CommandCommon
{
	static TArray<AActor*> GetSelectedActors()
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

	static ULevel* GetSelectedLevel()
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

	static AActor* AddActor(ULevel* InLevel, UClass* Class)
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

} // namespace CommandCommon

// Note that this isn't really creating an actor representing the source mesh, but is only copying over the materials and transform.  This
// is used in conjunction with other methods for filling the resulting Actor's GeometryCollection (ie: Clustering operations, fracturing, etc)
// #todo(dmp): at some point we should consider refactoring or renaming this.
/*
AGeometryCollectionActor* FGeometryCollectionCommandCommon::CreateNewGeometryActor(const FString& Name, const FTransform& Transform, UEditableMesh* SourceMesh, bool AddMaterials)
{
	// create an asset package first
	FString NewPackageName = FPackageName::FilenameToLongPackageName(FPaths::ProjectContentDir() + Name);

	FString UniquePackageName;
	FString UniqueAssetName;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(NewPackageName, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* Package = CreatePackage(*UniquePackageName);
	UGeometryCollection* GeometryCollection = static_cast<UGeometryCollection*>(
		UGeometryCollectionFactory::StaticFactoryCreateNew(UGeometryCollection::StaticClass(), Package,
			FName(*UniqueAssetName), RF_Standalone | RF_Public, NULL, GWarn));

	// Create the new Geometry Collection actor
	AGeometryCollectionActor* NewActor = Cast<AGeometryCollectionActor>(CommandCommon::AddActor(CommandCommon::GetSelectedLevel(), AGeometryCollectionActor::StaticClass()));
	check(NewActor->GetGeometryCollectionComponent());

	// Set the Geometry Collection asset in the new actor
	NewActor->GetGeometryCollectionComponent()->SetRestCollection(GeometryCollection);

	// copy transform of original static mesh actor to this new actor
	NewActor->SetActorLabel(Name);
	NewActor->SetActorTransform(Transform);

	// Next steps actually fill material slots

	if (AddMaterials)
	{
		// copy the original material(s) across
		TPolygonGroupAttributesConstRef<FName> MaterialSlotNames = SourceMesh->GetMeshDescription()->PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::MaterialAssetName);

		int CurrSlot = 0;
		if (MaterialSlotNames.GetNumElements() > 0)
		{
			for (const FPolygonGroupID PolygonGroupID : SourceMesh->GetMeshDescription()->PolygonGroups().GetElementIDs())
			{
				FString MaterialName = MaterialSlotNames[PolygonGroupID].ToString();
				UMaterialInterface* OriginalMaterial = LoadObject<UMaterialInterface>(nullptr, *MaterialName);

				if (OriginalMaterial)
				{
					// sync materials on the UObject
					GeometryCollection->Materials.Add(OriginalMaterial);
				}
			}
		}
	}

	// Mark relevant stuff dirty
	FAssetRegistryModule::AssetCreated(GeometryCollection);
	GeometryCollection->MarkPackageDirty();
	Package->SetDirtyFlag(true);

	return NewActor;
}*/

void FGeometryCollectionCommandCommon::RemoveActor(AActor* Actor)
{
	UWorld* World = CommandCommon::GetSelectedLevel()->OwningWorld;
	GEditor->SelectActor(Actor, false, true);
	bool ItWorked = World->DestroyActor(Actor, true, true);
}


void FGeometryCollectionCommandCommon::LogHierarchy(const UGeometryCollection* GeometryCollectionObject)
{
	if (GeometryCollectionObject)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{

			UE_LOG(LogGeometryCommandCommon, Log, TEXT("Sizes: VerticesGroup %d, FacesGroup %d, GeometryGroup %d, TransformGroup %d"),
				GeometryCollection->NumElements(FGeometryCollection::VerticesGroup),
				GeometryCollection->NumElements(FGeometryCollection::FacesGroup),
				GeometryCollection->NumElements(FGeometryCollection::GeometryGroup),
				GeometryCollection->NumElements(FGeometryCollection::TransformGroup));

			const TManagedArray<FVector>& ExplodedVectors = GeometryCollection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
			const TManagedArray<FTransform>& Transforms = GeometryCollection->Transform;
			const TManagedArray<FTransform>& ExplodedTransforms = GeometryCollection->GetAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);
			const TManagedArray<FString>& BoneNames = GeometryCollection->BoneName;
			const TManagedArray<int32>& Level = GeometryCollection->GetAttribute<int32>("Level", FGeometryCollection::TransformGroup);
			const TManagedArray<int32>& Parent = GeometryCollection->Parent;
			const TManagedArray<TSet<int32>>& Children = GeometryCollection->Children;

			for (int BoneIndex = 0; BoneIndex < Parent.Num(); BoneIndex++)
			{
				const FTransform& Transform = Transforms[BoneIndex];
				const FVector& LocalLocation = ExplodedTransforms[BoneIndex].GetLocation();

				UE_LOG(LogGeometryCommandCommon, Log, TEXT("Location %3.2f, %3.2f, %3.2f"), Transform.GetLocation().X, Transform.GetLocation().Y, Transform.GetLocation().Z);
				UE_LOG(LogGeometryCommandCommon, Log, TEXT("Scaling %3.2f, %3.2f, %3.2f"), Transform.GetScale3D().X, Transform.GetScale3D().Y, Transform.GetScale3D().Z);
				UE_LOG(LogGeometryCommandCommon, Log, TEXT("Local Location %3.2f, %3.2f, %3.2f"), LocalLocation.X, LocalLocation.Y, LocalLocation.Z);

				const FVector& Vector = ExplodedVectors[BoneIndex];
				UE_LOG(LogGeometryCommandCommon, Log, TEXT("BoneID %d, Name %s, Level %d, IsGeometry %d, ParentBoneID %d, Offset (%3.2f, %3.2f, %3.2f), Vector (%3.2f, %3.2f, %3.2f)"),
					BoneIndex, BoneNames[BoneIndex].GetCharArray().GetData(), Level[BoneIndex], GeometryCollection->IsGeometry(BoneIndex), Parent[BoneIndex], LocalLocation.X, LocalLocation.Y, LocalLocation.Z, Vector.X, Vector.Y, Vector.Z);

				for (const int32 & ChildIndex : Children[BoneIndex])
				{
					UE_LOG(LogGeometryCommandCommon, Log, TEXT("..ChildBoneID %d"), ChildIndex);
				}

			}
		}
	}
}

void FGeometryCollectionCommandCommon::UpdateExplodedView(class IMeshEditorModeEditingContract &MeshEditorMode, EViewResetType ResetType)
{
	// Update the exploded view in the UI based on the current exploded view slider position
	FFractureToolDelegates::Get().OnUpdateExplodedView.Broadcast(static_cast<uint8>(ResetType), static_cast<uint8>(MeshEditorMode.GetFractureSettings()->CommonSettings->ViewMode));

	SceneOutliner::FSceneOutlinerDelegates::Get().OnComponentsUpdated.Broadcast();
}

UGeometryCollectionComponent* FGeometryCollectionCommandCommon::GetGeometryCollectionComponent(UEditableMesh* SourceMesh)
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

class UStaticMesh* FGeometryCollectionCommandCommon::GetStaticMesh(UEditableMesh* SourceMesh)
{
	check(SourceMesh);
	const FEditableMeshSubMeshAddress& SubMeshAddress = SourceMesh->GetSubMeshAddress();
	return Cast<UStaticMesh>(static_cast<UObject*>(SubMeshAddress.MeshObjectPtr));
}

AActor* FGeometryCollectionCommandCommon::GetEditableMeshActor(UEditableMesh* EditableMesh)
{
	AActor* ReturnActor = nullptr;
	const TArray<AActor*>& SelectedActors = CommandCommon::GetSelectedActors();

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

UEditableMesh* FGeometryCollectionCommandCommon::GetEditableMeshForActor(AActor* Actor, TArray<UEditableMesh *>& SelectedMeshes)
{
	check(Actor);
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents(PrimitiveComponents);
	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		FEditableMeshSubMeshAddress SubMeshAddress = UEditableMeshFactory::MakeSubmeshAddress(Component, 0);

		for (UEditableMesh* EditableMesh : SelectedMeshes)
		{
			if (EditableMesh->GetSubMeshAddress() == SubMeshAddress)
			{
				return EditableMesh;
			}
		}
	}

	return nullptr;
}

UEditableMesh* FGeometryCollectionCommandCommon::GetEditableMeshForComponent(UActorComponent* ActorComponent, TArray<UEditableMesh *>& SelectedMeshes)
{
	check(ActorComponent);
	UPrimitiveComponent* Component = CastChecked<UPrimitiveComponent>(ActorComponent);
	FEditableMeshSubMeshAddress SubMeshAddress = UEditableMeshFactory::MakeSubmeshAddress(Component, 0);

	for (UEditableMesh* EditableMesh : SelectedMeshes)
	{
		if (EditableMesh->GetSubMeshAddress() == SubMeshAddress)
		{
			return EditableMesh;
		}
	}

	return nullptr;
}

/*
UPackage* FGeometryCollectionCommandCommon::CreateGeometryCollectionPackage(UGeometryCollection*& GeometryCollection)
{
	UPackage* Package = CreatePackage(TEXT("/Game/GeometryCollectionAsset"));
	GeometryCollection = static_cast<UGeometryCollection*>(
		UGeometryCollectionFactory::StaticFactoryCreateNew(UGeometryCollection::StaticClass(), Package,
			FName("GeometryCollectionAsset"), RF_Standalone | RF_Public, NULL, GWarn));		
	return Package;
}
*/

void FGeometryCollectionCommandCommon::AddSingleRootNodeIfRequired(UGeometryCollection* GeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
	{
		if (FGeometryCollectionClusteringUtility::ContainsMultipleRootBones(GeometryCollection))
		{
			FGeometryCollectionClusteringUtility::ClusterAllBonesUnderNewRoot(GeometryCollection);
		}
	}
}

void FGeometryCollectionCommandCommon::AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject)
{
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
	{
		if (!GeometryCollection->HasAttribute("ExplodedVector", FGeometryCollection::TransformGroup))
		{
			GeometryCollection->AddAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);
			GeometryCollection->AddAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);

			TManagedArray<FTransform>& ExplodedTransforms = GeometryCollection->GetAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);
			TManagedArray<FVector>& ExplodedVectors = GeometryCollection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);

			for (int Idx = 0; Idx < GeometryCollection->NumElements(FGeometryCollection::TransformGroup); Idx++)
			{
				ExplodedVectors[Idx] = GeometryCollection->Transform[Idx].GetLocation();
				ExplodedTransforms[Idx] = GeometryCollection->Transform[Idx];
			}
		}
		if (!GeometryCollection->HasAttribute("Level", FGeometryCollection::TransformGroup))
		{
			FGeometryCollectionClusteringUtility::UpdateHierarchyLevelOfChildren(GeometryCollection, -1);
		}
	}
}

int FGeometryCollectionCommandCommon::GetRootBone(const UGeometryCollection* GeometryCollectionObject)
{
	// Note - it is possible for their to be 2 roots briefly since FGeometryCollectionConversion::AppendStaticMesh puts new
	// geometry at the root, but this is very quickly fixed up in those situations, see AppendMeshesToGeometryCollection
	TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
	if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
	{
		TArray<int32> RootBones;
		FGeometryCollectionClusteringUtility::GetRootBones(GeometryCollection, RootBones);
		return RootBones[0];
	}
	check(false);
	return -1;
}

void FGeometryCollectionCommandCommon::AppendMeshesToGeometryCollection(TArray<AActor*>& SelectedActors, TArray<UEditableMesh*>& SelectedMeshes, UEditableMesh* SourceMesh, FTransform &SourceActorTransform, UGeometryCollection* GeometryCollectionObject, bool DeleteSourceMesh, TArray<int32>& OutNewNodeElements)
{
	if (GeometryCollectionObject)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{

			AddAdditionalAttributesIfRequired(GeometryCollectionObject);

			for (AActor* SelectedActor : SelectedActors)
			{
				UEditableMesh* EditableMesh = GetEditableMeshForActor(SelectedActor, SelectedMeshes);

				// don't want to add duplicate of itself
				if (EditableMesh == SourceMesh)
					continue;

				FTransform MeshTransform = FTransform::Identity;

				TManagedArray<FTransform>& ExplodedTransforms = GeometryCollection->GetAttribute<FTransform>("ExplodedTransform", FGeometryCollection::TransformGroup);
				TManagedArray<FVector>& ExplodedVectors = GeometryCollection->GetAttribute<FVector>("ExplodedVector", FGeometryCollection::TransformGroup);

				TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
				SelectedActor->GetComponents(PrimitiveComponents);
				for (UPrimitiveComponent* Component : PrimitiveComponents)
				{
					bool ValidComponent = false;

					if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Component))
					{
						MeshTransform = StaticMeshComp->GetComponentTransform();
						MeshTransform = MeshTransform.GetRelativeTransform(SourceActorTransform);

						FGeometryCollectionConversion::AppendStaticMesh(StaticMeshComp->GetStaticMesh(), StaticMeshComp, MeshTransform, GeometryCollectionObject, false);
						ValidComponent = true;
					}
					else if (UGeometryCollectionComponent* GeometryCollectionComponent = Cast<UGeometryCollectionComponent>(Component))
					{
						MeshTransform = GeometryCollectionComponent->GetComponentTransform();
						MeshTransform = MeshTransform.GetRelativeTransform(SourceActorTransform);

						const UGeometryCollection* OtherGeometryCollection = GeometryCollectionComponent->GetRestCollection();
						GeometryCollectionObject->AppendGeometry(*OtherGeometryCollection, false);
						ValidComponent = true;
					}
					if (ValidComponent)
					{
						// fix up the additional information required by fracture UI slider
						int LastElement = GeometryCollection->NumElements(FGeometryCollection::TransformGroup) - 1;
						(GeometryCollection->Transform)[LastElement] = MeshTransform;
						ExplodedVectors[LastElement] = MeshTransform.GetLocation();
						ExplodedTransforms[LastElement] = MeshTransform;
						TManagedArray<FString>& BoneName = GeometryCollection->BoneName;
						BoneName[LastElement] = "Root";

						OutNewNodeElements.Add(LastElement);
					}
				}

				if (DeleteSourceMesh)
				{
					RemoveActor(SelectedActor);
				}
			}

		}
	}
}

void FGeometryCollectionCommandCommon::MergeSelections(const UGeometryCollectionComponent* SourceComponent, const TArray<int32>& SelectionB, TArray<int32>& MergedSelectionOut)
{
	if (SourceComponent)
	{
		if (SourceComponent->GetSelectedBones().Num() == 0)
		{
			// just select all bones in this case
			const UGeometryCollection* GeometryCollection = SourceComponent->GetRestCollection();
			int32 NumTransforms = GeometryCollection->GetGeometryCollection()->NumElements(FGeometryCollection::TransformGroup);
			for (int32 Idx = 0; Idx < NumTransforms; Idx++)
			{
				MergedSelectionOut.Add(Idx);
			}
		}
		else
		{
			for (int32 NewElement : SourceComponent->GetSelectedBones())
			{
				MergedSelectionOut.AddUnique(NewElement);
			}
		}
	}

	for (int32 NewElement : SelectionB)
	{
		MergedSelectionOut.AddUnique(NewElement);
	}
}

void FGeometryCollectionCommandCommon::GetCenterOfBone(UGeometryCollection* GeometryCollectionObject, int Element, FVector& CentreOut)
{
	if (GeometryCollectionObject)
	{
		TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe> GeometryCollectionPtr = GeometryCollectionObject->GetGeometryCollection();
		if (FGeometryCollection* GeometryCollection = GeometryCollectionPtr.Get())
		{
			TArray<FTransform> Transforms;
			GeometryCollectionAlgo::GlobalMatrices(GeometryCollection->Transform, GeometryCollection->Parent, Transforms);
			check(GeometryCollection);
			const TManagedArray<TSet<int32>>& ChildrenArray = GeometryCollection->Children;

			FVector SumCOM(0, 0, 0);
			int Count = 0;
			CombineCenterOfGeometryRecursive(GeometryCollection, Transforms, ChildrenArray, Element, SumCOM, Count);

			if (Count > 0)
			{
				SumCOM /= Count;
			}

			CentreOut = SumCOM;
		}
	}
}

void FGeometryCollectionCommandCommon::CombineCenterOfGeometryRecursive(const FGeometryCollection* GeometryCollection, TArray<FTransform>& Transforms, const TManagedArray<TSet<int32>>& ChildrenArray, int Element, FVector& SumCOMOut, int& CountOut)
{
	if (GeometryCollection->IsGeometry(Element))
	{
		SumCOMOut += Transforms[Element].GetLocation();
		CountOut++;
	}

	for (int ChildElement : ChildrenArray[Element])
	{
		CombineCenterOfGeometryRecursive(GeometryCollection, Transforms, ChildrenArray, ChildElement, SumCOMOut, CountOut);
	}
}

TArray<AActor*> FGeometryCollectionCommandCommon::GetSelectedActors()
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

#undef LOCTEXT_NAMESPACE
