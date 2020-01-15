// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Modules/ModuleInterface.h"
#include "Templates/Tuple.h"
#include "USDMemory.h"

#include <string>
#include <vector>
#include <memory>

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
	#include "pxr/base/tf/token.h"
	#include "pxr/usd/usd/stageCache.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class GfMatrix4d;
	class SdfPath;
	class TfToken;
	class UsdAttribute;
	class UsdGeomMesh;
	class UsdPrim;
	class UsdStage;

	template< typename T > class TfRefPtr;
PXR_NAMESPACE_CLOSE_SCOPE

#endif // #if USE_USD_SDK

class IUsdPrim;

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

enum class EUsdUpAxis
{
	XAxis,
	YAxis,
	ZAxis,
};



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

class IUnrealUSDWrapperModule : public IModuleInterface
{
public:
	virtual void Initialize(const TArray< FString >& InPluginDirectories ) = 0;
};

class UnrealUSDWrapper
{
public:
#if USE_USD_SDK
	UNREALUSDWRAPPER_API static void Initialize( const TArray< FString > & InPluginDirectories );
	UNREALUSDWRAPPER_API static double GetDefaultTimeCode();

	UNREALUSDWRAPPER_API static TUsdStore< pxr::TfRefPtr< pxr::UsdStage > > OpenUsdStage(const char* Path, const char* Filename);
	UNREALUSDWRAPPER_API static pxr::UsdStageCache& GetUsdStageCache();
#endif  // #if USE_USD_SDK
private:
	static bool bInitialized;
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
	static UNREALUSDWRAPPER_API bool IsProxyOrGuide(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API bool HasGeometryData(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API bool HasGeometryDataOrLODVariants(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API int GetNumLODs(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API bool IsKindChildOf(const pxr::UsdPrim& Prim, const std::string& InBaseKind);
	static UNREALUSDWRAPPER_API pxr::TfToken GetKind(const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API pxr::GfMatrix4d GetLocalTransform(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API pxr::GfMatrix4d GetLocalToWorldTransform(const pxr::UsdPrim& Prim );
	static UNREALUSDWRAPPER_API pxr::GfMatrix4d GetLocalToWorldTransform(const pxr::UsdPrim& Prim, double Time );
	static UNREALUSDWRAPPER_API pxr::GfMatrix4d GetLocalToWorldTransform(const pxr::UsdPrim& Prim, double Time, const pxr::SdfPath& AbsoluteRootPath);

	static UNREALUSDWRAPPER_API TTuple< TArray< FString >, TArray< int32 > > GetGeometryMaterials(double Time, const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API bool IsUnrealProperty(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API bool HasTransform(const pxr::UsdPrim& Prim);
	static UNREALUSDWRAPPER_API std::string GetUnrealPropertyPath(const pxr::UsdPrim& Prim);

	static UNREALUSDWRAPPER_API TUsdStore< std::vector<pxr::UsdAttribute> > GetUnrealPropertyAttributes(const pxr::UsdPrim& Prim);

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
	/* Attribute name when assigning Unreal materials to UsdGeomMeshes */
	extern UNREALUSDWRAPPER_API const pxr::TfToken MaterialAssignments;
#endif // #if USE_USD_SDK
}
