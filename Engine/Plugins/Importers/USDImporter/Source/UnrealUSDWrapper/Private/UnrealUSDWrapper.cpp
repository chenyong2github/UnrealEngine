// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealUSDWrapper.h"

#include "USDMemory.h"

#include "Modules/ModuleManager.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/usd/usd/usdFileFormat.h"
#include "pxr/usd/usd/common.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/references.h"
#include "pxr/usd/usd/stageCacheContext.h"
#include "pxr/usd/usdGeom/modelAPI.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"
#include "pxr/usd/usdGeom/faceSetAPI.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "pxr/base/tf/getenv.h"
#include "pxr/base/tf/setenv.h"
#include "pxr/usd/ar/defaultResolver.h"
#include "pxr/usd/ar/defineResolver.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/usd/usd/variantSets.h"
#include "pxr/usd/usd/debugCodes.h"
#include "pxr/usd/kind/registry.h"

#include "USDIncludesEnd.h"


using std::vector;
using std::string;

using namespace pxr;

#ifdef PLATFORM_WINDOWS
	#define USDWRAPPER_USE_XFORMACHE	1
#else
	#define USDWRAPPER_USE_XFORMACHE	0
#endif


#if USDWRAPPER_USE_XFORMACHE
static TUsdStore< UsdGeomXformCache > XFormCache;
#endif // USDWRAPPER_USE_XFORMACHE

namespace UnrealIdentifiers
{
	/**
	* Identifies the LOD variant set on a primitive which means this primitive has child prims that LOD meshes
	* named LOD0, LOD1, LOD2, etc
	*/
	static const TfToken LOD("LOD");

	static const TfToken AssetPath("unrealAssetPath");

	static const TfToken ActorClass("unrealActorClass");

	static const TfToken PropertyPath("unrealPropertyPath");

	static const TfToken ProxyPurpose("proxy");

	static const TfToken GuidePurpose("guide");

	static const TfToken MaterialRelationship("material:binding");
}


void Log(const char* Format, ...)
{
	const int32 TempStrSize = 4096;
	ANSICHAR TempStr[TempStrSize];

	GET_VARARGS_ANSI(TempStr, TempStrSize, TempStrSize - 1, Format, Format);

	UE_LOG(LogTemp, Log, TEXT("%hs"), TempStr);
}


class USDHelpers
{

public:
	static void LogPrimTree(const UsdPrim& Root)
	{
		LogPrimTreeHelper("", Root);
	}

private:
	static void LogPrimTreeHelper(const string& Concat, const UsdPrim& Prim)
	{
		string TypeName = Prim.GetTypeName().GetString();
		bool bIsModel = Prim.IsModel();
		bool bIsAbstract = Prim.IsAbstract();
		bool bIsGroup = Prim.IsGroup();
		bool bIsInstance = Prim.IsInstance();
		bool bIsActive = Prim.IsActive();
		bool bInMaster = Prim.IsInMaster();
		bool bIsMaster = Prim.IsMaster();
		Log(string(Concat + "Prim: [%s] %s Model:%d Abstract:%d Group:%d Instance:%d(Master:%s) Active:%d InMaster:%d IsMaster:%d\n").c_str(),
			TypeName.c_str(), Prim.GetName().GetText(), bIsModel, bIsAbstract, bIsGroup, bIsInstance, bIsInstance ? Prim.GetMaster().GetName().GetString().c_str() : "", bIsActive, bInMaster, bIsMaster);
		{
			UsdMetadataValueMap Metadata = Prim.GetAllMetadata();
			if(Metadata.size())
			{
				Log(string(Concat+"\tMetaData:\n").c_str());
				for(auto KeyValue : Metadata)
				{
					Log(string(Concat+"\t\t[%s] %s\n").c_str(), KeyValue.second.GetTypeName().c_str(), KeyValue.first.GetText());
				}
			}

			vector<UsdRelationship> Relationships = Prim.GetRelationships();
			if (Relationships.size())
			{
				Log(string(Concat + "\tRelationships:\n").c_str());
				for (const UsdRelationship& Relationship : Relationships)
				{
					SdfPathVector Targets;
					Relationship.GetTargets(&Targets);

					for(SdfPath& Path : Targets)
					{
						Log(string(Concat + "\t\t%s\n").c_str(), Path.GetString().c_str());
					}
				}
			}


			vector<UsdAttribute> Attributes = Prim.GetAttributes();
			if(Attributes.size())
			{
				Log(string(Concat+"\tAttributes:\n").c_str());
				for(const UsdAttribute& Attribute : Attributes)
				{
					if (Attribute.IsAuthored())
					{
						Log(string(Concat + "\t\t[%s] %s %s\n").c_str(), Attribute.GetTypeName().GetAsToken().GetText(), Attribute.GetBaseName().GetText(), Attribute.GetDisplayName().c_str());
					}
				}
			}

			if (Prim.HasVariantSets())
			{
				Log(string(Concat + "\tVariant Sets:\n").c_str());
				UsdVariantSets VariantSets = Prim.GetVariantSets();
				vector<string> SetNames = VariantSets.GetNames();
				for (const string& SetName : SetNames)
				{
					Log(string(Concat + "\t\t%s:\n").c_str(), SetName.c_str());

					UsdVariantSet Set = Prim.GetVariantSet(SetName);

					vector<string> VariantNames = Set.GetVariantNames();
					for (const string& VariantName : VariantNames)
					{
						char ActiveChar = ' ';
						if (Set.GetVariantSelection() == VariantName)
						{
							ActiveChar = '*';
						}
						Log(string(Concat + "\t\t\t%s%c\n").c_str(), VariantName.c_str(), ActiveChar);
					}
				}
			}
		}


		for(const UsdPrim& Child : Prim.GetChildren())
		{
			LogPrimTreeHelper(Concat+"\t", Child);
		}

		//Log("\n");
	}
};

class FAttribInternalData
{
public:
	FAttribInternalData(UsdAttribute& InAttribute)
		: Attribute(InAttribute)
	{
		VtValue CustomData = Attribute.GetCustomDataByKey(UnrealIdentifiers::PropertyPath);

		AttributeName = Attribute.GetBaseName().GetString();
		TypeName = Attribute.GetTypeName().GetAsToken().GetString();

		if (CustomData.IsHolding<std::string>())
		{
			UnrealPropertyPath = CustomData.Get<std::string>();
		}
	}

	std::string UnrealPropertyPath;
	std::string AttributeName;
	std::string TypeName;
	UsdAttribute Attribute;
};

std::string FUsdAttribute::GetUnrealPropertyPath( const pxr::UsdAttribute& Attribute )
{
	std::string UnrealPropertyPath;

	VtValue CustomData = Attribute.GetCustomDataByKey(UnrealIdentifiers::PropertyPath);

	if (CustomData.IsHolding<std::string>())
	{
		UnrealPropertyPath = CustomData.Get<std::string>();
	}

	return UnrealPropertyPath;
}

template<typename T>
bool GetValue(T& OutVal, const pxr::UsdAttribute& Attrib, int ArrayIndex, double Time)
{
	bool bResult = false;

	if (ArrayIndex != -1)
	{
		// Note: VtArray is copy on write so this is cheap
		VtArray<T> Array;
		if (Attrib.Get(&Array, Time))
		{
			OutVal = Array[ArrayIndex];
			bResult = true;
		}
	}
	else
	{
		bResult = Attrib.Get(&OutVal, Time);
	}

	return bResult;
}

template<typename T>
bool IsHolding(const VtValue& Value)
{
	return Value.IsHolding<T>() || Value.IsHolding<VtArray<T>>();
}

bool FUsdAttribute::AsInt(int64_t& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	// We test multiple types of ints here. int64 is always returned as it can hold all other types
	// Unreal expects this
	VtValue Value;
	bool bResult = Attribute.Get(&Value, Time);
	if (IsHolding<int8_t>(Value))
	{
		uint8_t Val = 0;
		bResult = GetValue(Val, Attribute, ArrayIndex, Time);
		OutVal = Val;
	}
	else if (IsHolding<int32_t>(Value))
	{
		int32_t Val = 0;
		bResult = GetValue(Val, Attribute, ArrayIndex, Time);
		OutVal = Val;
	}
	else if (IsHolding<int64_t>(Value))
	{
		int64_t Val = 0;
		bResult = GetValue(Val, Attribute, ArrayIndex, Time);
		OutVal = Val;
	}

	return bResult;
}

bool FUsdAttribute::AsUnsignedInt(uint64_t& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	// We test multiple types of ints here. uint64 is always returned as it can hold all other types
	// Unreal expects this
	VtValue Value;
	bool bResult = Attribute.Get(&Value, Time);
	if (IsHolding<uint8_t>(Value))
	{
		uint8_t Val;
		bResult = GetValue(Val, Attribute, ArrayIndex, Time);
		OutVal = Val;
	}
	else if (IsHolding<uint32_t>(Value))
	{
		uint32_t Val;
		bResult = GetValue(Val, Attribute, ArrayIndex, Time);
		OutVal = Val;
	}
	else if (IsHolding<uint64_t>(Value))
	{
		uint64_t Val;
		bResult = GetValue(Val, Attribute, ArrayIndex, Time);
		OutVal = Val;
	}

	return bResult;
}

bool FUsdAttribute::AsDouble(double& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	bool bResult = false;

	bResult = GetValue<double>(OutVal, Attribute, ArrayIndex, Time);

	if (!bResult)
	{
		float Val = 0.0f;
		bResult = GetValue<float>(Val, Attribute, ArrayIndex, Time);
		OutVal = Val;
	}

	return bResult;
}

bool FUsdAttribute::AsString(const char*& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	// this method is very hacky to return temp strings
	// designed to have the string copied immediately
	bool bResult = false;

	VtValue Value;
	Attribute.Get(&Value);
	// mem leak
	static std::string Temp;
	if (IsHolding<std::string>(Value))
	{
		bResult = GetValue(Temp, Attribute, ArrayIndex, Time);

		OutVal = Temp.c_str();
	}
	else if (IsHolding<TfToken>(Value))
	{
		TfToken Token;
		bResult = GetValue(Token, Attribute, ArrayIndex, Time);

		Temp = Token.GetString();

		OutVal = Temp.c_str();
	}

	return bResult;
}

bool FUsdAttribute::AsBool(bool& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	return GetValue(OutVal, Attribute, ArrayIndex, Time);
}

bool FUsdAttribute::AsVector2(FUsdVector2Data& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	GfVec2f Value;
	const bool bResult = GetValue(Value, Attribute, ArrayIndex, Time);

	OutVal.X = Value[0];
	OutVal.Y = Value[1];

	return bResult;
}

bool FUsdAttribute::AsVector3(FUsdVectorData& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	GfVec3f Value;
	const bool bResult = GetValue(Value, Attribute, ArrayIndex, Time);

	OutVal.X = Value[0];
	OutVal.Y = Value[1];
	OutVal.Z = Value[2];

	return bResult;
}

bool FUsdAttribute::AsVector4(FUsdVector4Data& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	GfVec4f Value;
	const bool bResult = GetValue(Value, Attribute, ArrayIndex, Time);

	OutVal.X = Value[0];
	OutVal.Y = Value[1];
	OutVal.Z = Value[2];
	OutVal.W = Value[3];

	return bResult;
}

bool FUsdAttribute::AsColor(FUsdVector4Data& OutVal, const pxr::UsdAttribute& Attribute, int ArrayIndex, double Time)
{
	GfVec4f Value;
	bool bResult = GetValue(Value, Attribute, ArrayIndex, Time);

	if (bResult)
	{
		OutVal.X = Value[0];
		OutVal.Y = Value[1];
		OutVal.Z = Value[2];
		OutVal.W = Value[3];
	}
	else
	{
		// Try color 3 with a = 1;
		GfVec3f Value3;
		bResult = GetValue<GfVec3f>(Value3, Attribute, ArrayIndex, Time);
		OutVal.X = Value3[0];
		OutVal.Y = Value3[1];
		OutVal.Z = Value3[2];
		OutVal.W = 1;
	}

	return bResult;
}

bool FUsdAttribute::IsUnsigned(const pxr::UsdAttribute& Attribute)
{
	VtValue Value;
	Attribute.Get(&Value);

	return IsHolding<uint8_t>(Value)
		|| IsHolding<uint32_t>(Value)
		|| IsHolding<uint64_t>(Value);
}

int FUsdAttribute::GetArraySize( const pxr::UsdAttribute& Attribute )
{
	VtValue Value;
	Attribute.Get(&Value);

	return Value.IsArrayValued() ? (int)Value.GetArraySize() : -1;

}
bool IUsdPrim::IsProxyOrGuide( const UsdPrim& Prim )
{
	UsdGeomImageable Geom(Prim);
	if (Geom)
	{
		UsdAttribute PurposeAttr = Geom.GetPurposeAttr();

		TfToken Purpose;
		PurposeAttr.Get(&Purpose);

		return Purpose == UnrealIdentifiers::ProxyPurpose || Purpose == UnrealIdentifiers::GuidePurpose;
	}

	return false;
}

bool IUsdPrim::HasGeometryData(const UsdPrim& Prim)
{
	return UsdGeomMesh(Prim) ? true : false;
}

bool IUsdPrim::HasGeometryDataOrLODVariants(const UsdPrim& Prim)
{
	return HasGeometryData(Prim) || GetNumLODs(Prim) > 0;
}

int IUsdPrim::GetNumLODs(const UsdPrim& Prim)
{
	FScopedUsdAllocs UsdAllocs;

	// 0 indicates no variant or no lods in variant. 
	int NumLODs = 0;
	if (Prim.HasVariantSets())
	{
		UsdVariantSet LODVariantSet = Prim.GetVariantSet(UnrealIdentifiers::LOD);
		if(LODVariantSet.IsValid())
		{
			vector<string> VariantNames = LODVariantSet.GetVariantNames();
			NumLODs = VariantNames.size();
		}
	}

	return NumLODs;
}

bool IUsdPrim::IsKindChildOf(const UsdPrim& Prim, const std::string& InBaseKind)
{
	TfToken BaseKind(InBaseKind);
	
	KindRegistry& Registry = KindRegistry::GetInstance();

	TfToken PrimKind( GetKind(Prim) );

	return Registry.IsA(PrimKind, BaseKind);

}

TfToken IUsdPrim::GetKind(const pxr::UsdPrim& Prim)
{
	TfToken KindType;

	UsdModelAPI Model(Prim);
	if (Model)
	{
		Model.GetKind(&KindType);
	}
	else
	{
		// Prim is not a model, read kind directly from metadata
		static TfToken KindMetaDataToken("kind");
		Prim.GetMetadata(KindMetaDataToken, &KindType);
	}
	
	return KindType;
}

pxr::GfMatrix4d IUsdPrim::GetLocalTransform(const pxr::UsdPrim& Prim)
{
	pxr::GfMatrix4d USDMatrix(1);

	pxr::UsdGeomXformable XForm(Prim);
	if(XForm)
	{
		// Set transform
		bool bResetXFormStack = false;
		XForm.GetLocalTransformation(&USDMatrix, &bResetXFormStack);
	}

	return USDMatrix;
}

pxr::GfMatrix4d IUsdPrim::GetLocalToWorldTransform(const pxr::UsdPrim& Prim )
{
	return GetLocalToWorldTransform( Prim, pxr::UsdTimeCode::Default().GetValue() );
}

pxr::GfMatrix4d IUsdPrim::GetLocalToWorldTransform(const pxr::UsdPrim& Prim, double Time)
{
	pxr::SdfPath AbsoluteRootPath = pxr::SdfPath::AbsoluteRootPath();
	return GetLocalToWorldTransform(Prim, Time, AbsoluteRootPath);

}

pxr::GfMatrix4d IUsdPrim::GetLocalToWorldTransform(const pxr::UsdPrim& Prim, double Time, const pxr::SdfPath& AbsoluteRootPath)
{
	pxr::SdfPath PrimPath = Prim.GetPath();
	if (!Prim || PrimPath == AbsoluteRootPath)
	{
		return pxr::GfMatrix4d(1);
	}

	pxr::GfMatrix4d AccumulatedTransform(1.);
	bool bResetsXFormStack = false;
	pxr::UsdGeomXformable XFormable(Prim);
	// silently ignoring errors
	XFormable.GetLocalTransformation(&AccumulatedTransform, &bResetsXFormStack, Time);

	if (!bResetsXFormStack)
	{
		AccumulatedTransform = AccumulatedTransform * GetLocalToWorldTransform(Prim.GetParent(), Time, AbsoluteRootPath);
	}

	return AccumulatedTransform;
}

std::string IUsdPrim::GetUnrealPropertyPath(const pxr::UsdPrim& Prim)
{
	VtValue CustomData = Prim.GetCustomDataByKey(UnrealIdentifiers::PropertyPath);
	if (CustomData.IsHolding<std::string>())
	{
		return CustomData.Get<std::string>();
	}

	return {};
}

TUsdStore< std::vector<UsdAttribute> > PrivateGetAttributes(const pxr::UsdPrim& Prim, const TfToken& ByMetadata)
{
	FScopedUsdAllocs UsdAllocs;

	std::vector<UsdAttribute> Attributes = Prim.GetAttributes();

	std::vector<UsdAttribute> OutAttributes;
	OutAttributes.reserve(Attributes.size());

	for (UsdAttribute& Attr : Attributes)
	{
		if (ByMetadata.IsEmpty() || Attr.HasCustomDataKey(ByMetadata))
		{
			OutAttributes.push_back(Attr);
		}
	}

	return OutAttributes;
}

TUsdStore< std::vector<UsdAttribute> > IUsdPrim::GetUnrealPropertyAttributes(const pxr::UsdPrim& Prim)
{
	return PrivateGetAttributes(Prim, UnrealIdentifiers::PropertyPath);
}

std::string IUsdPrim::GetUnrealAssetPath(const pxr::UsdPrim& Prim)
{
	std::string UnrealAssetPath;

	UsdAttribute UnrealAssetPathAttr = Prim.GetAttribute(UnrealIdentifiers::AssetPath);
	if (UnrealAssetPathAttr.HasValue())
	{
		UnrealAssetPathAttr.Get(&UnrealAssetPath);
	}

	return UnrealAssetPath;
}

std::string IUsdPrim::GetUnrealActorClass(const pxr::UsdPrim& Prim)
{
	std::string UnrealActorClass;

	UsdAttribute UnrealActorClassAttr = Prim.GetAttribute(UnrealIdentifiers::ActorClass);
	if (UnrealActorClassAttr.HasValue())
	{
		UnrealActorClassAttr.Get(&UnrealActorClass);
	}

	return UnrealActorClass;
}

namespace Internal
{
	TArray< FString > FillMaterialInfo(const SdfPath& Path, UsdStageWeakPtr Stage)
	{
		TArray< FString > MaterialNames;

		// load each material at the material path; 
		UsdPrim MaterialPrim = Stage->Load(Path);
		
		if(MaterialPrim)
		{
			// Default to using the prim path name as the path for this material in Unreal
			FString MaterialName = ANSI_TO_TCHAR( MaterialPrim.GetName().GetString().c_str() ) ;

			std::string UsdMaterialName;

			// See if the material has an "unrealAssetPath" attribute.  This should be the full name of the material
			static const TfToken AssetPathToken = TfToken(UnrealIdentifiers::AssetPath);
			UsdAttribute UnrealAssetPathAttr = MaterialPrim.GetAttribute(AssetPathToken);
			if (UnrealAssetPathAttr && UnrealAssetPathAttr.HasValue())
			{
				UnrealAssetPathAttr.Get(&UsdMaterialName);


			}

			MaterialNames.Add( MoveTemp( MaterialName ) );
		}

		return MaterialNames;
	}
}

std::string DiscoverInformationAboutUsdMaterial(const UsdShadeMaterial& ShadeMaterial, const UsdGeomGprim& boundPrim)
{
	std::string ShadingEngineName = (ShadeMaterial ? ShadeMaterial.GetPrim() : boundPrim.GetPrim()).GetPrimPath().GetString();
	return ShadingEngineName;
}

TTuple< TArray< FString >, TArray< int32 > > IUsdPrim::GetGeometryMaterials(double Time, const UsdPrim& Prim)
{
	// Material mappings
	// @todo time not supported yet

	FScopedUsdAllocs UsdAllocs;

	TArray< FString > MaterialNames;
	TArray< int32 > FaceMaterialIndices;
	VtArray<int> FaceVertexCounts;

	UsdShadeMaterialBindingAPI BindingAPI(Prim);
	UsdShadeMaterial ShadeMaterial = BindingAPI.ComputeBoundMaterial();
	UsdPrim ShadeMaterialPrim = ShadeMaterial.GetPrim();
	if (ShadeMaterialPrim)
	{
		SdfPath Path = ShadeMaterialPrim.GetPath();
		std::string ShadingEngineName = DiscoverInformationAboutUsdMaterial(ShadeMaterial, UsdGeomGprim());

		MaterialNames.Emplace( ANSI_TO_TCHAR( ShadingEngineName.c_str() ) );
		FaceMaterialIndices.Emplace( 0 );

		return MakeTuple( MaterialNames, FaceMaterialIndices );
	}

	// If the gprim does not have a material faceSet which represents per-face 
	// shader assignments, assign the shading engine to the entire gprim.
	std::vector<UsdGeomSubset> FaceSubsets = UsdShadeMaterialBindingAPI(Prim).GetMaterialBindSubsets();
	std::vector<UsdGeomFaceSetAPI> FaceSets = UsdGeomFaceSetAPI::GetFaceSets(Prim);

	bool bHasOldStyleFaceSets = UsdShadeMaterial::HasMaterialFaceSet(Prim);

	if (FaceSubsets.empty() && !bHasOldStyleFaceSets && FaceSets.size() == 0)
	{
		return {};
	}

	UsdGeomMesh Mesh(Prim);

	if (!FaceSubsets.empty() && Mesh)
	{
		UsdAttribute FaceCounts = Mesh.GetFaceVertexCountsAttr();
		if (FaceCounts)
		{
			FaceCounts.Get(&FaceVertexCounts, Time);
		}

		int FaceCount = FaceVertexCounts.size();
		if (FaceCount == 0)
		{
			//MGlobal::displayError(TfStringPrintf("Unable to get face count "
			//	"for gprim at path <%s>.", primSchema.GetPath().GetText()).c_str());
			return {};
		}

		std::string ReasonWhyNotPartition;

		bool ValidPartition = UsdGeomSubset::ValidateSubsets(FaceSubsets, FaceCount, UsdGeomTokens->partition, &ReasonWhyNotPartition);
		if (!ValidPartition)
		{
			VtIntArray unassignedIndices = UsdGeomSubset::GetUnassignedIndices(FaceSubsets, FaceCount);
		}

		MaterialNames.AddDefaulted(FaceSubsets.size());
		FaceMaterialIndices.AddZeroed(FaceVertexCounts.size());

		int MaterialIndex = 0;
		for (const auto &Subset : FaceSubsets)
		{
			UsdShadeMaterialBindingAPI SubsetBindingAPI(Subset.GetPrim());
			UsdShadeMaterial BoundMaterial = SubsetBindingAPI.ComputeBoundMaterial();

			// Only transfer the first timeSample or default Indices, if 
			// there are no time-samples.
			VtIntArray Indices;
			Subset.GetIndicesAttr().Get(&Indices, UsdTimeCode::EarliestTime());

			if (!BoundMaterial)
			{
				++MaterialIndex;
				continue;
			}

			std::string ShadingEngineName = DiscoverInformationAboutUsdMaterial(BoundMaterial, UsdGeomGprim());
			MaterialNames[MaterialIndex] = ANSI_TO_TCHAR( ShadingEngineName.c_str() ) ;

			for (int i = 0; i < Indices.size(); ++i)
			{
				int PolygonIndex = Indices[i];
				if (PolygonIndex >= 0 && PolygonIndex < FaceMaterialIndices.Num())
				{
					FaceMaterialIndices[PolygonIndex] = MaterialIndex;
				}
			}

			++MaterialIndex;
		}
	}

	// Import per-face-set shader bindings.
	if (bHasOldStyleFaceSets)
	{
		UsdGeomFaceSetAPI MaterialFaceSet = UsdShadeMaterial::GetMaterialFaceSet(Prim);

		SdfPathVector BindingTargets;
		// TODO: ...
	}

	// TODO: ...

/////////////////////////////////////////////
	if (FaceMaterialIndices.Num() != 0)
	{
		return MakeTuple( MaterialNames, FaceMaterialIndices );
	}

	FaceMaterialIndices.AddZeroed(FaceVertexCounts.size());

	// Figure out a zero based material index for each face.  The mapping is FaceMaterialIndices[FaceIndex] = MaterialIndex;
	// This is done by walking the face sets and for each face set getting the number number of unique groups of faces in the set
	// Each one of these groups represents a material index for that face set.  If there are multiple face sets the material index is offset by the face set index
	// Once the groups of faces are determined, walk the Indices for the total number of faces in each group.  Each element in the face Indices array represents a single global face index
	// Assign the current material index to it

	// @todo USD/Unreal.  This is probably wrong for multiple face sets.  They don't make a ton of sense for unreal as there can only be one "set" of materials at once and there is no construct in the engine for material sets

	//MaterialNames.Resize(FaceSets)
	if (FaceSets.size() > 0)
	{
		for (int FaceSetIdx = 0; FaceSetIdx < FaceSets.size(); ++FaceSetIdx)
		{
			const UsdGeomFaceSetAPI& FaceSet = FaceSets[FaceSetIdx];

			SdfPathVector BindingTargets;
			FaceSet.GetBindingTargets(&BindingTargets);


			UsdStageWeakPtr Stage = Prim.GetStage();
			for (const SdfPath& Path : BindingTargets)
			{
				MaterialNames.Append( Internal::FillMaterialInfo(Path, Stage) );
			}
			// Faces must be mutually exclusive
			if (FaceSet.GetIsPartition())
			{
				// Get the list of faces in the face set.  The size of this list determines the number of materials in this set
				VtIntArray FaceCounts;
				FaceSet.GetFaceCounts(&FaceCounts, Time);

				// Get the list of global face Indices mapped in this set
				VtIntArray FaceIndices;
				FaceSet.GetFaceIndices(&FaceIndices, Time);

				// How far we are into the face Indices list
				int Offset = 0;

				// Walk each face group in the set
				for (int FaceCountIdx = 0; FaceCountIdx < FaceCounts.size(); ++FaceCountIdx)
				{
					int MaterialIdx = FaceSetIdx * FaceSets.size() + FaceCountIdx;

					// Number of faces with the material index
					int FaceCount = FaceCounts[FaceCountIdx];

					// Walk each face and map it to the computed material index
					for (int FaceNum = 0; FaceNum < FaceCount; ++FaceNum)
					{
						int Face = FaceIndices[FaceNum + Offset];
						FaceMaterialIndices[Face] = MaterialIdx;
					}
					Offset += FaceCount;
				}
			}
		}
	}
	else
	{
		// No face sets, find a relationship that defines the material 
		UsdRelationship Relationship = Prim.GetRelationship(UnrealIdentifiers::MaterialRelationship);
		if (Relationship)
		{
			SdfPathVector Targets;
			Relationship.GetTargets(&Targets);
			// Note there should only be one target without a face set but fill them out so we can warn later
			for (const SdfPath& Path : Targets)
			{
				MaterialNames.Append( Internal::FillMaterialInfo(Path, Prim.GetStage()) );
			}
		}
	}

	return MakeTuple( MaterialNames, FaceMaterialIndices );
}

bool IUsdPrim::IsUnrealProperty(const pxr::UsdPrim& Prim)
{
	return Prim.HasCustomDataKey(UnrealIdentifiers::PropertyPath);
}

bool IUsdPrim::HasTransform(const pxr::UsdPrim& Prim)
{
	return UsdGeomXformable(Prim) ? true : false;
}

bool IUsdPrim::SetActiveLODIndex(const pxr::UsdPrim& Prim, int LODIndex)
{
	FScopedUsdAllocs UsdAllocs;

	if (Prim.HasVariantSets())
	{
		UsdVariantSet LODVariantSet = Prim.GetVariantSet(UnrealIdentifiers::LOD);
		if (LODVariantSet.IsValid())
		{
			vector<string> VariantNames = LODVariantSet.GetVariantNames();

			bool bResult = false;
			if(LODIndex < VariantNames.size())
			{
				bResult = LODVariantSet.SetVariantSelection(VariantNames[LODIndex]);
			}
		}
	}
		
	return false;
}

EUsdGeomOrientation IUsdPrim::GetGeometryOrientation(const pxr::UsdGeomMesh& Mesh)
{
	return GetGeometryOrientation( Mesh, pxr::UsdTimeCode::Default().GetValue() );
}

EUsdGeomOrientation IUsdPrim::GetGeometryOrientation(const pxr::UsdGeomMesh& Mesh, double Time)
{
	EUsdGeomOrientation GeomOrientation = EUsdGeomOrientation::RightHanded;

	if (Mesh)
	{
		UsdAttribute Orientation = Mesh.GetOrientationAttr();
		if(Orientation)
		{ 
			static TfToken RightHanded("rightHanded");
			static TfToken LeftHanded("leftHanded");

			TfToken OrientationValue;
			Orientation.Get(&OrientationValue, Time);

			GeomOrientation = OrientationValue == LeftHanded ? EUsdGeomOrientation::LeftHanded : EUsdGeomOrientation::RightHanded;
		}
	}

	return GeomOrientation;
}

bool UnrealUSDWrapper::bInitialized = false;
std::string UnrealUSDWrapper::Errors;


void UnrealUSDWrapper::Initialize(const std::vector<std::string>& InPluginDirectories)
{
	{
		FScopedUsdAllocs UsdAllocs;
		PlugRegistry::GetInstance().RegisterPlugins(InPluginDirectories);
	}

	bInitialized = true;
}

TUsdStore< pxr::UsdStageRefPtr > UnrealUSDWrapper::OpenUsdStage(const char* Path, const char* Filename)
{
	Errors.clear();

	if (!bInitialized)
	{
		return {};
	}

	bool bImportedSuccessfully = false;

	TfErrorMark ErrorMark;

	string PathAndFilename = string(Path) + string(Filename);

	bool bIsSupported = UsdStage::IsSupportedFile(PathAndFilename);
	
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageCacheContext UsdStageCacheContext( GetUsdStageCache() );
	UsdStageRefPtr Stage = UsdStage::Open(PathAndFilename);

	if (!ErrorMark.IsClean())
	{
		TfErrorMark::Iterator i;
		for (i = ErrorMark.GetBegin(); i != ErrorMark.GetEnd(); ++i)
		{
			Errors += i->GetErrorCodeAsString();
			Errors += " ";
			Errors += i->GetCommentary();
			Errors += "\n";
		}
	}

	return Stage;
}

double UnrealUSDWrapper::GetDefaultTimeCode()
{
	return UsdTimeCode::Default().GetValue();
}

const char* UnrealUSDWrapper::GetErrors()
{
	return Errors.length() > 0 ? Errors.c_str() : nullptr;
}

pxr::UsdStageCache& UnrealUSDWrapper::GetUsdStageCache()
{
	static TUsdStore< pxr::UsdStageCache > UsdStageCache;
	return UsdStageCache.Get();
}
#endif // USE_USD_SDK

class FUnrealUSDWrapperModule : public IUnrealUSDWrapperModule
{
public:
	virtual void StartupModule() override
	{
		FUsdMemoryManager::Initialize();
	}

	virtual void ShutdownModule() override
	{
		FUsdMemoryManager::Shutdown();
	}

	virtual void Initialize(const std::vector<std::string>& InPluginDirectories) override
	{
#if USE_USD_SDK
		UnrealUSDWrapper::Initialize( InPluginDirectories );
#endif
	}
};

IMPLEMENT_MODULE_USD(FUnrealUSDWrapperModule, UnrealUSDWrapper);
