// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneImporter.h"

#include "DatasmithRuntimeAuxiliaryData.h"
#include "DatasmithRuntimeUtils.h"
#include "LogCategory.h"

#include "DatasmithImportOptions.h"
#include "DatasmithMeshUObject.h"
#include "DatasmithNativeTranslator.h"
#include "DatasmithPayload.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"
#include "Utility/DatasmithMeshHelper.h"

#include "Algo/Find.h"
#include "Algo/Sort.h"
#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/ScopeLock.h"
#include "StaticMeshAttributes.h"
#include "UObject/GarbageCollection.h"

#if WITH_EDITOR
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/Material.h"

//#define ACTOR_DEBUG
#endif

namespace DatasmithRuntime
{
	extern UMaterialInstanceDynamic* GetDefaultMaterial();

	void FSceneImporter::ProcessMeshData(FAssetData& MeshData)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessMeshData);

		if (MeshData.bProcessed)
		{
			return;
		}

		TSharedPtr< IDatasmithMeshElement > MeshElement = StaticCastSharedPtr< IDatasmithMeshElement >(Elements[MeshData.ElementId]);

		UStaticMesh* StaticMesh = MeshData.GetObject<UStaticMesh>();

		if (StaticMesh == nullptr)
		{
#ifdef ASSET_DEBUG
			FString MeshName = FString(MeshElement->GetLabel()) + TEXT("_LU_") + FString::FromInt(MeshData.ElementId);
			MeshName = FDatasmithUtils::SanitizeObjectName(MeshName);
			UPackage* Package = CreatePackage(nullptr, *FPaths::Combine(TEXT("/Engine/Transient/LU"), MeshName));
			StaticMesh = NewObject< UStaticMesh >(Package, *MeshName, RF_Public);
#else
			FString MeshName = TEXT("SM_") + FString(SceneElement->GetName()) + TEXT("_") + FString::FromInt(MeshData.ElementId);
			MeshName = FDatasmithUtils::SanitizeObjectName(MeshName);
			StaticMesh = NewObject< UStaticMesh >(GetTransientPackage(), *MeshName);
#endif
			check(StaticMesh);

			{
				IInterface_AssetUserData* AssetUserData = Cast< IInterface_AssetUserData >(StaticMesh);

				UDatasmithRuntimeAuxiliaryData* UserData = NewObject<UDatasmithRuntimeAuxiliaryData>(StaticMesh, NAME_None, RF_NoFlags);
				AssetUserData->AddAssetUserData(UserData);

				UserData->Auxiliary = NewObject< UDatasmithMesh >(UserData, NAME_None, RF_Standalone);
			}

			MeshData.Object = TStrongObjectPtr< UObject >(StaticMesh);
		}

		const int32 MaterialSlotCount = MeshElement->GetMaterialSlotCount();

		FActionTaskFunction AssignMaterialFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
		{
			return this->AssignMaterial(Referencer, Cast<UMaterialInstanceDynamic>(Object));
		};

		TArray< FStaticMaterial >& StaticMaterials = StaticMesh->StaticMaterials;
		StaticMaterials.SetNum(MaterialSlotCount);

		for (int32 Index = 0; Index < MaterialSlotCount; Index++)
		{
			FStaticMaterial& StaticMaterial = StaticMaterials[Index];

			StaticMaterial.MaterialSlotName = NAME_None;
			StaticMaterial.MaterialInterface = nullptr;

			// Done to remove an assert from an 'ensure' in UStaticMesh::GetUVChannelData
			StaticMaterial.UVChannelData = FMeshUVChannelInfo(1.f);

			// #ue_liveupdate: Revisit assignment of materials as it works on ids
			if (const IDatasmithMaterialIDElement* MaterialIDElement = MeshElement->GetMaterialSlotAt(Index).Get())
			{
				// #ue_liveupdate: Missing code to handle the case where a MaterialID's name is an asset's path
				if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialIDElement->GetName()))
				{
					FAssetData& MaterialData = AssetDataList[*MaterialElementIdPtr];

					ProcessMaterialData(MaterialData);

					AddToQueue(NONASYNC_QUEUE, { AssignMaterialFunc, *MaterialElementIdPtr, { EDataType::Mesh, MeshData.ElementId, (int8)Index } });
					TasksToComplete |= EDatasmithRuntimeWorkerTask::MaterialAssign;

					StaticMaterial.MaterialSlotName = *FString::Printf(TEXT("%d"), MaterialIDElement->GetId());
				}
			}
		}

		// Create BodySetup in game thread to avoid allocating during a garbage collect later on
		if (StaticMesh->BodySetup == nullptr)
		{
			StaticMesh->CreateBodySetup();
		}

		MeshData.bProcessed = true;
		MeshData.bCompleted = false;

		MeshElementSet.Add(MeshData.ElementId);
	}

	bool FSceneImporter::ProcessMeshActorData(FActorData& ActorData, IDatasmithMeshActorElement* MeshActorElement)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessMeshActorData);

		if (ActorData.bProcessed)
		{
			return true;
		}

		// Invalid reference to a mesh. Abort creation of component
		if (FCString::Strlen(MeshActorElement->GetStaticMeshPathName()) == 0)
		{
			ActorData.bProcessed = true;
			return false;
		}

		FActionTaskFunction CreateComponentFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
		{
			return this->CreateMeshComponent(Referencer.GetId(), Cast<UStaticMesh>(Object));
		};

		FString StaticMeshPathName(MeshActorElement->GetStaticMeshPathName());
		UStaticMesh* StaticMesh = nullptr;

		if (!StaticMeshPathName.StartsWith(TEXT("/Game/")))
		{
			if (FSceneGraphId* MeshElementIdPtr = AssetElementMapping.Find(MeshPrefix + StaticMeshPathName))
			{
				FAssetData& MeshData = AssetDataList[*MeshElementIdPtr];

				ProcessMeshData(MeshData);

				AddToQueue(NONASYNC_QUEUE, { CreateComponentFunc, *MeshElementIdPtr, { EDataType::Actor, ActorData.ElementId, 0 } });
				TasksToComplete |= EDatasmithRuntimeWorkerTask::MeshComponentCreate;

				ActorData.MeshId = *MeshElementIdPtr;

				StaticMesh = MeshData.GetObject<UStaticMesh>();
			}
		}
		else
		{
			StaticMesh = Cast<UStaticMesh>(FSoftObjectPath(StaticMeshPathName).TryLoad());
		}

		// The referenced static mesh was not found. Abort creation of component
		if (StaticMesh == nullptr)
		{
			return false;
		}

		if (MeshActorElement->GetMaterialOverridesCount() > 0)
		{
			FActionTaskFunction AssignMaterialFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
			{
				return this->AssignMaterial(Referencer, Cast<UMaterialInstanceDynamic>(Object));
			};

			TArray< FStaticMaterial >& StaticMaterials = StaticMesh->StaticMaterials;

			TMap<FString, int32> SlotMapping;
			SlotMapping.Reserve(StaticMaterials.Num());

			for (int32 Index = 0; Index < StaticMaterials.Num(); ++Index)
			{
				const FStaticMaterial& StaticMaterial = StaticMaterials[Index];

				if (StaticMaterial.MaterialSlotName != NAME_None)
				{
					SlotMapping.Add(StaticMaterial.MaterialSlotName.ToString(), Index);
				}
			}

			for (int32 Index = 0; Index < MeshActorElement->GetMaterialOverridesCount(); ++Index)
			{
				TSharedPtr<const IDatasmithMaterialIDElement> MaterialIDElement = MeshActorElement->GetMaterialOverride(Index);

				FString MaterialSlotName = FString::Printf(TEXT("%d"), MaterialIDElement->GetId());

				if (MaterialIDElement->GetId() != -1 && SlotMapping.Contains(MaterialSlotName))
				{
					int32 MaterialIndex = SlotMapping[MaterialSlotName];

					// #ue_liveupdate: Missing code to handle the case where a MaterialID's name is an asset's path
					if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialIDElement->GetName()))
					{
						FAssetData& MaterialData = AssetDataList[*MaterialElementIdPtr];

						ProcessMaterialData(MaterialData);
						AddToQueue(NONASYNC_QUEUE, { AssignMaterialFunc, *MaterialElementIdPtr, { EDataType::Actor, ActorData.ElementId, (int8)MaterialIndex } });
						TasksToComplete |= EDatasmithRuntimeWorkerTask::MaterialAssign;
					}
				}
			}
		}

		ActorData.bProcessed = true;
		ActorData.bCompleted = false;

		return true;
	}

#ifdef ACTOR_DEBUG
	// #ue_liveupdate: Remove debug counter
	static int32 DebugMeshActorsCount;
#endif

	void FSceneImporter::MeshPreProcessing()
	{
		LIVEUPDATE_LOG_TIME;

		TArray< FSceneGraphId > MeshElementArray;

		if (bIncrementalUpdate)
		{
			TSet< FSceneGraphId > LocalMeshElementSet;

			FParsingCallback FindMeshesCallback = 
				[this,&LocalMeshElementSet](const TSharedPtr< IDatasmithActorElement>& ActorElement, FSceneGraphId ActorId) -> void
			{
				if (ActorElement->IsA(EDatasmithElementType::StaticMeshActor))
				{
					IDatasmithMeshActorElement* MeshActorElement = static_cast<IDatasmithMeshActorElement*>(ActorElement.Get());

					FString StaticMeshPathName(MeshActorElement->GetStaticMeshPathName());

					if (!StaticMeshPathName.StartsWith(TEXT("/Game/")))
					{
						if (FSceneGraphId* MeshElementIdPtr = this->AssetElementMapping.Find(MeshPrefix + StaticMeshPathName))
						{
							LocalMeshElementSet.Add(*MeshElementIdPtr);
						}
					}
				}
			};

			for (int32 Index = 0; Index < SceneElement->GetActorsCount(); ++Index)
			{
				ParseScene( SceneElement->GetActor(Index), DirectLink::InvalidId, FindMeshesCallback );
			}

			MeshElementArray = LocalMeshElementSet.Array();
		}
		else
		{
			MeshElementArray = MeshElementSet.Array();
		}

		if(MeshElementArray.Num() == 0)
		{
			return;
		}

#ifdef ACTOR_DEBUG
		DebugMeshActorsCount = 0;
#endif

		CalculateMeshesLightmapWeights(MeshElementArray, Elements, LightmapWeights);

		// #ue_liveupdate: Find a way to sort meshes according to size. GetStatData is taking long
		// Sort mesh elements from lower poly count based on file size
		//IFileManager& FileManager = IFileManager::Get();
		//Algo::SortBy(ElementIndices, [&](int32 ElementId) -> int64
		//	{
		//		const TCHAR* FileName = static_cast<IDatasmithMeshElement*>(Elements[ElementId].Get())->GetFile();
		//		return -FileManager.GetStatData(FileName).FileSize;
		//	});

		for (FSceneGraphId MeshElementId : MeshElementSet)
		{
			float LightmapWeight = LightmapWeights[MeshElementId];

			FActionTaskFunction TaskFunc = [this, LightmapWeight](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
			{
				Async(
#if WITH_EDITOR
					EAsyncExecution::LargeThreadPool,
#else
					EAsyncExecution::ThreadPool,
#endif
					// #ue_liveupdate: LightmapWeights, what about incremental addition of meshes?
					[this, ElementId = Referencer.GetId(), LightmapWeight]()->bool
					{
						return this->CreateStaticMesh(ElementId, LightmapWeight);
					},
					[this]()->void
					{
						this->ActionCounter.Increment();
					}
				);

				return EActionResult::Succeeded;
			};

			AddToQueue(MESH_QUEUE, { TaskFunc, {EDataType::Mesh, MeshElementId, 0 } });
		}

		TasksToComplete |= EDatasmithRuntimeWorkerTask::MeshCreate;
	}

	bool FSceneImporter::CreateStaticMesh(FSceneGraphId ElementId, float LightmapWeight)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CreateStaticMesh);

		TSharedRef< IDatasmithMeshElement > MeshElement = StaticCastSharedPtr< IDatasmithMeshElement >(Elements[ElementId]).ToSharedRef();

		TFunction<bool()> MaterialRequiresAdjacency;
		MaterialRequiresAdjacency = [this, MeshElementPtr = &MeshElement.Get()]() -> bool
		{
			for (int32 Index = 0; Index < MeshElementPtr->GetMaterialSlotCount(); Index++)
			{
				if (const IDatasmithMaterialIDElement* MaterialIDElement = MeshElementPtr->GetMaterialSlotAt(Index).Get())
				{
					// #ue_liveupdate: Missing code to handle the case where a MaterialID's name is an asset's path
					if (FSceneGraphId* MaterialElementIdPtr = AssetElementMapping.Find(MaterialPrefix + MaterialIDElement->GetName()))
					{
						if (AssetDataList[*MaterialElementIdPtr].Requirements & EMaterialRequirements::RequiresAdjacency)
						{
							return true;
						}
					}
				}
			}

			return false;
		};

		FAssetData& MeshData = AssetDataList[ElementId];

		// #ueent_datasmithruntime: TODO - Stop processing if reset has set
		UStaticMesh* StaticMesh = MeshData.GetObject<UStaticMesh>();
		if (StaticMesh == nullptr)
		{
			ensure(false);
			return false;
		}

		TRACE_CPUPROFILER_EVENT_SCOPE(FDatasmithRuntimeModel::CreateStaticMesh);

		// #ue_liveupdate: Not necessary. Should not call create mesh if hashes are equal
		if (MeshData.Hash == MeshElement->GetFileHash())
		{
			MeshData.bCompleted = true;
			return true;
		}

		MeshData.Hash = MeshElement->GetFileHash();
		if (!MeshData.Hash.IsValid())
		{
			MeshData.Hash = FMD5Hash::HashFile(MeshElement->GetFile());
		}

		// If mesh file does not exist, add scene's resource path if valid
		if (!FPaths::FileExists(MeshElement->GetFile()) && FPaths::DirectoryExists(SceneElement->GetResourcePath()))
		{
			MeshElement->SetFile( *FPaths::Combine(SceneElement->GetResourcePath(), MeshElement->GetFile()) );
		}

		FDatasmithMeshElementPayload MeshPayload;
		{
			FDatasmithNativeTranslator NativeTranslator;

			// Prevent GC from running while loading meshes.
			// FDatasmithNativeTranslator::LoadStaticMesh is creating UDatasmithMesh objects
			FGCScopeGuard GCGuard;

			if (!NativeTranslator.LoadStaticMesh(MeshElement, MeshPayload))
			{
				ActionCounter.Add(MeshData.Referencers.Num());
				// #ue_liveupdate: Inform user loading mesh failed
				return false;
			}
		}

		TArray< FMeshDescription >& MeshDescriptions = MeshPayload.LodMeshes;

		// Empty mesh?
		if (MeshDescriptions.Num() == 0)
		{
			MeshData.bCompleted = true;
			return true;
		}

		// #ue_liveupdate: Cleanup mesh descriptions
		//FDatasmithStaticMeshImporter::CleanupMeshDescriptions(MeshDescriptions);

		//#ue_liveupdate: Implement task to build better lightmap sizes - See Dataprep operation
		int32 MinLightmapSize = FDatasmithStaticMeshImportOptions::ConvertLightmapEnumToValue(EDatasmithImportLightmapMin::LIGHTMAP_64);
		int32 MaxLightmapSize = FDatasmithStaticMeshImportOptions::ConvertLightmapEnumToValue(EDatasmithImportLightmapMax::LIGHTMAP_512);

		// 4. Collisions
		ProcessCollision(StaticMesh, MeshPayload);

		// Extracted from FDatasmithStaticMeshImporter::SetupStaticMesh
#if WITH_EDITOR
		StaticMesh->SetNumSourceModels(MeshDescriptions.Num());
#endif

		for (int32 LodIndex = 0; LodIndex < MeshDescriptions.Num(); ++LodIndex)
		{
			FMeshDescription& MeshDescription = MeshDescriptions[LodIndex];

			// UV Channels
			int32 SourceIndex = 0;
			int32 DestinationIndex = 1;
			bool bUseImportedLightmap = false;
			bool bGenerateLightmapUVs = true /* Default value for StaticMeshImportOptions.bGenerateLightmapUVs*/;
			const int32 FirstOpenUVChannel = GetNextOpenUVChannel(MeshDescription);

			// if a custom lightmap coordinate index was imported, disable lightmap generation
			if (DatasmithMeshHelper::HasUVData(MeshDescription, MeshElement->GetLightmapCoordinateIndex()))
			{
				bUseImportedLightmap = true;
				bGenerateLightmapUVs = false;
				DestinationIndex = MeshElement->GetLightmapCoordinateIndex();
			}
			else
			{
				if (MeshElement->GetLightmapCoordinateIndex() >= 0)
				{
					// #ue_liveupdate: Log error message
					//A custom lightmap index value was set but the data was invalid.
					//FFormatNamedArguments FormatArgs;
					//FormatArgs.Add(TEXT("LightmapCoordinateIndex"), FText::FromString(FString::FromInt(MeshElement->GetLightmapCoordinateIndex())));
					//FormatArgs.Add(TEXT("MeshName"), FText::FromName(StaticMesh->GetFName()));
					//AssetsContext.ParentContext.LogError(FText::Format(LOCTEXT("InvalidLightmapSourceUVError", "The lightmap coordinate index '{LightmapCoordinateIndex}' used for the mesh '{MeshName}' is invalid. A valid lightmap coordinate index was set instead."), FormatArgs));
				}

				DestinationIndex = FirstOpenUVChannel;
			}

			// Set the source lightmap index to the imported mesh data lightmap source if any, otherwise use the first open channel.
			if (DatasmithMeshHelper::HasUVData(MeshDescription, MeshElement->GetLightmapSourceUV()))
			{
				SourceIndex = MeshElement->GetLightmapSourceUV();
			}
			else
			{
				//If the lightmap source index was not set, we set it to the first open UV channel as it will be generated.
				//Also, it's okay to set both the source and the destination to be the same index as they are for different containers.
				SourceIndex = FirstOpenUVChannel;
			}

			if (bGenerateLightmapUVs)
			{
				if (!FMath::IsWithin<int32>(SourceIndex, 0, MAX_MESH_TEXTURE_COORDS_MD))
				{
					// #ue_liveupdate: Log error message
					//AssetsContext.ParentContext.LogError(FText::Format(LOCTEXT("InvalidLightmapSourceIndexError", "Lightmap generation error for mesh {0}: Specified source is invalid {1}. Cannot find an available fallback source channel."), FText::FromName(StaticMesh->GetFName()), MeshElement->GetLightmapSourceUV()));
					bGenerateLightmapUVs = false;
				}
				else if (!FMath::IsWithin<int32>(DestinationIndex, 0, MAX_MESH_TEXTURE_COORDS_MD))
				{
					// #ue_liveupdate: Log error message
					//AssetsContext.ParentContext.LogError(FText::Format(LOCTEXT("InvalidLightmapDestinationIndexError", "Lightmap generation error for mesh {0}: Cannot find an available destination channel."), FText::FromName(StaticMesh->GetFName())));
					bGenerateLightmapUVs = false;
				}

				if (!bGenerateLightmapUVs)
				{
					// #ue_liveupdate: Log error message
					//AssetsContext.ParentContext.LogWarning(FText::Format(LOCTEXT("LightmapUVsWontBeGenerated", "Lightmap UVs for mesh {0} won't be generated."), FText::FromName(StaticMesh->GetFName())));
				}
			}

			// We should always have some UV data in channel 0 because it is used in the mesh tangent calculation during the build.
			if (!DatasmithMeshHelper::HasUVData(MeshDescription, 0))
			{
				DatasmithMeshHelper::CreateDefaultUVs(MeshDescription);
			}

			if (bGenerateLightmapUVs && !DatasmithMeshHelper::HasUVData(MeshDescription, SourceIndex))
			{
				//If no UV data exist at the source index we generate unwrapped UVs.
				//Do this before calling DatasmithMeshHelper::CreateDefaultUVs() as the UVs may be unwrapped at channel 0.
				//UUVGenerationFlattenMapping::GenerateUVs(MeshDescription, SourceIndex, true);
				// #ue_liveupdate: Find runtime code to unwrap UVs
				// For the time being, just copy channel 0 to SourceIndex
				{
					TMeshAttributesRef<FVertexInstanceID, FVector2D> UVs = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2D>(MeshAttribute::VertexInstance::TextureCoordinate);
					if (UVs.GetNumIndices() <= SourceIndex)
					{
						UVs.SetNumIndices(SourceIndex + 1);
					}

					for (const FVertexInstanceID& VertexInstanceID : MeshDescription.VertexInstances().GetElementIDs())
					{
						UVs.Set(VertexInstanceID, SourceIndex, UVs.Get(VertexInstanceID, 0));
					}
				}
			}

			FVector BuildScale3D(1.0f, 1.0f, 1.0f);
#if WITH_EDITOR
			FMeshBuildSettings& BuildSettings = StaticMesh->GetSourceModel(LodIndex).BuildSettings;

			BuildSettings.bUseMikkTSpace = true;
			BuildSettings.bRecomputeNormals = ShouldRecomputeNormals(MeshDescription, MeshData.Requirements);
			BuildSettings.bRecomputeTangents = ShouldRecomputeTangents(MeshDescription, MeshData.Requirements);
			BuildSettings.bRemoveDegenerates = true /* Default value of StaticMeshImportOptions.bRemoveDegenerates */;
			BuildSettings.bUseHighPrecisionTangentBasis = true;
			BuildSettings.bUseFullPrecisionUVs = true;
			BuildSettings.bGenerateLightmapUVs = bGenerateLightmapUVs;
			BuildSettings.SrcLightmapIndex = SourceIndex;
			BuildSettings.DstLightmapIndex = DestinationIndex;
			BuildSettings.MinLightmapResolution = MinLightmapSize;
			BuildScale3D = BuildSettings.BuildScale3D;

			// Don't build adjacency buffer for meshes with over 500 000 triangles because it's too slow
			BuildSettings.bBuildAdjacencyBuffer = MeshDescription.Polygons().Num() < 500000 ? MaterialRequiresAdjacency() : false;
#endif
			if (DatasmithMeshHelper::IsMeshValid(MeshDescription, BuildScale3D))
			{
				if (bGenerateLightmapUVs && DatasmithMeshHelper::RequireUVChannel(MeshDescription, DestinationIndex))
				{
					GenerateLightmapUVResolution(MeshDescription, SourceIndex, MinLightmapSize);
				}
			}
		}

		TArray<const FMeshDescription*> MeshDescriptionPointers;
		for (FMeshDescription& MeshDescription : MeshDescriptions)
		{
			MeshDescriptionPointers.Add(&MeshDescription);
		}

		// #ue_liveupdate: Multi-threading issue with BodySetup::CreatePhysicsMeshes.
		static bool bEnableCollision = false;

		{
			FGCScopeGuard GCGuard;

			// Do not mark the package dirty since MarkPackageDirty is not thread safe
			UStaticMesh::FBuildMeshDescriptionsParams Params;
			Params.bUseHashAsGuid = true;
			Params.bMarkPackageDirty = false;
			Params.bBuildSimpleCollision = bEnableCollision;
			// Do not commit since we only need the render data and commit is slow
			Params.bCommitMeshDescription = false;
			StaticMesh->BuildFromMeshDescriptions(MeshDescriptionPointers, Params);
		}

		// Free up memory
		MeshDescriptions.Empty();
#if WITH_EDITORONLY_DATA
		StaticMesh->ClearMeshDescriptions();
#endif

		check(StaticMesh->RenderData && StaticMesh->RenderData->IsInitialized());

		MeshData.bCompleted = true;
		return true;
	}

	EActionResult::Type FSceneImporter::CreateMeshComponent(FSceneGraphId ActorId, UStaticMesh* StaticMesh)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CreateMeshComponent);

		if (StaticMesh == nullptr)
		{
			return EActionResult::Succeeded;
		}

		FActorData& ActorData = ActorDataList[ActorId];

		// Component has been removed, no action needed
		if (ActorData.ElementId == INDEX_NONE)
		{
			return EActionResult::Succeeded;
		}

		UStaticMeshComponent* MeshComponent = ActorData.GetObject<UStaticMeshComponent>();

		if (MeshComponent == nullptr)
		{
#ifdef ACTOR_DEBUG
			if (DebugMeshActorsCount < 100)
			{
				AStaticMeshActor* NewMeshActor = RootComponent->GetOwner()->GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity);
				MeshComponent = NewMeshActor->GetStaticMeshComponent();
				DebugMeshActorsCount++;
			}
			else
			{
				MeshComponent = NewObject< UStaticMeshComponent >(RootComponent->GetOwner(), NAME_None);
			}
#else // ACTOR_DEBUG
			MeshComponent = NewObject< UStaticMeshComponent >(RootComponent->GetOwner(), NAME_None);
#endif // ACTOR_DEBUG

			ActorData.Object = TStrongObjectPtr<UObject>(MeshComponent);

			MeshComponent->SetMobility(EComponentMobility::Movable);

			MeshComponent->AttachToComponent(RootComponent.Get(), FAttachmentTransformRules::KeepRelativeTransform);
			MeshComponent->RegisterComponentWithWorld(RootComponent->GetOwner()->GetWorld());
		}
		else
		{
			MeshComponent->MarkRenderStateDirty();
		}

		// #ueent_datasmithruntime: Enable collision after mesh component has been displayed. Can this be multi-threaded?
		MeshComponent->bAlwaysCreatePhysicsState = false;
		MeshComponent->BodyInstance.SetCollisionEnabled(ECollisionEnabled::NoCollision);

		MeshComponent->SetStaticMesh(StaticMesh);
#if WITH_EDITOR
		StaticMesh->ClearFlags(RF_Public);
#endif

		MeshComponent->SetRelativeTransform(ActorData.WorldTransform);

		// Allocate memory or not for override materials
		TArray< UMaterialInterface* >& OverrideMaterials = MeshComponent->OverrideMaterials;
		IDatasmithMeshActorElement* MeshActorElement = static_cast<IDatasmithMeshActorElement*>(Elements[ActorData.ElementId].Get());

		// There are override materials, make sure the slots are allocated
		if (MeshActorElement->GetMaterialOverridesCount() > 0)
		{
			OverrideMaterials.SetNum(StaticMesh->StaticMaterials.Num());
			for (int32 Index = 0; Index < OverrideMaterials.Num(); ++Index)
			{
				OverrideMaterials[Index] = nullptr;
			}
		}
		// No override material, discard the array if necessary
		else if (OverrideMaterials.Num() > 0)
		{
			OverrideMaterials.Empty();
		}

		ActorData.bCompleted = true;

		// Update counters
		ActionCounter.Increment();

		return EActionResult::Succeeded;
	}

	EActionResult::Type FSceneImporter::AssignMaterial(const FReferencer& Referencer, UMaterialInstanceDynamic* Material)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::AssignMaterial);

		if (Material == nullptr)
		{
			// #ue_dsruntime: Log message material not assigned
			return EActionResult::Failed;
		}

		switch (Referencer.Type)
		{
		case EDataType::Mesh:
		{
			FAssetData& MeshData = AssetDataList[Referencer.GetId()];

			if (!MeshData.bCompleted)
			{
				return EActionResult::Retry;
			}

			// Static mesh can be null if creation failed
			if (UStaticMesh* StaticMesh = MeshData.GetObject<UStaticMesh>())
			{
				TArray< FStaticMaterial >& StaticMaterials = StaticMesh->StaticMaterials;

				if (!StaticMaterials.IsValidIndex(Referencer.Slot))
				{
					ensure(false);
					return EActionResult::Failed;
				}

				StaticMaterials[Referencer.Slot].MaterialInterface = Material;

				// Update counters
				ActionCounter.Increment();
#if WITH_EDITOR
				Material->ClearFlags(RF_Public);
#endif
				// Mark dependent mesh components' render state as dirty
				for (FReferencer& ActorReferencer : MeshData.Referencers)
				{
					FActorData& ActorData = ActorDataList[ActorReferencer.GetId()];

					if (UActorComponent* ActorComponent = ActorData.GetObject<UActorComponent>())
					{
						ActorComponent->MarkRenderStateDirty();
					}
				}
			}

			break;
		}

		case EDataType::Actor:
		{
			FActorData& ActorData = ActorDataList[Referencer.GetId()];

			const TCHAR* ActorLabel = Elements[ActorData.ElementId]->GetLabel();

			if (!ActorData.bCompleted)
			{
				return EActionResult::Retry;
			}

			// Static mesh can be null if creation failed
			if (UStaticMeshComponent* MeshComponent = ActorData.GetObject<UStaticMeshComponent>())
			{
				if ((int32)Referencer.Slot >= MeshComponent->GetNumMaterials())
				{
					ensure(false);
					return EActionResult::Failed;
				}

				MeshComponent->SetMaterial(Referencer.Slot, Material);
				ActionCounter.Increment();

				// Force rebuilding of render data for mesh component
				MeshComponent->MarkRenderStateDirty();
#if WITH_EDITOR
				Material->ClearFlags(RF_Public);
#endif
			}
			else
			{
				UE_LOG(LogDatasmithRuntime, Log, TEXT("AssignMaterial: Actor %s has no mesh component"), ActorLabel);
				ensure(false);
				return EActionResult::Failed;
			}

			break;
		}

		default:
		{
			ensure(false);
			return EActionResult::Failed;
		}
		}

		return EActionResult::Succeeded;
	}
}
