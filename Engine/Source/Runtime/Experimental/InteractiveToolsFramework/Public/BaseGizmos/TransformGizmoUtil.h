// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmo.h"
#include "BaseGizmos/TransformGizmo.h"

#include "TransformGizmoUtil.generated.h"

class UInteractiveToolsContext;
class UInteractiveToolManager;
class UInteractiveGizmoManager;
class FTransformGizmoActorFactory;


namespace UE
{
namespace TransformGizmoUtil
{
	//
	// The functions below are helper functions that simplify usage of a UTransformGizmoContextObject 
	// that is registered as a ContextStoreObject in an InteractiveToolsContext
	//

	/**
	 * If one does not already exist, create a new instance of UTransformGizmoContextObject and add it to the
	 * ToolsContext's ContextObjectStore
	 * @return true if the ContextObjectStore now has a UTransformGizmoContextObject (whether it already existed, or was created)
	 */
	INTERACTIVETOOLSFRAMEWORK_API bool RegisterTransformGizmoContextObject(UInteractiveToolsContext* ToolsContext);

	/**
	 * Remove any existing UTransformGizmoContextObject from the ToolsContext's ContextObjectStore
	 * @return true if the ContextObjectStore no longer has a UTransformGizmoContextObject (whether it was removed, or did not exist)
	 */
	INTERACTIVETOOLSFRAMEWORK_API bool DeregisterTransformGizmoContextObject(UInteractiveToolsContext* ToolsContext);


	/**
	 * Spawn a new standard 3-axis Transform gizmo (see UTransformGizmoContextObject::Create3AxisTransformGizmo for details)
	 * GizmoManager's ToolsContext must have a UTransformGizmoContextObject registered (see UTransformGizmoContextObject for details)
	 */
	INTERACTIVETOOLSFRAMEWORK_API UTransformGizmo* Create3AxisTransformGizmo(UInteractiveGizmoManager* GizmoManager, void* Owner = nullptr, const FString& InstanceIdentifier = FString());
	/**
	 * Spawn a new standard 3-axis Transform gizmo (see UTransformGizmoContextObject::Create3AxisTransformGizmo for details)
	 * ToolManager's ToolsContext must have a UTransformGizmoContextObject registered (see UTransformGizmoContextObject for details)
	 */
	INTERACTIVETOOLSFRAMEWORK_API UTransformGizmo* Create3AxisTransformGizmo(UInteractiveToolManager* ToolManager, void* Owner = nullptr, const FString& InstanceIdentifier = FString());


	/**
	 * Spawn a new custom Transform gizmo (see UTransformGizmoContextObject::CreateCustomTransformGizmo for details)
	 * GizmoManager's ToolsContext must have a UTransformGizmoContextObject registered (see UTransformGizmoContextObject for details)
	 */
	INTERACTIVETOOLSFRAMEWORK_API UTransformGizmo* CreateCustomTransformGizmo(UInteractiveGizmoManager* GizmoManager, ETransformGizmoSubElements Elements, void* Owner = nullptr, const FString& InstanceIdentifier = FString());
	/**
	 * Spawn a new custom Transform gizmo (see UTransformGizmoContextObject::CreateCustomTransformGizmo for details)
	 * ToolManager's ToolsContext must have a UTransformGizmoContextObject registered (see UTransformGizmoContextObject for details)
	 */
	INTERACTIVETOOLSFRAMEWORK_API UTransformGizmo* CreateCustomTransformGizmo(UInteractiveToolManager* ToolManager, ETransformGizmoSubElements Elements, void* Owner = nullptr, const FString& InstanceIdentifier = FString());


	/**
	 * Spawn a new custom Transform gizmo (see UTransformGizmoContextObject::CreateCustomTransformGizmo for details)
	 * GizmoManager's ToolsContext must have a UTransformGizmoContextObject registered (see UTransformGizmoContextObject for details)
	 */
	INTERACTIVETOOLSFRAMEWORK_API UTransformGizmo* CreateCustomRepositionableTransformGizmo(UInteractiveGizmoManager* GizmoManager, ETransformGizmoSubElements Elements, void* Owner = nullptr, const FString& InstanceIdentifier = FString());
	/**
	 * Spawn a new custom Transform gizmo (see UTransformGizmoContextObject::CreateCustomRepositionableTransformGizmo for details)
	 * ToolManager's ToolsContext must have a UTransformGizmoContextObject registered (see UTransformGizmoContextObject for details)
	 */
	INTERACTIVETOOLSFRAMEWORK_API UTransformGizmo* CreateCustomRepositionableTransformGizmo(UInteractiveToolManager* ToolManager, ETransformGizmoSubElements Elements, void* Owner = nullptr, const FString& InstanceIdentifier = FString());
}
}


/**
 * UTransformGizmoContextObject is a utility object that registers a set of Gizmo Builders
 * for UTransformGizmo and variants. The intended usage is to call RegisterGizmosWithManager(),
 * and then the UTransformGizmoContextObject will register itself as a ContextObject in the
 * InteractiveToolsContext's ContextObjectStore. Then the Create3AxisTransformGizmo()/etc functions
 * will spawn different variants of UTransformGizmo. The above UE::TransformGizmoUtil:: functions
 * will look up the UTransformGizmoContextObject instance in the ContextObjectStore and then
 * call the associated function below.
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UTransformGizmoContextObject : public UObject
{
	GENERATED_BODY()
public:

public:
	// builder identifiers for default gizmo types. Perhaps should have an API for this...
	static const FString DefaultAxisPositionBuilderIdentifier;
	static const FString DefaultPlanePositionBuilderIdentifier;
	static const FString DefaultAxisAngleBuilderIdentifier;
	static const FString DefaultThreeAxisTransformBuilderIdentifier;
	static const FString CustomThreeAxisTransformBuilderIdentifier;
	static const FString CustomRepositionableThreeAxisTransformBuilderIdentifier;

	void RegisterGizmosWithManager(UInteractiveToolManager* ToolManager);
	void DeregisterGizmosWithManager(UInteractiveToolManager* ToolManager);

	/**
	 * Activate a new instance of the default 3-axis transformation Gizmo. RegisterDefaultGizmos() must have been called first.
	 * @param Owner optional void pointer to whatever "owns" this Gizmo. Allows Gizmo to later be deleted using DestroyAllGizmosByOwner()
	 * @param InstanceIdentifier optional client-defined *unique* string that can be used to locate this instance
	 * @return new Gizmo instance that has been created and initialized
	 */
	virtual UTransformGizmo* Create3AxisTransformGizmo(UInteractiveGizmoManager* GizmoManager, void* Owner = nullptr, const FString& InstanceIdentifier = FString());

	/**
	 * Activate a new customized instance of the default 3-axis transformation Gizmo, with only certain elements included. RegisterDefaultGizmos() must have been called first.
	 * @param Elements flags that indicate which standard gizmo sub-elements should be included
	 * @param Owner optional void pointer to whatever "owns" this Gizmo. Allows Gizmo to later be deleted using DestroyAllGizmosByOwner()
	 * @param InstanceIdentifier optional client-defined *unique* string that can be used to locate this instance
	 * @return new Gizmo instance that has been created and initialized
	 */
	virtual UTransformGizmo* CreateCustomTransformGizmo(UInteractiveGizmoManager* GizmoManager, ETransformGizmoSubElements Elements, void* Owner = nullptr, const FString& InstanceIdentifier = FString());

	/**
	 * Variant of CreateCustomTransformGizmo that creates a URepositionableTransformGizmo, which is an extension to UTransformGizmo that 
	 * supports various snapping interactions
	 */
	virtual UTransformGizmo* CreateCustomRepositionableTransformGizmo(UInteractiveGizmoManager* GizmoManager, ETransformGizmoSubElements Elements, void* Owner = nullptr, const FString& InstanceIdentifier = FString());

protected:
	TSharedPtr<FTransformGizmoActorFactory> GizmoActorBuilder;
	bool bDefaultGizmosRegistered = false;
};
