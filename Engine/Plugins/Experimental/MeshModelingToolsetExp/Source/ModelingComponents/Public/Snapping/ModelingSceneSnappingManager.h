// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "SceneQueries/SceneSnappingManager.h"
#include "ModelingSceneSnappingManager.generated.h"


class IToolsContextQueriesAPI;
class UInteractiveToolsContext;
class UInteractiveToolManager;


/**
 * UModelingSceneSnappingManager is an implementation of snapping suitable for use in
 * Modeling Tools/Gizmos (and potentially other places). 
 * 
 * Currently Supports:
 *    - snap to position/rotation grid
 *    - snap to vertex position for Static Mesh Assets
 *    - snap to edge position for Static Mesh Assets
 * 
 * Note that currently the StaticMesh vertex/edge snapping is dependent on the Physics
 * system, and may fail or return nonsense results in some cases, due to the physics
 * complex-collision mesh deviating from the source-model mesh
 */
UCLASS()
class MODELINGCOMPONENTS_API UModelingSceneSnappingManager : public USceneSnappingManager
{
	GENERATED_BODY()

public:

	virtual void Initialize(TObjectPtr<UInteractiveToolsContext> ToolsContext);
	virtual void Shutdown();


	/**
	* Try to find Snap Targets in the scene that satisfy the Snap Query.
	* @param Request snap query configuration
	* @param Results list of potential snap results
	* @return true if any valid snap target was found
	* @warning implementations are not required (and may not be able) to support snapping
	*/
	virtual bool ExecuteSceneSnapQuery(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const override;


protected:

	UPROPERTY()
	TObjectPtr<UInteractiveToolsContext> ParentContext;

	const IToolsContextQueriesAPI* QueriesAPI = nullptr;

	virtual bool ExecuteSceneSnapQueryRotation(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const;
	virtual bool ExecuteSceneSnapQueryPosition(const FSceneSnapQueryRequest& Request, TArray<FSceneSnapQueryResult>& Results) const;
};





namespace UE
{
namespace Geometry
{
	//
	// The functions below are helper functions that simplify usage of a UModelingSceneSnappingManager 
	// that is registered as a ContextStoreObject in an InteractiveToolsContext
	//

	/**
	 * If one does not already exist, create a new instance of UModelingSceneSnappingManager and add it to the
	 * ToolsContext's ContextObjectStore
	 * @return true if the ContextObjectStore now has a UModelingSceneSnappingManager (whether it already existed, or was created)
	 */
	MODELINGCOMPONENTS_API bool RegisterSceneSnappingManager(UInteractiveToolsContext* ToolsContext);

	/**
	 * Remove any existing UModelingSceneSnappingManager from the ToolsContext's ContextObjectStore
	 * @return true if the ContextObjectStore no longer has a UModelingSceneSnappingManager (whether it was removed, or did not exist)
	 */
	MODELINGCOMPONENTS_API bool DeregisterSceneSnappingManager(UInteractiveToolsContext* ToolsContext);


	/**
	 * Find an existing UModelingSceneSnappingManager in the ToolsContext's ContextObjectStore
	 * @return SelectionManager pointer or nullptr if not found
	 */
	MODELINGCOMPONENTS_API UModelingSceneSnappingManager* FindModelingSceneSnappingManager(UInteractiveToolManager* ToolManager);


}
}