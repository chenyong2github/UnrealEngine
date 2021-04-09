// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"

#include "USDIncludesStart.h"

#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdShade/tokens.h"

#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class TfToken;
	class UsdGeomMesh;
	class UsdPrim;
	class UsdShadeMaterial;
	class UsdTyped;
PXR_NAMESPACE_CLOSE_SCOPE

class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;
class UStaticMesh;
struct FMeshDescription;
struct FStaticMeshLODResources;
namespace UsdUtils
{
	struct FDisplayColorMaterial;
	struct FUsdPrimMaterialAssignmentInfo;
}
namespace UE
{
	class FUsdStage;
}

namespace UsdToUnreal
{
	/**
	 * Extracts mesh data from UsdSchema at TimeCode and places the results in MeshDescription.
	 *
	 * @param UsdSchema - UsdGeomMesh that contains the data to convert
	 * @param MeshDescription - Output parameter that will be filled with the converted data
	 * @param MaterialAssignments - Output parameter that will be filled with the material data extracted from UsdSchema
	 * @param AdditionalTransform - Transform (in UE4 space) to bake into the vertex positions and normals of the converted mesh
	 * @param MaterialToPrimvarsUVSetNames - Maps from a material prim path, to pairs indicating which primvar names are used as 'st' coordinates for this mesh, and which UVIndex materials will sample from (e.g. ["st0", 0], ["myUvSet2", 2], etc). This is used to pick which primvars will become UV sets.
	 * @param TimeCode - USD timecode when the UsdGeomMesh should be sampled for mesh data and converted
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime(), const pxr::TfToken& RenderContext = pxr::UsdShadeTokens->universalRenderContext );
	USDUTILITIES_API bool ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments, const FTransform& AdditionalTransform, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime(), const pxr::TfToken& RenderContext = pxr::UsdShadeTokens->universalRenderContext );
	USDUTILITIES_API bool ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments, const FTransform& AdditionalTransform, const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarsUVSetNames, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime(), const pxr::TfToken& RenderContext = pxr::UsdShadeTokens->universalRenderContext );

	/** DEPRECATED and will not convert material information. Use the three first signatures above this one */
	USDUTILITIES_API bool ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime() );
	USDUTILITIES_API bool ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, const FTransform& AdditionalTransform, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime() );
	USDUTILITIES_API bool ConvertGeomMesh( const pxr::UsdTyped& UsdSchema, FMeshDescription& MeshDescription, const FTransform& AdditionalTransform, const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarsUVSetNames, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime() );

	/** Configure Material to become a vertex color/displayColor material, according to the given description */
	USDUTILITIES_API bool ConvertDisplayColor( const UsdUtils::FDisplayColorMaterial& DisplayColorDescription, UMaterialInstanceConstant& Material );
}

namespace UnrealToUsd
{
	/**
	 * Extracts mesh data from StaticMesh and places the results in UsdPrim, as children UsdGeomMeshes.
	 * This function receives the parent UsdPrim as it may create a variant set named 'LOD', and create a separate
	 * UsdGeomMeshes for each LOD, as a variant of 'LOD'
	 * @param StaticMesh - StaticMesh to convert
	 * @param UsdPrim - Prim to receive the mesh data or LOD variant set
	 * @param TimeCode - TimeCode to author the attributes with
	 * @param StageForMaterialAssignments - Stage to use when authoring material assignments (we use this when we want to export the mesh to a payload layer, but the material assignments to an asset layer)
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertStaticMesh( const UStaticMesh* StaticMesh, pxr::UsdPrim& UsdPrim, pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::Default(), UE::FUsdStage* StageForMaterialAssignments = nullptr );

	/**
	 * Converts an array of mesh descriptions into mesh data, and places that data within the UsdGeomMesh UsdPrim.
	 * If only one MeshDescription is provided, the mesh data is added directly to the prim.
	 * If more than one MeshDescription are provided, a 'LOD' variant set will be created for UsdPrim, and LOD0, LOD1, etc. variants will be
	 * created for each provided LOD index. Within each variant, a single Mesh prim also named LOD0, LOD1, etc. will contain the mesh data.
	 */
	USDUTILITIES_API bool ConvertMeshDescriptions( const TArray<FMeshDescription>& LODIndexToMeshDescription, pxr::UsdPrim& UsdPrim, const FMatrix& AdditionalTransform, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::Default() );
}

namespace UsdUtils
{
	/** Describes the type of vertex color/DisplayColor material that we would need in order to render a prim's displayColor data as intended */
	struct USDUTILITIES_API FDisplayColorMaterial
	{
		bool bHasOpacity = false;
		bool bIsDoubleSided = false;

		FString ToString();
		static TOptional<FDisplayColorMaterial> FromString(const FString& DisplayColorString);
	};

	/** Describes what type of material assignment a FUsdPrimMaterialSlot represents */
	enum class EPrimAssignmentType : uint8
	{
		None,			// There is no assignment for this material slot (or no material override)
		DisplayColor,	// MaterialSource is a serialized FDisplayColorMaterial (e.g. '!DisplayColor_0_0')
		MaterialPrim,	// MaterialSource is the USD path to a Material prim on the stage (e.g. '/Root/Materials/Red')
		UnrealMaterial	// MaterialSource is the package path to an UE material (e.g. '/Game/Materials/Red.Red')
	};

	/**
	 * Description of a material slot we need to add to a UStaticMesh or USkeletalMesh to have it represent a material binding in a prim.
	 * It may or may not contain an actual material assignment.
	 */
	struct FUsdPrimMaterialSlot
	{
		/** What this represents depends on AssignedMaterialType */
		FString MaterialSource;
		EPrimAssignmentType AssignmentType = EPrimAssignmentType::None;

		friend bool operator==( const FUsdPrimMaterialSlot& Lhs, const FUsdPrimMaterialSlot& Rhs )
		{
			return Lhs.AssignmentType == Rhs.AssignmentType && Lhs.MaterialSource.Equals( Rhs.MaterialSource, ESearchCase::CaseSensitive );
		}

		friend uint32 GetTypeHash( const FUsdPrimMaterialSlot& Slot )
		{
			return HashCombine( GetTypeHash( Slot.MaterialSource ), static_cast<uint32>(Slot.AssignmentType) );
		}

		friend FArchive& operator<<( FArchive& Ar, FUsdPrimMaterialSlot& Slot )
		{
			Ar << Slot.MaterialSource;
			Ar << Slot.AssignmentType;

			return Ar;
		}
	};

	/** Complete description of material assignment data of a pxr::UsdPrim */
	struct FUsdPrimMaterialAssignmentInfo
	{
		TArray<FUsdPrimMaterialSlot> Slots;

		/** Describes the index of the Slot that each polygon/face of a mesh uses. Matches the order Slots */
		TArray<int32> MaterialIndices;
	};

	/** Creates a FDisplayColorMaterial object describing the vertex color/opacity data from UsdMesh at time TimeCode */
	USDUTILITIES_API TOptional<FDisplayColorMaterial> ExtractDisplayColorMaterial( const pxr::UsdGeomMesh& UsdMesh, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime() );

	/** Creates a dynamic material instance using the right master material depending on the given description */
	USDUTILITIES_API UMaterialInstanceDynamic* CreateDisplayColorMaterialInstanceDynamic( const UsdUtils::FDisplayColorMaterial& DisplayColorDescription );
	USDUTILITIES_API UMaterialInstanceConstant* CreateDisplayColorMaterialInstanceConstant( const UsdUtils::FDisplayColorMaterial& DisplayColorDescription );

	/**
	 * Extracts all material assignment data from UsdPrim, including material binding, multiple assignment with GeomSubsets, and the unrealMaterial custom USD attribute.
	 * Guaranteed to return at least one material slot. If UsdPrim is a UsdGeomMesh, it is also guaranteed to have valid material indices (one for every face).
	 * @param UsdPrim - Prim to extract material assignments from
	 * @param TimeCode - Instant where the material data is sampled
	 * @param bProvideMaterialIndices - Whether to fill out the material index information for the assignment info (which can be expensive). If this is false, MaterialIndices on the result Struct will have zero values
	 * @param RenderContext - Which render context to get the materials for. Defaults to universal.
	 * @return Struct containing an array of material assignments, and a corresponding array of material indices for all polygons of the prim, matching the ordering of the material assignments.
	 */
	USDUTILITIES_API FUsdPrimMaterialAssignmentInfo GetPrimMaterialAssignments( const pxr::UsdPrim& UsdPrim, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime(),
		bool bProvideMaterialIndices = true, const pxr::TfToken& RenderContext = pxr::UsdShadeTokens->universalRenderContext );

	/** Returns whether this UsdMesh can be interpreted as a LOD of a mesh with multiple LODs */
	USDUTILITIES_API bool IsGeomMeshALOD( const pxr::UsdPrim& UsdMeshPrim );

	/** Returns how many LOD variants the Prim has. Note that this will return 0 if called on one of the LOD meshes themselves, it's meant to be called on its parent */
	USDUTILITIES_API int32 GetNumberOfLODVariants( const pxr::UsdPrim& Prim );

	/**
	 * If a prim has a variant set named "LOD", with variants named "LOD0", "LOD1", etc., and each has a single Mesh prim, this function
	 * will actively switch the variants of ParentPrim so that each child mesh is loaded, and call Func on each.
	 * Returns whether ParentPrim met the aforementioned criteria and Func was called at least once.
	 * WARNING: There is no guarantee about LOD index ordering! Func may receive LOD2, followed by LOD0, then LOD1, etc.
	 * WARNING: This will temporarily mutate the stage, and can invalidate references to children of ParentPrim!
	 */
	USDUTILITIES_API bool IterateLODMeshes( const pxr::UsdPrim& ParentPrim, TFunction<bool(const pxr::UsdGeomMesh& LODMesh, int32 LODIndex)> Func);
}

#endif // #if USE_USD_SDK
