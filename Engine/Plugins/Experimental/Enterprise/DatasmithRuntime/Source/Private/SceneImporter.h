// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DirectLink/DirectLinkCommon.h"

#include "Async/AsyncWork.h"
#include "Async/Future.h"
#include "Containers/Queue.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Misc/SecureHash.h"
#include "PixelFormat.h"
#include "RHI.h"
#include "Templates/Casts.h"
#include "Tickable.h"
#include "UObject/GCObject.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtr.h"

class ADatasmithRuntimeActor;
class IDatasmithActorElement;
class IDatasmithElement;
class IDatasmithCameraActorElement;
class IDatasmithLightActorElement;
class IDatasmithMeshActorElement;
class IDatasmithScene;
class IDatasmithTranslator;
class UMaterial;
class UMaterialInstanceDynamic;
class ULightComponent;
class USceneComponent;
class UStaticMesh;
class UTexture2D;
class UTextureLightProfile;

struct FUpdateContext;

#ifdef WITH_EDITOR
#define LIVEUPDATE_TIME_LOGGING
//#define ASSET_DEBUG
#endif

DECLARE_MULTICAST_DELEGATE_OneParam(FOnStaticMeshComplete, UStaticMesh*)
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMaterialComplete, UMaterialInstanceDynamic*)
DECLARE_MULTICAST_DELEGATE_OneParam(FOnTextureComplete, UTexture2D*)

namespace EDatasmithRuntimeWorkerTask
{
	enum Type : uint16
	{
		NoTask					= 0x0000,
		CollectSceneData		= 0x0002,
		UpdateElement			= 0x4000,
		SetupTasks				= 0x0004,
		PreMeshCreate			= 0x8000,
		MeshCreate				= 0x0200,
		MaterialCreate			= 0x0008,
		TextureLoad				= 0x0010,
		MeshComponentCreate		= 0x0020,
		LightComponentCreate	= 0x1000,
		ComponentFinalize		= 0x0040,
		MaterialFinalize		= 0x0080,
		MaterialAssign			= 0x0400,
		TextureCreate			= 0x0100,
		TextureAssign			= 0x0800,
		DeleteElement			= 0x2000,
		ResetScene				= 0x4000,
		AllTasks				= 0xffff
	};
}

ENUM_CLASS_FLAGS(EDatasmithRuntimeWorkerTask::Type);

namespace DatasmithRuntime
{
#ifdef LIVEUPDATE_TIME_LOGGING
	class Timer
	{
	public:
		Timer(double InTimeOrigin, const char* InText);
		~Timer();

	private:
		double TimeOrigin;
		double StartTime;
		FString Text;
	};
#endif

	typedef DirectLink::FSceneGraphId FSceneGraphId;

	typedef TFunctionRef<void(const TSharedPtr<IDatasmithActorElement>&, FSceneGraphId)> FParsingCallback;

	namespace EDataType
	{
		enum Type : uint8
		{
			None		= 0,
			Texture,
			Material,
			PbrMaterial,
			Mesh,
			Actor,
		};
	};

	struct FReferencer
	{
		uint32 Type:4;
		uint32 ElementId:28; // Assuming 2^28 should be plenty to store index of referencers
		int8 Slot; // Assuming 127 slots is plenty

		FReferencer(uint8 InType, FSceneGraphId InIndex, int8 InSlot)
			: Type( InType )
			, ElementId( uint32(InIndex) )
			, Slot( InSlot )
		{
		}

		FReferencer(uint8 InType, FSceneGraphId InIndex)
			: Type( InType )
			, ElementId( uint32(InIndex) )
			, Slot( 0 )
		{
		}

		FReferencer(FSceneGraphId InIndex)
			: Type( 0 )
			, ElementId( uint32(InIndex) )
			, Slot( 0 )
		{
		}

		FReferencer()
			: Type( 0 )
			, ElementId( 0 )
			, Slot( 0 )
		{
		}

		FSceneGraphId GetId() const { return (FSceneGraphId)ElementId; }
	};

	struct FBaseData
	{
		FSceneGraphId ElementId;

		TStrongObjectPtr< UObject > Object;

		FThreadSafeBool bCompleted;

		bool bProcessed;

		TArray< FReferencer > Referencers;

		FBaseData(FSceneGraphId InElementId)
			: ElementId(InElementId)
			, bCompleted(false)
			, bProcessed(false)
		{
		}

		template< typename T = UObject>
		T* GetObject()
		{
			return Cast< T >(Object.Get());
		}
	};

	struct FAssetData : public FBaseData
	{
		/** Build settings requirements defined by materials, used by static meshes */
		int32 Requirements;

		FMD5Hash Hash;

		FAssetData(FSceneGraphId InElementId)
			: FBaseData(InElementId)
			, Requirements(-1)
		{
		}

		FAssetData& operator=(const FAssetData& Source)
		{
			ElementId = Source.ElementId;
			Object = Source.Object;
			bCompleted = (bool)Source.bCompleted;
			Referencers = Source.Referencers;

			Requirements = Source.Requirements;
			Hash = Source.Hash;

			return *this;
		}

		static FAssetData EmptyAsset;
	};

	struct FActorData : public FBaseData
	{
		/** Index of parent actor in FSceneImporter's array of FActorData */
		FSceneGraphId ParentId;

		/** Transform relative to parent */
		FTransform RelativeTransform;

		/** Transform relative to world */
		FTransform WorldTransform;

		/** Index of referenced mesh in FSceneImporter's array of Datasmith elements */
		int32 MeshId;

		FActorData(FSceneGraphId InElementId)
			: FBaseData(InElementId)
			, ParentId(DirectLink::InvalidId)
			, MeshId(INDEX_NONE)
		{
		}

		FActorData(FSceneGraphId InElementId, FSceneGraphId InParentID)
			: FBaseData(InElementId)
			, ParentId(InParentID)
			, MeshId(INDEX_NONE)
		{
		}

		FActorData& operator=(const FActorData& Source)
		{
			ElementId = Source.ElementId;
			Object = Source.Object;
			bCompleted = (bool)Source.bCompleted;
			Referencers = Source.Referencers;

			ParentId = Source.ParentId;
			RelativeTransform = Source.RelativeTransform;
			WorldTransform = Source.WorldTransform;
			MeshId = Source.MeshId;

			return *this;
		}
	};

	// Texture assets can only be created and built on the main thread
	// Consequently, their creation has been divided in two steps:
	//		- Asynchronously load the data of the texture
	//		- At each tick create a texture from its data until all required textures are done
	struct FTextureData : public FAssetData
	{
		int32 Width;
		int32 Height;
		int16 Pitch;
		int16 BytesPerPixel;
		FUpdateTextureRegion2D Region;
		uint8* ImageData;
		// For IES profile
		float Brightness;
		float TextureMultiplier;

		FTextureData(FSceneGraphId InElementId)
			: FAssetData(InElementId)
			, Width(0)
			, Height(0)
			, Pitch(0)
			, BytesPerPixel(0)
			, Region(0,0,0,0,0,0)
			, ImageData(nullptr)
			, Brightness(-FLT_MAX)
			, TextureMultiplier(-FLT_MAX)
		{
			Requirements = EPixelFormat::PF_Unknown;
		}

		FTextureData(FAssetData& AssetData)
			: FTextureData(AssetData.ElementId)
		{
			Referencers = MoveTemp(AssetData.Referencers);
		}
	};

	namespace EActionType
	{
		enum Type : uint8
		{
			Unknown				= 0,
			CreateComponent		= 1,
			AssignMaterial		= 2,
			AssignTexture		= 3,
			CreateTexture		= 4,
			CreateLight			= 5,
			DeleteElement		= 6,
			UpdateElement		= 7,
		};
	};

	namespace EActionResult
	{
		enum Type : uint8
		{
			Unknown			= 0,
			Succeeded		= 1,
			Failed			= 2,
			Retry			= 3,
		};
	};

	using FActionTaskBase = TTuple< EActionType::Type, UObject*, FReferencer >;

	using FActionTaskFunction = TFunction<EActionResult::Type(UObject* Object, const FReferencer& Referencer)>;

	class FNewActionTask
	{
	public:
		~FNewActionTask()
		{
			//ensure(Executed > bCheck || (AssetId == DirectLink::InvalidId && Referencer.GetId() == DirectLink::InvalidId));
		}

		FNewActionTask()
			: AssetId(DirectLink::InvalidId)
		{
		}

		FNewActionTask(FActionTaskFunction&& Function, const FReferencer& InReferencer)
			: AssetId(DirectLink::InvalidId)
			, bIsTexture(false)
			, Referencer(InReferencer)
			, ActionFunc(MoveTemp(Function))
		{
		}

		FNewActionTask(const FActionTaskFunction& Function, const FReferencer& InReferencer)
			: AssetId(DirectLink::InvalidId)
			, bIsTexture(false)
			, Referencer(InReferencer)
			, ActionFunc(Function)
		{
		}

		FNewActionTask(FActionTaskFunction&& Function, FSceneGraphId InAssetId, const FReferencer& InReferencer)
			: AssetId(InAssetId)
			, bIsTexture(false)
			, Referencer(InReferencer)
			, ActionFunc(MoveTemp(Function))
		{
		}

		FNewActionTask(const FActionTaskFunction& Function, FSceneGraphId InAssetId, const FReferencer& InReferencer)
			: AssetId(InAssetId)
			, bIsTexture(false)
			, Referencer(InReferencer)
			, ActionFunc(Function)
		{
		}

		FNewActionTask(FActionTaskFunction&& Function, FSceneGraphId InAssetId, bool bInIsTexture, const FReferencer& InReferencer)
			: AssetId(InAssetId)
			, bIsTexture(bInIsTexture)
			, Referencer(InReferencer)
			, ActionFunc(MoveTemp(Function))
		{
		}

		FNewActionTask(const FActionTaskFunction& Function, FSceneGraphId InAssetId, bool bInIsTexture, const FReferencer& InReferencer)
			: AssetId(InAssetId)
			, bIsTexture(bInIsTexture)
			, Referencer(InReferencer)
			, ActionFunc(Function)
		{
		}

		FSceneGraphId GetAssetId() const { return AssetId; }

		bool IsTexture() const { return bIsTexture; }

		const FReferencer& GetReferencer() const { return Referencer; }

		EActionResult::Type Execute(FAssetData& AssetData)
		{
			return AssetData.bCompleted ? ActionFunc(AssetData.GetObject<>(), Referencer) : EActionResult::Retry;
		}
	
	private:
		FSceneGraphId AssetId;
		bool bIsTexture;
		FReferencer Referencer;
		FActionTaskFunction ActionFunc;
	};

	extern const FString TexturePrefix;
	extern const FString MaterialPrefix;
	extern const FString MeshPrefix;

	#define UPDATE_QUEUE	0
	#define DELETE_QUEUE	1
	#define MESH_QUEUE		2
	#define MATERIAL_QUEUE	3
	#define TEXTURE_QUEUE	4
	#define NONASYNC_QUEUE		5
	#define MAX_QUEUES		6

	class FSceneImporter : public FTickableGameObject
	{
	public:
		FSceneImporter() = delete;
		FSceneImporter(const FSceneImporter&) = delete;
		FSceneImporter& operator=(const FSceneImporter&) = delete;

		FSceneImporter(ADatasmithRuntimeActor* InDatasmithRuntimeActor);
		virtual ~FSceneImporter();

		void StartImport(TSharedRef< IDatasmithScene > InSceneElement);

		void Reset(bool bIsNewScene);

		int32 GetElementIdFromName(const FString& PrefixedName)
		{
			FSceneGraphId* ElementIdPtr = AssetElementMapping.Find(PrefixedName);
			return ElementIdPtr ? *ElementIdPtr : INDEX_NONE;
		}

		TSharedPtr< IDatasmithElement > GetElementFromName(const FString& PrefixedName)
		{
			FSceneGraphId* ElementIdPtr = AssetElementMapping.Find(PrefixedName);

			if (ElementIdPtr && Elements.Contains(*ElementIdPtr) )
			{
				return Elements[*ElementIdPtr];
			}

			return {};
		}

		TSharedPtr< IDatasmithElement > GetElement(FSceneGraphId ElementId)
		{
			if (Elements.Contains(ElementId))
			{
				return Elements[ElementId];
			}

			return {};
		}

		bool IncrementalUpdate(FUpdateContext& UpdateContext);

	protected:
		//~ Begin FTickableEditorObject interface
		virtual void Tick(float DeltaSeconds) override;
		virtual bool IsTickable() const override { return RootComponent.IsValid() && TasksToComplete != EDatasmithRuntimeWorkerTask::NoTask; }
		virtual TStatId GetStatId() const override;
		//~ End FTickableEditorObject interface

	private:

		void DeleteData();

		EActionResult::Type DeleteElement(FSceneGraphId ElementId);

		bool DeleteComponent(FActorData& ActorData);

		bool DeleteAsset(FAssetData& AssetData);

		void CollectSceneData();

		void SetupTasks();

		void AddAsset(TSharedPtr<IDatasmithElement>&& ElementPtr, const FString& Prefix);

		void ParseScene(const TSharedPtr<IDatasmithActorElement>& ActorElement, FSceneGraphId ParentId, FParsingCallback Callback);

		void ProcessActorElement(const TSharedPtr< IDatasmithActorElement >& ActorElement, FSceneGraphId ParentId);

		bool ProcessMeshActorData(FActorData& ActorData, IDatasmithMeshActorElement* MeshActorElement);

		bool ProcessLightActorData(FActorData& ActorData, IDatasmithLightActorElement* LightActorElement);

		bool ProcessCameraActorData(FActorData& ActorData, IDatasmithCameraActorElement* CameraActorElement);

		void ProcessMaterialData(FAssetData& MaterialData);

		EActionResult::Type ProcessMaterial(FSceneGraphId MaterialId);

		/** Mesh element section */
		void MeshPreProcessing();

		bool CreateStaticMesh(FSceneGraphId ElementId, float LightmapWeight);

		void ProcessMeshData(FAssetData& MeshData);

		EActionResult::Type CreateMeshComponent(FSceneGraphId ActorId, UStaticMesh* StaticMesh);

		EActionResult::Type AssignMaterial(const FReferencer& Referencer, UMaterialInstanceDynamic* Material);

		bool LoadTexture(FSceneGraphId ElementId);

		EActionResult::Type CreateTexture(FSceneGraphId ElementId);

		void ProcessTextureData(FSceneGraphId TextureId);

		EActionResult::Type AssignTexture(const FReferencer& Referencer, UTexture2D* Texture);

		EActionResult::Type AssignProfileTexture(const FReferencer& Referencer, UTextureLightProfile* TextureProfile);

		EActionResult::Type CreateLightComponent(FSceneGraphId ActorId);

		void SetupLightComponent(FActorData& ActorData, ULightComponent* LightComponent, IDatasmithLightActorElement* LightElement);

		void AddToQueue(int32 WhichQueue, FNewActionTask&& ActionTask)
		{
			++QueuedTaskCount;
			if (ActionTask.GetAssetId() != DirectLink::InvalidId)
			{
				FAssetData& AssetData = ActionTask.IsTexture() ? TextureDataList[ActionTask.GetAssetId()] : AssetDataList[ActionTask.GetAssetId()];
				AssetData.Referencers.Add(ActionTask.GetReferencer());
			}
			ActionQueues[WhichQueue].Enqueue(MoveTemp(ActionTask));
		}

		void ProcessQueue(int32 Which, double EndTime, EDatasmithRuntimeWorkerTask::Type TaskCompleted = EDatasmithRuntimeWorkerTask::NoTask, EDatasmithRuntimeWorkerTask::Type TaskFollowing = EDatasmithRuntimeWorkerTask::NoTask)
		{
			FNewActionTask ActionTask;
			while (FPlatformTime::Seconds() < EndTime)
			{
				if (!ActionQueues[Which].Dequeue(ActionTask))
				{
					TasksToComplete &= ~TaskCompleted;
					TasksToComplete |= TaskFollowing;

					break;
				}

				ensure(DirectLink::InvalidId == ActionTask.GetAssetId());
				ActionTask.Execute(FAssetData::EmptyAsset);
				ActionCounter.Increment();
			}
		}

	private:
		/** DatasmithRuntime actor associated with this model */
		TWeakObjectPtr<USceneComponent> RootComponent;

		/** IDatasmithScene associated with DatasmithRuntime actor */
		TSharedPtr<IDatasmithScene> SceneElement;

		/** Flatten list of all elements in the IDatasmithScene */
		TMap< FSceneGraphId, TSharedPtr< IDatasmithElement > > Elements;

		/** Mapping between prefixed asset element's name and index of element in flatten element list */
		TMap< FString, FSceneGraphId > AssetElementMapping;

		TMap< FSceneGraphId, FAssetData > AssetDataList;

		FCriticalSection MeshCriticalSection;

		TSet< FSceneGraphId > MeshElementSet;

		TSet< FSceneGraphId > MaterialElementSet;
		TArray< FSceneGraphId > MaterialElementArray;

		TSet< FSceneGraphId > TextureElementSet;

		TMap< FSceneGraphId, FActorData > ActorDataList;

		TMap< FSceneGraphId, FTextureData > TextureDataList;

		TMap< FSceneGraphId, float > LightmapWeights;

		TQueue< FNewActionTask, EQueueMode::Mpsc > ActionQueues[MAX_QUEUES];

		int32 QueuedTaskCount;

		EDatasmithRuntimeWorkerTask::Type TasksToComplete;

		uint8 bGarbageCollect:1;
		uint8 bIncrementalUpdate:1;

		float& OverallProgress;
		FThreadSafeCounter ActionCounter;
		double ProgressStep;

	#ifdef LIVEUPDATE_TIME_LOGGING
		double GlobalStartTime;
	#endif
	};

} // End namespace DatasmithRuntime

#ifdef LIVEUPDATE_TIME_LOGGING
#define LIVEUPDATE_LOG_TIME Timer( GlobalStartTime, __func__ )
#else
#define LIVEUPDATE_LOG_TIME
#endif
