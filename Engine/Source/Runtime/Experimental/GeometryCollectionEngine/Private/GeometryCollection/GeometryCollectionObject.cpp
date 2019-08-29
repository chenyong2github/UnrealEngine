// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: UGeometryCollection methods.
=============================================================================*/
#include "GeometryCollection/GeometryCollectionObject.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionCache.h"
#include "UObject/DestructionObjectVersion.h"
#include "Serialization/ArchiveCountMem.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"
#include "Materials/MaterialInstance.h"

#if WITH_EDITOR
#include "GeometryCollection/DerivedDataGeometryCollectionCooker.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"
#endif

#include "GeometryCollection/GeometryCollectionSimulationCoreTypes.h"
#include "Chaos/ChaosArchive.h"

DEFINE_LOG_CATEGORY_STATIC(UGeometryCollectionLogging, NoLogging, All);

UGeometryCollection::UGeometryCollection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CollisionType(ECollisionTypeEnum::Chaos_Surface_Volumetric)
	, ImplicitType(EImplicitTypeEnum::Chaos_Implicit_Box)
	, MinLevelSetResolution(10)
	, MaxLevelSetResolution(10)
	, MinClusterLevelSetResolution(50)
	, MaxClusterLevelSetResolution(50)
	, CollisionObjectReductionPercentage(0.0f)
	, bMassAsDensity(false)
	, Mass(1.0f)
	, MinimumMassClamp(0.1f)
	, CollisionParticlesFraction(1.0f)
	, MaximumCollisionParticles(60)
	, EnableRemovePiecesOnFracture(false)
	, GeometryCollection(new FGeometryCollection())
{
	PersistentGuid = FGuid::NewGuid();
	InvalidateCollection();
}

FGeometryCollectionSizeSpecificData::FGeometryCollectionSizeSpecificData()
	: MaxSize(0.0f)
	, CollisionType(ECollisionTypeEnum::Chaos_Surface_Volumetric)
	, ImplicitType(EImplicitTypeEnum::Chaos_Implicit_Box)
	, MinLevelSetResolution(5)
	, MaxLevelSetResolution(10)
	, MinClusterLevelSetResolution(25)
	, MaxClusterLevelSetResolution(50)
	, CollisionObjectReductionPercentage(0.0f)
	, CollisionParticlesFraction(1.0f)
	, MaximumCollisionParticles(60)
{
}

void FillSharedSimulationSizeSpecificData(FSharedSimulationSizeSpecificData& ToData, const FGeometryCollectionSizeSpecificData& FromData)
{
	ToData.CollisionType = FromData.CollisionType;
	ToData.ImplicitType = FromData.ImplicitType;
	ToData.MaxSize = FromData.MaxSize;
	ToData.MinLevelSetResolution = FromData.MinLevelSetResolution;
	ToData.MaxLevelSetResolution = FromData.MaxLevelSetResolution;
	ToData.MinClusterLevelSetResolution = FromData.MinClusterLevelSetResolution;
	ToData.MaxClusterLevelSetResolution = FromData.MaxClusterLevelSetResolution;
	ToData.CollisionObjectReductionPercentage = FromData.CollisionObjectReductionPercentage;
	ToData.CollisionParticlesFraction = FromData.CollisionParticlesFraction;
	ToData.MaximumCollisionParticles = FromData.MaximumCollisionParticles;
}


float KgCm3ToKgM3(float Density)
{
	return Density * 1000000;
}

float KgM3ToKgCm3(float Density)
{
	return Density / 1000000;
}

void UGeometryCollection::GetSharedSimulationParams(FSharedSimulationParameters& OutParams) const
{
	OutParams.bMassAsDensity = bMassAsDensity;
	OutParams.Mass = bMassAsDensity ? KgM3ToKgCm3(Mass) : Mass;	//todo(ocohen): we still have the solver working in old units. This is mainly to fix ui issues. Long term need to normalize units for best precision
	OutParams.MinimumMassClamp = MinimumMassClamp;
	OutParams.MaximumCollisionParticleCount = MaximumCollisionParticles;

	FGeometryCollectionSizeSpecificData InfSize;
	InfSize.MaxSize = FLT_MAX;
	InfSize.CollisionType = CollisionType;
	InfSize.ImplicitType = ImplicitType;
	InfSize.MinLevelSetResolution = MinLevelSetResolution;
	InfSize.MaxLevelSetResolution = MaxLevelSetResolution;
	InfSize.MinClusterLevelSetResolution = MinClusterLevelSetResolution;
	InfSize.MaxClusterLevelSetResolution = MaxClusterLevelSetResolution;
	InfSize.CollisionObjectReductionPercentage = CollisionObjectReductionPercentage;
	InfSize.CollisionParticlesFraction = CollisionParticlesFraction;
	InfSize.MaximumCollisionParticles = MaximumCollisionParticles;

	OutParams.SizeSpecificData.SetNum(SizeSpecificData.Num() + 1);
	FillSharedSimulationSizeSpecificData(OutParams.SizeSpecificData[0], InfSize);

	for (int32 Idx = 0; Idx < SizeSpecificData.Num(); ++Idx)
	{
		FillSharedSimulationSizeSpecificData(OutParams.SizeSpecificData[Idx+1], SizeSpecificData[Idx]);
	}

	if (EnableRemovePiecesOnFracture)
	{
		FixupRemoveOnFractureMaterials(OutParams);
	}

	OutParams.SizeSpecificData.Sort();	//can we do this at editor time on post edit change?
}

void UGeometryCollection::FixupRemoveOnFractureMaterials(FSharedSimulationParameters& SharedParms) const
{
	// Match RemoveOnFracture materials with materials in model and record the material indices

	int32 NumMaterials = Materials.Num();
	for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; MaterialIndex++)
	{
		UMaterialInterface* MaterialInfo = Materials[MaterialIndex];

		for (int32 ROFMaterialIndex = 0; ROFMaterialIndex < RemoveOnFractureMaterials.Num(); ROFMaterialIndex++)
		{
			if (MaterialInfo == RemoveOnFractureMaterials[ROFMaterialIndex])
			{
				SharedParms.RemoveOnFractureIndices.Add(MaterialIndex);
				break;
			}
		}

	}
}

/** AppendGeometry */
int32 UGeometryCollection::AppendGeometry(const UGeometryCollection & Element, bool ReindexAllMaterials, const FTransform& TransformRoot)
{
	Modify();
	InvalidateCollection();

	// add all materials
	// if there are none, we assume all material assignments in Element are shared by this GeometryCollection
	// otherwise, we assume all assignments come from the contained materials
	int32 MaterialIDOffset = 0;
	if (Element.Materials.Num() > 0)
	{
		MaterialIDOffset = Materials.Num();
		Materials.Append(Element.Materials);
	}	

	return GeometryCollection->AppendGeometry(*Element.GetGeometryCollection(), MaterialIDOffset, ReindexAllMaterials, TransformRoot);
}

/** NumElements */
int32 UGeometryCollection::NumElements(const FName & Group) const
{
	return GeometryCollection->NumElements(Group);
}


/** RemoveElements */
void UGeometryCollection::RemoveElements(const FName & Group, const TArray<int32>& SortedDeletionList)
{
	Modify();
	GeometryCollection->RemoveElements(Group, SortedDeletionList);
	InvalidateCollection();
}


/** ReindexMaterialSections */
void UGeometryCollection::ReindexMaterialSections()
{
	Modify();
	GeometryCollection->ReindexMaterials();
	InvalidateCollection();
}

void UGeometryCollection::InitializeMaterials()
{
	Modify();

	// Consolidate materials

	// add all materials to a set
	TSet<UMaterialInterface*> MaterialSet;
	for (UMaterialInterface* Curr : Materials)
	{
		MaterialSet.Add(Curr);
	}

	// create the final material array only containing unique materials
	// and one slot for each internal material
	TMap< UMaterialInterface *, int32> MaterialPtrToArrayIndex;
	TArray<UMaterialInterface*> FinalMaterials;
	for (UMaterialInterface *Curr : MaterialSet)
	{
		// Add base material
		TTuple< UMaterialInterface *, int32> CurrTuple(Curr, FinalMaterials.Add(Curr));
		MaterialPtrToArrayIndex.Add(CurrTuple);

		// Add interior material
		FinalMaterials.Add(Curr);
	}

	TManagedArray<int32>& MaterialID = GeometryCollection->MaterialID;

	// Reassign materialid for each face given the new consolidated array of materials
	for (int i = 0; i < MaterialID.Num(); ++i)
	{
		if (MaterialID[i] < Materials.Num())
		{
			UMaterialInterface* OldMaterialPtr = Materials[MaterialID[i]];
			MaterialID[i] = *MaterialPtrToArrayIndex.Find(OldMaterialPtr);
		}
	}

	// Set new material array on the collection
	Materials = FinalMaterials;

	// Last Material is the selection one
	UMaterialInterface* BoneSelectedMaterial = LoadObject<UMaterialInterface>(NULL, TEXT("/Engine/EditorMaterials/GeometryCollection/SelectedGeometryMaterial.SelectedGeometryMaterial"), NULL, LOAD_None, NULL);
	BoneSelectedMaterialIndex = Materials.Add(BoneSelectedMaterial);
	
	GeometryCollection->ReindexMaterials();
	InvalidateCollection();
}

/** Returns true if there is anything to render */
bool UGeometryCollection::HasVisibleGeometry() const
{
	return GeometryCollection->HasVisibleGeometry();
}

/** Serialize */
void UGeometryCollection::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FDestructionObjectVersion::GUID);
	Chaos::FChaosArchive ChaosAr(Ar);

#if WITH_EDITOR
	//Early versions did not have tagged properties serialize first
	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::GeometryCollectionInDDC)
	{
		GeometryCollection->Serialize(ChaosAr);
	}

	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::AddedTimestampedGeometryComponentCache)
	{
		if (Ar.IsLoading())
		{
			// Strip old recorded cache data
			int32 DummyNumFrames;
			TArray<TArray<FTransform>> DummyTransforms;

			Ar << DummyNumFrames;
			DummyTransforms.SetNum(DummyNumFrames);
			for (int32 Index = 0; Index < DummyNumFrames; ++Index)
			{
				Ar << DummyTransforms[Index];
			}
		}
	}
	else
#endif
	{
		// Push up the chain to hit tagged properties too
		// This should have always been in here but because we have saved assets
		// from before this line was here it has to be gated
		Super::Serialize(Ar);
	}

	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::DensityUnitsChanged)
	{
		if (bMassAsDensity)
		{
			Mass = KgCm3ToKgM3(Mass);
		}
	}

	bool bIsCookedOrCooking = Ar.IsCooking();
	if (Ar.CustomVer(FDestructionObjectVersion::GUID) >= FDestructionObjectVersion::GeometryCollectionInDDC)
	{
		Ar << bIsCookedOrCooking;
	}

#if WITH_EDITOR
	if (Ar.CustomVer(FDestructionObjectVersion::GUID) == FDestructionObjectVersion::GeometryCollectionInDDC)
	{
		//This version only saved content into DDC so skip serializing, but copy from DDC at a specific version
		bool bCopyFromDDC = Ar.IsLoading();
		CreateSimulationDataImp(bCopyFromDDC, TEXT("8724C70A140146B5A2F4CF0C16083041"));
	}
#endif
	//new versions serialize geometry collection after tagged properties
	if (Ar.CustomVer(FDestructionObjectVersion::GUID) >= FDestructionObjectVersion::GeometryCollectionInDDCAndAsset)
	{
#if WITH_EDITOR
		if (Ar.IsSaving() && !Ar.IsTransacting())
		{
			CreateSimulationDataImp(/*bCopyFromDDC=*/false);	//make sure content is built before saving
		}
#endif
		GeometryCollection->Serialize(ChaosAr);
	}

	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::GroupAndAttributeNameRemapping)
	{
		GeometryCollection->UpdateOldAttributeNames( );
		InvalidateCollection();
#if WITH_EDITOR
		CreateSimulationData();
#endif
	}

#if WITH_EDITOR
	//for all versions loaded, make sure sim data is up to date
	if (Ar.IsLoading())
	{
		CreateSimulationDataImp(/*bCopyFromDDC=*/ true);	//make sure loaded content is built
	}
#endif
}

#if WITH_EDITOR
void UGeometryCollection::CreateSimulationDataImp(bool bCopyFromDDC, const TCHAR* OverrideVersion)
{
	//Use the DDC to build simulation data. If we are loading in the editor we then serialize this data into the geometry collection
	TArray<uint8> DDCData;
	FDerivedDataGeometryCollectionCooker* GeometryCollectionCooker = new FDerivedDataGeometryCollectionCooker(*this);
	GeometryCollectionCooker->SetOverrideVersion(OverrideVersion);

	if (GeometryCollectionCooker->CanBuild())
	{
		GetDerivedDataCacheRef().GetSynchronous(GeometryCollectionCooker, DDCData);
		if (bCopyFromDDC)
		{
			FMemoryReader Ar(DDCData);
			Chaos::FChaosArchive ChaosAr(Ar);
			GeometryCollection->Serialize(ChaosAr);
		}
	}
}

void UGeometryCollection::CreateSimulationData()
{
	CreateSimulationDataImp(/*bCopyFromDDC=*/false);
}
#endif

void UGeometryCollection::InvalidateCollection()
{
	StateGuid = FGuid::NewGuid();
}

FGuid UGeometryCollection::GetIdGuid() const
{
	return PersistentGuid;
}

FGuid UGeometryCollection::GetStateGuid() const
{
	return StateGuid;
}

#if WITH_EDITOR
void UGeometryCollection::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() != GET_MEMBER_NAME_CHECKED(UGeometryCollection, Materials))
	{
		InvalidateCollection();
		CreateSimulationData();
	}
}

bool UGeometryCollection::Modify(bool bAlwaysMarkDirty /*= true*/)
{
	bool bSuperResult = Super::Modify(bAlwaysMarkDirty);

	UPackage* Package = GetOutermost();
	if(Package->IsDirty())
	{
		InvalidateCollection();
	}

	return bSuperResult;
}

void UGeometryCollection::EnsureDataIsCooked()
{
	if (StateGuid != LastBuiltGuid)
	{
		CreateSimulationDataImp(/*bCopyFromDDC=*/ true);
		LastBuiltGuid = StateGuid;
	}
}
#endif
