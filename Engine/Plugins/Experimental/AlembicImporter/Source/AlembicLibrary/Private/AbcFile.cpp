// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbcFile.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

#include "AbcImporter.h"
#include "AbcTransform.h"
#include "AbcPolyMesh.h"

#include "AbcImportUtilities.h"

#include "AssetRegistryModule.h"
#include "Materials/MaterialInterface.h"
#include "Logging/TokenizedMessage.h"
#include "AbcImportLogger.h"

#include "HAL/Platform.h"


#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
#include <Alembic/AbcGeom/All.h>
#include <Alembic/Abc/ArchiveInfo.h>
#include "Materials/MaterialInstance.h"
THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "AbcFile"

FAbcFile::FAbcFile(const FString& InFilePath)
	: FilePath(InFilePath)
	, RootObject(nullptr)
	, MinFrameIndex(TNumericLimits<int32>::Max())
	, MaxFrameIndex(TNumericLimits<int32>::Min())
	, ArchiveSecondsPerFrame(0.0)
	, NumFrames(0)	
	, FramesPerSecond(0)
	, SecondsPerFrame(0.0)
	, StartFrameIndex(0)
	, EndFrameIndex(0)
	, ArchiveBounds(EForceInit::ForceInitToZero)
	, MinTime(TNumericLimits<float>::Max())
	, MaxTime(TNumericLimits<float>::Lowest())
	, ImportTimeOffset(0.0f)
	, ImportLength(0.0f)
{
}

FAbcFile::~FAbcFile()
{
	for (FAbcPolyMesh* PolyMesh : PolyMeshes)
	{
		delete PolyMesh;
	}
	PolyMeshes.Empty();

	for (FAbcTransform* Transform : Transforms)
	{
		delete Transform;
	}
	Transforms.Empty();
}

EAbcImportError FAbcFile::Open()
{
	Factory.setPolicy(Alembic::Abc::ErrorHandler::kThrowPolicy);
	Factory.setOgawaNumStreams(12);
	
	// Extract Archive and compression type from file
	Archive = Factory.getArchive(TCHAR_TO_ANSI(*FPaths::ConvertRelativePathToFull(FilePath)), CompressionType);
	if (!Archive.valid())
	{
		return EAbcImportError::AbcImportError_InvalidArchive;
	}

	// Get Top/root object
	TopObject = Alembic::Abc::IObject(Archive, Alembic::Abc::kTop);
	if (!TopObject.valid())
	{
		return EAbcImportError::AbcImportError_NoValidTopObject;
	}

	TraverseAbcHierarchy(TopObject, nullptr);

	// Fallback values for 0/1 frame Alembic
	if (NumFrames < 2)
	{
		MinTime = 0.f;
		MaxTime = 0.f;
		MinFrameIndex = 0;
		MaxFrameIndex = NumFrames;
	}

	Alembic::Abc::ObjectHeader Header = TopObject.getHeader();

	// Determine top level archive bounding box
	const Alembic::Abc::MetaData ObjectMetaData = TopObject.getMetaData();
	Alembic::Abc::ICompoundProperty Properties = TopObject.getProperties();

	std::string TmpAppName;
	std::string TmpLibVersionString;
	std::string TmpDateWritten;
	std::string TmpUserDescription;

	GetArchiveInfo(Archive, TmpAppName, TmpLibVersionString, LibVersion, TmpDateWritten, TmpUserDescription);

	AppName = ANSI_TO_TCHAR(TmpAppName.c_str());
	LibVersionString = ANSI_TO_TCHAR(TmpLibVersionString.c_str());
	DateWritten = ANSI_TO_TCHAR(TmpDateWritten.c_str());
	UserDescription = ANSI_TO_TCHAR(TmpUserDescription.c_str());

	Alembic::Abc::IBox3dProperty ArchiveBoundsProperty = Alembic::AbcGeom::GetIArchiveBounds(Archive, Alembic::Abc::ErrorHandler::kQuietNoopPolicy);

	if (ArchiveBoundsProperty.valid())
	{
		ArchiveBounds = AbcImporterUtilities::ExtractBounds(ArchiveBoundsProperty);
	}

	const int32 TimeSamplingIndex = Archive.getNumTimeSamplings() > 1 ? 1 : 0;

	Alembic::Abc::TimeSamplingPtr TimeSampler = Archive.getTimeSampling(TimeSamplingIndex);
	if (TimeSampler)
	{
		ArchiveSecondsPerFrame = TimeSampler->getTimeSamplingType().getTimePerCycle();
	}

	MeshUtilities = FModuleManager::Get().LoadModulePtr<IMeshUtilities>("MeshUtilities");
	
	return EAbcImportError::AbcImportError_NoError;
}

TArray<FAbcFile::FMetaData> FAbcFile::GetArchiveMetaData() const
{
	using FMetaDataEntry = TPairInitializer<FString, FString>;

	TArray<FMetaData> MetaData
	({
		FMetaDataEntry(TEXT("Abc.AppName"), AppName),
		FMetaDataEntry(TEXT("Abc.LibraryVersion"), LibVersionString),
		FMetaDataEntry(TEXT("Abc.WrittenOn"), DateWritten),
		FMetaDataEntry(TEXT("Abc.UserDescription"), UserDescription)
	});

	for (const FMetaData& CustomAttribute : CustomAttributes)
	{
		MetaData.Add(CustomAttribute);
	}

	return MetaData;
}

EAbcImportError FAbcFile::Import(UAbcImportSettings* InImportSettings)
{
	ImportSettings = InImportSettings;	

	FAbcSamplingSettings& SamplingSettings = ImportSettings->SamplingSettings;

	// Compute start/end frames based on the settings and report back the computed values to the settings for display and serialization
	StartFrameIndex = SamplingSettings.bSkipEmpty ? (FMath::Max(SamplingSettings.FrameStart, MinFrameIndex)) : SamplingSettings.FrameStart;
	SamplingSettings.FrameStart = StartFrameIndex;

	int32 LowerFrameIndex = FMath::Min((StartFrameIndex + 1), MaxFrameIndex);
	int32 UpperFrameIndex = FMath::Max((StartFrameIndex + 1), MaxFrameIndex);
	EndFrameIndex = SamplingSettings.FrameEnd == 0 ? MaxFrameIndex : FMath::Clamp(SamplingSettings.FrameEnd, LowerFrameIndex, UpperFrameIndex);
	SamplingSettings.FrameEnd = EndFrameIndex;

	if (ImportSettings->ImportType == EAlembicImportType::StaticMesh)
	{
		EndFrameIndex = StartFrameIndex;
	}

	int32 FrameSpan = EndFrameIndex - StartFrameIndex;
	// If Start==End or Start > End output error message due to invalid frame span
	if (FrameSpan <= 0 && ImportSettings->ImportType != EAlembicImportType::StaticMesh)
	{
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("NoFramesForMeshObject", "Invalid frame range specified {0} - {1}."), FText::FromString(FString::FromInt(StartFrameIndex)), FText::FromString(FString::FromInt(EndFrameIndex))));
		FAbcImportLogger::AddImportMessage(Message);
		return AbcImportError_FailedToImportData;
	}

	// Calculate time step and min/max frame indices according to user sampling settings
	float TimeStep = 0.0f;
	float CacheLength = MaxTime - MinTime;
	EAlembicSamplingType SamplingType = SamplingSettings.SamplingType;
	switch (SamplingType)
	{
	case EAlembicSamplingType::PerFrame:
	{
		// Calculates the time step required to get the number of frames
		TimeStep = !FMath::IsNearlyZero(ArchiveSecondsPerFrame) ? ArchiveSecondsPerFrame : CacheLength / (float)(MaxFrameIndex - MinFrameIndex);
		break;
	}

	case EAlembicSamplingType::PerTimeStep:
	{
		// Calculates the original time step and the ratio between it and the user specified time step
		const float OriginalTimeStep = CacheLength / (float)(MaxFrameIndex - MinFrameIndex);
		const float FrameStepRatio = OriginalTimeStep / SamplingSettings.TimeSteps;
		TimeStep = SamplingSettings.TimeSteps;

		AbcImporterUtilities::CalculateNewStartAndEndFrameIndices(FrameStepRatio, StartFrameIndex, EndFrameIndex);
		FrameSpan = EndFrameIndex - StartFrameIndex;
		break;
	}
	case EAlembicSamplingType::PerXFrames:
	{
		// Calculates the original time step and the ratio between it and the user specified time step
		const float OriginalTimeStep = CacheLength / (float)(MaxFrameIndex - MinFrameIndex);
		const float FrameStepRatio = OriginalTimeStep / ((float)SamplingSettings.FrameSteps * OriginalTimeStep);
		TimeStep = ((float)SamplingSettings.FrameSteps * OriginalTimeStep);

		AbcImporterUtilities::CalculateNewStartAndEndFrameIndices(FrameStepRatio, StartFrameIndex, EndFrameIndex);
		FrameSpan = EndFrameIndex - StartFrameIndex;
		break;
	}

	default:
		checkf(false, TEXT("Incorrect sampling type found in import settings (%i)"), (uint8)SamplingType);
	}

	SecondsPerFrame = TimeStep;
	FramesPerSecond = TimeStep > 0.f ? FMath::RoundToInt(1.f / TimeStep) : 30;
	ImportLength = FrameSpan * TimeStep;

	// Calculate time offset from start of import animation range
	ImportTimeOffset = StartFrameIndex * SecondsPerFrame;

	// Read first-frames for both the transforms and poly meshes

	bool bValidFirstFrames = true;
	for (FAbcTransform* Transform : Transforms)
	{
		bValidFirstFrames &= Transform->ReadFirstFrame(StartFrameIndex * SecondsPerFrame, StartFrameIndex);
	}

	for (FAbcPolyMesh* PolyMesh : PolyMeshes)
	{
		if (PolyMesh->bShouldImport)
		{
			bValidFirstFrames &= PolyMesh->ReadFirstFrame(StartFrameIndex * SecondsPerFrame, StartFrameIndex);
		}
	}	

	if (!bValidFirstFrames)
	{
		return AbcImportError_FailedToImportData;
	}

	// Add up all poly mesh bounds
	FBoxSphereBounds MeshBounds(EForceInit::ForceInitToZero);
	for (const FAbcPolyMesh* PolyMesh : PolyMeshes)
	{
		if (PolyMesh->bShouldImport)
		{
			MeshBounds = MeshBounds + PolyMesh->SelfBounds + PolyMesh->ChildBounds;
		}
	}

	// If there were not bounds available at the archive level or the mesh bounds are larger than the archive bounds use those
	if (FMath::IsNearlyZero(ArchiveBounds.SphereRadius) || MeshBounds.SphereRadius >ArchiveBounds.SphereRadius )
	{
		ArchiveBounds = MeshBounds;
	}
	AbcImporterUtilities::ApplyConversion(ArchiveBounds, ImportSettings->ConversionSettings);

	// If the users opted to try and find materials in the project whos names match one of the face sets
	if (ImportSettings->MaterialSettings.bFindMaterials)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> AssetData;
		const UClass* Class = UMaterialInterface::StaticClass();
		AssetRegistryModule.Get().GetAssetsByClass(Class->GetFName(), AssetData, true);
		for (FAbcPolyMesh* PolyMesh : PolyMeshes)
		{
			for (const FString& FaceSetName : PolyMesh->FaceSetNames)
			{
				UMaterialInterface** ExistingMaterial = MaterialMap.Find(*FaceSetName);
				if (!ExistingMaterial)
				{
					FAssetData* MaterialAsset = AssetData.FindByPredicate([=](const FAssetData& Asset)
					{
						return Asset.AssetName.ToString() == FaceSetName;
					});

					if (MaterialAsset)
					{
						UMaterialInterface* FoundMaterialInterface = Cast<UMaterialInterface>(MaterialAsset->GetAsset());
						if (FoundMaterialInterface)
						{							
							MaterialMap.Add(FaceSetName, FoundMaterialInterface);
							UMaterial* BaseMaterial = Cast<UMaterial>(FoundMaterialInterface);
							if ( !BaseMaterial )
							{
								if (UMaterialInstance* FoundInstance = Cast<UMaterialInstance>(FoundMaterialInterface))
								{
									BaseMaterial = FoundInstance->GetMaterial();
								}
							}

							if (BaseMaterial)
							{
								bool bNeedsRecompile = false;
								if (ImportSettings->ImportType == EAlembicImportType::Skeletal)
								{
									BaseMaterial->SetMaterialUsage(bNeedsRecompile, MATUSAGE_SkeletalMesh);
									BaseMaterial->SetMaterialUsage(bNeedsRecompile, MATUSAGE_MorphTargets);
								}
								else if (ImportSettings->ImportType == EAlembicImportType::GeometryCache)
								{
									BaseMaterial->SetMaterialUsage(bNeedsRecompile, MATUSAGE_GeometryCache);
								}
							}							
						}
					}
					else
					{
						TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Warning, FText::Format(LOCTEXT("NoMaterialForFaceSet", "Unable to find matching Material for Face Set {0}, using default material instead."), FText::FromString(FaceSetName)));
						FAbcImportLogger::AddImportMessage(Message);
					}
				}
			}
		}
	}
	// Or the user opted to create materials with and for the faceset names in this ABC file
	else if (ImportSettings->MaterialSettings.bCreateMaterials)
	{
		// Creates materials according to the face set names that were found in the Alembic file
		for (FAbcPolyMesh* PolyMesh : PolyMeshes)
		{
			for (const FString& FaceSetName : PolyMesh->FaceSetNames)
			{
				// Preventing duplicate material creation
				UMaterialInterface** ExistingMaterial = MaterialMap.Find(*FaceSetName);
				if (!ExistingMaterial)
				{ 
					UMaterial* Material = NewObject<UMaterial>((UObject*)GetTransientPackage(), *FaceSetName);
					Material->bUsedWithMorphTargets = true;
					MaterialMap.Add(FaceSetName, Material);
				}
			}
		}
	}

	// Populate the list of unique face set names from the meshes that should be imported regardless of the import material settings
	bool bRequiresDefaultMaterial = false;
	for (FAbcPolyMesh* PolyMesh : PolyMeshes)
	{
		if (PolyMesh->bShouldImport)
		{
			for (const FString& FaceSetName : PolyMesh->FaceSetNames)
			{
				UniqueFaceSetNames.AddUnique(FaceSetName);
			}
			bRequiresDefaultMaterial |= PolyMesh->FaceSetNames.Num() == 0;
		}
	}

	if (bRequiresDefaultMaterial)
	{
		UniqueFaceSetNames.Insert(TEXT("DefaultMaterial"), 0);
	}

	return AbcImportError_NoError;
}

void FAbcFile::TraverseAbcHierarchy(const Alembic::Abc::IObject& InObject, IAbcObject* InParent)
{
	// Get Header and MetaData info from current Alembic Object
	Alembic::AbcCoreAbstract::ObjectHeader Header = InObject.getHeader();
	const Alembic::Abc::MetaData ObjectMetaData = InObject.getMetaData();
	const uint32 NumChildren = InObject.getNumChildren();

	IAbcObject* CreatedObject = nullptr;

	if (AbcImporterUtilities::IsType<Alembic::AbcGeom::IPolyMesh>(ObjectMetaData))
	{
		Alembic::AbcGeom::IPolyMesh Mesh = Alembic::AbcGeom::IPolyMesh(InObject, Alembic::Abc::kWrapExisting);
		
		FAbcPolyMesh* PolyMesh = new FAbcPolyMesh(Mesh, this, InParent);
		PolyMeshes.Add(PolyMesh);
		CreatedObject = PolyMesh;
		Objects.Add(CreatedObject);

		ExtractCustomAttributes(Mesh);

		// Ignore constant nodes for the computation of the animation time/index range
		// Note that a constant mesh could be animated through its parent transform
		// in which case, the animation range will reflect that of the IXform
		if (PolyMesh->GetNumberOfSamples() > 1)
		{
			MinTime = FMath::Min(MinTime, PolyMesh->GetTimeForFirstData());
			MaxTime = FMath::Max(MaxTime, PolyMesh->GetTimeForLastData());
			NumFrames = FMath::Max(NumFrames, PolyMesh->GetNumberOfSamples());
			MinFrameIndex = FMath::Min(MinFrameIndex, PolyMesh->GetFrameIndexForFirstData());
			MaxFrameIndex = FMath::Max(MaxFrameIndex, PolyMesh->GetFrameIndexForFirstData() + PolyMesh->GetNumberOfSamples());
		}
	}
	else if (AbcImporterUtilities::IsType<Alembic::AbcGeom::IXform>(ObjectMetaData))
	{
		Alembic::AbcGeom::IXform Xform = Alembic::AbcGeom::IXform(InObject, Alembic::Abc::kWrapExisting);
		FAbcTransform* Transform = new FAbcTransform(Xform, this, InParent);
		Transforms.Add(Transform);
		CreatedObject = Transform;
		Objects.Add(CreatedObject);

		// Ignore constant nodes for the computation of the animation time/index range
		// A constant identity transform has 0 frame while a constant non-identity transform has 1 frame
		// In either case, the min/max times are invalid and irrelevant
		if (Transform->GetNumberOfSamples() > 1)
		{
			MinTime = FMath::Min(MinTime, Transform->GetTimeForFirstData());
			MaxTime = FMath::Max(MaxTime, Transform->GetTimeForLastData());
			NumFrames = FMath::Max(NumFrames, Transform->GetNumberOfSamples());
			MinFrameIndex = FMath::Min(MinFrameIndex, Transform->GetFrameIndexForFirstData());
			MaxFrameIndex = FMath::Max(MaxFrameIndex, Transform->GetFrameIndexForFirstData() + Transform->GetNumberOfSamples());
		}
	}

	if (RootObject == nullptr && CreatedObject != nullptr)
	{
		RootObject = CreatedObject;
	}

	// Recursive traversal of child objects
	if (NumChildren > 0)
	{
		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			const Alembic::Abc::IObject& AbcChildObject = InObject.getChild(ChildIndex);
			TraverseAbcHierarchy(AbcChildObject, CreatedObject);
		}
	}
}

void FAbcFile::ExtractCustomAttributes(const Alembic::AbcGeom::IPolyMesh& InMesh)
{
	// Extract the custom attributes from the mesh's arbitrary GeomParams
	Alembic::AbcGeom::ICompoundProperty ArbParams = InMesh.getSchema().getArbGeomParams();
	if (ArbParams)
	{
		FString ObjectName(ANSI_TO_TCHAR(InMesh.getName().c_str()));

		for (int Index = 0; Index < ArbParams.getNumProperties(); ++Index)
		{
			Alembic::Abc::PropertyHeader PropertyHeader = ArbParams.getPropertyHeader(Index);
			Alembic::Abc::PropertyType PropType = PropertyHeader.getPropertyType();
			Alembic::Abc::DataType DataType = PropertyHeader.getDataType();

			// Extract only scalar string attributes
			if (PropType == Alembic::Abc::kScalarProperty && DataType.getPod() == Alembic::Util::kStringPOD)
			{
				std::string PropName = PropertyHeader.getName();
				Alembic::Abc::IStringProperty Param(ArbParams, PropName);
				FString AttributeName(ANSI_TO_TCHAR(PropName.c_str()));
				FString AttributeValue(ANSI_TO_TCHAR(Param.getValue().c_str()));

				AttributeName = FString::Printf(TEXT("Abc.%s.%s"), *ObjectName, *AttributeName);
				CustomAttributes.Add(AttributeName, AttributeValue);
			}
		}
	}
}

void FAbcFile::ReadFrame(int32 FrameIndex, const EFrameReadFlags InFlags, const int32 ReadIndex /*= INDEX_NONE*/)
{
	for (IAbcObject* Object : Objects)
	{
		Object->SetFrameAndTime(FrameIndex * SecondsPerFrame, FrameIndex, InFlags, ReadIndex);
	}
}

void FAbcFile::CleanupFrameData(const int32 ReadIndex)
{
	for (IAbcObject* Object : Objects)
	{
		Object->PurgeFrameData(ReadIndex);
	}
}

void FAbcFile::ProcessFrames(TFunctionRef<void(int32, FAbcFile*)> InCallback, const EFrameReadFlags InFlags)
{
	const int32 NumWorkerThreads = FMath::Min(FTaskGraphInterface::Get().GetNumWorkerThreads(), MaxNumberOfResidentSamples);
	const bool bSingleThreaded = (CompressionType == Alembic::AbcCoreFactory::IFactory::kHDF5) || ImportSettings->NumThreads == 1 ||
								 EnumHasAnyFlags(InFlags, EFrameReadFlags::ForceSingleThreaded) || !FApp::ShouldUseThreadingForPerformance();
	int32 ProcessedFrameIndex = StartFrameIndex - 1;

	if (bSingleThreaded)
	{
		for (int32 FrameIndex = StartFrameIndex; FrameIndex <= EndFrameIndex; ++FrameIndex)
		{
			ReadFrame(FrameIndex, InFlags, 0);
			InCallback(FrameIndex, this);
			CleanupFrameData(0);
		}
	}
	else
	{
		ParallelFor(NumWorkerThreads, [this, InFlags, InCallback, bSingleThreaded, NumWorkerThreads, &ProcessedFrameIndex](int32 ThreadIndex)
		{			
			int32 FrameIndex = INDEX_NONE;
				
			int32 StepIndex = 0;
			FrameIndex = (NumWorkerThreads * StepIndex + ThreadIndex) + StartFrameIndex;

			while (FrameIndex <= EndFrameIndex)
			{
				// Read frame data into memory
				ReadFrame(FrameIndex, InFlags, ThreadIndex);

				// Spin until we can process our frame
				while (ProcessedFrameIndex < (FrameIndex - 1))
				{	
					FPlatformProcess::Sleep(0.1f);
				}

				// Call user defined callback and mark this frame index as processed
				InCallback(FrameIndex, this);					
				ProcessedFrameIndex = FrameIndex;

				// Now cleanup the frame data
				CleanupFrameData(ThreadIndex);

				// Get new frame index to read for next run cycle
				++StepIndex;
				FrameIndex = (NumWorkerThreads * StepIndex + ThreadIndex) + StartFrameIndex;
			};
		});
	}
}

const int32 FAbcFile::GetMinFrameIndex() const
{
	return MinFrameIndex;
}

const int32 FAbcFile::GetMaxFrameIndex() const
{
	return MaxFrameIndex;
}

const int32 FAbcFile::GetStartFrameIndex() const
{
	return StartFrameIndex;
}

const int32 FAbcFile::GetEndFrameIndex() const
{
	return EndFrameIndex;
}

const UAbcImportSettings* FAbcFile::GetImportSettings() const 
{
	return ImportSettings;
}

const TArray<FAbcPolyMesh*>& FAbcFile::GetPolyMeshes() const
{
	return PolyMeshes;
}

const TArray<FAbcTransform*>& FAbcFile::GetTransforms() const
{
	return Transforms;
}

const int32 FAbcFile::GetNumPolyMeshes() const
{
	return PolyMeshes.Num();
}

const FString FAbcFile::GetFilePath() const
{
	return FilePath;
}

const float FAbcFile::GetImportTimeOffset() const
{
	return ImportTimeOffset;
}

const float FAbcFile::GetImportLength() const
{
	return ImportLength;
}

const int32 FAbcFile::GetImportNumFrames() const
{
	return EndFrameIndex - StartFrameIndex;
}

const int32 FAbcFile::GetFramerate() const
{
	return FramesPerSecond;
}

const float FAbcFile::GetSecondsPerFrame() const
{
	return SecondsPerFrame;
}

int32 FAbcFile::GetFrameIndex(float Time)
{
	if (SecondsPerFrame > 0.f)
	{
		int32 FrameIndex = StartFrameIndex + FMath::RoundToInt(Time / SecondsPerFrame);
		return FMath::Clamp(FrameIndex, StartFrameIndex, EndFrameIndex);
	}
	return 0;
}

const FBoxSphereBounds& FAbcFile::GetArchiveBounds() const
{
	return ArchiveBounds;
}

const bool FAbcFile::ContainsHeterogeneousMeshes() const
{
	bool bHomogeneous = true;

	for (const FAbcPolyMesh* Mesh : PolyMeshes)
	{
		bHomogeneous &= ( Mesh->bConstantTopology || !Mesh->bShouldImport );
	}

	return !bHomogeneous;
}

IMeshUtilities* FAbcFile::GetMeshUtilities() const
{
	return MeshUtilities;
}

UMaterialInterface** FAbcFile::GetMaterialByName(const FString& InMaterialName)
{
	return MaterialMap.Find(InMaterialName);
}

#undef LOCTEXT_NAMESPACE // "AbcFile"
