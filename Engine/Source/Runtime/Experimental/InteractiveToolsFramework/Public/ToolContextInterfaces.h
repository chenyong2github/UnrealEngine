// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ComponentSourceInterfaces.h"
#include "UObject/ObjectMacros.h"


// predeclarations so we don't have to include these in all tools
class UWorld;
class AActor;
class UActorComponent;
class FToolCommandChange;
class UPackage;
class FPrimitiveDrawInterface;
class FSceneView;
class UInteractiveToolManager;
class UInteractiveGizmoManager;
struct FMeshDescription;
class UTexture2D;

#if WITH_EDITOR
class HHitProxy;
#endif

/**
 * FToolBuilderState is a bucket of state information that a ToolBuilder might need
 * to construct a Tool. This information comes from a level above the Tools framework,
 * and depends on the context we are in (Editor vs Runtime, for example).
 */
struct INTERACTIVETOOLSFRAMEWORK_API FToolBuilderState
{
	/** The current UWorld */
	UWorld* World = nullptr;
	/** The current ToolManager */
	UInteractiveToolManager* ToolManager = nullptr;
	/** The current GizmoManager */
	UInteractiveGizmoManager* GizmoManager = nullptr;

	/** Current selected Actors. May be empty or nullptr. */
	TArray<AActor*> SelectedActors;
	/** Current selected Components. May be empty or nullptr. */
	TArray<UActorComponent*> SelectedComponents;
};


/**
 * FViewCameraState is a bucket of state information that a Tool might
 * need to implement interactions that depend on the current scene view.
 */
struct INTERACTIVETOOLSFRAMEWORK_API FViewCameraState
{
	/** Current camera/head position */
	FVector Position;
	/** Current camera/head orientation */
	FQuat Orientation;
	/** Current Horizontal Field of View Angle in Degrees. Only relevant if bIsOrthographic is false. */
	float HorizontalFOVDegrees;
	/** Current width of viewport in world space coordinates. Only valid if bIsOrthographic is true. */
	float OrthoWorldCoordinateWidth;
	/** Current Aspect Ratio */
	float AspectRatio;
	/** Is current view an orthographic view */
	bool bIsOrthographic;
	/** Is current view a VR view */
	bool bIsVR;

	/** @return "right"/horizontal direction in camera plane */
	FVector Right() const { return Orientation.GetAxisY(); }
	/** @return "up"/vertical direction in camera plane */
	FVector Up() const { return Orientation.GetAxisZ(); }
	/** @return forward camera direction */
	FVector Forward() const { return Orientation.GetAxisX(); }

	/** @return scaling factor that should be applied to PDI thickness/size */
	float GetPDIScalingFactor() const { return HorizontalFOVDegrees / 90.0f; }

	/** @return FOV normalization factor that should be applied when comparing angles */
	float GetFOVAngleNormalizationFactor() const { return HorizontalFOVDegrees / 90.0f; }

};



/** Types of Snap Queries that a ToolsContext parent may support, that Tools may request */
UENUM()
enum class ESceneSnapQueryType
{
	/** snapping a position */
	Position = 1,
	Rotation = 2
};

/** Types of snap targets that a Tool may want to run snap queries against. */
UENUM()
enum class ESceneSnapQueryTargetType
{
	None = 0,
	/** Consider any mesh vertex */
	MeshVertex = 1,
	/** Consider any mesh edge */
	MeshEdge = 2,
	/** Grid Snapping */
	Grid = 4,

	All = MeshVertex | MeshEdge | Grid
};
ENUM_CLASS_FLAGS(ESceneSnapQueryTargetType);

/**
 * Configuration variables for a IToolsContextQueriesAPI snap query request.
 */
struct INTERACTIVETOOLSFRAMEWORK_API FSceneSnapQueryRequest
{
	/** What type of snap query geometry is this */
	ESceneSnapQueryType RequestType = ESceneSnapQueryType::Position;
	/** What does caller want to try to snap to */
	ESceneSnapQueryTargetType TargetTypes = ESceneSnapQueryTargetType::Grid;

	/** Optional explicitly specified position grid */
	TOptional<FVector> GridSize{};

	/** Optional explicitly specified rotation grid */
	TOptional<FRotator> RotGridSize{};

	/** Snap input position */
	FVector Position;
	/** Another position must deviate less than this number of degrees (in visual angle) to be considered an acceptable snap position */
	float VisualAngleThresholdDegrees;

	/** Snap input direction */
	FVector Direction;
	/** Another direction must deviate less than this number of degrees from Direction to be considered an acceptable snap direction */
	float DirectionAngleThresholdDegrees;

	/** Snap input rotation delta */
	FQuat DeltaRotation;
};


/**
 * Computed result of a IToolsContextQueriesAPI snap query request
 */
struct INTERACTIVETOOLSFRAMEWORK_API FSceneSnapQueryResult
{
	/** Actor that owns snap target */
	AActor* TargetActor = nullptr;
	/** Component that owns snap target */
	UActorComponent* TargetComponent = nullptr;
	/** What kind of geometric element was snapped to */
	ESceneSnapQueryTargetType TargetType = ESceneSnapQueryTargetType::None;

	/** Snap position (may not be set depending on query types) */
	FVector Position;
	/** Snap normal (may not be set depending on query types) */
	FVector Normal;
	/** Snap direction (may not be set depending on query types) */
	FVector Direction;
	/** Snap rotation delta (may not be set depending on query types) */
	FQuat   DeltaRotation;

	/** Vertices of triangle that contains result (for debugging, may not be set) */
	FVector TriVertices[3];
	/** Vertex/Edge index we snapped to in triangle */
	int TriSnapIndex;

};



/** Types of standard materials that Tools may request from Context */
UENUM()
enum class EStandardToolContextMaterials
{
	/** White material that displays vertex colors set on mesh */
	VertexColorMaterial = 1
};


/** Types of coordinate systems that a Tool/Gizmo might use */
UENUM()
enum class EToolContextCoordinateSystem
{
	World = 0,
	Local = 1
};


/**
 * Users of the Tools Framework need to implement IToolsContextQueriesAPI to provide
 * access to scene state information like the current UWorld, active USelections, etc.
 */
class IToolsContextQueriesAPI
{
public:
	virtual ~IToolsContextQueriesAPI() {}

	/**
	 * Collect up current-selection information for the current scene state (ie what is selected in Editor, etc)
	 * @param StateOut this structure is populated with available state information
	 */
	virtual void GetCurrentSelectionState(FToolBuilderState& StateOut) const = 0;


	/**
	 * Request information about current view state
	 * @param StateOut this structure is populated with available state information
	 */
	virtual void GetCurrentViewState(FViewCameraState& StateOut) const = 0;


	/**
	 * Request current external coordinate-system setting
	 */
	virtual EToolContextCoordinateSystem GetCurrentCoordinateSystem() const = 0;	


	/**
	 * Try to find Snap Targets in the scene that satisfy the Snap Query.
	 * @param Request snap query configuration
	 * @param Results list of potential snap results
	 * @return true if any valid snap target was found
	 * @warning implementations are not required (and may not be able) to support snapping
	 */
	virtual bool ExecuteSceneSnapQuery(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results ) const = 0;


	/**
	 * Many tools need standard types of materials that the user should provide (eg a vertex-color material, etc)
	 * @param MaterialType the type of material being requested
	 * @return Instance of material to use for this purpose
	 */
	virtual UMaterialInterface* GetStandardMaterial(EStandardToolContextMaterials MaterialType) const = 0;

#if WITH_EDITOR
	/**
	* When selecting, sometimes we need a hit proxy rather than a physics trace or other raycast.                                                                   
	*/
	virtual HHitProxy* GetHitProxy(int32 X, int32 Y) const = 0;
#endif
};




/** Level of severity of messages emitted by Tool framework */
UENUM()
enum class EToolMessageLevel
{
	/** Development message goes into development log */
	Internal = 0,
	/** User message should appear in user-facing log */
	UserMessage = 1,
	/** Notification message should be shown in a non-modal notification window */
	UserNotification = 2,
	/** Warning message should be shown in a non-modal notification window with panache */
	UserWarning = 3,
	/** Error message should be shown in a modal notification window */
	UserError = 4
};


/** Type of change we want to apply to a selection */
UENUM()
enum class ESelectedObjectsModificationType
{
	Replace = 0,
	Add = 1,
	Remove = 2,
	Clear = 3
};


/** Represents a change to a set of selected Actors and Components */
struct FSelectedOjectsChangeList
{
	/** How should this list be interpreted in the context of a larger selection set */
	ESelectedObjectsModificationType ModificationType;
	/** List of Actors */
	TArray<AActor*> Actors;
	/** List of Componets */
	TArray<UActorComponent*> Components;
};


/**
 * Users of the Tools Framework need to implement IToolsContextTransactionsAPI so that
 * the Tools have the ability to create Transactions and emit Changes. Note that this is
 * technically optional, but that undo/redo won't be supported without it.
 */
class IToolsContextTransactionsAPI
{
public:
	virtual ~IToolsContextTransactionsAPI() {}

	/**
	 * Request that context display message information.
	 * @param Message text of message
	 * @param Level severity level of message
	 */
	virtual void DisplayMessage(const FText& Message, EToolMessageLevel Level) = 0;

	/** 
	 * Forward an invalidation request from Tools framework, to cause repaint/etc. 
	 * This is not always necessary but in some situations (eg in Non-Realtime mode in Editor)
	 * a redraw will not happen every frame. 
	 * See UInputRouter for options to enable auto-invalidation.
	 */
	virtual void PostInvalidation() = 0;
	
	/**
	 * Begin a Transaction, whatever this means in the current Context. For example in the
	 * Editor it means open a GEditor Transaction. You must call EndUndoTransaction() after calling this.
	 * @param Description text description of the transaction that could be shown to user
	 */
	virtual void BeginUndoTransaction(const FText& Description) = 0;

	/**
	 * Complete the Transaction. Assumption is that Begin/End are called in pairs.
	 */
	virtual void EndUndoTransaction() = 0;

	/**
	 * Insert an FChange into the transaction history in the current Context. 
	 * This cannot be called between Begin/EndUndoTransaction, the FChange should be 
	 * automatically inserted into a Transaction.
	 * @param TargetObject The UObject this Change is applied to
	 * @param Change The Change implementation
	 * @param Description text description of the transaction that could be shown to user
	 */
	virtual void AppendChange(UObject* TargetObject, TUniquePtr<FToolCommandChange> Change, const FText& Description) = 0;



	/**
	 * Request a modification to the current selected objects
	 * @param SelectionChange desired modification to current selection
	 * @return true if the selection change could be applied
	 */
	virtual bool RequestSelectionChange(const FSelectedOjectsChangeList& SelectionChange) = 0;

};

UENUM()
enum class EViewInteractionState {
	None = 0,
	Hovered = 1,
	Focused = 2
};
ENUM_CLASS_FLAGS(EViewInteractionState);

/**
 * Users of the Tools Framework need to implement IToolsContextRenderAPI to allow
 * Tools, Indicators, and Gizmos to make low-level rendering calls for things like line drawing.
 * This API will be passed to eg UInteractiveTool::Render(), so access is only provided when it
 * makes sense to call the functions
 */
class IToolsContextRenderAPI
{
public:
	virtual ~IToolsContextRenderAPI() {}

	/** @return Current PDI */
	virtual FPrimitiveDrawInterface* GetPrimitiveDrawInterface() = 0;

	/** @return Current FSceneView */
	virtual const FSceneView* GetSceneView() = 0;

	/** @return Current Camera State for this Render API */
	virtual FViewCameraState GetCameraState() = 0;

	/** @return Current interaction state of the view to render */
	virtual EViewInteractionState GetViewInteractionState() = 0;
};


/**
 * FGeneratedStaticMeshAssetConfig is passed to IToolsContextAssetAPI::GenerateStaticMeshActor() to
 * provide the underlying mesh, materials, and configuration settings. Note that all implementations
 * may not use/respect all settings.
 */
struct FGeneratedStaticMeshAssetConfig
{
	INTERACTIVETOOLSFRAMEWORK_API ~FGeneratedStaticMeshAssetConfig();

	TUniquePtr<FMeshDescription> MeshDescription;
	TArray<UMaterialInterface*> Materials;

	bool bEnableRecomputeNormals = false;
	bool bEnableRecomputeTangents = false;

	bool bEnablePhysics = true;
	bool bEnableComplexAsSimpleCollision = true;
};


/**
 * Users of the Tools Framework need to provide an IToolsContextAssetAPI implementation
 * that allows Packages and Assets to be created/saved. Note that this is not strictly
 * necessary, for example a trivial implementation could just store things in the Transient
 * package and not do any saving.
 */
class IToolsContextAssetAPI
{
public:
	virtual ~IToolsContextAssetAPI() {}

	/** 
	 * Get a path to save assets in that is relative to the given UWorld. 
	 */
	virtual FString GetWorldRelativeAssetRootPath(const UWorld* World) = 0;

	/** 
	 * Get a "currently-visible/selected" location to save assets in. For example the currently-visible path in the Editor Content Browser.
	 */
	virtual FString GetActiveAssetFolderPath() = 0;

	/** Allow the user to select a path and filename for an asset using a modal dialog */
	virtual FString InteractiveSelectAssetPath(const FString& DefaultAssetName, const FText& DialogTitleMessage) = 0;

	/**
	 * Creates a new package for an asset
	 * @param FolderPath path for new package
	 * @param AssetBaseName base name for asset
	 * @param UniqueAssetNameOut unique name in form of AssetBaseName##, where ## is a unqiue index
	 * @return new package
	 */
	virtual UPackage* MakeNewAssetPackage(const FString& FolderPath, const FString& AssetBaseName, FString& UniqueAssetNameOut) = 0;

	/** Request saving of asset to persistent storage via something like an interactive popup dialog */
	virtual void InteractiveSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage) = 0;

	/** Autosave asset to persistent storage */
	virtual void AutoSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage) = 0;

	/** Notify that asset has been created and is dirty */
	virtual void NotifyGeneratedAssetModified(UObject* Asset, UPackage* AssetPackage) = 0;



	/**
	 * Create a new UStaticMeshAsset for the provided mesh, then a new UStaticMeshComponent and AStaticMeshActor in the TargetWorld
	 * @param TargetWorld world to create new Actor in
	 * @param Transform transform to set on Actor
	 * @param ObjectBaseName a base name for the Asset and Actor. Unique name will be generated by appending a number, if a name collision occurs.
	 * @param AssetConfig configuration information for the new Asset
	 * @return new AStaticMeshActor with new Asset assigned to it's UStaticMeshComponent
	 * @warning this function may return null. In this case the asset creation failed and/or the user cancelled during the process, if it was interactive.
	 */
	virtual AActor* GenerateStaticMeshActor(
		UWorld* TargetWorld,
		FTransform Transform,
		FString ObjectBaseName,
		FGeneratedStaticMeshAssetConfig&& AssetConfig) = 0;


	/**
	 * Save a generated UTexture2D as an Asset.
	 * Assumption is that the texture was generated in code and is in the Transient package.
	 * @param GeneratedTexture Texture2D to save
	 * @param ObjectBaseName a base name for the Asset. Unique name will be generated by appending a number, if a name collision occurs.
	 * @param RelativeToAsset New texture will be saved at the same path as the UPackage for this UObject, which we assume to be an Asset (eg like a UStaticMesh)
	 */
	virtual bool SaveGeneratedTexture2D(
		UTexture2D* GeneratedTexture,
		FString ObjectBaseName,
		const UObject* RelativeToAsset)
	{
		check(false); return false;
	}
};

