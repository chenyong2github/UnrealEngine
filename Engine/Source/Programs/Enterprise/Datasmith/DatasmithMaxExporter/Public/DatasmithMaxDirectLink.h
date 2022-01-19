// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithMaxExporterDefines.h"

#include "IDatasmithSceneElements.h"
#include "DatasmithMesh.h"

#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDatasmithMaxExporter, Log, All);

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "impexp.h"
	#include "max.h"

	#include "decomp.h" // for matrix
	#include "ISceneEventManager.h"

MAX_INCLUDES_END


// Log all messages
// #define LOG_DEBUG_HEAVY_ENABLE 1
// #define LOG_DEBUG_ENABLE 1

void AssignMeshMaterials(TSharedPtr<IDatasmithMeshElement>&MeshElement, Mtl * Material, const TSet<uint16>&SupportedChannels);

namespace DatasmithMaxDirectLink
{

class ISceneTracker;
class FLayerTracker;


// Global export options, stored in preferences
class IPersistentExportOptions
{
public:
	 // Whether to export all visible or selected objects only
	virtual void SetSelectedOnly(bool) = 0;
	virtual bool GetSelectedOnly() = 0;


	// Whether to export animation or not
	virtual void SetAnimatedTransforms(bool) = 0;
	virtual bool GetAnimatedTransforms() = 0;
};

//---- Main class for export/change tracking
class IExporter
{
	public:
	virtual ~IExporter() {}

	virtual void Shutdown() = 0;

	virtual void SetOutputPath(const TCHAR* Path) = 0;
	virtual void SetName(const TCHAR* Name) = 0;

	virtual ISceneTracker& GetSceneTracker() = 0;

	// Scene update
	virtual void ParseScene() = 0;
	virtual bool UpdateScene(bool bQuiet) = 0;
	virtual void Reset() = 0;

	// ChangeTracking
	virtual void StartSceneChangeTracking() = 0;

	// DirectLink
	virtual void InitializeDirectLinkForScene() = 0;
	virtual void UpdateDirectLinkScene() = 0;
	virtual bool ToggleAutoSync() = 0;
	virtual bool IsAutoSyncEnabled() = 0;
	virtual void SetAutoSyncDelay(float Seconds) = 0;
	virtual void SetAutoSyncIdleDelay(float Seconds) = 0;
};

bool CreateExporter(bool bEnableUI, const TCHAR* EnginePath); // Create exporter with ability for DirectLink change tracking
IExporter* GetExporter();
void ShutdownExporter();
bool Export(const TCHAR* Name, const TCHAR* OutputPath, bool bQuiet);
void ShutdownScripts();


IPersistentExportOptions& GetPersistentExportOptions();

// Max nodes are matched by these keys
typedef NodeEventNamespace::NodeKey FNodeKey;
typedef MtlBase* FMaterialKey;

// Identifies Max node to track its changes
class FNodeTracker: FNoncopyable
{
public:
	explicit FNodeTracker(FNodeKey InNodeKey, INode* InNode) : NodeKey(InNodeKey), Node(InNode), Name(Node->GetName()) {}

	void Invalidate()
	{
		bInvalidated = true;
	}

	bool IsInvalidated() const
	{
		return bInvalidated;
	}

	bool IsInstance()
	{
		return InstanceHandle != 0;
	}

	FNodeKey NodeKey;
	INode* const Node;
	FNodeTracker* Collision = nullptr;
	FLayerTracker* Layer = nullptr;
	AnimHandle InstanceHandle = 0; // todo: rename - this is handle for object this node is instance of
	FString Name; 
	bool bInvalidated = true;
	bool bDeleted = false;

	TSharedPtr<IDatasmithActorElement> DatasmithActorElement;

	class FMaterialTracker* MaterialTracker = nullptr;

	TSharedPtr<IDatasmithMeshActorElement> DatasmithMeshActor;

};

class FRenderMeshForConversion: FNoncopyable
{
public:
	explicit FRenderMeshForConversion(){}
	explicit FRenderMeshForConversion(INode* InNode, Mesh* InMaxMesh, bool bInNeedsDelete, FTransform InPivot=FTransform::Identity)
		: Node(InNode)
		, MaxMesh(InMaxMesh)
		, bNeedsDelete(bInNeedsDelete)
		, Pivot(InPivot)
	{}
	~FRenderMeshForConversion()
	{
		if (bNeedsDelete)
		{
			MaxMesh->DeleteThis();
		}
	}

	bool IsValid() const
	{
		return MaxMesh != nullptr;
	}

	INode* GetNode() const
	{
		return Node;
	}

	Mesh* GetMesh() const
	{
		return MaxMesh;
	}

	const FTransform& GetPivot() const
	{
		return Pivot;
	}


private:
	INode* Node = nullptr;
	Mesh* MaxMesh = nullptr;
	bool bNeedsDelete = false;
	FTransform Pivot;

};

// Modifies Datasmith scene in responce to change notification calls
// Subscription to various Max notification systems is done separately, see  FNotifications
class ISceneTracker
{
public:
	virtual ~ISceneTracker(){}

	// Change notifications
	virtual void NodeAdded(INode* Node) = 0;
	virtual void NodeDeleted(INode* Node) = 0;
	virtual void NodeGeometryChanged(FNodeKey NodeKey) = 0;
	virtual void NodeHideChanged(FNodeKey NodeKey) = 0;
	virtual void NodePropertiesChanged(FNodeKey NodeKey) = 0;
	virtual void NodeTransformChanged(FNodeKey NodeKey) = 0;
	virtual void NodeMaterialAssignmentChanged(FNodeKey NodeKey) = 0;
	virtual void NodeMaterialGraphModified(FNodeKey NodeKey) = 0;

	// Scene modification
	virtual void AddMeshElement(TSharedPtr<IDatasmithMeshElement>& Mesh, FDatasmithMesh& DatasmithMesh, FDatasmithMesh* CollisionMesh) = 0;
	virtual void ReleaseMeshElement(TSharedPtr<IDatasmithMeshElement> Mesh) = 0;
	virtual void SetupActor(FNodeTracker& NodeTracker) = 0;
	virtual void SetupDatasmithHISMForNode(FNodeTracker& NodeTracker, INode* GeometryNode, const FRenderMeshForConversion& RenderMesh, Mtl* Material, int32 MeshIndex, const TArray<Matrix3>& Transforms) = 0;
	virtual void RemoveMaterial(const TSharedPtr<IDatasmithBaseMaterialElement>& DatasmithMaterial) = 0;
};

//---- Geometry utility function
bool ConvertRailClone(ISceneTracker& SceneTracker, FNodeTracker& NodeTracker, Object* Obj);
bool ConvertForest(ISceneTracker& Scene, FNodeTracker& NodeTracker, Object* Obj);

Mesh* GetMeshFromRenderMesh(INode* Node, BOOL& bNeedsDelete, TimeValue CurrentTime);
FRenderMeshForConversion GetMeshForGeomObject(INode* Node, Object* Obj); // Extract mesh using already evaluated object
FRenderMeshForConversion GetMeshForNode(INode* Node, FTransform Pivot); // Extract mesh evaluating node object
FRenderMeshForConversion GetMeshForCollision(INode* Node);

void FillDatasmithMeshFromMaxMesh(FDatasmithMesh& DatasmithMesh, Mesh& MaxMesh, INode* ExportedNode, bool bForceSingleMat, TSet<uint16>& SupportedChannels, TMap<int32, int32>& UVChannelsMap, FTransform Pivot);


// Creates Mesh element and converts max mesh into it
bool ConvertMaxMeshToDatasmith(ISceneTracker& Scene, TSharedPtr<IDatasmithMeshElement>& DatasmithMeshElement, INode* Node, const TCHAR* MeshName, const FRenderMeshForConversion& RenderMesh, TSet<uint16>& SupportedChannels, const FRenderMeshForConversion& CollisionMesh = FRenderMeshForConversion());

bool OpenDirectLinkUI();
const TCHAR* GetDirectlinkCacheDirectory();


//---- Max Notifications/Events/Callbacks handling
class FNodeEventCallback;
class FNodeObserver;
class FMaterialObserver;

class FNotifications
{
public:

	FNotifications(IExporter& InExporter);
	~FNotifications();

	void RegisterForSystemNotifications();

	void StartSceneChangeTracking();
	void StopSceneChangeTracking();

	void AddNode(INode*);

	FString ConvertNotificationCodeToString(int code);

	static void On3dsMaxNotification(void* param, NotifyInfo* info);

	bool bSceneChangeTracking = false;

	IExporter& Exporter;
	TMap<int, FString> NotificationCodetoString; // todo: remove, just for debug to output strings for notification codes
	TArray<int> NotificationCodesRegistered;
	TUniquePtr<FNodeEventCallback> NodeEventCallback;
	TUniquePtr<FNodeObserver> NodeObserver;
	TUniquePtr<FMaterialObserver> MaterialObserver;
};


class FDatasmithConverter
{
public:
	const float UnitToCentimeter;

	FDatasmithConverter();

	FVector toDatasmithVector(Point3 Point) const
	{
		return FVector(UnitToCentimeter * Point.x,
			UnitToCentimeter * -Point.y,
			UnitToCentimeter * Point.z);
	}

	FVector toDatasmithNormal(Point3 Point) const
	{
		return FVector(Point.x, -Point.y, Point.z);
	}

	FColor toDatasmithColor(Point3& Point)
	{
		//The 3ds Max vertex colors are encoded from 0 to 1 in float
		return FColor(Point.x * MAX_uint8, Point.y * MAX_uint8, Point.z * MAX_uint8);
	}

	void MaxToUnrealCoordinates(Matrix3 Matrix, FVector& Translation, FQuat& Rotation, FVector& Scale)
	{
		Point3 Pos = Matrix.GetTrans();
		Translation.X = Pos.x * UnitToCentimeter;
		Translation.Y = -Pos.y * UnitToCentimeter;
		Translation.Z = Pos.z * UnitToCentimeter;

		// Clear the transform on the matrix
		Matrix.NoTrans();

		// We're only doing Scale - save out the
		// rotation so we can put it back
		AffineParts Parts;
		decomp_affine(Matrix, &Parts);
		ScaleValue ScaleVal = ScaleValue(Parts.k * Parts.f, Parts.u);
		Scale = FVector(ScaleVal.s.x, ScaleVal.s.y, ScaleVal.s.z);

		Rotation = FQuat(Parts.q.x, -Parts.q.y, Parts.q.z, Parts.q.w);
	}
};

//---- Material change tracking
class FMaterialTracker
{
public:
	explicit FMaterialTracker(Mtl* InMaterial) : Material(InMaterial) {}


	Mtl* Material;

	TArray<Mtl*> Materials; // Actual materials used for this assigned material
	TArray<Texmap*> Textures;

	bool bInvalidated = true;

	TArray<Mtl*>& GetActualMaterials()
	{
		return Materials;
	}

	void ResetActualMaterialAndTextures()
	{
		Materials.Reset();
		Textures.Reset(); //todo: unregister textures
	}

	void AddActualMaterial(Mtl* ActualMaterial)
	{
		Materials.AddUnique(ActualMaterial);
	}

	void AddActualTexture(Texmap* Texture)
	{
		Textures.AddUnique(Texture);
	}
};

class FMaterialTrackerHandle
{
public:

	explicit FMaterialTrackerHandle(Mtl* InMaterial) : Impl(MakeShared<FMaterialTracker>(InMaterial)) {}

	FMaterialTracker* GetMaterialTracker()
	{
		return Impl.Get();
	}

private:
	TSharedPtr<FMaterialTracker> Impl; // todo: reuse material tracker objects(e.g. make a pool)

	FMaterialTracker& GetImpl()
	{
		return *Impl;
	}
};


class FMaterialsCollectionTracker
{
public:

	FMaterialsCollectionTracker(ISceneTracker& InSceneTracker) : SceneTracker(InSceneTracker) {}

	void Reset();

	FMaterialTracker* AddMaterial(Mtl* Material);

	void InvalidateMaterial(Mtl* Material);

	void UpdateMaterial(FMaterialTracker* MaterialTracker);

	// When Material is not used my scene anymore - stop tracking it
	void ReleaseMaterial(FMaterialTracker& MaterialTracker);

	// Clean all converted data, remove from datasmith scene(before rebuild)
	void RemoveConvertedMaterial(FMaterialTracker& MaterialTracker);

	void ResetInvalidatedMaterials()
	{
		InvalidatedMaterialTrackers.Reset();		
	}

	TSet<FMaterialTracker*> GetInvalidatedMaterials()
	{
		return InvalidatedMaterialTrackers;
	}

	void SetDatasmithMaterial(Mtl* ActualMaterial, TSharedPtr<IDatasmithBaseMaterialElement> DatastmihMaterial)
	{
		UsedMaterialToDatasmithMaterial.Add(ActualMaterial, DatastmihMaterial);
	}

	ISceneTracker& SceneTracker;

	TMap<FMaterialKey, FMaterialTrackerHandle> MaterialTrackers; // Tracks all assigned materials
	TSet<FMaterialTracker*> InvalidatedMaterialTrackers; // Materials needing update

	TSet<Mtl*> EncounteredMaterials; // All materials from the assigned material's graphs 
	TSet<Texmap*> EncounteredTextures; // All materials from the assigned material's graphs 

	TArray<FString> MaterialNames; // todo: UETOOL-4369 fix changing materials names(to make them unique for easy export)

	TMap<Mtl*, TSet<FMaterialTracker*>> UsedMaterialToMaterialTracker; // Materials uses by nodes keep set of assigned materials they are used for
	TMap<Mtl*, TSharedPtr<IDatasmithBaseMaterialElement>> UsedMaterialToDatasmithMaterial;
};

//----

#ifdef LOG_DEBUG_HEAVY_ENABLE
	#define LOG_DEBUG_HEAVY(message) LogDebug(message)
#else
	#define LOG_DEBUG_HEAVY(message) 
#endif

void LogDebug(const TCHAR* Msg);
void LogDebug(const FString& Msg);
void LogInfo(const TCHAR* Msg);
void LogInfo(const FString& Msg);
void LogDebugNode(const FString& Name, class INode* Node);
void LogNodeEvent(const MCHAR* Name, INodeEventCallback::NodeKeyTab& nodes);
void LogFlush();

void LogWarningDialog(const TCHAR* Msg);
void LogWarningDialog(const FString& Msg);

}


#include "Windows/HideWindowsPlatformTypes.h"
