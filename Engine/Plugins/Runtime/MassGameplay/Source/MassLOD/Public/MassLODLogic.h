// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassLODManager.h"
#include "Containers/StaticArray.h"
#include "ConvexVolume.h"


#define DECLARE_CONDITIONAL_MEMBER_ACCESSORS( Condition, MemberType, MemberName ) \
template <bool Condition, typename TemplateClass> static FORCEINLINE typename TEnableIf< Condition, MemberType>::Type Get##MemberName(TemplateClass& Obj, MemberType DefaultValue) { return Obj.MemberName; } \
template <bool Condition, typename TemplateClass> static FORCEINLINE typename TEnableIf<!Condition, MemberType>::Type Get##MemberName(TemplateClass& Obj, MemberType DefaultValue) { return DefaultValue; } \
template <bool Condition, typename TemplateClass> static FORCEINLINE void Set##MemberName(TemplateClass& Obj, typename TEnableIf< Condition, MemberType>::Type Value) { Obj.MemberName = Value; } \
template <bool Condition, typename TemplateClass> static FORCEINLINE void Set##MemberName(TemplateClass& Obj, typename TEnableIf<!Condition, MemberType>::Type Value) {}

#define DECLARE_CONDITIONAL_MEMBER_ARRAY_ACCESSORS( Condition, MemberType, MemberName ) \
template <bool Condition, typename TemplateClass> static FORCEINLINE typename TEnableIf< Condition, MemberType>::Type Get##MemberName(TemplateClass& Obj, int32 Index, MemberType DefaultValue) { return Obj.MemberName[Index]; } \
template <bool Condition, typename TemplateClass> static FORCEINLINE typename TEnableIf<!Condition, MemberType>::Type Get##MemberName(TemplateClass& Obj, int32 Index, MemberType DefaultValue) { return DefaultValue; } \
template <bool Condition, typename TemplateClass> static FORCEINLINE void Set##MemberName(TemplateClass& Obj, int32 Index, typename TEnableIf< Condition, MemberType>::Type Value) { Obj.MemberName[Index] = Value; } \
template <bool Condition, typename TemplateClass> static FORCEINLINE void Set##MemberName(TemplateClass& Obj, int32 Index, typename TEnableIf<!Condition, MemberType>::Type Value) {}

/**
 * Traits for LOD logic calculation behaviors
 */
struct FLODDefaultLogic
{
	enum
	{
		bStoreInfoPerViewer = false, // Enable to store all calculated information per viewer
		bCalculateLODPerViewer = false, // Enable to calculate and store the result LOD per viewer in the FMassLODResultInfo::LODPerViewer and FMassLODResultInfo::PrevLODPerViewer.
		bMaximizeCountPerViewer = false, // Enable to maximize count per viewer, requires a valid InLODMaxCountPerViewer parameter during initialization of TMassLODCalculator.
		bDoVisibilityLogic = false, // Enable to calculate visibility and apply its own LOD distances. Requires a valid InVisibleLODDistance parameter during initialization of TMassLODCalculator.
		bCalculateLODSignificance = false, // Enable to calculate and set the a more precise LOD floating point significance in member FMassLODResultInfo::LODSignificance.
		bLocalViewersOnly = false, // Enable to calculate LOD from LocalViewersOnly, otherwise will be done on all viewers.
		bDoVariableTickRate = false, // Enable to update entity variable tick rate calculation
	};
};

struct FMassSimulationLODLogic : public FLODDefaultLogic
{
	enum
	{
		bDoVariableTickRate = true,
	};
};

struct FMassRepresentationLODLogic : public FLODDefaultLogic
{
	enum
	{
		bDoVisibilityLogic = true,
		bCalculateLODSignificance = true,
		bLocalViewersOnly = true,
	};
};

struct FMassCombinedLODLogic : public FLODDefaultLogic
{
	enum
	{
		bDoVariableTickRate = true,
		bDoVisibilityLogic = true,
		bCalculateLODSignificance = true,
		bLocalViewersOnly = true,
	};
};

/**
 * TMassLODCollector outputs
 *
struct FMassViewerInfoFragment
{
	// Closest viewer distance  (Always needed)
	float ClosestViewerDistanceSq;

	// Square distances to each valid viewers (Always needed)
	TStaticArray<float, UE::MassLOD::MaxNumOfViewers> DistanceToViewerSq;

	// @Todo optimize by adding a boolean in the FLODLogic to enable the storing store that only when wanted
	// Closest distance to frustum (Required only when FLODLogic::bDoVisibilityLogic is enabled)
	float ClosestDistanceToFrustum;

	// @Todo optimize by adding a boolean in the FLODDefaultLogic to enable the storing store that only when wanted
	// Distances to each valid viewers frustums (Required only when FLODLogic::bDoVisibilityLogic)
	TStaticArray<float, UE::MassLOD::MaxNumOfViewers> DistanceToFrustum;
};
*/

/**
 * TMassLODCalculator outputs
 *
 struct FMassLODFragment
{
	// LOD information
	TEnumAsByte<EMassLOD::Type> LOD;
	TEnumAsByte<EMassLOD::Type> PrevLOD;

	// Per viewer LOD information (Required only when FLODLogic::bStoreLODPerViewer is enabled)
	TStaticArray<EMassLOD::Type, UE::MassLOD::MaxNumOfViewers> LODPerViewer;
	TStaticArray<EMassLOD::Type, UE::MassLOD::MaxNumOfViewers> PrevLODPerViewer;

	// Floating point LOD value, scaling from 0 to 3, 0 highest LOD and 3 being completely off LOD 
	// (Required only when FLODLogic::bCalculateLODSignificance is enabled)
	float LODSignificance = 0.0f; // 

	// Visibility information (Required only when FLODLogic::bDoVisibilityLogic is enabled)
	bool bIsVisibleByAViewer;
	bool bWasVisibleByAViewer;
	bool bIsInVisibleRange;
	bool bWasInVisibleRange;

	// Visibility information per viewer (Only when FLODLogic::bDoVisibilityLogic is enabled)
	TStaticArray<bool, UE::MassLOD::MaxNumOfViewers> bIsVisibleByViewer;
	TStaticArray<bool, UE::MassLOD::MaxNumOfViewers> bWasVisibleByViewer;

	// @Todo move to its own fragment remove the FLODLogic::bDoVariableTickRate
	// Accumulated DeltaTime (Required only when FLODLogic::bDoVariableTickRate is enalbed)
	float DeltaTime = 0.0f;
	float LastTickedTime = 0.0f;
};
*/

struct FViewerLODInfo
{
	/* Boolean indicating the viewer data needs to be cleared */
	bool bClearData = false;

	/** The handle to the viewer */
	FMassViewerHandle Handle;

	/** Viewer location and looking direction */
	FVector Location;
	FVector Direction;

	/** Viewer frustum (will not include near and far planes) */
	FConvexVolume Frustum;
};

/**
 * Base struct for the LOD calculation helpers
 */
struct MASSLOD_API FMassLODBaseLogic
{
protected:
	void CacheViewerInformation(TConstArrayView<FViewerInfo> ViewerInfos, const bool bLocalViewersOnly);

	// Per viewer LOD information conditional fragment accessors
	DECLARE_CONDITIONAL_MEMBER_ARRAY_ACCESSORS(Condition, EMassLOD::Type, LODPerViewer);
	DECLARE_CONDITIONAL_MEMBER_ARRAY_ACCESSORS(Condition, EMassLOD::Type, PrevLODPerViewer);

	// LOD Significance conditional fragment accessors
	DECLARE_CONDITIONAL_MEMBER_ACCESSORS(Condition, float, LODSignificance);

	// Visibility conditional fragment accessors
	DECLARE_CONDITIONAL_MEMBER_ACCESSORS(Condition, float, ClosestDistanceToFrustum);
	DECLARE_CONDITIONAL_MEMBER_ACCESSORS(Condition, bool, bIsVisibleByAViewer);
	DECLARE_CONDITIONAL_MEMBER_ACCESSORS(Condition, bool, bWasVisibleByAViewer);
	DECLARE_CONDITIONAL_MEMBER_ACCESSORS(Condition, bool, bIsInVisibleRange);
	DECLARE_CONDITIONAL_MEMBER_ACCESSORS(Condition, bool, bWasInVisibleRange);

	// Per viewer distance conditional fragment accessors
	DECLARE_CONDITIONAL_MEMBER_ARRAY_ACCESSORS(Condition, float, DistanceToViewerSq);

	// Per viewer visibility conditional fragment accessors
	DECLARE_CONDITIONAL_MEMBER_ARRAY_ACCESSORS(Condition, float, DistanceToFrustum);
	DECLARE_CONDITIONAL_MEMBER_ARRAY_ACCESSORS(Condition, bool, bIsVisibleByViewer);
	DECLARE_CONDITIONAL_MEMBER_ARRAY_ACCESSORS(Condition, bool, bWasVisibleByViewer);

	// @todo, there should be a fragment dedicated to this and it would remove the need to this
	// Tick controller conditional fragment accessors,=
	DECLARE_CONDITIONAL_MEMBER_ACCESSORS(Condition, float, DeltaTime);
	DECLARE_CONDITIONAL_MEMBER_ACCESSORS(Condition, float, LastTickedTime);

	TArray<FViewerLODInfo> Viewers;
};