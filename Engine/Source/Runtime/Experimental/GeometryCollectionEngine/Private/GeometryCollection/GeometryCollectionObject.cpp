// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GeometryCollection.cpp: UGeometryCollection methods.
=============================================================================*/
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionCache.h"
#include "UObject/DestructionObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "Serialization/ArchiveCountMem.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "UObject/Package.h"
#include "Materials/MaterialInstance.h"
#include "ProfilingDebugging/CookStats.h"


#if WITH_EDITOR
#include "GeometryCollection/DerivedDataGeometryCollectionCooker.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"
#include "NaniteBuilder.h"
#include "Rendering/NaniteResources.h"
// TODO: Temp until new asset-agnostic builder API
#include "StaticMeshResources.h"
#endif

#include "GeometryCollection/GeometryCollectionSimulationCoreTypes.h"
#include "Chaos/ChaosArchive.h"
#include "GeometryCollectionProxyData.h"

DEFINE_LOG_CATEGORY_STATIC(LogGeometryCollectionInternal, Log, All);

#if ENABLE_COOK_STATS
namespace GeometryCollectionCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("GeometryCollection.Usage"), TEXT(""));
	});
}
#endif

UGeometryCollection::UGeometryCollection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	#if WITH_EDITOR
	, bManualDataCreate(false)
	#endif
	, EnableNanite(false)
	, CollisionType(ECollisionTypeEnum::Chaos_Volumetric)
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
#if WITH_EDITOR
	SimulationDataGuid = StateGuid;
#endif
}

FGeometryCollectionSizeSpecificData::FGeometryCollectionSizeSpecificData()
	: MaxSize(0.0f)
	, CollisionType(ECollisionTypeEnum::Chaos_Volumetric)
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

	ECollisionTypeEnum SelectedCollisionType = CollisionType;

	if(SelectedCollisionType == ECollisionTypeEnum::Chaos_Volumetric && ImplicitType == EImplicitTypeEnum::Chaos_Implicit_LevelSet)
	{
		UE_LOG(LogGeometryCollectionInternal, Verbose, TEXT("LevelSet geometry selected but non-particle collisions selected. Forcing particle-implicit collisions for %s"), *GetPathName());
		SelectedCollisionType = ECollisionTypeEnum::Chaos_Surface_Volumetric;
	}

	FGeometryCollectionSizeSpecificData InfSize;
	InfSize.MaxSize = FLT_MAX;
	InfSize.CollisionType = SelectedCollisionType;
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

void UGeometryCollection::Reset()
{
	if (GeometryCollection.IsValid())
	{
		Modify();
		GeometryCollection->Empty();
		Materials.Empty();
		InvalidateCollection();
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

	// Reassign material ID for each face given the new consolidated array of materials
	for (int32 Material = 0; Material < MaterialID.Num(); ++Material)
	{
		if (MaterialID[Material] < Materials.Num())
		{
			UMaterialInterface* OldMaterialPtr = Materials[MaterialID[Material]];
			MaterialID[Material] = *MaterialPtrToArrayIndex.Find(OldMaterialPtr);
		}
	}

	// Set new material array on the collection
	Materials = FinalMaterials;

	// Last Material is the selection one
	UMaterialInterface* BoneSelectedMaterial = LoadObject<UMaterialInterface>(nullptr, GetSelectedMaterialPath(), nullptr, LOAD_None, nullptr);
	BoneSelectedMaterialIndex = Materials.Add(BoneSelectedMaterial);
	
	GeometryCollection->ReindexMaterials();
	InvalidateCollection();
}

/** Returns true if there is anything to render */
bool UGeometryCollection::HasVisibleGeometry() const
{
	if(ensureMsgf(GeometryCollection.IsValid(), TEXT("Geometry Collection %s has an invalid internal collection")))
	{
		return GeometryCollection->HasVisibleGeometry();
	}

	return false;
}

/** Serialize */
void UGeometryCollection::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FDestructionObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5MainStreamObjectVersion::GUID);
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

		// Fix up the type change for implicits here, previously they were unique ptrs, now they're shared
		TManagedArray<TUniquePtr<Chaos::FImplicitObject>>* OldAttr = GeometryCollection->FindAttributeTyped<TUniquePtr<Chaos::FImplicitObject>>(FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup);
		TManagedArray<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>>* NewAttr = GeometryCollection->FindAttributeTyped<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>>(FGeometryDynamicCollection::SharedImplicitsAttribute, FTransformCollection::TransformGroup);
		if(OldAttr)
		{
			if(!NewAttr)
			{
				NewAttr = &GeometryCollection->AddAttribute<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>>(FGeometryDynamicCollection::SharedImplicitsAttribute, FTransformCollection::TransformGroup);

				const int32 NumElems = GeometryCollection->NumElements(FTransformCollection::TransformGroup);
				for(int32 Index = 0; Index < NumElems; ++Index)
				{
					(*NewAttr)[Index] = TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>((*OldAttr)[Index].Release());
				}
			}

			GeometryCollection->RemoveAttribute(FGeometryDynamicCollection::ImplicitsAttribute, FTransformCollection::TransformGroup);
		}
	}

	if (Ar.CustomVer(FDestructionObjectVersion::GUID) < FDestructionObjectVersion::GroupAndAttributeNameRemapping)
	{
		GeometryCollection->UpdateOldAttributeNames();
		InvalidateCollection();
#if WITH_EDITOR
		CreateSimulationData();
#endif
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) == FUE5MainStreamObjectVersion::GeometryCollectionNaniteData)
	{
		// This legacy version serialized structure information into archive, but the data is transient.
		// Just load it and throw away here, it will be rebuilt later and resaved past this point.

		int32 NumNaniteResources = 0;
		Ar << NumNaniteResources;

		TArray<Nanite::FResources> NaniteResources;
		NaniteResources.Reset(NumNaniteResources);
		NaniteResources.AddDefaulted(NumNaniteResources);

		for (int32 ResourceIndex = 0; ResourceIndex < NumNaniteResources; ++ResourceIndex)
		{
			NaniteResources[ResourceIndex].Serialize(ChaosAr, this);
		}
	}

	if (Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) >= FUE5MainStreamObjectVersion::GeometryCollectionNaniteCooked)
	{
		if (NaniteData == nullptr)
		{
			NaniteData = MakeUnique<FGeometryCollectionNaniteData>();
		}

		NaniteData->Serialize(ChaosAr, this);
	}

#if WITH_EDITOR
	//for all versions loaded, make sure sim data is up to date
	if (Ar.IsLoading())
	{
		EnsureDataIsCooked();	//make sure loaded content is built
	}
#endif
}

const TCHAR* UGeometryCollection::GetSelectedMaterialPath() const
{
	return TEXT("/Engine/EditorMaterials/GeometryCollection/SelectedGeometryMaterial.SelectedGeometryMaterial");
}

#if WITH_EDITOR

void UGeometryCollection::CreateSimulationDataImp(bool bCopyFromDDC)
{
	COOK_STAT(auto Timer = GeometryCollectionCookStats::UsageStats.TimeSyncWork());

	// Skips the DDC fetch entirely for testing the builder without adding to the DDC
	const static bool bSkipDDC = false;

	//Use the DDC to build simulation data. If we are loading in the editor we then serialize this data into the geometry collection
	TArray<uint8> DDCData;
	FDerivedDataGeometryCollectionCooker* GeometryCollectionCooker = new FDerivedDataGeometryCollectionCooker(*this);

	if (GeometryCollectionCooker->CanBuild())
	{
		if (bSkipDDC)
		{
			GeometryCollectionCooker->Build(DDCData);
			COOK_STAT(Timer.AddMiss(DDCData.Num()));
		}
		else
		{
			bool bBuilt = false;
			const bool bSuccess = GetDerivedDataCacheRef().GetSynchronous(GeometryCollectionCooker, DDCData, &bBuilt);
			COOK_STAT(Timer.AddHitOrMiss(!bSuccess || bBuilt ? FCookStats::CallStats::EHitOrMiss::Miss : FCookStats::CallStats::EHitOrMiss::Hit, DDCData.Num()));
		}

		if (bCopyFromDDC)
		{
			FMemoryReader Ar(DDCData, true);	// Must be persistent for BulkData to serialize
			Chaos::FChaosArchive ChaosAr(Ar);
			GeometryCollection->Serialize(ChaosAr);

			NaniteData = MakeUnique<FGeometryCollectionNaniteData>();
			NaniteData->Serialize(ChaosAr, this);
			for (Nanite::FResources& Resource : NaniteData->Resources)
			{
				check(Resource.RootClusterPage.Num() == 0 || Resource.bLZCompressed);
			}
		}
	}
}

void UGeometryCollection::CreateSimulationData()
{
	CreateSimulationDataImp(/*bCopyFromDDC=*/false);
	SimulationDataGuid = StateGuid;
}

TUniquePtr<FGeometryCollectionNaniteData> UGeometryCollection::CreateNaniteData(FGeometryCollection* Collection)
{
	TUniquePtr<FGeometryCollectionNaniteData> NaniteData;

	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(TEXT("UGeometryCollection::CreateNaniteData"));

	Nanite::IBuilderModule& NaniteBuilderModule = Nanite::IBuilderModule::Get();

	NaniteData = MakeUnique<FGeometryCollectionNaniteData>();

	// Transform Group
	const TManagedArray<int32>& TransformToGeometryIndexArray = Collection->TransformToGeometryIndex;
	const TManagedArray<int32>& SimulationTypeArray = Collection->SimulationType;
	const TManagedArray<int32>& StatusFlagsArray = Collection->StatusFlags;

	// Vertices Group
	const TManagedArray<FVector>& VertexArray = Collection->Vertex;
	const TManagedArray<FVector2D>& UVArray = Collection->UV;
	const TManagedArray<FLinearColor>& ColorArray = Collection->Color;
	const TManagedArray<FVector>& TangentUArray = Collection->TangentU;
	const TManagedArray<FVector>& TangentVArray = Collection->TangentV;
	const TManagedArray<FVector>& NormalArray = Collection->Normal;
	const TManagedArray<int32>& BoneMapArray = Collection->BoneMap;

	// Faces Group
	const TManagedArray<FIntVector>& IndicesArray = Collection->Indices;
	const TManagedArray<bool>& VisibleArray = Collection->Visible;
	const TManagedArray<int32>& MaterialIndexArray = Collection->MaterialIndex;
	const TManagedArray<int32>& MaterialIDArray = Collection->MaterialID;

	// Geometry Group
	const TManagedArray<int32>& TransformIndexArray = Collection->TransformIndex;
	const TManagedArray<FBox>& BoundingBoxArray = Collection->BoundingBox;
	const TManagedArray<float>& InnerRadiusArray = Collection->InnerRadius;
	const TManagedArray<float>& OuterRadiusArray = Collection->OuterRadius;
	const TManagedArray<int32>& VertexStartArray = Collection->VertexStart;
	const TManagedArray<int32>& VertexCountArray = Collection->VertexCount;
	const TManagedArray<int32>& FaceStartArray = Collection->FaceStart;
	const TManagedArray<int32>& FaceCountArray = Collection->FaceCount;

	// Material Group
	const TManagedArray<FGeometryCollectionSection>& Sections = Collection->Sections;

	int32 NumGeometry = Collection->NumElements(FGeometryCollection::GeometryGroup);
	NaniteData->Resources.AddDefaulted(NumGeometry);

	for (int32 GeometryGroupIndex = 0; GeometryGroupIndex < NumGeometry; GeometryGroupIndex++)
	{
		Nanite::FResources& NaniteResource = NaniteData->Resources[GeometryGroupIndex];
		NaniteResource = {};

		uint32 NumTexCoords = 1;// NumTextureCoord;
		bool bHasColors = ColorArray.Num() > 0;

		const int32 VertexStart = VertexStartArray[GeometryGroupIndex];
		const int32 VertexCount = VertexCountArray[GeometryGroupIndex];

		TArray<FStaticMeshBuildVertex> BuildVertices;
		BuildVertices.Reserve(VertexCount);
		for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
		{
			FStaticMeshBuildVertex& Vertex = BuildVertices.Emplace_GetRef();
			Vertex.Position = VertexArray[VertexStart + VertexIndex];
			Vertex.Color = bHasColors ? ColorArray[VertexStart + VertexIndex].ToFColor(false /* sRGB */) : FColor::White;
			Vertex.TangentX = FVector::ZeroVector;
			Vertex.TangentY = FVector::ZeroVector;
			Vertex.TangentZ = NormalArray[VertexStart + VertexIndex];
			Vertex.UVs[0] = UVArray[VertexStart + VertexIndex];
			if (Vertex.UVs[0].ContainsNaN())
			{
				Vertex.UVs[0] = FVector2D::ZeroVector;
			}
		}

		const int32 FaceStart = FaceStartArray[GeometryGroupIndex];
		const int32 FaceCount = FaceCountArray[GeometryGroupIndex];

		// TODO: Respect multiple materials like in FGeometryCollectionConversion::AppendStaticMesh

		TArray<int32> MaterialIndices;
		MaterialIndices.Reserve(FaceCount);

		TArray<uint32> BuildIndices;
		BuildIndices.Reserve(FaceCount * 3);
		for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
		{
			if (!VisibleArray[FaceStart + FaceIndex]) // TODO: Always in range?
			{
				continue;
			}

			const FIntVector FaceIndices = IndicesArray[FaceStart + FaceIndex];
			BuildIndices.Add(FaceIndices.X - VertexStart);
			BuildIndices.Add(FaceIndices.Y - VertexStart);
			BuildIndices.Add(FaceIndices.Z - VertexStart);

			const int32 MaterialIndex = MaterialIDArray[FaceStart + FaceIndex];
			MaterialIndices.Add(MaterialIndex);
		}

		if (BuildIndices.Num() == 0)
		{
			// No visible faces of entire geometry, skip any building/rendering.
			continue;
		}

		FMeshNaniteSettings NaniteSettings = {};
		NaniteSettings.bEnabled = true;
		NaniteSettings.PercentTriangles = 1.0f; // 100% - no reduction

		if (!NaniteBuilderModule.Build(NaniteResource, BuildVertices, BuildIndices, MaterialIndices, NumTexCoords, NaniteSettings))
		{
			UE_LOG(LogStaticMesh, Error, TEXT("Failed to build Nanite for geometry collection. See previous line(s) for details."));
		}
	}

	return NaniteData;
}

#endif

void UGeometryCollection::InitResources()
{
	if (NaniteData)
	{
		NaniteData->InitResources(this);
	}
}

void UGeometryCollection::ReleaseResources()
{
	if (NaniteData)
	{
		NaniteData->ReleaseResources();
	}
}

void UGeometryCollection::InvalidateCollection()
{
	StateGuid = FGuid::NewGuid();
}

#if WITH_EDITOR
bool UGeometryCollection::IsSimulationDataDirty() const
{
	return StateGuid != SimulationDataGuid;
}
#endif

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
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UGeometryCollection, EnableNanite))
		{
			InvalidateCollection();
			EnsureDataIsCooked();
		}
		else if (PropertyChangedEvent.Property->GetFName() != GET_MEMBER_NAME_CHECKED(UGeometryCollection, Materials))
		{
			InvalidateCollection();
			
			if (!bManualDataCreate)
			{
				CreateSimulationData();
			}
		}
	}
}

bool UGeometryCollection::Modify(bool bAlwaysMarkDirty /*= true*/)
{
	bool bSuperResult = Super::Modify(bAlwaysMarkDirty);

	UPackage* Package = GetOutermost();
	if (Package->IsDirty())
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
		
		if (FApp::CanEverRender())
		{
			NaniteData->InitResources(this);
		}

		LastBuiltGuid = StateGuid;
	}
}
#endif

void UGeometryCollection::PostLoad()
{
	Super::PostLoad();

	// Initialize rendering resources.
	if (FApp::CanEverRender())
	{
		InitResources();
	}
}

void UGeometryCollection::BeginDestroy()
{
	Super::BeginDestroy();
	ReleaseResources();
}

FGeometryCollectionNaniteData::FGeometryCollectionNaniteData()
{
}

FGeometryCollectionNaniteData::~FGeometryCollectionNaniteData()
{
}

void FGeometryCollectionNaniteData::Serialize(FArchive& Ar, UGeometryCollection* Owner)
{
	if (Ar.IsSaving())
	{
		if (Owner->EnableNanite)
		{
			// Nanite data is currently 1:1 with each geometry group in the collection.
			const int32 NumGeometryGroups = Owner->NumElements(FGeometryCollection::GeometryGroup);
			if (NumGeometryGroups != Resources.Num())
			{
				Ar.SetError();
			}
		}

		int32 NumNaniteResources = Resources.Num();
		Ar << NumNaniteResources;

		for (int32 ResourceIndex = 0; ResourceIndex < NumNaniteResources; ++ResourceIndex)
		{
			Resources[ResourceIndex].Serialize(Ar, Owner);
		}
	}
	else if (Ar.IsLoading())
	{
		int32 NumNaniteResources = 0;
		Ar << NumNaniteResources;

		Resources.Reset(NumNaniteResources);
		Resources.AddDefaulted(NumNaniteResources);

		for (int32 ResourceIndex = 0; ResourceIndex < NumNaniteResources; ++ResourceIndex)
		{
			Resources[ResourceIndex].Serialize(Ar, Owner);
		}

		if (!Owner->EnableNanite)
		{
			Resources.Reset(0);
		}
	}
}

void FGeometryCollectionNaniteData::InitResources(UGeometryCollection* Owner)
{
	if (bIsInitialized)
	{
		ReleaseResources();
	}

	for (Nanite::FResources& Resource : Resources)
	{
		Resource.InitResources();
	}

	bIsInitialized = true;
}

void FGeometryCollectionNaniteData::ReleaseResources()
{
	if (!bIsInitialized)
	{
		return;
	}

	for (Nanite::FResources& Resource : Resources)
	{
		Resource.ReleaseResources();
	}

	bIsInitialized = false;
}
