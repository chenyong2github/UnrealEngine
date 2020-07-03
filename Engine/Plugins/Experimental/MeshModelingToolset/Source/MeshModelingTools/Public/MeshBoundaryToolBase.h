// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "DynamicMeshAABBTree3.h"
#include "GroupTopology.h"
#include "SingleSelectionTool.h"

#include "MeshBoundaryToolBase.generated.h"

class FDynamicMesh3;
class UPolygonSelectionMechanic;
class USingleClickInputBehavior;

/**
  * Base class for tools that do things with a mesh boundary. Provides ability to select mesh boundaries
  * and some other boilerplate code.
  *	TODO: We can refactor to make the HoleFiller tool inherit from this.
  */
UCLASS()
class MESHMODELINGTOOLS_API UMeshBoundaryToolBase : public USingleSelectionTool, public IClickBehaviorTarget, public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	virtual void SetWorld(UWorld* World) { TargetWorld = World; }

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	// CanAccept() needs to be provided by child.

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;


	// IClickBehaviorTarget
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	// This may need overriding, depending on the child class, though the child could also just
	// override OnSelectionChanged().
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	// IHoverBehaviorTarget
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {};
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

protected:

	UWorld* TargetWorld;

	TSharedPtr<FDynamicMesh3> OriginalMesh;

	// Used for hit querying
	FDynamicMeshAABBTree3 MeshSpatial;

	UPROPERTY()
	UPolygonSelectionMechanic* SelectionMechanic = nullptr;

	// Behavior that wraps this class's selection click operation. Useful to have a pointer to so that its
	// priority can be worked with.
	UPROPERTY()
	USingleClickInputBehavior* LoopSelectClickBehavior = nullptr;

	// A variant of group topology that considers all triangles one group, so that group edges are boundary
	// edges in the mesh.
	class FBasicTopology : public FGroupTopology
	{
	public:
		FBasicTopology(const FDynamicMesh3* Mesh, bool bAutoBuild) :
			FGroupTopology(Mesh, bAutoBuild)
		{}

		int GetGroupID(int TriangleID) const override
		{
			return Mesh->IsTriangle(TriangleID) ? 1 : 0;
		}
	};
	TUniquePtr<FBasicTopology> Topology;

	// Override in the child to determine whether new selection should attempt to append
	virtual bool ShouldSelectionAppend() const { return true; }

	// Override in the child to respond to new loop selections.
	virtual void OnSelectionChanged() {};
};
