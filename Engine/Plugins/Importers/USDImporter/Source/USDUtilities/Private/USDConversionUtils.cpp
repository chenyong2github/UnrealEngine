// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDConversionUtils.h"

#include "USDAssetImportData.h"
#include "USDErrorUtils.h"
#include "USDLayerUtils.h"
#include "USDLog.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "Algo/Copy.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "GeometryCache.h"

#if WITH_EDITOR
#include "ObjectTools.h"
#endif // WITH_EDITOR

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/base/tf/stringUtils.h"
	#include "pxr/base/tf/token.h"
	#include "pxr/usd/usd/attribute.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/payloads.h"
	#include "pxr/usd/usd/primRange.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usd/variantSets.h"
	#include "pxr/usd/usdGeom/camera.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/metrics.h"
	#include "pxr/usd/usdGeom/primvar.h"
	#include "pxr/usd/usdGeom/primvarsAPI.h"
	#include "pxr/usd/usdGeom/scope.h"
	#include "pxr/usd/usdGeom/xform.h"
	#include "pxr/usd/usdLux/diskLight.h"
	#include "pxr/usd/usdLux/distantLight.h"
	#include "pxr/usd/usdLux/domeLight.h"
	#include "pxr/usd/usdLux/rectLight.h"
	#include "pxr/usd/usdLux/shapingAPI.h"
	#include "pxr/usd/usdLux/sphereLight.h"
	#include "pxr/usd/usdSkel/binding.h"
	#include "pxr/usd/usdSkel/cache.h"
	#include "pxr/usd/usdSkel/root.h"
	#include "pxr/usd/usdSkel/skeletonQuery.h"
#include "USDIncludesEnd.h"

#include <string>

#define LOCTEXT_NAMESPACE "USDConversionUtils"

namespace USDConversionUtilsImpl
{
	// Adapted from ObjectTools as it is within an Editor-only module
	FString SanitizeObjectName( const FString& InObjectName )
	{
		FString SanitizedText = InObjectName;
		const TCHAR* InvalidChar = INVALID_OBJECTNAME_CHARACTERS;
		while ( *InvalidChar )
		{
			SanitizedText.ReplaceCharInline( *InvalidChar, TCHAR( '_' ), ESearchCase::CaseSensitive );
			++InvalidChar;
		}

		return SanitizedText;
	}

	/** Show some warnings if the UVSet primvars show some unsupported/problematic behavior */
	void CheckUVSetPrimvars( TMap<int32, TArray<pxr::UsdGeomPrimvar>> UsablePrimvars, TMap<int32, TArray<pxr::UsdGeomPrimvar>> UsedPrimvars, const FString& MeshPath )
	{
		// Show a warning if the mesh has a primvar that could be used as a UV set but will actually be ignored because it targets a UV set with index
		// larger than MAX_STATIC_TEXCOORDS - 1
		TArray<FString> IgnoredPrimvarNames;
		for ( const TPair< int32, TArray<pxr::UsdGeomPrimvar> >& UsedPrimvar : UsedPrimvars )
		{
			if ( UsedPrimvar.Key > MAX_STATIC_TEXCOORDS - 1 )
			{
				for ( const pxr::UsdGeomPrimvar& Primvar : UsedPrimvar.Value )
				{
					IgnoredPrimvarNames.AddUnique( UsdToUnreal::ConvertToken( Primvar.GetBaseName() ) );
				}
			}
		}
		for ( const TPair< int32, TArray<pxr::UsdGeomPrimvar> >& UsablePrimvar : UsablePrimvars )
		{
			if ( UsablePrimvar.Key > MAX_STATIC_TEXCOORDS - 1 )
			{
				for ( const pxr::UsdGeomPrimvar& Primvar : UsablePrimvar.Value )
				{
					// Only consider texcoord2f here because the user may have some other float2[] for some other reason
					if ( Primvar.GetTypeName().GetRole() == pxr::SdfValueTypeNames->TexCoord2f.GetRole() )
					{
						IgnoredPrimvarNames.AddUnique( UsdToUnreal::ConvertToken( Primvar.GetBaseName() ) );
					}
				}
			}
		}
		if ( IgnoredPrimvarNames.Num() > 0 )
		{
			FString PrimvarNames = FString::Join( IgnoredPrimvarNames, TEXT( ", " ) );
			FUsdLogManager::LogMessage(
				EMessageSeverity::Warning,
				FText::Format(
					LOCTEXT( "TooHighUVIndex", "Mesh '{0}' has some valid UV set primvars ({1}) that will be ignored because they target an UV index larger than the highest supported ({2})" ),
					FText::FromString( MeshPath ),
					FText::FromString( PrimvarNames ),
					MAX_STATIC_TEXCOORDS - 1
				)
			);
		}

		// Show a warning if the mesh does not contain the exact primvars the material wants
		for ( const TPair< int32, TArray<pxr::UsdGeomPrimvar> >& UVAndPrimvars : UsedPrimvars )
		{
			const int32 UVIndex = UVAndPrimvars.Key;
			const TArray<pxr::UsdGeomPrimvar>& UsedPrimvarsForIndex = UVAndPrimvars.Value;
			if ( UsedPrimvarsForIndex.Num() < 1 )
			{
				continue;
			}

			// If we have multiple, we'll pick the first one and show a warning about this later
			const pxr::UsdGeomPrimvar& UsedPrimvar = UsedPrimvarsForIndex[0];

			bool bFoundUsablePrimvar = false;
			if ( const TArray<pxr::UsdGeomPrimvar>* FoundUsablePrimvars = UsablePrimvars.Find( UVIndex ) )
			{
				// We will only ever use the first one, but will show more warnings in case there are multiple
				if ( FoundUsablePrimvars->Contains( UsedPrimvar ) )
				{
					bFoundUsablePrimvar = true;
				}
			}

			if ( !bFoundUsablePrimvar )
			{
				FUsdLogManager::LogMessage(
					EMessageSeverity::Warning,
					FText::Format(
						LOCTEXT( "DidNotFindPrimvar", "Could not find primvar '{0}' on mesh '{1}', used by its bound material" ),
						FText::FromString( UsdToUnreal::ConvertString( UsedPrimvar.GetBaseName() ) ),
						FText::FromString( MeshPath )
					)
				);
			}
		}

		// Show a warning if the mesh has multiple primvars that want to write to the same UV set (e.g. 'st', 'st_0' and 'st0' at the same time)
		for ( const TPair< int32, TArray<pxr::UsdGeomPrimvar> >& UVAndPrimvars : UsablePrimvars )
		{
			const int32 UVIndex = UVAndPrimvars.Key;
			const TArray<pxr::UsdGeomPrimvar>& Primvars = UVAndPrimvars.Value;
			if ( Primvars.Num() > 1 )
			{
				// Find out what primvar we'll actually end up using, as UsedPrimvars will take precedence. Note that in the best case scenario,
				// UsablePrimvars will *contain* UsedPrimvars, so that really we're just picking which of the UsedPrimvars we'll choose. If we're not in that scenario,
				// then we will show another warning about it
				const pxr::UsdGeomPrimvar* UsedPrimvar = nullptr;
				bool bUsedByMaterial = false;
				if ( const TArray<pxr::UsdGeomPrimvar>* FoundUsedPrimvars = UsedPrimvars.Find( UVIndex ) )
				{
					if ( FoundUsedPrimvars->Num() > 0 )
					{
						UsedPrimvar = &(*FoundUsedPrimvars)[0];
						bUsedByMaterial = true;
					}
				}
				else
				{
					UsedPrimvar = &Primvars[0];
				}

				FUsdLogManager::LogMessage(
					EMessageSeverity::Warning,
					FText::Format(
						LOCTEXT( "MoreThanOnePrimvarForIndex", "Mesh '{0}' has more than one primvar used as UV set with index '{1}'. The UV set will use the values from primvar '{2}'{3}" ),
						FText::FromString( MeshPath ),
						UVAndPrimvars.Key,
						FText::FromString( UsdToUnreal::ConvertString( UsedPrimvar->GetBaseName() ) ),
						bUsedByMaterial ? FText::FromString( TEXT( ", as its used by its bound material" ) ) : FText::GetEmpty()
					)
				);
			}
		}
	}
}

template< typename ValueType >
ValueType UsdUtils::GetUsdValue( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode )
{
	ValueType Value{};
	if ( Attribute )
	{
		Attribute.Get( &Value, TimeCode );
	}

	return Value;
}

// Explicit template instantiation
template USDUTILITIES_API bool							UsdUtils::GetUsdValue< bool >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API float							UsdUtils::GetUsdValue< float >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::GfVec3f					UsdUtils::GetUsdValue< pxr::GfVec3f >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::GfMatrix4d				UsdUtils::GetUsdValue< pxr::GfMatrix4d >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::SdfAssetPath				UsdUtils::GetUsdValue< pxr::SdfAssetPath >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::VtArray< pxr::GfVec3f >	UsdUtils::GetUsdValue< pxr::VtArray< pxr::GfVec3f > >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::VtArray< float >			UsdUtils::GetUsdValue< pxr::VtArray< float > >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );
template USDUTILITIES_API pxr::VtArray< int >			UsdUtils::GetUsdValue< pxr::VtArray< int > >( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );

pxr::TfToken UsdUtils::GetUsdStageUpAxis( const pxr::UsdStageRefPtr& Stage )
{
	return pxr::UsdGeomGetStageUpAxis( Stage );
}

EUsdUpAxis UsdUtils::GetUsdStageUpAxisAsEnum( const pxr::UsdStageRefPtr& Stage )
{
	pxr::TfToken UpAxisToken = pxr::UsdGeomGetStageUpAxis( Stage );
	return UpAxisToken == pxr::UsdGeomTokens->z ? EUsdUpAxis::ZAxis : EUsdUpAxis::YAxis;
}

void UsdUtils::SetUsdStageUpAxis( const pxr::UsdStageRefPtr& Stage, pxr::TfToken Axis )
{
	pxr::UsdGeomSetStageUpAxis( Stage, Axis );
}

void UsdUtils::SetUsdStageUpAxis( const pxr::UsdStageRefPtr& Stage, EUsdUpAxis Axis )
{
	pxr::TfToken UpAxisToken = Axis == EUsdUpAxis::ZAxis ? pxr::UsdGeomTokens->z : pxr::UsdGeomTokens->y;
	SetUsdStageUpAxis( Stage, UpAxisToken );
}

float UsdUtils::GetUsdStageMetersPerUnit( const pxr::UsdStageRefPtr& Stage )
{
	return (float)pxr::UsdGeomGetStageMetersPerUnit( Stage );
}

void UsdUtils::SetUsdStageMetersPerUnit( const pxr::UsdStageRefPtr& Stage, float MetersPerUnit )
{
	if ( !Stage || !Stage->GetRootLayer() )
	{
		return;
	}

	pxr::UsdEditContext( Stage, Stage->GetRootLayer() );
	pxr::UsdGeomSetStageMetersPerUnit( Stage, MetersPerUnit );
}

bool UsdUtils::HasCompositionArcs( const pxr::UsdPrim& Prim )
{
	if ( !Prim || !Prim.IsActive() )
	{
		return false;
	}

	return Prim.HasAuthoredReferences() || Prim.HasPayload() || Prim.HasAuthoredInherits() || Prim.HasAuthoredSpecializes() || Prim.HasVariantSets();
}

UClass* UsdUtils::GetActorTypeForPrim( const pxr::UsdPrim& Prim )
{
	// If we have this attribute and a valid child camera prim then we'll assume
	// we correspond to the root scene component of an exported cine camera actor. Let's assume
	// then that we have an actual ACineCameraActor class so that the schema translators can
	// reuse the main UCineCameraComponent for the actual child camera prim
	bool bIsCineCameraActorRootComponent = false;
	if ( pxr::UsdAttribute Attr = Prim.GetAttribute( UnrealToUsd::ConvertToken( TEXT( "unrealCameraPrimName" ) ).Get() ) )
	{
		pxr::TfToken CameraComponentPrim;
		if ( Attr.Get<pxr::TfToken>( &CameraComponentPrim ) )
		{
			pxr::UsdPrim ChildCameraPrim = Prim.GetChild( CameraComponentPrim );
			if ( ChildCameraPrim && ChildCameraPrim.IsA<pxr::UsdGeomCamera>() )
			{
				bIsCineCameraActorRootComponent = true;
			}
		}
	}

	if ( Prim.IsA< pxr::UsdGeomCamera >() || bIsCineCameraActorRootComponent )
	{
		return ACineCameraActor::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxDistantLight >() )
	{
		return ADirectionalLight::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxRectLight >() || Prim.IsA< pxr::UsdLuxDiskLight >() )
	{
		return ARectLight::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxSphereLight >() )
	{
		if ( Prim.HasAPI< pxr::UsdLuxShapingAPI >() )
		{
			return ASpotLight::StaticClass();
		}
		else
		{
			return APointLight::StaticClass();
		}
	}
	else if ( Prim.IsA< pxr::UsdLuxDomeLight >() )
	{
		return ASkyLight::StaticClass();
	}
	else
	{
		return AActor::StaticClass();
	}
}

UClass* UsdUtils::GetComponentTypeForPrim( const pxr::UsdPrim& Prim )
{
	if ( Prim.IsA< pxr::UsdSkelRoot >() )
	{
		return UPoseableMeshComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdGeomMesh >() )
	{
		return UStaticMeshComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdGeomCamera >() )
	{
		return UCineCameraComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxDistantLight >() )
	{
		return UDirectionalLightComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxRectLight >() || Prim.IsA< pxr::UsdLuxDiskLight >() )
	{
		return URectLightComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdLuxSphereLight >() )
	{
		if ( Prim.HasAPI< pxr::UsdLuxShapingAPI >() )
		{
			return USpotLightComponent::StaticClass();
		}
		else
		{
			return UPointLightComponent::StaticClass();
		}
	}
	else if ( Prim.IsA< pxr::UsdLuxDomeLight >() )
	{
		return USkyLightComponent::StaticClass();
	}
	else if ( Prim.IsA< pxr::UsdGeomXformable >() )
	{
		return USceneComponent::StaticClass();
	}
	else
	{
		return nullptr;
	}
}

TUsdStore< pxr::TfToken > UsdUtils::GetUVSetName( int32 UVChannelIndex )
{
	FScopedUnrealAllocs UnrealAllocs;

	FString UVSetName = TEXT("primvars:st");

	if ( UVChannelIndex > 0 )
	{
		UVSetName += LexToString( UVChannelIndex );
	}

	TUsdStore< pxr::TfToken > UVSetNameToken = MakeUsdStore< pxr::TfToken >( UnrealToUsd::ConvertString( *UVSetName ).Get() );

	return UVSetNameToken;
}

int32 UsdUtils::GetPrimvarUVIndex( FString PrimvarName )
{
	int32 Index = PrimvarName.Len();
	while ( Index > 0 && PrimvarName[ Index - 1 ] >= '0' && PrimvarName[ Index - 1 ] <= '9' )
	{
		--Index;
	}

	if ( Index < PrimvarName.Len() )
	{
		return FCString::Atoi( *PrimvarName.RightChop( Index ) );
	}

	return 0;
}

TArray< TUsdStore< pxr::UsdGeomPrimvar > > UsdUtils::GetUVSetPrimvars( const pxr::UsdGeomMesh& UsdMesh )
{
	return UsdUtils::GetUVSetPrimvars(UsdMesh, {});
}

TArray< TUsdStore< pxr::UsdGeomPrimvar > > UsdUtils::GetUVSetPrimvars( const pxr::UsdGeomMesh& UsdMesh, const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarsUVSetNames )
{
	if ( !UsdMesh )
	{
		return {};
	}

	FScopedUsdAllocs Allocs;

	// Collect all primvars that could be used as UV sets
	TMap<FString, pxr::UsdGeomPrimvar> PrimvarsByName;
	TMap<int32, TArray<pxr::UsdGeomPrimvar>> UsablePrimvarsByUVIndex;
	pxr::UsdGeomPrimvarsAPI PrimvarsAPI{ UsdMesh };
	for (const pxr::UsdGeomPrimvar& Primvar : PrimvarsAPI.GetPrimvars() )
	{
		if ( !Primvar || !Primvar.HasValue() )
		{
			continue;
		}

		// We only care about primvars that can be used as float2[]. TexCoord2f is included
		const pxr::SdfValueTypeName& TypeName = Primvar.GetTypeName();
		if ( !TypeName.GetType().IsA( pxr::SdfValueTypeNames->Float2Array.GetType() ) )
		{
			continue;
		}

		FString PrimvarName = UsdToUnreal::ConvertToken( Primvar.GetBaseName() );
		int32 TargetUVIndex = UsdUtils::GetPrimvarUVIndex( PrimvarName );

		UsablePrimvarsByUVIndex.FindOrAdd( TargetUVIndex ).Add( Primvar );
		PrimvarsByName.Add( PrimvarName, Primvar );
	}

	// Collect all primvars that are in fact used by the materials assigned to this mesh
	TMap<int32, TArray<pxr::UsdGeomPrimvar>> PrimvarsUsedByAssignedMaterialsPerUVIndex;
	TTuple<TArray<FString>, TArray<int32>> Materials = IUsdPrim::GetGeometryMaterials( 0.0, UsdMesh.GetPrim() );
	for ( const FString& MaterialPath : Materials.Key )
	{
		if ( const TMap< FString, int32 >* FoundMaterialPrimvars = MaterialToPrimvarsUVSetNames.Find( MaterialPath ) )
		{
			for ( const TPair<FString, int32>& PrimvarAndUVIndex : *FoundMaterialPrimvars )
			{
				if ( pxr::UsdGeomPrimvar* FoundPrimvar = PrimvarsByName.Find( PrimvarAndUVIndex.Key ) )
				{
					PrimvarsUsedByAssignedMaterialsPerUVIndex.FindOrAdd( PrimvarAndUVIndex.Value ).AddUnique( *FoundPrimvar );
				}
			}
		}
	}

	// Sort all primvars we found by name, so we get consistent results
	for ( TPair<int32, TArray<pxr::UsdGeomPrimvar>>& UVIndexToPrimvars : UsablePrimvarsByUVIndex )
	{
		TArray<pxr::UsdGeomPrimvar>& Primvars = UVIndexToPrimvars.Value;
		Primvars.Sort( []( const pxr::UsdGeomPrimvar& A, const pxr::UsdGeomPrimvar& B )
		{
			return A.GetName() < B.GetName();
		} );
	}
	for ( TPair<int32, TArray<pxr::UsdGeomPrimvar>>& UVIndexToPrimvars : PrimvarsUsedByAssignedMaterialsPerUVIndex )
	{
		TArray<pxr::UsdGeomPrimvar>& Primvars = UVIndexToPrimvars.Value;
		Primvars.Sort( []( const pxr::UsdGeomPrimvar& A, const pxr::UsdGeomPrimvar& B )
		{
			return A.GetName() < B.GetName();
		} );
	}

	// A lot of things can go wrong, so show some feedback in case they do
	FString MeshPath = UsdToUnreal::ConvertPath( UsdMesh.GetPrim().GetPath() );
	USDConversionUtilsImpl::CheckUVSetPrimvars( UsablePrimvarsByUVIndex, PrimvarsUsedByAssignedMaterialsPerUVIndex, MeshPath );

	// Assemble our final results by picking the best primvar we can find for each UV index.
	// Note that we should keep searching even if we don't find our ideal case, because we don't
	// want to just discard potential UV sets if the material we happen to have bound doesn't use them, as the
	// user may just want to assign another material that does.
	TArray< TUsdStore< pxr::UsdGeomPrimvar > > Result;
	Result.SetNum( MAX_STATIC_TEXCOORDS );
	for ( int32 UVIndex = 0; UVIndex < MAX_STATIC_TEXCOORDS; ++UVIndex )
	{
		// Best case scenario: float2[]-like primvars that are actually being used by texture readers as texcoords, regardless of role
		TArray<pxr::UsdGeomPrimvar>* FoundUsedPrimvars = PrimvarsUsedByAssignedMaterialsPerUVIndex.Find( UVIndex );
		if ( FoundUsedPrimvars && FoundUsedPrimvars->Num() > 0 )
		{
			Result[ UVIndex ] = ( *FoundUsedPrimvars )[ 0 ];
			continue;
		}

		TArray<pxr::UsdGeomPrimvar>* FoundUsablePrimvars = UsablePrimvarsByUVIndex.Find( UVIndex );
		if ( FoundUsablePrimvars && FoundUsablePrimvars->Num() > 0 )
		{
			pxr::UsdGeomPrimvar* FoundTexcoordPrimvar = FoundUsablePrimvars->FindByPredicate( []( const pxr::UsdGeomPrimvar& Primvar )
			{
				return Primvar.GetTypeName().GetRole() == pxr::SdfValueTypeNames->TexCoord2f.GetRole();
			} );

			// Second-best case: Primvars with texcoord2f role
			if ( FoundTexcoordPrimvar )
			{
				Result[ UVIndex ] = *FoundTexcoordPrimvar;
				continue;
			}

			// Third-best case: Any valid primvar
			// Disabled for now as these could be any other random data the user may have as float2[]
			// Result[UVIndex] = FoundUsedPrimvars[0];
		}
	}

	return Result;
}

bool UsdUtils::IsAnimated( const pxr::UsdPrim& Prim )
{
	if ( !Prim || !Prim.IsActive() )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdGeomXformable Xformable( Prim );
	if ( Xformable )
	{
		std::vector< double > TimeSamples;
		Xformable.GetTimeSamples( &TimeSamples );

		if ( TimeSamples.size() > 0 )
		{
			return true;
		}
	}

	const std::vector< pxr::UsdAttribute >& Attributes = Prim.GetAttributes();
	for ( const pxr::UsdAttribute& Attribute : Attributes )
	{
		if ( Attribute.ValueMightBeTimeVarying() )
		{
			return true;
		}
	}

	if ( pxr::UsdSkelRoot SkeletonRoot{ Prim } )
	{
		pxr::UsdSkelCache SkeletonCache;
		SkeletonCache.Populate( SkeletonRoot, pxr::UsdTraverseInstanceProxies() );

		std::vector< pxr::UsdSkelBinding > SkeletonBindings;
		SkeletonCache.ComputeSkelBindings( SkeletonRoot, &SkeletonBindings, pxr::UsdTraverseInstanceProxies() );

		for ( const pxr::UsdSkelBinding& Binding : SkeletonBindings )
		{
			const pxr::UsdSkelSkeleton& Skeleton = Binding.GetSkeleton();
			pxr::UsdSkelSkeletonQuery SkelQuery = SkeletonCache.GetSkelQuery( Skeleton );
			pxr::UsdSkelAnimQuery AnimQuery = SkelQuery.GetAnimQuery();
			if ( !AnimQuery )
			{
				continue;
			}

			if ( AnimQuery.JointTransformsMightBeTimeVarying() || AnimQuery.BlendShapeWeightsMightBeTimeVarying() )
			{
				return true;
			}
		}
	}

	return false;
}

TArray< TUsdStore< pxr::UsdPrim > > UsdUtils::GetAllPrimsOfType( const pxr::UsdPrim& StartPrim, const pxr::TfType& SchemaType, const TArray< TUsdStore< pxr::TfType > >& ExcludeSchemaTypes )
{
    return GetAllPrimsOfType( StartPrim, SchemaType, []( const pxr::UsdPrim& ) { return false; }, ExcludeSchemaTypes );
}

TArray< TUsdStore< pxr::UsdPrim > > UsdUtils::GetAllPrimsOfType( const pxr::UsdPrim& StartPrim, const pxr::TfType& SchemaType, TFunction< bool( const pxr::UsdPrim& ) > PruneChildren, const TArray< TUsdStore< pxr::TfType > >& ExcludeSchemaTypes )
{
	TArray< TUsdStore< pxr::UsdPrim > > Result;

	pxr::UsdPrimRange PrimRange( StartPrim, pxr::UsdTraverseInstanceProxies() );

	for ( pxr::UsdPrimRange::iterator PrimRangeIt = PrimRange.begin(); PrimRangeIt != PrimRange.end(); ++PrimRangeIt )
	{
		bool bIsExcluded = false;

		for ( const TUsdStore< pxr::TfType >& SchemaToExclude : ExcludeSchemaTypes )
		{
			if ( PrimRangeIt->IsA( SchemaToExclude.Get() ) )
			{
				bIsExcluded = true;
				break;
			}
		}

		if ( !bIsExcluded && PrimRangeIt->IsA( SchemaType ) )
		{
			Result.Add( *PrimRangeIt );
		}

		if ( bIsExcluded || PruneChildren( *PrimRangeIt ) )
		{
			PrimRangeIt.PruneChildren();
		}
	}

	return Result;
}

FString UsdUtils::GetAssetPathFromPrimPath( const FString& RootContentPath, const pxr::UsdPrim& Prim )
{
	FString FinalPath;

	auto GetEnclosingModelPrim = []( const pxr::UsdPrim& Prim ) -> pxr::UsdPrim
	{
		pxr::UsdPrim ModelPrim = Prim.GetParent();

		while ( ModelPrim )
		{
			if ( IUsdPrim::IsKindChildOf( ModelPrim, "model" ) )
			{
				break;
			}
			else
			{
				ModelPrim = ModelPrim.GetParent();
			}
		}

		return ModelPrim.IsValid() ? ModelPrim : Prim;
	};

	const pxr::UsdPrim& ModelPrim = GetEnclosingModelPrim( Prim );

	const FString RawPrimName = UsdToUnreal::ConvertString( Prim.GetName() );

	pxr::UsdModelAPI ModelApi = pxr::UsdModelAPI( ModelPrim );

	std::string RawAssetName;
	ModelApi.GetAssetName( &RawAssetName );

	FString AssetName = UsdToUnreal::ConvertString( RawAssetName );
	FString MeshName = USDConversionUtilsImpl::SanitizeObjectName( RawPrimName );

	FString USDPath = UsdToUnreal::ConvertString( Prim.GetPrimPath().GetString().c_str() );

	pxr::SdfAssetPath AssetPath;
	if ( ModelApi.GetAssetIdentifier( &AssetPath ) )
	{
		std::string AssetIdentifier = AssetPath.GetAssetPath();
		USDPath = UsdToUnreal::ConvertString( AssetIdentifier.c_str() );

		USDPath = FPaths::ConvertRelativePathToFull( RootContentPath, USDPath );

		FPackageName::TryConvertFilenameToLongPackageName( USDPath, USDPath );
		USDPath.RemoveFromEnd( AssetName );
	}

	FString VariantName;

	if ( ModelPrim.HasVariantSets() )
	{
		pxr::UsdVariantSet ModelVariantSet = ModelPrim.GetVariantSet( "modelingVariant" );
		if ( ModelVariantSet.IsValid() )
		{
			std::string VariantSelection = ModelVariantSet.GetVariantSelection();

			if ( VariantSelection.length() > 0 )
			{
				VariantName = UsdToUnreal::ConvertString( VariantSelection.c_str() );
			}
		}
	}

	if ( !VariantName.IsEmpty() )
	{
		USDPath = USDPath / VariantName;
	}

	USDPath.RemoveFromStart( TEXT("/") );
	USDPath.RemoveFromEnd( RawPrimName );
	FinalPath /= (USDPath / MeshName);

	return FinalPath;
}
#endif // #if USE_USD_SDK

bool UsdUtils::IsAnimated( const UE::FUsdPrim& Prim )
{
#if USE_USD_SDK
	return IsAnimated( static_cast< const pxr::UsdPrim& >( Prim ) );
#else
	return false;
#endif // #if USE_USD_SDK
}

TArray< UE::FUsdPrim > UsdUtils::GetAllPrimsOfType( const UE::FUsdPrim& StartPrim, const TCHAR* SchemaName )
{
	return GetAllPrimsOfType( StartPrim, SchemaName, []( const UE::FUsdPrim& ) { return false; } );
}

TArray< UE::FUsdPrim > UsdUtils::GetAllPrimsOfType( const UE::FUsdPrim& StartPrim, const TCHAR* SchemaName, TFunction< bool( const UE::FUsdPrim& ) > PruneChildren, const TArray<const TCHAR*>& ExcludeSchemaNames )
{
	TArray< UE::FUsdPrim > Result;

#if USE_USD_SDK
	const pxr::TfType SchemaType = pxr::TfType::FindByName( TCHAR_TO_ANSI( SchemaName ) );

	TArray< TUsdStore< pxr::TfType > > ExcludeSchemaTypes;
	ExcludeSchemaTypes.Reserve( ExcludeSchemaNames.Num() );
	for ( const TCHAR* ExcludeSchemaName : ExcludeSchemaNames )
	{
		ExcludeSchemaTypes.Add( pxr::TfType( pxr::TfType::FindByName( TCHAR_TO_ANSI( ExcludeSchemaName ) ) ) );
	}

	auto UsdPruneChildren = [ &PruneChildren ]( const pxr::UsdPrim& ChildPrim ) -> bool
	{
		return PruneChildren( UE::FUsdPrim( ChildPrim ) );
	};

	TArray< TUsdStore< pxr::UsdPrim > > UsdResult = GetAllPrimsOfType( StartPrim, SchemaType, UsdPruneChildren, ExcludeSchemaTypes );

	for ( const TUsdStore< pxr::UsdPrim >& Prim : UsdResult )
	{
		Result.Emplace( Prim.Get() );
	}
#endif // #if USE_USD_SDK

	return Result;
}

double UsdUtils::GetDefaultTimeCode()
{
#if USE_USD_SDK
	return pxr::UsdTimeCode::Default().GetValue();
#else
	return 0.0;
#endif
}

UUsdAssetImportData* UsdUtils::GetAssetImportData( UObject* Asset )
{
	UUsdAssetImportData* ImportData = nullptr;
#if WITH_EDITORONLY_DATA
	if ( UStaticMesh* Mesh = Cast<UStaticMesh>( Asset ) )
	{
		ImportData = Cast<UUsdAssetImportData>( Mesh->AssetImportData );
	}
	else if ( USkeleton* Skeleton = Cast<USkeleton>( Asset ) )
	{
		if ( USkeletalMesh* SkMesh = Skeleton->GetPreviewMesh() )
		{
			ImportData = Cast<UUsdAssetImportData>( SkMesh->GetAssetImportData() );
		}
	}
	else if ( USkeletalMesh* SkMesh = Cast<USkeletalMesh>( Asset ) )
	{
		ImportData = Cast<UUsdAssetImportData>( SkMesh->GetAssetImportData() );
	}
	else if ( UAnimSequence* SkelAnim = Cast<UAnimSequence>( Asset ) )
	{
		ImportData = Cast<UUsdAssetImportData>( SkelAnim->AssetImportData );
	}
	else if ( UMaterialInterface* Material = Cast<UMaterialInterface>( Asset ) )
	{
		ImportData = Cast<UUsdAssetImportData>( Material->AssetImportData );
	}
	else if ( UTexture* Texture = Cast<UTexture>( Asset ) )
	{
		ImportData = Cast<UUsdAssetImportData>( Texture->AssetImportData );
	}
	else if ( UGeometryCache* GeometryCache = Cast<UGeometryCache>( Asset ) )
	{
		ImportData = Cast<UUsdAssetImportData>( GeometryCache->AssetImportData );
	}
#endif
	return ImportData;
}

void UsdUtils::AddReference( UE::FUsdPrim& Prim, const TCHAR* AbsoluteFilePath )
{
#if USE_USD_SDK
	if ( !Prim || !AbsoluteFilePath )
	{
		return;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrim UsdPrim( Prim );

	const std::string UsdAbsoluteFilePath = UnrealToUsd::ConvertString( AbsoluteFilePath ).Get();
	pxr::UsdReferences References = UsdPrim.GetReferences();

	pxr::SdfLayerRefPtr ReferenceLayer = pxr::SdfLayer::FindOrOpen( UsdAbsoluteFilePath );

	// Group updates or else the SetTypeName and AddReference calls below will both trigger separate resyncs of the same prim path
	pxr::SdfChangeBlock ChangeBlock;

	if ( ReferenceLayer && !UsdPrim.GetTypeName().IsEmpty() )
	{
		pxr::SdfPrimSpecHandle DefaultPrimSpec = ReferenceLayer->GetPrimAtPath( pxr::SdfPath( ReferenceLayer->GetDefaultPrim() ) );
		if ( DefaultPrimSpec )
		{
			// Set the same prim type as its reference so that they are compatible
			pxr::TfType DefaultPrimType = pxr::UsdSchemaRegistry::GetTypeFromName( DefaultPrimSpec->GetTypeName() );
			if ( DefaultPrimType.IsUnknown() )
			{
				UsdPrim.ClearTypeName();
			}
			else if ( !UsdPrim.IsA( DefaultPrimType ) )
			{
				UsdPrim.SetTypeName( DefaultPrimSpec->GetTypeName() );
			}
		}
	}

	pxr::SdfLayerHandle EditLayer = UsdPrim.GetStage()->GetEditTarget().GetLayer();

	FString RelativePath = AbsoluteFilePath;
	MakePathRelativeToLayer( UE::FSdfLayer( EditLayer ), RelativePath );

	References.AddReference( UnrealToUsd::ConvertString( *RelativePath ).Get() );
#endif // #if USE_USD_SDK
}

void UsdUtils::AddPayload( UE::FUsdPrim& Prim, const TCHAR* AbsoluteFilePath )
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrim UsdPrim( Prim );

	pxr::SdfLayerHandle EditLayer = UsdPrim.GetStage()->GetEditTarget().GetLayer();

	FString RelativePath = AbsoluteFilePath;
	MakePathRelativeToLayer( UE::FSdfLayer( EditLayer ), RelativePath );

	pxr::UsdPayloads Payloads = UsdPrim.GetPayloads();
	Payloads.AddPayload(
		UnrealToUsd::ConvertString( *RelativePath ).Get()
	);
#endif // #if USE_USD_SDK
}

bool UsdUtils::RenamePrim( UE::FUsdPrim& Prim, const TCHAR* NewPrimName )
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	if ( !Prim || !NewPrimName )
	{
		return false;
	}

	if ( Prim.GetName() == FName( NewPrimName ) )
	{
		return false;
	}

	pxr::UsdPrim PxrUsdPrim{ Prim };
	pxr::UsdStageRefPtr PxrUsdStage{ Prim.GetStage() };
	if ( !PxrUsdStage )
	{
		return false;
	}

	pxr::TfToken NewNameToken = UnrealToUsd::ConvertToken( NewPrimName ).Get();
	pxr::SdfPath TargetPath = PxrUsdPrim.GetPrimPath().ReplaceName( NewNameToken );

	std::vector<pxr::SdfPrimSpecHandle> SpecStack = PxrUsdPrim.GetPrimStack();
	TArray<TPair<pxr::SdfLayerRefPtr, pxr::SdfBatchNamespaceEdit>> Edits;

	// Check if we can apply this rename, and collect error messages if we can't
	// We will only rename if we can change all specs, or else we'd split the prim
	TArray<FString> ErrorMessages;
	pxr::SdfNamespaceEditDetailVector Details;
	int32 LastDetailsSize = 0;
	bool bCanApply = true;
	for ( const pxr::SdfPrimSpecHandle& Spec : SpecStack )
	{
		pxr::SdfPath SpecPath = Spec->GetPath();
		if ( !SpecPath.IsPrimPath() )
		{
			// Especially when it comes to variants, we can have many different specs for a prim.
			// e.g. we can simultaneously have "/Prim{Varset=}", "/Prim{Varset=Var}" and "/Prim" in there, and
			// we will fail to do anything if these paths are not prim paths
			continue;
		}

		pxr::SdfLayerRefPtr SpecLayer = Spec->GetLayer();

		pxr::SdfBatchNamespaceEdit BatchEdit;
		BatchEdit.Add( pxr::SdfNamespaceEdit::Rename( SpecPath, NewNameToken ) );

		int32 CurrentNumDetails = Details.size();
		if ( SpecLayer->CanApply( BatchEdit, &Details ) != pxr::SdfNamespaceEditDetail::Result::Okay )
		{
			FString LayerIdentifier = UsdToUnreal::ConvertString( SpecLayer->GetIdentifier() );

			// This error pushed something new into the Details vector. Get it as an error message
			FString ErrorMessage;
			if ( CurrentNumDetails != LastDetailsSize )
			{
				ErrorMessage = UsdToUnreal::ConvertString( Details[ CurrentNumDetails - 1 ].reason );
			}

			ErrorMessages.Add( FString::Printf( TEXT( "\t%s: %s" ), *LayerIdentifier, *ErrorMessage ) );
			bCanApply = false;
			// Don't break so we can collect all error messages
		}

		LastDetailsSize = CurrentNumDetails;
		Edits.Add( TPair<pxr::SdfLayerRefPtr, pxr::SdfBatchNamespaceEdit>{ SpecLayer, BatchEdit } );
	}

	if ( !bCanApply )
	{
		UE_LOG( LogUsd, Error, TEXT( "Failed to rename prim with path '%s' to name '%s'. Errors:\n%s" ),
			*Prim.GetPrimPath().GetString(),
			NewPrimName,
			*FString::Join( ErrorMessages, TEXT( "\n" ) )
		);

		return false;
	}

	// Actually apply the renames
	{
		pxr::SdfChangeBlock Block;

		for ( const TPair<pxr::SdfLayerRefPtr, pxr::SdfBatchNamespaceEdit>& Pair : Edits )
		{
			const pxr::SdfLayerRefPtr& Layer = Pair.Key;
			const pxr::SdfBatchNamespaceEdit& Edit = Pair.Value;

			if ( !Layer->Apply( Edit ) )
			{
				// This should not be happening since CanApply was true, so stop doing whatever it is we're doing
				UE_LOG( LogUsd, Error, TEXT( "Failed to rename prim with path '%s' to name '%s' in layer '%s'" ),
					*Prim.GetPrimPath().GetString(),
					NewPrimName,
					*UsdToUnreal::ConvertString( Layer->GetIdentifier() )
				);

				return false;
			}
		}
	}

	// For whatever reason, if the renamed prim is within a variant set it will be left inactive (i.e. effectively deleted) post-rename by USD.
	// Here we override that with a SetActive opinion on the session layer, which will also trigger a new resync of that prim.
	//
	// We must send a separate notice for this (which is why this function can't be inside a change block) for two reasons:
	// - In order to let the transactor know that this edit is done on the session layer (so that we can have our active=true opinion there and not save it to disk);
	// - Because after we apply the rename, usd *needs* to responds to notices in order to make the target path valid again. Until
	//   it does so, we can't Get/Override/Define a prim at the target path at all, and so can't set it to active.
	//
	// We can't do this *before* we rename because if we already have a prim defined/overriden on "/Root/Target", then we
	// can't apply a rename from a prim onto "/Root/Target": Meaning we'd lose all extra data we have on the prim on the session layer.
	{
		pxr::UsdEditContext EditContext{ PxrUsdStage, PxrUsdStage->GetSessionLayer() };

		if ( pxr::UsdPrim PostRenamePrim = PxrUsdStage->OverridePrim( TargetPath ) )
		{
			// We need to toggle it back and forth because whenever we undo a rename we'll rename our spec on the session layer
			// back to the original path, and that spec *already* has an active=true opinion that we set during the first rename.
			// This means that just setting it to active here wouldn't send any notice (because it already is). We need a new notice
			// to update to the fact that the child prim is now active again (the rename notice is a resync, but it already comes with the prim set to inactive)
			pxr::SdfChangeBlock Block;
			const bool bActive = true;
			PostRenamePrim.SetActive( !bActive );
			PostRenamePrim.SetActive( bActive );
		}
	}

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

FString UsdUtils::SanitizeUsdIdentifier( const TCHAR* InIdentifier )
{
#if USE_USD_SDK
	FScopedUsdAllocs Allocs;

	std::string UsdInName = UnrealToUsd::ConvertString( InIdentifier ).Get();
	std::string UsdValidName = pxr::TfMakeValidIdentifier( UsdInName );

	return UsdToUnreal::ConvertString( UsdValidName );
#endif // USE_USD_SDK

	return InIdentifier;
}

void UsdUtils::MakeVisible( UE::FUsdPrim& Prim, double TimeCode )
{
#if USE_USD_SDK
	FScopedUsdAllocs Allocs;

	pxr::UsdPrim PxrUsdPrim{ Prim };
	if ( pxr::UsdGeomImageable Imageable{ PxrUsdPrim } )
	{
		Imageable.MakeVisible( TimeCode );
	}
#endif // USE_USD_SDK
}

void UsdUtils::MakeInvisible( UE::FUsdPrim& Prim, double TimeCode )
{
#if USE_USD_SDK
	FScopedUsdAllocs Allocs;

	pxr::UsdPrim PxrUsdPrim{ Prim };
	if ( pxr::UsdGeomImageable Imageable{ PxrUsdPrim } )
	{
		Imageable.MakeInvisible( TimeCode );
	}
#endif // USE_USD_SDK
}

bool UsdUtils::IsVisible( UE::FUsdPrim& Prim, double TimeCode )
{
#if USE_USD_SDK
	FScopedUsdAllocs Allocs;

	pxr::UsdPrim PxrUsdPrim{ Prim };
	if ( pxr::UsdGeomImageable Imageable{ PxrUsdPrim } )
	{
		return Imageable.ComputeVisibility( TimeCode ) == pxr::UsdGeomTokens->inherited;
	}

	return true;
#else
	return false;
#endif // USE_USD_SDK
}

bool UsdUtils::HasInheritedVisibility( UE::FUsdPrim& Prim, double TimeCode )
{
#if USE_USD_SDK
	FScopedUsdAllocs Allocs;

	pxr::UsdPrim PxrUsdPrim{ Prim };
	if ( pxr::UsdGeomImageable Imageable{ PxrUsdPrim } )
	{
		if ( pxr::UsdAttribute VisibilityAttr = Imageable.GetVisibilityAttr() )
		{
			pxr::TfToken Visibility;
			if ( !VisibilityAttr.Get<pxr::TfToken>( &Visibility, TimeCode ) )
			{
				return true;
			}

			return Visibility == pxr::UsdGeomTokens->inherited;
		}
	}

	// If it doesn't have the attribute the default is for it to be 'inherited'
	return true;
#else
	return false;
#endif // USE_USD_SDK
}

UE::FSdfPath UsdUtils::GetPrimSpecPathForLayer( const UE::FUsdPrim& Prim, const UE::FSdfLayer& Layer )
{
	UE::FSdfPath Result;
#if USE_USD_SDK
	FScopedUsdAllocs Allocs;

	pxr::UsdPrim UsdPrim{ Prim };
	pxr::SdfLayerRefPtr UsdLayer{ Layer };

	// We may have multiple specs in the same layer if we're within a variant set (e.g "/Root/Parent/Child" and
	// "/Root{Varset=Var}Parent/Child{ChildSet=ChildVar}" and "/Root{Varset=Var}Parent/Child").
	// This function needs to return a prim path with all of its variant selections (i.e. the last example above)
	std::size_t LargestPathLength = 0;
	for ( const pxr::SdfPrimSpecHandle& Spec : UsdPrim.GetPrimStack() )
	{
		pxr::SdfPath SpecPath = Spec->GetPath();
		if ( !SpecPath.IsPrimPath() )
		{
			continue;
		}

		if ( Spec->GetLayer() == UsdLayer )
		{
			const std::size_t NewPathLength = Spec->GetPath().GetString().length();
			if ( NewPathLength > LargestPathLength )
			{
				Result = UE::FSdfPath{ SpecPath };
			}
		}
	}

#endif // USE_USD_SDK
	return Result;
}

USDUTILITIES_API void UsdUtils::RemoveAllPrimSpecs( const UE::FUsdPrim& Prim, const UE::FSdfLayer& Layer )
{
#if USE_USD_SDK
	FScopedUsdAllocs Allocs;

	pxr::UsdPrim UsdPrim{ Prim };
	if ( !UsdPrim )
	{
		return;
	}

	pxr::SdfChangeBlock Block;

	pxr::SdfLayerRefPtr UsdLayer{ Layer };
	pxr::UsdStageRefPtr UsdStage = UsdPrim.GetStage();

	for ( const pxr::SdfPrimSpecHandle& Spec : UsdPrim.GetPrimStack() )
	{
		pxr::SdfPath SpecPath = Spec->GetPath();
		if ( !SpecPath.IsPrimPath() )
		{
			continue;
		}

		if ( Spec->GetLayer() != UsdLayer )
		{
			continue;
		}

		UE_LOG( LogUsd, Log, TEXT( "Removing prim spec '%s' from edit target '%s'" ), *UsdToUnreal::ConvertPath( SpecPath ), *UsdToUnreal::ConvertString( UsdLayer->GetIdentifier() ) );
		UsdStage->RemovePrim( SpecPath );
	}

#endif // USE_USD_SDK
}

#if USE_USD_SDK
#undef LOCTEXT_NAMESPACE
#endif
