// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/GizmoElementRenderState.h"
#include "BaseGizmos/GizmoElementShared.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "InputState.h"
#include "SceneView.h"
#include "ToolContextInterfaces.h"
#include "UObject/GCObject.h"
#include "GizmoElementBase.generated.h"

class UMaterialInterface;

/**
 * Base class for 2d and 3d primitive objects intended to be used as part of 3D Gizmos.
 * Contains common properties and utility functions.
 * This class does nothing by itself, use subclasses like UGizmoElementCylinder
 */
UCLASS(Transient, Abstract)
class INTERACTIVETOOLSFRAMEWORK_API UGizmoElementBase : public UObject
{
	GENERATED_BODY()
public:

	static constexpr float DefaultViewDependentAngleTol = 0.052f;			// ~3 degrees
	static constexpr float DefaultViewAlignAxialMaxCosAngleTol = 0.998f;	// Cos(DefaultViewDependentAngleTol)
	static constexpr float DefaultViewAlignPlanarMinCosAngleTol = 0.052f;	// Cos(HALF_PI - DefaultViewDependentAngleTol)

	static constexpr float DefaultViewAlignAngleTol = 0.052f;				// ~3 degrees
	static constexpr float DefaultViewAlignMaxCosAngleTol = 0.998f;			// Cos(DefaultViewAlignAngleTol)

	static constexpr uint32 DefaultPartIdentifier = 0;						// Default part ID, used for elements that are not associated with any gizmo part

	// 
	// Render traversal state structure used to maintain the current render state while rendering.
	// As the gizmo element hierarchy is traversed, current state is maintained and updated. 
	// Element state attribute inheritance works as follows:
	//
	// - Child element state that is not set inherits from parent state.
	// - Child element state that is set replaces the parent state, except in the case of overrides.
	// - Overrides: parent element state with override set to true replaces all child state regardless of whether the child state has been set.
	//
	struct FRenderTraversalState
	{
		// LocalToWorld transform 
		// Note: non-uniform scale is not supported and the X scale element will be used for uniform scaling.
		FTransform LocalToWorldTransform;

		// Pixel to world scale
		double PixelToWorldScale = 1.0;

		// Interact state, if not equal to none, overrides the element's interact state 
		EGizmoElementInteractionState InteractionState = EGizmoElementInteractionState::None;

		// Current state used for rendering meshes
		FGizmoElementMeshRenderStateAttributes MeshRenderState;

		// Current state used for rendering lines
		FGizmoElementLineRenderStateAttributes LineRenderState;

		// Initialize state 
		void Initialize(const FSceneView* InSceneView, FTransform InTransform)
		{
			LocalToWorldTransform = InTransform;
			PixelToWorldScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(InSceneView, InTransform.GetLocation());
		}

		// Returns the mesh material based on the current interaction state.
		const UMaterialInterface* GetCurrentMaterial()
		{
			return MeshRenderState.GetMaterial(InteractionState);
		}

		// Returns the mesh vertex color.
		FLinearColor GetVertexColor()
		{
			return MeshRenderState.GetVertexColor();
		}

		// Returns the line color based on the current interaction state.
		FLinearColor GetCurrentLineColor()
		{
			return LineRenderState.GetLineColor(InteractionState);
		}
	};

public:

	// Render enabled visible element.
	virtual void Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState) PURE_VIRTUAL(UGizmoElementBase::Render);

	// Line trace enabled hittable element.
	virtual FInputRayHit LineTrace(const FVector Start, const FVector Direction) PURE_VIRTUAL(UGizmoElementBase::LineTrace, return FInputRayHit(););

	// Calcute box sphere bounds for use when hit testing.
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const PURE_VIRTUAL(UGizmoElementBase::CalcBounds, return FBoxSphereBounds(););

	// Reset the cached render state.
	virtual void ResetCachedRenderState();

	// Whether this object is visible.
	virtual bool IsVisible() const;

	// Whether this object is hittable.
	virtual bool IsHittable() const;

	// Whether this object is hittable in the most recently cached view.
	virtual bool IsHittableInView() const;

	// Render and LineTrace should only occur when bEnabled is true.
	virtual void SetEnabled(bool InEnabled);
	virtual bool GetEnabled() const;

	// For an element hierarchy representing multiple parts of a single gizmo, the part identifier establishes 
	// a correspondence between a gizmo part and the elements that represent that part. The recognized
	// part identifier values should be defined in the gizmo. Gizmo part identifiers must be greater than or 
	// equal to one. Identifier 0 is reserved for the default ID which should be assigned to elements
	// that do not correspond to any gizmo part, such as non-hittable decorative elements.
	virtual void SetPartIdentifier(uint32 InPartId);
	virtual uint32 GetPartIdentifier();

	// Object type bitmask indicating whether this object is visible or hittable or both
	virtual void SetElementState(EGizmoElementState InElementState);
	virtual EGizmoElementState GetElementState() const;

	// Object interaction state - None, Hovering or Interacting
	virtual void SetElementInteractionState(EGizmoElementInteractionState InInteractionState);
	virtual EGizmoElementInteractionState GetElementInteractionState() const;

	// Update element's visibility state if element is associated with the specified gizmo part.
	virtual void UpdatePartVisibleState(bool bVisible, uint32 InPartIdentifier);

	// Update element's hittable state if element is associated with the specified gizmo part.
	virtual void UpdatePartHittableState(bool bHittable, uint32 InPartIdentifier);

	// Update element's interaction state if element is associated with the specified gizmo part.
	virtual void UpdatePartInteractionState(EGizmoElementInteractionState InInteractionState, uint32 InPartIdentifier);

	// View-dependent type - None, Axis or Plane. 
	virtual void SetViewDependentType(EGizmoElementViewDependentType ViewDependentType);
	virtual EGizmoElementViewDependentType GetViewDependentType() const;

	// View-dependent angle tolerance in radians 
	//   For Axis, object is culled when angle between view dependent axis and view direction is less than tolerance angle.
	//   For Planar, cos of angle between view dependent axis (plane normal) and view direction. 
	// When the view direction is within this tolerance from the plane or axis, this object will be culled.
	virtual void SetViewDependentAngleTol(float InMaxAngleTol);
	virtual float GetViewDependentAngleTol() const;

	// View-dependent axis or plane normal, based on the view-dependent type.
	virtual void SetViewDependentAxis(FVector InAxis);
	virtual FVector GetViewDependentAxis() const;

	// View align type: None, PointEye, PointOnly, or Axial.
	virtual void SetViewAlignType(EGizmoElementViewAlignType InViewAlignType);
	virtual EGizmoElementViewAlignType GetViewAlignType() const;

	// View align axis. 
	// PointEye, PointScreen and PointOnly rotate this axis to align with view up.
	// Axial rotates about this axis.
	virtual void SetViewAlignAxis(FVector InAxis);
	virtual FVector GetViewAlignAxis() const;

	// View align normal.
	// PointEye rotates the normal to align with camera view direction.
	// PointScreen rotates the normal to align with screen forward direction.
	// Axial rotates the normal around the axis to align as closely as possible with the view direction.
	virtual void SetViewAlignNormal(FVector InAxis);
	virtual FVector GetViewAlignNormal() const;

	// View-align angle tolerance in radians.
	// Viewer alignment will not occur when the viewing angle is within this angle of view align axis.
	virtual void SetViewAlignAxialAngleTol(float InMaxAngleTol);
	virtual float GetViewAlignAxialAngleTol() const;

	// Pixel hit distance threshold, element will be scaled enough to add this threshold when line-tracing. */
	virtual void SetPixelHitDistanceThreshold(float InPixelHitDistanceThreshold);
	virtual float GetPixelHitDistanceThreshold() const;

	//
	// Methods for managing render state attributes: Material, HoverMaterial, InteractMaterial, VertexColor 
	// 
	// State inheritance works as follows: 
	// - Gizmo element state that is not set inherits from the corresponding state in the current render traversal.
	// - Gizmo element state that is set replaces the corresponding state in the current render traversal, except in the case of overrides.
	// - Gizmo element state that is set to override, will override any corresponding state in children.
	//

	// Set mesh render state material attribute. 
	//  @param InMaterial - material to be set
	//  @param InOverridesChildState - when true, this material will override the material of all child elements.
	virtual void SetMaterial(TWeakObjectPtr<UMaterialInterface> InMaterial, bool InOverridesChildState = false);

	// Get mesh render state material attribute's value. 
	virtual const UMaterialInterface* GetMaterial() const;

	// Get mesh render state material attribute's override setting. 
	virtual bool GetMaterialOverridesChildState() const;

	// Clear mesh render state material attribute. 
	virtual void ClearMaterial();

	// Set mesh render state hover material attribute. 
	//  @param InHoverMaterial - hover material to be set
	//  @param InOverridesChildState - when true, this hover material will override the material of all child elements.
	virtual void SetHoverMaterial(TWeakObjectPtr<UMaterialInterface> InHoverMaterial, bool InOverridesChildState = false);

	// Get mesh render state hover material attribute's value.
	virtual const UMaterialInterface* GetHoverMaterial() const;

	// Get mesh render state hover material attribute's override setting.
	virtual bool GetHoverMaterialOverridesChildState() const;

	// Clear mesh render state hover material attribute. 
	virtual void ClearHoverMaterial();

	// Set mesh render state interact material attribute. 
	//  @param InHoverMaterial - interact material to be set
	//  @param InOverridesChildState - when true, this interact material will override the material of all child elements.
	virtual void SetInteractMaterial(TWeakObjectPtr<UMaterialInterface> InInteractMaterial, bool InOverridesChildState = false);

	// Get mesh render state interact material attribute's value. 
	virtual const UMaterialInterface* GetInteractMaterial() const;

	// Get mesh render state interact material attribute's override setting. 
	virtual bool GetInteractMaterialOverridesChildState() const;

	// Clear mesh render interact state material attribute. 
	virtual void ClearInteractMaterial();

	// Set mesh render state vertex color attribute. 
	//  @param InVertexColor - vertex color to be set
	//  @param InOverridesChildState - when true, this vertex color will override the material of all child elements.
	virtual void SetVertexColor(FLinearColor InVertexColor, bool InOverridesChildState = false);

	// Get mesh render state vertex color attribute's value. 
	virtual FLinearColor GetVertexColor() const;

	// Returns true, if mesh render state vertex color attribute has been set. 
	virtual bool HasVertexColor() const;

	// Get mesh render state vertex color attribute's override setting. 
	virtual bool GetVertexColorOverridesChildState() const;

	// Clear mesh render state vertex color attribute.
	virtual void ClearVertexColor();

protected:

	// Render and LineTrace should only occur when bEnabled is true.
	UPROPERTY()
	bool bEnabled = true;

	// Part identifier
	UPROPERTY()
	uint32 PartIdentifier = DefaultPartIdentifier;

	// Mesh render state attributes for this element
	UPROPERTY()
	FGizmoElementMeshRenderStateAttributes MeshRenderAttributes;

	// Element state - indicates whether object is visible or hittable
	UPROPERTY()
	EGizmoElementState ElementState = EGizmoElementState::VisibleAndHittable;

	// Element interaction state - None, Hovering or Interacting
	UPROPERTY()
	EGizmoElementInteractionState ElementInteractionState = EGizmoElementInteractionState::None;

	// View-dependent type - None, Axis or Plane. 
	UPROPERTY()
	EGizmoElementViewDependentType ViewDependentType = EGizmoElementViewDependentType::None;

	// View-dependent axis or plane normal, based on the view-dependent type.
	UPROPERTY()
	FVector ViewDependentAxis = FVector::UpVector;

	// View-dependent angle tolerance based on :
	//   For Axis, minimum radians between view dependent axis and view direction.
	//   For Planar, minimum radians between view dependent axis and the plane where axis is its normal.
	// When the angle between the view direction and the axis/plane is less than this tolerance, this object should be culled.
	UPROPERTY()
	float ViewDependentAngleTol = DefaultViewDependentAngleTol;

	// Axial view alignment minimum cos angle tolerance, computed based on ViewDependentAngleTol. 
	// When the cos of the angle between the view direction and the axis is less than this value, this object should not be culled.
	UPROPERTY()
	float ViewDependentAxialMaxCosAngleTol = DefaultViewAlignAxialMaxCosAngleTol;

	// Planar view alignment minimum cos angle tolerance, computed based on ViewDependentAngleTol. 
	// When the cos of the angle between the view direction and the axis is greater than this value, this object should not be culled.
	UPROPERTY()
	float ViewDependentPlanarMinCosAngleTol = DefaultViewAlignPlanarMinCosAngleTol;

	// View align type: None, PointEye, or PointWorld.
	// PointEye rotates this axis to align with the view up axis.
	// PointWorld rotates this axis to align with the world up axis.
	// Axial rotates around this axis to align the normal as closely as possible to the view direction.
	UPROPERTY()
	EGizmoElementViewAlignType ViewAlignType = EGizmoElementViewAlignType::None;

	// View align axis. 
	UPROPERTY()
	FVector ViewAlignAxis = FVector::UpVector;

	// View align normal.
	// PointEye and PointWorld both rotate the normal to align with the view direction.
	// Axial rotates the normal to align as closely as possible with view direction.
	UPROPERTY()
	FVector ViewAlignNormal = -FVector::ForwardVector;

	// Axial view alignment angle tolerance in radians, based on angle between align normal and view direction. 
	// When angle between the view align normal and the view direction is greater than this angle, the align rotation will be computed.
	UPROPERTY()
	float ViewAlignAxialAngleTol = DefaultViewAlignAngleTol;

	// Axial view alignment minimum cos angle tolerance, computed based on ViewAlignAxialAngleTol. 
	// When the cos of the angle between the view direction and the align normal is less than this value, the align rotation will be computed.
	UPROPERTY()
	float ViewAlignAxialMaxCosAngleTol = DefaultViewAlignMaxCosAngleTol;

	// Pixel hit distance threshold, element will be scaled enough to add this threshold when line-tracing.
	UPROPERTY()
	float PixelHitDistanceThreshold = 7.0;

	// Render stores the last cached transform, used by line trace.
	UPROPERTY()
	FTransform CachedLocalToWorldTransform = FTransform::Identity;

	// Render stores the last pixel to world scale, used by line trace.
	UPROPERTY()
	float CachedPixelToWorldScale = 1.0;

	// Whether last transform has been cached.
	UPROPERTY()
	bool bHasCachedLocalToWorldTransform = false;

	// Whether visible object was visible during the last render.
	UPROPERTY()
	bool bCachedVisibleViewDependent = true;

	// Cached box sphere bounds.
	UPROPERTY()
	FBoxSphereBounds CachedBoxSphereBounds;

	// Cached box sphere bounds.
	UPROPERTY()
	bool bHasCachedBoxSphereBounds;

protected:

	// Returns whether object is visible based on view-dependent visibility settings. 
	virtual bool GetViewDependentVisibility(const FSceneView* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter) const;

	// Returns true when view alignment is enabled. OutAlignRot is the rotation in local space which will align the object to the view base
	// on view-dependent alignment settings. So it should be prepended to the local-to-world transform.
	virtual bool GetViewAlignRot(const FSceneView* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter, FQuat& OutAlignRot) const;

	// Helper method to calculate rotation between coord spaces.
	FQuat GetAlignRotBetweenCoordSpaces(FVector SourceForward, FVector SourceSide, FVector SourceUp, FVector TargetForward, FVector TargetSide, FVector TargetUp) const;

	// Update render state during render traversal, determines the current render state for this element 
	// @return view dependent visibility, true if this element is visible in the current view. 
	virtual bool UpdateRenderState(IToolsContextRenderAPI* RenderAPI, const FVector& InLocalOrigin, FRenderTraversalState& InOutRenderState);

	// Cache render state during render traversal to be used subsequently when line tracing.
	virtual void CacheRenderState(const FTransform& InLocalToWorldState, double InPixelToWorldScale, bool InVisibleViewDependent = true);

};

