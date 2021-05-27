// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Modules/ModuleInterface.h"
#include "Templates/Tuple.h"
#include "UObject/ObjectMacros.h"
#include "USDMemory.h"

#include <string>
#include <vector>
#include <memory>

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"
#endif // #if USE_USD_SDK

#if USE_USD_SDK
PXR_NAMESPACE_OPEN_SCOPE
	class GfMatrix4d;
	class SdfPath;
	class TfToken;
	class UsdAttribute;
	class UsdGeomMesh;
	class UsdPrim;
	class UsdStage;
	class UsdStageCache;

	template< typename T > class TfRefPtr;
PXR_NAMESPACE_CLOSE_SCOPE
#endif // #if USE_USD_SDK

class IUsdPrim;
class FUsdDiagnosticDelegate;

namespace UE
{
	class FUsdAttribute;
	class FUsdStage;
}

enum class EUsdInterpolationMethod
{
	/** Each element in a buffer maps directly to a specific vertex */
	Vertex,
	/** Each element in a buffer maps to a specific face/vertex pair */
	FaceVarying,
	/** Each vertex on a face is the same value */
	Uniform,
	/** Single value */
	Constant
};

enum class EUsdGeomOrientation
{
	/** Right handed coordinate system */
	RightHanded,
	/** Left handed coordinate system */
	LeftHanded,
};

enum class EUsdSubdivisionScheme
{
	None,
	CatmullClark,
	Loop,
	Bilinear,

};

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EUsdPurpose : int32
{
	Default = 0 UMETA(Hidden),
	Proxy = 1,
	Render = 2,
	Guide = 4
};
ENUM_CLASS_FLAGS(EUsdPurpose);


struct FUsdVector2Data
{
	FUsdVector2Data(float InX = 0, float InY = 0)
		: X(InX)
		, Y(InY)
	{}

	float X;
	float Y;
};


struct FUsdVectorData
{
	FUsdVectorData(float InX = 0, float InY = 0, float InZ = 0)
		: X(InX)
		, Y(InY)
		, Z(InZ)
	{}

	float X;
	float Y;
	float Z;
};

struct FUsdVector4Data
{
	FUsdVector4Data(float InX = 0, float InY = 0, float InZ = 0, float InW = 0)
		: X(InX)
		, Y(InY)
		, Z(InZ)
		, W(InW)
	{}

	float X;
	float Y;
	float Z;
	float W;
};


struct FUsdUVData
{
	FUsdUVData()
	{}

	/** Defines how UVs are mapped to faces */
	EUsdInterpolationMethod UVInterpMethod;

	/** Raw UVs */
	std::vector<FUsdVector2Data> Coords;
};

struct FUsdQuatData
{
	FUsdQuatData(float InX=0, float InY = 0, float InZ = 0, float InW = 0)
		: X(InX)
		, Y(InY)
		, Z(InZ)
		, W(InW)
	{}

	float X;
	float Y;
	float Z;
	float W;
};

UENUM()
enum class EUsdInitialLoadSet : uint8
{
	LoadAll,
	LoadNone
};

class IUnrealUSDWrapperModule : public IModuleInterface
{
};

class UnrealUSDWrapper
{
public:
#if USE_USD_SDK
	UNREALUSDWRAPPER_API static double GetDefaultTimeCode();

	/** DEPRECATED: Prefer OpenStage */
	UNREALUSDWRAPPER_API static TUsdStore< pxr::TfRefPtr< pxr::UsdStage > > OpenUsdStage(const char* Path, const char* Filename);
#endif  // #if USE_USD_SDK

	/** Returns the file extensions the USD SDK supports reading from (e.g. ["usd", "usda", "usdc", etc.]) */
	UNREALUSDWRAPPER_API static TArray<FString> GetAllSupportedFileFormats();

	/**
	 * Opens a file as a root layer of an USD stage, and returns that stage.
	 * @param Identifier - Path to a file that the USD SDK can open (or the identifier of a root layer), which will become the root layer of the new stage
	 * @param InitialLoadSet - How to handle USD payloads when opening this stage
	 * @param bUseStageCache - If true, and the stage is already opened in the stage cache (or the layers are already loaded in the registry) then
	 *						   the file reading may be skipped, and the existing stage returned. When false, the stage and all its referenced layers
	 *						   will be re-read anew, and the stage will not be added to the stage cache.
	 * @return The opened stage, which may be invalid.
	 */
	UNREALUSDWRAPPER_API static UE::FUsdStage OpenStage( const TCHAR* Identifier, EUsdInitialLoadSet InitialLoadSet, bool bUseStageCache = true );

	/** Creates a new USD root layer file, opens it as a new stage and returns that stage */
	UNREALUSDWRAPPER_API static UE::FUsdStage NewStage( const TCHAR* FilePath );

	/** Creates a new memory USD root layer, opens it as a new stage and returns that stage */
	UNREALUSDWRAPPER_API static UE::FUsdStage NewStage();

	/** Returns all the stages that are currently opened in the USD utils stage cache, shared between C++ and Python */
	UNREALUSDWRAPPER_API static TArray< UE::FUsdStage > GetAllStagesFromCache();

	/** Removes the stage from the stage cache. See UsdStageCache::Erase. */
	UNREALUSDWRAPPER_API static void EraseStageFromCache( const UE::FUsdStage& Stage );

	/** Starts listening to error/warning/log messages emitted by USD */
	UNREALUSDWRAPPER_API static void SetupDiagnosticDelegate();

	/** Stops listening to error/warning/log messages emitted by USD */
	UNREALUSDWRAPPER_API static void ClearDiagnosticDelegate();

private:
	static TUniquePtr<FUsdDiagnosticDelegate> Delegate;
};

class FUsdAttribute
{
public:
#if USE_USD_SDK
	UNREALUSDWRAPPER_API static std::string GetUnrealPropertyPath(const pxr::UsdAttribute& Attribute);

	// Get the number of elements in the array if it is an array.  Otherwise -1
	UNREALUSDWRAPPER_API static int GetArraySize(const pxr::UsdAttribute& Attribute);

	UNREALUSDWRAPPER_API static bool AsInt(int64_t& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());
	UNREALUSDWRAPPER_API static bool AsUnsignedInt(uint64_t& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());
	UNREALUSDWRAPPER_API static bool AsDouble(double& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());
	UNREALUSDWRAPPER_API static bool AsString(const char*& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());
	UNREALUSDWRAPPER_API static bool AsBool(bool& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());
	UNREALUSDWRAPPER_API static bool AsVector2(FUsdVector2Data& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());
	UNREALUSDWRAPPER_API static bool AsVector3(FUsdVectorData& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());
	UNREALUSDWRAPPER_API static bool AsVector4(FUsdVector4Data& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());
	UNREALUSDWRAPPER_API static bool AsColor(FUsdVector4Data& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex = -1, double Time = UnrealUSDWrapper::GetDefaultTimeCode());

	UNREALUSDWRAPPER_API static bool IsUnsigned(const pxr::UsdAttribute& Attribute);
#endif // #if USE_USD_SDK

};



class IUsdPrim
{
public:
#if USE_USD_SDK
	static UNREALUSDWRAPPER_API bool IsValidPrimName(const FString& Name, FText& OutReason);

	static UNREALUSDWRAPPER_API EUsdPurpose GetPurpose(const pxr::UsdPrim& Prim, bool bComputed = true);

	static UNREALUSDWRAPPER_API bool HasGeometryData(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API bool HasGeometryDataOrLODVariants(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API int GetNumLODs(const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API bool IsKindChildOf(const pxr::UsdPrim& Prim, const std::string& InBaseKind);
	static UNREALUSDWRAPPER_API pxr::TfToken GetKind(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API bool SetKind(const pxr::UsdPrim& Prim, const pxr::TfToken& Kind);
	static UNREALUSDWRAPPER_API bool ClearKind(const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API pxr::GfMatrix4d GetLocalTransform(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API pxr::GfMatrix4d GetLocalToWorldTransform(const pxr::UsdPrim& Prim );
	static UNREALUSDWRAPPER_API pxr::GfMatrix4d GetLocalToWorldTransform(const pxr::UsdPrim& Prim, double Time );
	static UNREALUSDWRAPPER_API pxr::GfMatrix4d GetLocalToWorldTransform(const pxr::UsdPrim& Prim, double Time, const pxr::SdfPath& AbsoluteRootPath);

	/** DEPRECATED: Use UsdUtils::GetPrimMaterialAssignments from USDGeomMeshConversion.h */
	static UNREALUSDWRAPPER_API TTuple< TArray< FString >, TArray< int32 > > GetGeometryMaterials(double Time, const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API bool IsUnrealProperty(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API bool HasTransform(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API std::string GetUnrealPropertyPath(const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API TArray< UE::FUsdAttribute > GetUnrealPropertyAttributes(const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API std::string GetUnrealAssetPath(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API std::string GetUnrealActorClass(const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API bool SetActiveLODIndex(const pxr::UsdPrim& Prim, int LODIndex);

	static UNREALUSDWRAPPER_API EUsdGeomOrientation GetGeometryOrientation(const pxr::UsdGeomMesh& Mesh);
	static UNREALUSDWRAPPER_API EUsdGeomOrientation GetGeometryOrientation(const pxr::UsdGeomMesh& Mesh, double Time);
#endif // #if USE_USD_SDK
};

namespace UnrealIdentifiers
{
#if USE_USD_SDK
	extern UNREALUSDWRAPPER_API const pxr::TfToken LOD;

	/* Attribute name when assigning Unreal materials to UsdGeomMeshes */
	extern UNREALUSDWRAPPER_API const pxr::TfToken MaterialAssignments; // DEPRECATED in favor of MaterialAssignment
	extern UNREALUSDWRAPPER_API const pxr::TfToken MaterialAssignment;

	extern UNREALUSDWRAPPER_API const pxr::TfToken DiffuseColor;
	extern UNREALUSDWRAPPER_API const pxr::TfToken EmissiveColor;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Metallic;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Roughness;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Opacity;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Normal;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Specular;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Anisotropy;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Tangent;
	extern UNREALUSDWRAPPER_API const pxr::TfToken SubsurfaceColor;
	extern UNREALUSDWRAPPER_API const pxr::TfToken AmbientOcclusion;

	// Tokens used mostly for shade material conversion
	extern UNREALUSDWRAPPER_API const pxr::TfToken Surface;
	extern UNREALUSDWRAPPER_API const pxr::TfToken St;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Varname;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Result;
	extern UNREALUSDWRAPPER_API const pxr::TfToken File;
	extern UNREALUSDWRAPPER_API const pxr::TfToken Fallback;
	extern UNREALUSDWRAPPER_API const pxr::TfToken R;
	extern UNREALUSDWRAPPER_API const pxr::TfToken RGB;

	// Tokens copied from usdImaging, because at the moment it's all we need from it
	extern UNREALUSDWRAPPER_API const pxr::TfToken UsdPreviewSurface;
	extern UNREALUSDWRAPPER_API const pxr::TfToken UsdPrimvarReader_float2;
	extern UNREALUSDWRAPPER_API const pxr::TfToken UsdUVTexture;

	// Token used to indicate that a material parsed from a material prim should use world space normals
	extern UNREALUSDWRAPPER_API const pxr::TfToken WorldSpaceNormals;
#endif // #if USE_USD_SDK

	extern UNREALUSDWRAPPER_API const TCHAR* Invisible;
	extern UNREALUSDWRAPPER_API const TCHAR* Inherited;
	extern UNREALUSDWRAPPER_API const TCHAR* IdentifierPrefix;
}

struct UNREALUSDWRAPPER_API FUsdDelegates
{
	DECLARE_MULTICAST_DELEGATE_OneParam( FUsdImportDelegate, FString /* FilePath */);
	static FUsdImportDelegate OnPreUsdImport;
	static FUsdImportDelegate OnPostUsdImport;
};