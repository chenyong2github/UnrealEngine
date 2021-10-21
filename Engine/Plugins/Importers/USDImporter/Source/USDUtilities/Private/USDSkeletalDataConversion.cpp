// Copyright Epic Games, Inc. All Rights Reserved.


#include "USDSkeletalDataConversion.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDErrorUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/UsdStage.h"

#include "Animation/AnimCurveTypes.h"
#include "AnimationRuntime.h"
#include "AnimEncoding.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Misc/CoreMisc.h"
#include "Modules/ModuleManager.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

#if WITH_EDITOR
#include "Animation/DebugSkelMeshComponent.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "MeshUtilities.h"
#endif // WITH_EDITOR

#if USE_USD_SDK && WITH_EDITOR
#include "USDIncludesStart.h"
	#include "pxr/usd/sdf/types.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/variantSets.h"
	#include "pxr/usd/usdGeom/mesh.h"
	#include "pxr/usd/usdGeom/primvarsAPI.h"
	#include "pxr/usd/usdGeom/subset.h"
	#include "pxr/usd/usdGeom/tokens.h"
	#include "pxr/usd/usdShade/tokens.h"
	#include "pxr/usd/usdSkel/animation.h"
	#include "pxr/usd/usdSkel/animMapper.h"
	#include "pxr/usd/usdSkel/binding.h"
	#include "pxr/usd/usdSkel/bindingAPI.h"
	#include "pxr/usd/usdSkel/blendShape.h"
	#include "pxr/usd/usdSkel/cache.h"
	#include "pxr/usd/usdSkel/root.h"
	#include "pxr/usd/usdSkel/skeletonQuery.h"
	#include "pxr/usd/usdSkel/topology.h"
	#include "pxr/usd/usdSkel/utils.h"
#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "UsdSkeletalDataConversion"

namespace SkelDataConversionImpl
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

	// Adapted from LODUtilities.cpp
	struct FMeshDataBundle
	{
		TArray< FVector > Vertices;
		TArray< FVector> NormalsPerVertex;
		TArray< uint32 > Indices;
		TArray< FVector2D > UVs;
		TArray< uint32 > SmoothingGroups;
		TArray<SkeletalMeshImportData::FTriangle> Faces;
		TMap< uint32, TArray< uint32 > > VertexIndexToFaceIndices;
	};

	struct FMorphedMeshBundle
	{
		TArray< FVector > Vertices;
		TArray< FVector> NormalsPerIndex;
		TArray< uint32 > Indices;
		TArray< FVector2D > UVs;
		TArray< uint32 > SmoothingGroups;
		TArray< uint32 > MorphedIndexToSourceIndex;
	};

	/** Converts from wedge-based vertex format into a flat format we can give to MeshUtilities */
	void ConvertImportDataToMeshData( const FSkeletalMeshImportData& ImportData, FMeshDataBundle& MeshDataBundle )
	{
		MeshDataBundle.VertexIndexToFaceIndices.Reserve(ImportData.Points.Num());

		for ( const SkeletalMeshImportData::FTriangle& Face : ImportData.Faces )
		{
			SkeletalMeshImportData::FTriangle FaceTriangle;
			FaceTriangle = Face;
			for ( int32 Index = 0; Index < 3; ++Index )
			{
				const SkeletalMeshImportData::FVertex& Wedge = ImportData.Wedges[ Face.WedgeIndex[ Index ] ];
				FaceTriangle.WedgeIndex[ Index ] = Wedge.VertexIndex;
				MeshDataBundle.Indices.Add(Wedge.VertexIndex);
				MeshDataBundle.UVs.Add( Wedge.UVs[ 0 ] );

				MeshDataBundle.VertexIndexToFaceIndices.FindOrAdd( Wedge.VertexIndex ).Add( MeshDataBundle.Faces.Num() );
			}
			MeshDataBundle.Faces.Add( FaceTriangle );
			MeshDataBundle.SmoothingGroups.Add( Face.SmoothingGroups );
		}

		MeshDataBundle.Vertices = ImportData.Points;
	}

	/**
	 * Creates a FMorphedMeshBundle by applying the InOutDeltas to InMeshDataBundle, also creating additional deltas.
	 * The point of this function is to prepare OutBundle for computing normals with MeshUtilities. We create new deltas because
	 * the skeletal mesh shares vertices between faces, so if a vertex is morphed, not only does its normal need to be recomputed, but also
	 * the normals of all vertices of triangles that the vertex is a part of.
	 */
	void MorphMeshData( const FMeshDataBundle& InMeshDataBundle, TArray<FMorphTargetDelta>& InOutDeltas, FMorphedMeshBundle& OutBundle)
	{
		OutBundle.Vertices.Reserve( InOutDeltas.Num());
		OutBundle.Indices.Reserve( InOutDeltas.Num());
		OutBundle.UVs.Reserve( InOutDeltas.Num());
		OutBundle.SmoothingGroups.Reserve( InOutDeltas.Num());
		OutBundle.MorphedIndexToSourceIndex.Reserve( InOutDeltas.Num());

		TSet< uint32 > AddedFaces;
		TArray< FMorphTargetDelta > NewDeltas;
		TMap< uint32, uint32 > SourceIndexToMorphedIndex;

		// Add the existing deltas to the vertices array first
		// Don't add indices yet as we can't guarantee these come in triangle order (they're straight from USD)
		for ( const FMorphTargetDelta& Delta : InOutDeltas )
		{
			uint32 SourceIndex = Delta.SourceIdx;
			uint32 MorphedIndex = OutBundle.Vertices.Add(InMeshDataBundle.Vertices[ SourceIndex ] + Delta.PositionDelta);

			OutBundle.MorphedIndexToSourceIndex.Add( SourceIndex );
			SourceIndexToMorphedIndex.Add( SourceIndex, MorphedIndex );
		}

		// Add all indices, creating any missing deltas/vertices
		for ( const FMorphTargetDelta& Delta : InOutDeltas )
		{
			if ( const TArray< uint32 >* FoundFaceIndices = InMeshDataBundle.VertexIndexToFaceIndices.Find( Delta.SourceIdx ) )
			{
				for ( uint32 FaceIndex : *FoundFaceIndices )
				{
					if ( AddedFaces.Contains( FaceIndex ) )
					{
						continue;
					}
					AddedFaces.Add( FaceIndex );

					const SkeletalMeshImportData::FTriangle& Face = InMeshDataBundle.Faces[ FaceIndex ];
					OutBundle.SmoothingGroups.Add( Face.SmoothingGroups );

					for ( uint32 Index = 0; Index < 3; ++Index )
					{
						uint32 SourceIndex = Face.WedgeIndex[ Index ];
						uint32 MorphedIndex = INDEX_NONE;

						if ( uint32* FoundMorphedIndex = SourceIndexToMorphedIndex.Find( SourceIndex ) )
						{
							MorphedIndex = *FoundMorphedIndex;
						}
						else
						{
							// Add a new vertex and delta if we don't have one for this vertex yet
							FMorphTargetDelta& NewDelta = NewDeltas.Emplace_GetRef();
							NewDelta.PositionDelta = FVector(0, 0, 0);
							NewDelta.TangentZDelta = FVector(0, 0, 0);
							NewDelta.SourceIdx = SourceIndex;

							MorphedIndex = OutBundle.Vertices.Add( InMeshDataBundle.Vertices[ SourceIndex ] );

							OutBundle.MorphedIndexToSourceIndex.Add( SourceIndex );
							SourceIndexToMorphedIndex.Add( SourceIndex, MorphedIndex );
						}

						OutBundle.Indices.Add( MorphedIndex );
						OutBundle.UVs.Add( InMeshDataBundle.UVs[ SourceIndex ] );
					}
				}
			}
		}

		InOutDeltas.Append(NewDeltas);
	}

	/**
	 * Updates the TangentZDelta for the vertices within BlendShape with the correct value, so that lighting is correct
	 * when the morph target is applied to the skeletal mesh.
	 * Note: This may add deltas to the blend shape: See MorphMeshData
	 */
	bool ComputeTangentDeltas( const FMeshDataBundle& MeshDataBundle, UsdUtils::FUsdBlendShape& BlendShape )
	{
		if ( BlendShape.bHasAuthoredTangents )
		{
			return false;
		}

		FMorphedMeshBundle MorphedBundle;
		MorphMeshData(MeshDataBundle, BlendShape.Vertices, MorphedBundle);

		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>( "MeshUtilities" );
		ETangentOptions::Type TangentOptions = ( ETangentOptions::Type ) ( ETangentOptions::BlendOverlappingNormals | ETangentOptions::UseMikkTSpace );
		MeshUtilities.CalculateNormals( MorphedBundle.Vertices, MorphedBundle.Indices, MorphedBundle.UVs, MorphedBundle.SmoothingGroups, TangentOptions, MorphedBundle.NormalsPerIndex );

		TMap< uint32, FMorphTargetDelta* > SourceIndexToMorphDelta;
		for ( FMorphTargetDelta& Delta : BlendShape.Vertices )
		{
			SourceIndexToMorphDelta.Add( Delta.SourceIdx, &Delta );
		}

		uint32 NumMorphedIndices = static_cast<uint32> ( MorphedBundle.Indices.Num() );
		for ( uint32 MorphedIndexIndex = 0; MorphedIndexIndex < NumMorphedIndices; ++MorphedIndexIndex )
		{
			const uint32 MorphedIndex = MorphedBundle.Indices[ MorphedIndexIndex ];
			const uint32 SourceIndex = MorphedBundle.MorphedIndexToSourceIndex[ MorphedIndex ];

			// Note that we store the source normals as one per vertex, but we don't need to do that conversion for the
			// morphed normals, as we're iterating directly over the indices anyway
			const FVector& SourceNormal = MeshDataBundle.NormalsPerVertex[ SourceIndex ];
			const FVector& MorphedNormal = MorphedBundle.NormalsPerIndex[ MorphedIndexIndex ];

			if ( FMorphTargetDelta** FoundDelta = SourceIndexToMorphDelta.Find( SourceIndex ) )
			{
				( *FoundDelta )->TangentZDelta = MorphedNormal - SourceNormal;

				// We will visit each delta multiple times because we're iterating indices and these are per-vertex,
				// so this prevents us from recalculating the delta many times
				SourceIndexToMorphDelta.Remove( SourceIndex );
			}
		}

		return true;
	}

	/** Converts the given offsets into UE4 space and fills in an FUsdBlendShape object with all the data that will become a morph target */
	bool CreateUsdBlendShape( const FString& Name, const pxr::VtArray< pxr::GfVec3f >& PointOffsets, const pxr::VtArray< pxr::GfVec3f >& NormalOffsets, const pxr::VtArray< int >& PointIndices, const FUsdStageInfo& StageInfo, const FTransform& AdditionalTransform, uint32 PointIndexOffset, int32 LODIndex, UsdUtils::FUsdBlendShape& OutBlendShape )
	{
		uint32 NumOffsets = PointOffsets.size();
		uint32 NumIndices = PointIndices.size();
		uint32 NumNormals = NormalOffsets.size();

		if ( NumNormals > 0 && NumOffsets != NumNormals )
		{
			UE_LOG( LogUsd, Warning, TEXT( "BlendShape '%s' has mismatching numbers of offsets (%d) and normalOffsets (%d) and will be ignored" ), *Name, NumOffsets, NumNormals );
			return false;
		}

		if ( NumIndices > 0 && NumOffsets != NumIndices )
		{
			UE_LOG( LogUsd, Warning, TEXT( "BlendShape '%s' has mismatching numbers of offsets (%d) and point indices (%d) and will be ignored" ), *Name, NumOffsets, NumIndices );
			return false;
		}

		if ( NumOffsets + NumNormals == 0 )
		{
			UE_LOG( LogUsd, Warning, TEXT( "BlendShape '%s' zero offsets and normalOffsets and will be ignored" ), *Name);
			return false;
		}

		if ( NumNormals > 0 )
		{
			OutBlendShape.bHasAuthoredTangents = true;
		}

		OutBlendShape.Name = Name;
		OutBlendShape.LODIndicesThatUseThis.Add( LODIndex );

		// Prepare the indices of the corresponding base points/normals for every local point/normal we have
		TArray<int32> BaseIndices;
		BaseIndices.Reserve( NumOffsets );
		if ( NumIndices == 0 )
		{
			// If we have no indices it means we have information for all of our local points/normals
			for ( uint32 BaseIndex = PointIndexOffset; BaseIndex < PointIndexOffset + NumOffsets; ++BaseIndex )
			{
				BaseIndices.Add( static_cast< int32 >( BaseIndex ) );
			}
		}
		else
		{
			// If we have indices it means our morph target only affects a subset of the base vertices
			for ( uint32 LocalIndex = 0; LocalIndex < NumOffsets; ++LocalIndex )
			{
				int32 BaseIndex = PointIndices[ LocalIndex ] + static_cast< int32 >( PointIndexOffset );

				BaseIndices.Add( BaseIndex );
			}
		}

		// This comes from geomBindTransform, which is a manually-input transform, and so can have non-uniform scales, shears, etc
		FTransform NormalTransform = FTransform(AdditionalTransform.ToInverseMatrixWithScale().GetTransposed());

		OutBlendShape.Vertices.SetNumUninitialized( NumOffsets );
		for ( uint32 OffsetIndex = 0; OffsetIndex < NumOffsets; ++OffsetIndex )
		{
			const FVector UE4Offset = UsdToUnreal::ConvertVector( StageInfo, PointOffsets[ OffsetIndex ] );
			const FVector UE4Normal = OutBlendShape.bHasAuthoredTangents
				? UsdToUnreal::ConvertVector( StageInfo, NormalOffsets[ OffsetIndex ] )
				: FVector( 0, 0, 0 );

			FMorphTargetDelta& ModifiedVertex = OutBlendShape.Vertices[ OffsetIndex ];

			// Intentionally ignore translation on PositionDelta as this is really a direction vector,
			// and geomBindTransform's translation is already applied to the mesh vertices
			ModifiedVertex.PositionDelta = AdditionalTransform.TransformVector(UE4Offset);
			ModifiedVertex.TangentZDelta = NormalTransform.TransformVector(UE4Normal);
			ModifiedVertex.SourceIdx = BaseIndices[ OffsetIndex ];
		}

		return true;
	}

	FString GetUniqueName( FString Prefix, TSet<FString>& UsedNames)
	{
		if ( !UsedNames.Contains( Prefix ) )
		{
			return Prefix;
		}

		int32 Suffix = 0;
		FString Result;
		do
		{
			Result = FString::Printf( TEXT( "%s_%d" ), *Prefix, Suffix++ );
		} while ( UsedNames.Contains( Result ) );

		return Result;
	}

	/**
	 * Updates MorphTargetDeltas, remapping/adding/removing deltas according to the index remapping in OrigIndexToBuiltIndices.
	 * This is required because the SkeletalMesh build process may create/optimize/destroy vertices, and the indices through
	 * which our deltas refer to these vertices come directly from USD. Example: If a vertex affected by the blend shape is split, we need
	 * to duplicate the delta to all the split versions.
	 */
	void UpdatesDeltasToMeshBuild(TArray<FMorphTargetDelta>& MorphTargetDeltas, const TMap<int32, TArray<int32>>& OrigIndexToBuiltIndices )
	{
		TSet<int32> DeltasToDelete;
		TArray<FMorphTargetDelta> NewDeltas;
		for ( int32 DeltaIndex = 0; DeltaIndex < MorphTargetDeltas.Num(); ++DeltaIndex )
		{
			FMorphTargetDelta& ModifiedVertex = MorphTargetDeltas[ DeltaIndex ];

			if ( const TArray< int32 >* BuiltIndices = OrigIndexToBuiltIndices.Find( ModifiedVertex.SourceIdx ) )
			{
				// Our index just got remapped somewhere else: Update it
				if ( BuiltIndices->Num() >= 1 )
				{
					ModifiedVertex.SourceIdx = static_cast< uint32 >( ( *BuiltIndices )[ 0 ] );
				}

				// The vertex we were pointing at got split into multiple vertices: Add a matching delta for each
				for ( int32 NewDeltaIndex = 1; NewDeltaIndex < BuiltIndices->Num(); ++NewDeltaIndex )
				{
					FMorphTargetDelta& NewDelta = NewDeltas.Add_GetRef( ModifiedVertex );
					NewDelta.SourceIdx = static_cast< uint32 >( ( *BuiltIndices )[ NewDeltaIndex ] );
				}
			}
			// The vertex we were pointing at got deleted: Remove the delta
			else
			{
				DeltasToDelete.Add( DeltaIndex );
			}
		}
		if ( DeltasToDelete.Num() > 0 )
		{
			for ( int32 DeltaIndex = MorphTargetDeltas.Num() - 1; DeltaIndex >= 0; --DeltaIndex )
			{
				if ( DeltasToDelete.Contains( DeltaIndex ) )
				{
					MorphTargetDeltas.RemoveAt( DeltaIndex, 1, false );
				}
			}
		}
		MorphTargetDeltas.Append( MoveTemp( NewDeltas ) );
	}

	/**
	 * Will find or create a AACF_DefaultCurve float curve with CurveName, and set its data to a copy of SourceData.
	 * Adapted from UnFbx::FFbxImporter::ImportCurveToAnimSequence
	 */
	void SetFloatCurveData( UAnimSequence* Sequence, FName CurveName, const FRichCurve& SourceData )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( SkelDataConversionImpl::SetFloatCurveData );

		if ( !Sequence )
		{
			return;
		}

		USkeleton* Skeleton = Sequence->GetSkeleton();
		if ( !Skeleton )
		{
			return;
		}

		// Ignore curves that don't contribute to the animation
		bool bHasNonZeroKey = false;
		for ( const FRichCurveKey& Key : SourceData.Keys )
		{
			if ( !FMath::IsNearlyEqual( Key.Value, 0.0f ) )
			{
				bHasNonZeroKey = true;
				break;
			}
		}
		if ( !bHasNonZeroKey )
		{
			return;
		}

		const FSmartNameMapping* NameMapping = Skeleton->GetSmartNameContainer( USkeleton::AnimCurveMappingName );
		if ( !NameMapping )
		{
			return;
		}

		FSmartName NewName;
		Skeleton->AddSmartNameAndModify( USkeleton::AnimCurveMappingName, CurveName, NewName );

		FFloatCurve* Curve = static_cast< FFloatCurve* >( Sequence->RawCurveData.GetCurveData( NewName.UID, ERawCurveTrackTypes::RCT_Float ) );
		if ( !Curve )
		{
			if ( Sequence->RawCurveData.AddCurveData( NewName, AACF_DefaultCurve ) )
			{
				Curve = static_cast< FFloatCurve* > ( Sequence->RawCurveData.GetCurveData( NewName.UID, ERawCurveTrackTypes::RCT_Float ) );
				Curve->Name = NewName;
			}
		}
		else
		{
			if ( !( Curve->FloatCurve == SourceData ) )
			{
				FUsdLogManager::LogMessage(
						EMessageSeverity::Warning,
						FText::Format( LOCTEXT( "OverwritingMorphTargetCurves", "Overwriting animation curve for morph target '{0}' with different data! If the Skeletal Mesh has multiple LODs, make sure each LOD mesh that wants to animate a certain blend shape does so with the same blend shape curve." ),
						FText::FromName( CurveName ) )
				);
			}

			Curve->FloatCurve.Reset();
			Curve->SetCurveTypeFlags( Curve->GetCurveTypeFlags() | AACF_DefaultCurve );
		}

		Sequence->RawCurveData.RefreshName( NameMapping );

		if ( Curve )
		{
			Curve->FloatCurve = SourceData;
			Curve->FloatCurve.RemoveRedundantKeys( KINDA_SMALL_NUMBER );
		}
		else
		{
			UE_LOG( LogUsd, Error, TEXT( "Failed to create float curve with name '%s' for UAnimSequence '%s'" ), *CurveName.ToString(), *Sequence->GetName() );
		}
	}

	/**
	 * If ChannelWeightCurve is the SkelAnim channel intended to affect a USD blend shape and its inbetweens,
	 * this function will remap it into multiple FRichCurve that can be apply to all the independent morph
	 * targets that were generated from the blend shape and its inbetweens, if any.
	 * Index 0 of the returned array always contains the remapped primary morph target weight, and the rest match the inbetween order
	 */
	TArray<FRichCurve> ResolveWeightsForBlendShapeCurve( const UsdUtils::FUsdBlendShape& PrimaryBlendShape, const FRichCurve& ChannelWeightCurve )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( SkelDataConversionImpl::ConvertSkinnedMesh );

		int32 NumInbetweens = PrimaryBlendShape.Inbetweens.Num();
		if ( NumInbetweens == 0 )
		{
			return { ChannelWeightCurve };
		}

		TArray<FRichCurve> Result;
		Result.SetNum( NumInbetweens + 1 ); // One for each inbetween and an additional one for the morph target generated from the primary blend shape

		TArray<float> ResolvedInbetweenWeightsSample;
		ResolvedInbetweenWeightsSample.SetNum( NumInbetweens );

		for ( const FRichCurveKey& SourceKey : ChannelWeightCurve.Keys )
		{
			const float SourceTime = SourceKey.Time;
			const float SourceValue = SourceKey.Value;

			float ResolvedPrimarySample;
			UsdUtils::ResolveWeightsForBlendShape( PrimaryBlendShape, SourceValue, ResolvedPrimarySample, ResolvedInbetweenWeightsSample );

			FRichCurve& PrimaryCurve = Result[ 0 ];
			FKeyHandle PrimaryHandle = PrimaryCurve.AddKey( SourceTime, ResolvedPrimarySample );
			PrimaryCurve.SetKeyInterpMode( PrimaryHandle, SourceKey.InterpMode );

			for ( int32 InbetweenIndex = 0; InbetweenIndex < NumInbetweens; ++InbetweenIndex )
			{
				FRichCurve& InbetweenCurve = Result[ InbetweenIndex + 1 ];
				FKeyHandle InbetweenHandle = InbetweenCurve.AddKey( SourceTime, ResolvedInbetweenWeightsSample[ InbetweenIndex ] );
				InbetweenCurve.SetKeyInterpMode( InbetweenHandle, SourceKey.InterpMode );
			}
		}

		return Result;
	}
}

namespace UsdToUnrealImpl
{
	int32 GetPrimValueIndex( const EUsdInterpolationMethod& InterpMethod, const int32 VertexIndex, const int32 VertexInstanceIndex, const int32 PolygonIndex )
	{
		switch ( InterpMethod )
		{
		case EUsdInterpolationMethod::Vertex:
			return VertexIndex;
			break;
		case EUsdInterpolationMethod::FaceVarying:
			return VertexInstanceIndex;
			break;
		case EUsdInterpolationMethod::Uniform:
			return PolygonIndex;
			break;
		case EUsdInterpolationMethod::Constant:
		default:
			return 0;
			break;
		}
	}

	void ComputeSourceNormals( SkelDataConversionImpl::FMeshDataBundle& UnmorphedShape )
	{
		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>(TEXT("MeshUtilities"));

		// Calculate base normals for the mesh so that we can compute tangent deltas if we need to
		ETangentOptions::Type TangentOptions = ( ETangentOptions::Type ) ( ETangentOptions::BlendOverlappingNormals | ETangentOptions::UseMikkTSpace );
		TArray<FVector> NormalsPerIndex;
		MeshUtilities.CalculateNormals( UnmorphedShape.Vertices, UnmorphedShape.Indices, UnmorphedShape.UVs, UnmorphedShape.SmoothingGroups, TangentOptions, NormalsPerIndex );

		// Convert our normals to one normal per vertex, making it faster to unpack the normals we compute in ComputeTangentDeltas
		// This is possible because we compute them with ETangentOptions::BlendOverlappingNormals, so they are identical for all instances of the vertex
		UnmorphedShape.NormalsPerVertex.SetNumZeroed( UnmorphedShape.Vertices.Num() );
		for ( int32 IndexIndex = 0; IndexIndex < UnmorphedShape.Indices.Num(); ++IndexIndex )
		{
			uint32 VertexIndex = UnmorphedShape.Indices[ IndexIndex ];
			UnmorphedShape.NormalsPerVertex[ VertexIndex ] = NormalsPerIndex[ IndexIndex ];
		}
	}

	void CreateMorphTargets( UsdUtils::FBlendShapeMap& BlendShapes, const TArray<FSkeletalMeshImportData>& LODIndexToSkeletalMeshImportData, USkeletalMesh* SkeletalMesh )
	{
		FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
		if ( LODIndexToSkeletalMeshImportData.Num() != ImportedResource->LODModels.Num() )
		{
			return;
		}

		int32 NumLODs = ImportedResource->LODModels.Num();

		// Temporarily pack the mesh data into a different format that MeshUtilities can use
		TArray<SkelDataConversionImpl::FMeshDataBundle> TempMeshBundlesPerLOD;
		TempMeshBundlesPerLOD.Reserve( NumLODs );

		TArray<TMap<int32, TArray<int32>>> OrigIndexToBuiltIndicesPerLOD;
		OrigIndexToBuiltIndicesPerLOD.Reserve( NumLODs );

		TArray<bool> AreNormalsComputedForLODIndex;
		AreNormalsComputedForLODIndex.SetNumZeroed( NumLODs );

		for ( int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex )
		{
			FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[LODIndex];

			// BuildSkeletalMesh may create/remove vertices, and reorder/optimize the index buffers. We can use MeshToImportVertexMap
			// to go from new vertex index to original vertex index. Here we invert this map, because our FMorphTargetDeltas all refer
			// to original vertex indices, so we'll need to map them to the post-build vertex indices
			TArray<int32>& BuildIndexToOrigIndex = LODModel.MeshToImportVertexMap;
			TMap<int32, TArray<int32>>& OrigIndexToBuiltIndices = OrigIndexToBuiltIndicesPerLOD.Emplace_GetRef();
			OrigIndexToBuiltIndices.Reserve( BuildIndexToOrigIndex.Num() );
			for ( int32 BuiltIndex = 0; BuiltIndex < BuildIndexToOrigIndex.Num(); ++BuiltIndex )
			{
				int32 OrigIndex = BuildIndexToOrigIndex[ BuiltIndex ];
				OrigIndexToBuiltIndices.FindOrAdd( OrigIndex ).Add( BuiltIndex );
			}

			SkelDataConversionImpl::FMeshDataBundle& LODMeshBundle = TempMeshBundlesPerLOD.Emplace_GetRef();
			SkelDataConversionImpl::ConvertImportDataToMeshData( LODIndexToSkeletalMeshImportData[LODIndex], LODMeshBundle );
		}

		bool bHasValidMorphTarget = false;
		for ( TPair<FString, UsdUtils::FUsdBlendShape> BlendShapeByPath : BlendShapes )
		{
			UsdUtils::FUsdBlendShape& BlendShape = BlendShapeByPath.Value;

			if ( !BlendShape.IsValid() )
			{
				continue;
			}
			bHasValidMorphTarget = true;

			UMorphTarget* MorphTarget = NewObject<UMorphTarget>( SkeletalMesh, *BlendShape.Name );

			for ( int32 LODIndex : BlendShape.LODIndicesThatUseThis )
			{
				SkelDataConversionImpl::FMeshDataBundle& UnmorphedShape = TempMeshBundlesPerLOD[ LODIndex ];

				// Recompute normals for the final morphed shape in case it doesn't have authored normals
				// This is required or else the morphed shape will reuse the unmorphed normals, and lighting may look incorrect for
				// aggressive morph targets.
				// Note that this should happen *before* we call UpdatesDeltasToMeshBuild, because our MeshDataBundle refers to import data,
				// and so should our BlendShape
				if ( !BlendShape.bHasAuthoredTangents )
				{
					if ( !AreNormalsComputedForLODIndex[ LODIndex ] )
					{
						ComputeSourceNormals( UnmorphedShape );
						AreNormalsComputedForLODIndex[ LODIndex ] = true;
					}

					SkelDataConversionImpl::ComputeTangentDeltas( UnmorphedShape, BlendShape );
				}

				TArray<FMorphTargetDelta> Vertices;
				if ( BlendShape.LODIndicesThatUseThis.Num() > 1 )
				{
					// Need to copy this here because different LODs may build differently, and so the deltas may need to be updated differently
					Vertices = BlendShape.Vertices;
				}
				else
				{
					Vertices = MoveTemp(BlendShape.Vertices);
				}

				SkelDataConversionImpl::UpdatesDeltasToMeshBuild( Vertices, OrigIndexToBuiltIndicesPerLOD[ LODIndex ] );

				const bool bCompareNormal = true;
				FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[LODIndex];
				MorphTarget->PopulateDeltas( Vertices, LODIndex, LODModel.Sections, bCompareNormal );
			}

			// Don't need this data anymore as it has been moved into UMorphTarget
			BlendShape.Vertices.Empty();

			MorphTarget->BaseSkelMesh = SkeletalMesh;
			SkeletalMesh->GetMorphTargets().Add( MorphTarget );
		}

		if ( bHasValidMorphTarget )
		{
			SkeletalMesh->MarkPackageDirty();
			SkeletalMesh->InitMorphTargetsAndRebuildRenderData();
		}
	}
}

namespace UnrealToUsdImpl
{
	void ConvertSkeletalMeshLOD( const USkeletalMesh* SkeletalMesh, const FSkeletalMeshLODModel& LODModel, pxr::UsdGeomMesh& UsdLODPrimGeomMesh, bool bHasVertexColors, pxr::VtArray< std::string >& MaterialAssignments, const TArray<int32>& LODMaterialMap, const pxr::UsdTimeCode TimeCode, pxr::UsdPrim MaterialPrim )
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::UsdPrim MeshPrim = UsdLODPrimGeomMesh.GetPrim();
		pxr::UsdStageRefPtr Stage = MeshPrim.GetStage();

		// In 21.05 we now must apply the skel binding API to this mesh prim, or else the joints/etc. attributes may be ignored
		if ( !pxr::UsdSkelBindingAPI::Apply( MeshPrim ) )
		{
			return;
		}

		if ( !Stage )
		{
			return;
		}

		const FUsdStageInfo StageInfo( Stage );

		// Vertices
		{
			const int32 VertexCount = LODModel.NumVertices;
			if ( VertexCount == 0 )
			{
				return;
			}

			TArray<FSoftSkinVertex> Vertices;
			LODModel.GetVertices( Vertices );

			// Points
			{
				pxr::UsdAttribute Points = UsdLODPrimGeomMesh.CreatePointsAttr();
				if ( Points )
				{
					pxr::VtArray< pxr::GfVec3f > PointsArray;
					PointsArray.reserve( VertexCount );

					for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
					{
						PointsArray.push_back( UnrealToUsd::ConvertVector( StageInfo, Vertices[ VertexIndex ].Position ) );
					}

					Points.Set( PointsArray, TimeCode );
				}
			}

			// Normals
			{
				pxr::UsdAttribute NormalsAttribute = UsdLODPrimGeomMesh.CreateNormalsAttr();
				if ( NormalsAttribute )
				{
					pxr::VtArray< pxr::GfVec3f > Normals;
					Normals.reserve( VertexCount );

					for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
					{
						Normals.push_back( UnrealToUsd::ConvertVector( StageInfo, Vertices[ VertexIndex ].TangentZ ) );
					}

					NormalsAttribute.Set( Normals, TimeCode );
				}
			}

			// UVs
			{
				for ( uint32 TexCoordSourceIndex = 0; TexCoordSourceIndex < LODModel.NumTexCoords; ++TexCoordSourceIndex )
				{
					pxr::TfToken UsdUVSetName = UsdUtils::GetUVSetName( TexCoordSourceIndex ).Get();

					pxr::UsdGeomPrimvar PrimvarST = UsdLODPrimGeomMesh.CreatePrimvar( UsdUVSetName, pxr::SdfValueTypeNames->TexCoord2fArray, pxr::UsdGeomTokens->vertex );

					if ( PrimvarST )
					{
						pxr::VtVec2fArray UVs;

						for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
						{
							FVector2D TexCoord = Vertices[ VertexIndex ].UVs[ TexCoordSourceIndex ];
							TexCoord[ 1 ] = 1.f - TexCoord[ 1 ];

							UVs.push_back( UnrealToUsd::ConvertVector( TexCoord ) );
						}

						PrimvarST.Set( UVs, TimeCode );
					}
				}
			}

			// Vertex colors
			if ( bHasVertexColors )
			{
				pxr::UsdGeomPrimvar DisplayColorPrimvar = UsdLODPrimGeomMesh.CreateDisplayColorPrimvar( pxr::UsdGeomTokens->vertex );
				pxr::UsdGeomPrimvar DisplayOpacityPrimvar = UsdLODPrimGeomMesh.CreateDisplayOpacityPrimvar( pxr::UsdGeomTokens->vertex );

				if ( DisplayColorPrimvar && DisplayOpacityPrimvar )
				{
					pxr::VtArray< pxr::GfVec3f > DisplayColors;
					DisplayColors.reserve( VertexCount );

					pxr::VtArray< float > DisplayOpacities;
					DisplayOpacities.reserve( VertexCount );

					for ( int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex )
					{
						const FColor& VertexColor = Vertices[ VertexIndex ].Color;

						pxr::GfVec4f Color = UnrealToUsd::ConvertColor( VertexColor );
						DisplayColors.push_back( pxr::GfVec3f( Color[ 0 ], Color[ 1 ], Color[ 2 ] ) );
						DisplayOpacities.push_back( Color[ 3 ] );
					}

					DisplayColorPrimvar.Set( DisplayColors, TimeCode );
					DisplayOpacityPrimvar.Set( DisplayOpacities, TimeCode );
				}
			}

			// Joint indices & weights
			{
				pxr::UsdSkelBindingAPI SkelBindingAPI{ UsdLODPrimGeomMesh };
				const int32 NumInfluencesPerVertex = LODModel.GetMaxBoneInfluences();

				const bool bConstantPrimvar = false;
				pxr::UsdGeomPrimvar JointIndicesPrimvar = SkelBindingAPI.CreateJointIndicesPrimvar( bConstantPrimvar, NumInfluencesPerVertex );
				pxr::UsdGeomPrimvar JointWeightsPrimvar = SkelBindingAPI.CreateJointWeightsPrimvar( bConstantPrimvar, NumInfluencesPerVertex );

				if ( JointIndicesPrimvar && JointWeightsPrimvar )
				{
					pxr::VtArray< int > JointIndices;
					JointIndices.reserve( VertexCount * NumInfluencesPerVertex );

					pxr::VtArray< float > JointWeights;
					JointWeights.reserve( VertexCount * NumInfluencesPerVertex );

					const int32 SectionCount = LODModel.Sections.Num();
					for ( const FSkelMeshSection& Section : LODModel.Sections )
					{
						for ( const FSoftSkinVertex& Vertex : Section.SoftVertices )
						{
							for ( int32 InfluenceIndex = 0; InfluenceIndex < NumInfluencesPerVertex; ++InfluenceIndex )
							{
								int32 BoneIndex = Section.BoneMap[ Vertex.InfluenceBones[ InfluenceIndex ] ];

								JointIndices.push_back( BoneIndex );
								JointWeights.push_back( Vertex.InfluenceWeights[ InfluenceIndex ] / 255.0f );
							}
						}
					}

					JointIndicesPrimvar.Set( JointIndices, TimeCode );
					JointWeightsPrimvar.Set( JointWeights, TimeCode );
				}
			}
		}

		// Faces
		{
			int32 TotalNumTriangles = 0;

			// Face Vertex Counts
			{
				for ( const FSkelMeshSection& Section : LODModel.Sections )
				{
					TotalNumTriangles += Section.NumTriangles;
				}

				pxr::UsdAttribute FaceCountsAttribute = UsdLODPrimGeomMesh.CreateFaceVertexCountsAttr();
				if ( FaceCountsAttribute )
				{
					pxr::VtArray< int > FaceVertexCounts;
					FaceVertexCounts.reserve( TotalNumTriangles );

					for ( int32 FaceIndex = 0; FaceIndex < TotalNumTriangles; ++FaceIndex )
					{
						FaceVertexCounts.push_back( 3 );
					}

					FaceCountsAttribute.Set( FaceVertexCounts, TimeCode );
				}
			}

			// Face Vertex Indices
			{
				pxr::UsdAttribute FaceVertexIndicesAttribute = UsdLODPrimGeomMesh.GetFaceVertexIndicesAttr();

				if ( FaceVertexIndicesAttribute )
				{
					pxr::VtArray< int > FaceVertexIndices;
					FaceVertexIndices.reserve( TotalNumTriangles * 3 );

					for ( const FSkelMeshSection& Section : LODModel.Sections )
					{
						int32 TriangleCount = Section.NumTriangles;
						for ( uint32 TriangleIndex = 0; TriangleIndex < Section.NumTriangles; ++TriangleIndex )
						{
							for ( uint32 PointIndex = 0; PointIndex < 3; PointIndex++ )
							{
								int32 VertexPositionIndex = LODModel.IndexBuffer[ Section.BaseIndex + ( ( TriangleIndex * 3 ) + PointIndex ) ];
								check( VertexPositionIndex >= 0 );

								FaceVertexIndices.push_back(VertexPositionIndex);
							}
						}
					}

					FaceVertexIndicesAttribute.Set( FaceVertexIndices, TimeCode );
				}
			}
		}

		// Material assignments
		{
			bool bHasUEMaterialAssignements = false;

			pxr::VtArray< std::string > UnrealMaterialsForLOD;
			for ( const FSkelMeshSection& Section : LODModel.Sections )
			{
				int32 SkeletalMaterialIndex = LODMaterialMap.IsValidIndex( Section.MaterialIndex ) ? LODMaterialMap[ Section.MaterialIndex ] : Section.MaterialIndex;
				if ( SkeletalMaterialIndex >= 0 && SkeletalMaterialIndex < MaterialAssignments.size() )
				{
					UnrealMaterialsForLOD.push_back( MaterialAssignments[ SkeletalMaterialIndex ] );
					bHasUEMaterialAssignements = true;
				}
				else
				{
					// Keep unrealMaterials with the same number of elements as our MaterialIndices expect
					UnrealMaterialsForLOD.push_back( "" );
				}
			}

			// This LOD has a single material assignment, just add an unrealMaterials attribute to the mesh prim
			if ( bHasUEMaterialAssignements && UnrealMaterialsForLOD.size() == 1 )
			{
				if ( pxr::UsdAttribute UEMaterialsAttribute = MaterialPrim.CreateAttribute( UnrealIdentifiers::MaterialAssignment, pxr::SdfValueTypeNames->String ) )
				{
					UEMaterialsAttribute.Set( UnrealMaterialsForLOD[0] );
				}
			}
			// Multiple material assignments to the same LOD (and so the same mesh prim). Need to create a GeomSubset for each UE mesh section
			else if ( UnrealMaterialsForLOD.size() > 1 )
			{
				// Need to fetch all triangles of a section, and add their indices to the GeomSubset
				for ( int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex )
				{
					const FSkelMeshSection& Section = LODModel.Sections[ SectionIndex ];

					// Note that we will continue on even if we have no material assignment, so as to satisfy the "partition" family condition
					std::string SectionMaterial = UnrealMaterialsForLOD[ SectionIndex ];

					pxr::UsdPrim GeomSubsetPrim = Stage->DefinePrim(
						UsdLODPrimGeomMesh.GetPath().AppendPath( pxr::SdfPath( "Section" + std::to_string( SectionIndex ) ) ),
						UnrealToUsd::ConvertToken( TEXT( "GeomSubset" ) ).Get()
					);

					pxr::UsdPrim MaterialGeomSubsetPrim = GeomSubsetPrim;
					if ( MaterialPrim.GetStage() != MeshPrim.GetStage() )
					{
						MaterialGeomSubsetPrim = MaterialPrim.GetStage()->OverridePrim(
							MaterialPrim.GetPath().AppendPath( pxr::SdfPath( "Section" + std::to_string( SectionIndex ) ) )
						);
					}

					pxr::UsdGeomSubset GeomSubsetSchema{ GeomSubsetPrim };

					// Element type attribute
					pxr::UsdAttribute ElementTypeAttr = GeomSubsetSchema.CreateElementTypeAttr();
					ElementTypeAttr.Set( pxr::UsdGeomTokens->face, TimeCode );

					// Indices attribute
					const uint32 TriangleCount = Section.NumTriangles;
					const uint32 FirstTriangleIndex = Section.BaseIndex / 3; // BaseIndex is the first *vertex* instance index
					pxr::VtArray<int> IndicesAttrValue;
					for ( uint32 TriangleIndex = FirstTriangleIndex; TriangleIndex - FirstTriangleIndex < TriangleCount; ++TriangleIndex )
					{
						// Note that we add VertexInstances in sequence to the usda file for the faceVertexInstances attribute, which
						// also constitutes our triangle order
						IndicesAttrValue.push_back( static_cast< int >( TriangleIndex ));
					}

					pxr::UsdAttribute IndicesAttr = GeomSubsetSchema.CreateIndicesAttr();
					IndicesAttr.Set( IndicesAttrValue, TimeCode );

					// Family name attribute
					pxr::UsdAttribute FamilyNameAttr = GeomSubsetSchema.CreateFamilyNameAttr();
					FamilyNameAttr.Set( pxr::UsdShadeTokens->materialBind, TimeCode );

					// Family type
					pxr::UsdGeomSubset::SetFamilyType( UsdLODPrimGeomMesh, pxr::UsdShadeTokens->materialBind, pxr::UsdGeomTokens->partition );

					// unrealMaterials attribute
					if ( pxr::UsdAttribute UEMaterialsAttribute = MaterialGeomSubsetPrim.CreateAttribute( UnrealIdentifiers::MaterialAssignment, pxr::SdfValueTypeNames->String ) )
					{
						UEMaterialsAttribute.Set( UnrealMaterialsForLOD[ SectionIndex ] );
					}
				}
			}
		}
	}

	// Converts UE morph target deltas from DeltaArray into offsets, pointIndices and normalOffsets attributes of BlendShape
	bool ConvertMorphTargetDeltas( FMorphTargetDelta* DeltaArray, int32 NumDeltas, pxr::UsdSkelBlendShape& BlendShape, pxr::UsdTimeCode TimeCode )
	{
		if ( !DeltaArray || NumDeltas == 0 || !BlendShape )
		{
			return false;
		}

		FUsdStageInfo StageInfo{ BlendShape.GetPrim().GetStage() };
		FScopedUsdAllocs Allocs;

		pxr::VtArray< pxr::GfVec3f > Offsets;
		pxr::VtArray< int > PointIndices;
		pxr::VtArray< pxr::GfVec3f > Normals;

		Offsets.reserve(NumDeltas);
		PointIndices.reserve(NumDeltas);
		Normals.reserve(NumDeltas);

		for ( int32 DeltaIndex = 0; DeltaIndex < NumDeltas; ++DeltaIndex )
		{
			const FMorphTargetDelta& Delta = DeltaArray[ DeltaIndex ];

			Offsets.push_back( UnrealToUsd::ConvertVector( StageInfo, Delta.PositionDelta ) );
			PointIndices.push_back( Delta.SourceIdx );
			Normals.push_back( UnrealToUsd::ConvertVector( StageInfo, Delta.TangentZDelta ) );
		}

		BlendShape.CreateOffsetsAttr().Set(Offsets, TimeCode);
		BlendShape.CreatePointIndicesAttr().Set(PointIndices, TimeCode);
		BlendShape.CreateNormalOffsetsAttr().Set(Normals, TimeCode);

		return true;
	}

	// BoneNamesInOrder represents a hierarchy of bones. OutFullPaths will be the full path to each bone, in the same order
	// e.g. 'Root/Arm/Foot'
	void CreateFullBonePaths(const TArray<FMeshBoneInfo>& BoneNamesInOrder, TArray<FString>& OutFullPaths )
	{
		int32 NumBones = BoneNamesInOrder.Num();
		if ( NumBones < 1 )
		{
			return;
		}

		OutFullPaths.SetNum( NumBones );

		// The first bone is the root, and has ParentIndex == -1, so do it separately here to void checking the indices for all bones
		// Sanitize because ExportName can have spaces, which USD doesn't like
		OutFullPaths[0] = SkelDataConversionImpl::SanitizeObjectName(BoneNamesInOrder[0].ExportName);

		// Bones are always stored in an increasing order, so we can do all paths in a single pass
		for ( int32 BoneIndex = 1; BoneIndex < NumBones; ++BoneIndex )
		{
			const FMeshBoneInfo& BoneInfo = BoneNamesInOrder[ BoneIndex ];
			FString SanitizedBoneName = SkelDataConversionImpl::SanitizeObjectName(BoneInfo.ExportName);

			OutFullPaths[BoneIndex] = FString::Printf(TEXT("%s/%s"), *OutFullPaths[ BoneInfo.ParentIndex ], *SanitizedBoneName );
		}
	}

	// Sets the JointsAttr value based on the bone paths of ReferenceSkeleton
	void SetJoinsAttr( const FReferenceSkeleton& ReferenceSkeleton, pxr::UsdAttribute JointsAttr )
	{
		TArray<FString> FullBonePaths;
		UnrealToUsdImpl::CreateFullBonePaths( ReferenceSkeleton.GetRefBoneInfo(), FullBonePaths );

		pxr::VtArray<pxr::TfToken> Joints;
		Joints.reserve( FullBonePaths.Num() );
		for ( const FString& BonePath : FullBonePaths )
		{
			Joints.push_back( UnrealToUsd::ConvertToken( *BonePath ).Get() );
		}
		JointsAttr.Set( Joints );
	}
}

bool UsdToUnreal::ConvertSkeleton(const pxr::UsdSkelSkeletonQuery& SkeletonQuery, FSkeletalMeshImportData& SkelMeshImportData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::ConvertSkeleton );

	using namespace pxr;

	TArray<FString> JointNames;
	TArray<int32> ParentJointIndices;

	// Retrieve the joint names and parent indices from the skeleton topology
	// GetJointOrder already orders them from parent-to-child
	VtArray<TfToken> JointOrder = SkeletonQuery.GetJointOrder();
	const UsdSkelTopology& SkelTopology = SkeletonQuery.GetTopology();
	for (uint32 Index = 0; Index < SkelTopology.GetNumJoints(); ++Index)
	{
		SdfPath JointPath(JointOrder[Index]);

		FString JointName = UsdToUnreal::ConvertString(JointPath.GetName());
		JointNames.Add(JointName);

		int ParentIndex = SkelTopology.GetParent(Index);
		ParentJointIndices.Add(ParentIndex);
	}

	// Skeleton has no joints: Generate a dummy single "Root" bone skeleton
	if ( JointNames.Num() == 0 )
	{
		FString SkeletonPrimPath = UsdToUnreal::ConvertPath( SkeletonQuery.GetPrim().GetPath() );

		FUsdLogManager::LogMessage(
			EMessageSeverity::Warning,
			FText::Format( LOCTEXT( "NoBonesInSkeleton", "Skeleton prim '{0}' has no joints! "
				"A new skeleton with a single 'Root' bone will be generated as USkeletalMeshes require valid skeletons. "
				"Note that this new skeleton may be written back to the USD stage when exporting the corresponding asset." ),
				FText::FromString( SkeletonPrimPath )
			)
		);

		SkelMeshImportData.RefBonesBinary.AddZeroed( 1 );

		SkeletalMeshImportData::FBone& Bone = SkelMeshImportData.RefBonesBinary.Last();
		Bone.Name = TEXT("Root");
		Bone.ParentIndex = INDEX_NONE;
		Bone.NumChildren = 0;
		Bone.BonePos.Transform = FTransform::Identity;
		Bone.BonePos.Length = 1.0f;
		Bone.BonePos.XSize = 100.0f;
		Bone.BonePos.YSize = 100.0f;
		Bone.BonePos.ZSize = 100.0f;
		return true;
	}

	if ( JointNames.Num() > MAX_BONES )
	{
		return false;
	}

	// Retrieve the bone transforms to be used as the reference pose
	VtArray<GfMatrix4d> UsdBoneTransforms;
	TArray<FTransform> BoneTransforms;

	bool bJointTransformsComputed = SkeletonQuery.ComputeJointLocalTransforms(&UsdBoneTransforms, UsdTimeCode::Default());
	if (bJointTransformsComputed)
	{
		UsdStageWeakPtr Stage = SkeletonQuery.GetSkeleton().GetPrim().GetStage();
		const FUsdStageInfo StageInfo(Stage);

		for (uint32 Index = 0; Index < UsdBoneTransforms.size(); ++Index)
		{
			const GfMatrix4d& UsdMatrix = UsdBoneTransforms[Index];
			FTransform BoneTransform = UsdToUnreal::ConvertMatrix( StageInfo, UsdMatrix );
			BoneTransforms.Add(BoneTransform);
		}
	}

	if (JointNames.Num() != BoneTransforms.Num())
	{
		return false;
	}

	// Store the retrieved data as bones into the SkeletalMeshImportData
	SkelMeshImportData.RefBonesBinary.AddZeroed( JointNames.Num() );
	for (int32 Index = 0; Index < JointNames.Num(); ++Index)
	{
		SkeletalMeshImportData::FBone& Bone = SkelMeshImportData.RefBonesBinary[ Index ];

		Bone.Name = JointNames[Index];
		Bone.ParentIndex = ParentJointIndices[Index];
		// Increment the number of children each time a bone is referenced as a parent bone; the root has a parent index of -1
		if (Bone.ParentIndex >= 0)
		{
			// The joints are ordered from parent-to-child so the parent will already have been added to the array
			SkeletalMeshImportData::FBone& ParentBone = SkelMeshImportData.RefBonesBinary[Bone.ParentIndex];
			++ParentBone.NumChildren;
		}

		SkeletalMeshImportData::FJointPos& JointMatrix = Bone.BonePos;
		JointMatrix.Transform = BoneTransforms[Index];

		// Not sure if Length and X/Y/Z Size need to be set, there are no equivalents in USD
		JointMatrix.Length = 1.f;
		JointMatrix.XSize = 100.f;
		JointMatrix.YSize = 100.f;
		JointMatrix.ZSize = 100.f;
	}

	return true;
}

bool UsdToUnreal::ConvertSkinnedMesh(const pxr::UsdSkelSkinningQuery& SkinningQuery, const FTransform& AdditionalTransform, FSkeletalMeshImportData& SkelMeshImportData, TArray< UsdUtils::FUsdPrimMaterialSlot >& MaterialAssignments, const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarsUVSetNames )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::ConvertSkinnedMesh );

	using namespace pxr;

	const UsdPrim& SkinningPrim = SkinningQuery.GetPrim();
	UsdSkelBindingAPI SkelBinding(SkinningPrim);

	// Ref. FFbxImporter::FillSkelMeshImporterFromFbx
	UsdGeomMesh UsdMesh = UsdGeomMesh(SkinningPrim);
	if ( !UsdMesh )
	{
		return false;
	}

	const FUsdStageInfo StageInfo( SkinningPrim.GetStage() );

	// Retrieve the mesh points (vertices) from USD and append it to the SkeletalMeshImportData Points
	uint32 NumPoints = 0;
	uint32 NumExistingPoints = SkelMeshImportData.Points.Num();

	UsdAttribute PointsAttr = UsdMesh.GetPointsAttr();
	if (PointsAttr)
	{
		VtArray<GfVec3f> UsdPoints;
		PointsAttr.Get(&UsdPoints, UsdTimeCode::Default());

		NumPoints = UsdPoints.size();
		SkelMeshImportData.Points.AddUninitialized(NumPoints);

		for (uint32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			const GfVec3f& Point = UsdPoints[PointIndex];

			// Convert the USD vertex to Unreal and apply the GeomBindTransform to it
			FVector Pos = UsdToUnreal::ConvertVector(StageInfo, Point);
			Pos = AdditionalTransform.TransformPosition(Pos);

			SkelMeshImportData.Points[PointIndex + NumExistingPoints] = Pos;
		}
	}

	if (NumPoints == 0)
	{
		return false;
	}

	// Convert the face data into SkeletalMeshImportData

	// Face counts
	VtArray<int> FaceCounts;
	UsdAttribute FaceCountsAttribute = UsdMesh.GetFaceVertexCountsAttr();
	if (FaceCountsAttribute)
	{
		FaceCountsAttribute.Get(&FaceCounts, UsdTimeCode::Default());
	}

	// Face indices
	VtArray<int> OriginalFaceIndices;
	UsdAttribute FaceIndicesAttribute = UsdMesh.GetFaceVertexIndicesAttr();
	if (FaceIndicesAttribute)
	{
		FaceIndicesAttribute.Get(&OriginalFaceIndices, UsdTimeCode::Default());
	}

	uint32 NumVertexInstances = static_cast<uint32>(OriginalFaceIndices.size());

	// Normals
	VtArray<GfVec3f> Normals;
	UsdAttribute NormalsAttribute = UsdMesh.GetNormalsAttr();
	if (NormalsAttribute)
	{
		NormalsAttribute.Get(&Normals, UsdTimeCode::Default());
	}

	uint32 NumExistingFaces = SkelMeshImportData.Faces.Num();
	uint32 NumExistingWedges = SkelMeshImportData.Wedges.Num();

	uint32 NumFaces = FaceCounts.size();
	SkelMeshImportData.Faces.Reserve( NumFaces * 2 );

	// Material assignments
	UsdUtils::FUsdPrimMaterialAssignmentInfo LocalInfo = UsdUtils::GetPrimMaterialAssignments( SkinningPrim );
	TArray< UsdUtils::FUsdPrimMaterialSlot >& LocalMaterialSlots = LocalInfo.Slots;
	TArray< int32 >& FaceMaterialIndices = LocalInfo.MaterialIndices;

	// We want to combine identical slots for skeletal meshes, which is different to static meshes, where each section gets a slot
	// Note: This is a different index remapping to the one that happens for LODs, using LODMaterialMap! Here we're combining meshes of the same LOD
	TMap<UsdUtils::FUsdPrimMaterialSlot, int32> SlotToCombinedMaterialIndex;
	TMap<int32, int32> LocalToCombinedMaterialIndex;
	for (int32 Index = 0; Index < MaterialAssignments.Num(); ++Index)
	{
		SlotToCombinedMaterialIndex.Add( MaterialAssignments[ Index ], Index );
	}
	for (int32 LocalIndex = 0; LocalIndex < LocalInfo.Slots.Num(); ++LocalIndex)
	{
		UsdUtils::FUsdPrimMaterialSlot& LocalSlot = LocalInfo.Slots[LocalIndex];

		int32 CombinedMaterialIndex = INDEX_NONE;
		if ( int32* FoundCombinedMaterialIndex = SlotToCombinedMaterialIndex.Find( LocalSlot ) )
		{
			CombinedMaterialIndex = *FoundCombinedMaterialIndex;
		}
		else
		{
			CombinedMaterialIndex = MaterialAssignments.Add( LocalSlot );
			SlotToCombinedMaterialIndex.Add( LocalSlot, CombinedMaterialIndex );
		}

		LocalToCombinedMaterialIndex.Add( LocalIndex, CombinedMaterialIndex );
	}

	// Retrieve vertex colors
	UsdGeomPrimvar ColorPrimvar = UsdMesh.GetDisplayColorPrimvar();
	TArray<FColor> Colors;
	EUsdInterpolationMethod DisplayColorInterp = EUsdInterpolationMethod::Constant;
	if (ColorPrimvar)
	{
		pxr::VtArray<pxr::GfVec3f> UsdColors;
		if ( ColorPrimvar.ComputeFlattened( &UsdColors ) )
		{
			uint32 NumExpectedColors = 0;
			uint32 NumColors = UsdColors.size();
			pxr::TfToken USDInterpType = ColorPrimvar.GetInterpolation();

			if ( USDInterpType == pxr::UsdGeomTokens->uniform )
			{
				NumExpectedColors = NumFaces;
				DisplayColorInterp = EUsdInterpolationMethod::Uniform;
			}
			else if ( USDInterpType == pxr::UsdGeomTokens->vertex || USDInterpType == pxr::UsdGeomTokens->varying )
			{
				NumExpectedColors = NumPoints;
				DisplayColorInterp = EUsdInterpolationMethod::Vertex;
			}
			else if ( USDInterpType == pxr::UsdGeomTokens->faceVarying )
			{
				NumExpectedColors = NumVertexInstances;
				DisplayColorInterp = EUsdInterpolationMethod::FaceVarying;
			}
			else if ( USDInterpType == pxr::UsdGeomTokens->constant )
			{
				NumExpectedColors = 1;
				DisplayColorInterp = EUsdInterpolationMethod::Constant;
			}

			if ( NumExpectedColors == NumColors )
			{
				Colors.Reserve( NumColors );
				for ( uint32 Index = 0; Index < NumColors; ++Index )
				{
					const bool bSRGB = true;
					Colors.Add( UsdToUnreal::ConvertColor( UsdColors[ Index ] ).ToFColor( bSRGB ) );
				}

				SkelMeshImportData.bHasVertexColors = true;
			}
			else
			{
				UE_LOG( LogUsd, Warning, TEXT( "Prim '%s' has invalid number of displayColor values for primvar interpolation type '%s'! (expected %d, found %d)" ),
					*UsdToUnreal::ConvertPath( SkinningPrim.GetPath() ),
					*UsdToUnreal::ConvertToken( USDInterpType ),
					NumExpectedColors,
					NumColors );
			}
		}
	}

	// Retrieve vertex opacity
	UsdGeomPrimvar OpacityPrimvar = UsdMesh.GetDisplayOpacityPrimvar();
	TArray<float> Opacities;
	EUsdInterpolationMethod DisplayOpacityInterp = EUsdInterpolationMethod::Constant;
	if ( OpacityPrimvar )
	{
		pxr::VtArray< float > UsdOpacities;
		if ( OpacityPrimvar.ComputeFlattened( &UsdOpacities ) )
		{
			uint32 NumExpectedOpacities = 0;
			const uint32 NumOpacities = UsdOpacities.size();
			pxr::TfToken USDInterpType = OpacityPrimvar.GetInterpolation();

			if ( USDInterpType == pxr::UsdGeomTokens->uniform )
			{
				NumExpectedOpacities = NumFaces;
				DisplayOpacityInterp = EUsdInterpolationMethod::Uniform;
			}
			else if ( USDInterpType == pxr::UsdGeomTokens->vertex || USDInterpType == pxr::UsdGeomTokens->varying )
			{
				NumExpectedOpacities = NumPoints;
				DisplayOpacityInterp = EUsdInterpolationMethod::Vertex;
			}
			else if ( USDInterpType == pxr::UsdGeomTokens->faceVarying )
			{
				NumExpectedOpacities = NumVertexInstances;
				DisplayOpacityInterp = EUsdInterpolationMethod::FaceVarying;
			}
			else if ( USDInterpType == pxr::UsdGeomTokens->constant )
			{
				NumExpectedOpacities = 1;
				DisplayOpacityInterp = EUsdInterpolationMethod::Constant;
			}

			if ( NumExpectedOpacities == NumOpacities )
			{
				Opacities.Reserve( NumOpacities );
				for ( uint32 Index = 0; Index < NumOpacities; ++Index )
				{
					Opacities.Add( UsdOpacities[ Index ] );
				}

				SkelMeshImportData.bHasVertexColors = true; // We'll need to store these in the vertex colors
			}
			else
			{
				UE_LOG( LogUsd, Warning, TEXT( "Prim '%s' has invalid number of displayOpacity values for primvar interpolation type '%s'! (expected %d, found %d)" ),
					*UsdToUnreal::ConvertPath( SkinningPrim.GetPath() ),
					*UsdToUnreal::ConvertToken( USDInterpType ),
					NumExpectedOpacities,
					NumOpacities );
			}
		}
	}

	// Make sure these have at least one valid entry, as we'll default to Constant and we may have either valid opacities or colors
	if ( Colors.Num() < 1 )
	{
		Colors.Add( FColor::White );
	}
	if ( Opacities.Num() < 1 )
	{
		Opacities.Add( 1.0f );
	}

	SkelMeshImportData.NumTexCoords = 0;

	bool bReverseOrder = IUsdPrim::GetGeometryOrientation(UsdMesh) == EUsdGeomOrientation::LeftHanded;

	struct FUVSet
	{
		TOptional< VtIntArray > UVIndices; // UVs might be indexed or they might be flat (one per vertex)
		VtVec2fArray UVs;

		EUsdInterpolationMethod InterpolationMethod = EUsdInterpolationMethod::FaceVarying;
	};

	TArray< FUVSet > UVSets;

	TArray< TUsdStore< UsdGeomPrimvar > > PrimvarsByUVIndex = UsdUtils::GetUVSetPrimvars( UsdMesh, MaterialToPrimvarsUVSetNames );

	int32 UVChannelIndex = 0;
	while ( true )
	{
		if ( !PrimvarsByUVIndex.IsValidIndex( UVChannelIndex ) )
		{
			break;
		}

		UsdGeomPrimvar& PrimvarST = PrimvarsByUVIndex[ UVChannelIndex ].Get();
		if ( !PrimvarST )
		{
			break;
		}

		if ( PrimvarST )
		{
			FUVSet UVSet;

			if ( PrimvarST.GetInterpolation() == UsdGeomTokens->vertex )
			{
				UVSet.InterpolationMethod = EUsdInterpolationMethod::Vertex;
			}
			else if ( PrimvarST.GetInterpolation() == UsdGeomTokens->faceVarying )
			{
				UVSet.InterpolationMethod = EUsdInterpolationMethod::FaceVarying;
			}
			else if (  PrimvarST.GetInterpolation() == UsdGeomTokens->uniform )
			{
				UVSet.InterpolationMethod = EUsdInterpolationMethod::Uniform;
			}
			else if ( PrimvarST.GetInterpolation() == UsdGeomTokens->constant )
			{
				UVSet.InterpolationMethod = EUsdInterpolationMethod::Constant;
			}

			if ( PrimvarST.IsIndexed() )
			{
				UVSet.UVIndices.Emplace();

				if ( PrimvarST.GetIndices( &UVSet.UVIndices.GetValue() ) && PrimvarST.Get( &UVSet.UVs ) )
				{
					if ( UVSet.UVs.size() > 0 )
					{
						UVSets.Add( MoveTemp( UVSet ) );
					}
				}
			}
			else
			{
				if ( PrimvarST.Get( &UVSet.UVs ) )
				{
					if ( UVSet.UVs.size() > 0 )
					{
						UVSets.Add( MoveTemp( UVSet ) );
					}
				}
			}
		}
		else
		{
			break;
		}

		++UVChannelIndex;
	}

	SkelMeshImportData.Wedges.Reserve( ( NumExistingFaces + NumFaces ) * 6 );

	uint32 NumProcessedFaceVertexIndices = 0;
	for (uint32 PolygonIndex = NumExistingFaces, LocalIndex = 0; PolygonIndex < NumExistingFaces + NumFaces; ++PolygonIndex, ++LocalIndex)
	{
		const uint32 NumOriginalFaceVertices = FaceCounts[LocalIndex];
		const uint32 NumFinalFaceVertices = 3;

		// Manage materials
		int32 LocalMaterialIndex = 0;
		if ( FaceMaterialIndices.IsValidIndex( PolygonIndex ) )
		{
			LocalMaterialIndex = FaceMaterialIndices[ PolygonIndex ];
			if ( !LocalMaterialSlots.IsValidIndex(LocalMaterialIndex) )
			{
				LocalMaterialIndex = 0;
			}
		}

		int32 RealMaterialIndex = LocalToCombinedMaterialIndex[ LocalMaterialIndex ];
		SkelMeshImportData.MaxMaterialIndex = FMath::Max< uint32 >( SkelMeshImportData.MaxMaterialIndex, RealMaterialIndex );

		// SkeletalMeshImportData uses triangle faces so quads will have to be split into triangles
		const bool bIsQuad = ( NumOriginalFaceVertices == 4 );
		const uint32 NumTriangles = bIsQuad ? 2 : 1;

		for ( uint32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex )
		{
			// This needs to be zeroed as we'll hash these faces later
			int32 TriangleFaceIndex = SkelMeshImportData.Faces.AddZeroed();

			SkeletalMeshImportData::FTriangle& Triangle = SkelMeshImportData.Faces[ TriangleFaceIndex ];

			// Set the face smoothing by default. It could be any number, but not zero
			Triangle.SmoothingGroups = 255;

			// #ueent_todo: Convert normals to TangentZ (TangentX/TangentY are tangents/binormals)

			Triangle.MatIndex = RealMaterialIndex;
			Triangle.AuxMatIndex = 0;

			// Fill the wedge data and complete the triangle setup with the wedge indices
			for ( uint32 CornerIndex = 0; CornerIndex < NumFinalFaceVertices; ++CornerIndex )
			{
				uint32 OriginalCornerIndex = ( ( TriangleIndex * ( NumOriginalFaceVertices - 2 ) ) + CornerIndex ) % NumOriginalFaceVertices;
				uint32 OriginalVertexInstanceIndex = NumProcessedFaceVertexIndices + OriginalCornerIndex;
				int32 OriginalVertexIndex = OriginalFaceIndices[ OriginalVertexInstanceIndex ];

				int32 FinalCornerIndex = bReverseOrder ? NumFinalFaceVertices - 1 - CornerIndex : CornerIndex;

				// Its important to make sure the UVs aren't just uninitialized memory because BuildSkeletalMesh will read them
				// when trying to merge vertices. Uninitialized memory would lead to inconsistent, non-deterministic meshes
				const uint32 WedgeIndex = SkelMeshImportData.Wedges.AddZeroed();
				SkeletalMeshImportData::FVertex& SkelMeshWedge = SkelMeshImportData.Wedges[ WedgeIndex ];

				if ( SkelMeshImportData.bHasVertexColors )
				{
					uint32 DisplayColorIndex = UsdToUnrealImpl::GetPrimValueIndex( DisplayColorInterp, OriginalVertexIndex, OriginalVertexInstanceIndex, LocalIndex );
					uint32 DisplayOpacityIndex = UsdToUnrealImpl::GetPrimValueIndex( DisplayOpacityInterp, OriginalVertexIndex, OriginalVertexInstanceIndex, LocalIndex );

					const FColor& DisplayColor = Colors[ DisplayColorIndex ];

					SkelMeshWedge.Color.R = DisplayColor.R;
					SkelMeshWedge.Color.G = DisplayColor.G;
					SkelMeshWedge.Color.B = DisplayColor.B;
					SkelMeshWedge.Color.A = static_cast<uint8>(FMath::Clamp(Opacities[ DisplayOpacityIndex ], 0.0f, 1.0f) * 255.0f + 0.5f);
				}

				SkelMeshWedge.MatIndex = Triangle.MatIndex;
				SkelMeshWedge.VertexIndex = NumExistingPoints + OriginalVertexIndex;
				SkelMeshWedge.Reserved = 0;

				int32 UVLayerIndex = 0;
				for ( const FUVSet& UVSet : UVSets )
				{
					int32 ValueIndex = 0;

					if ( UVSet.InterpolationMethod == EUsdInterpolationMethod::Vertex )
					{
						ValueIndex = OriginalVertexIndex;
					}
					else if ( UVSet.InterpolationMethod == EUsdInterpolationMethod::FaceVarying )
					{
						ValueIndex = OriginalVertexInstanceIndex;
					}
					else if ( UVSet.InterpolationMethod == EUsdInterpolationMethod::Uniform )
					{
						ValueIndex = PolygonIndex;
					}
					else if ( UVSet.InterpolationMethod == EUsdInterpolationMethod::Constant )
					{
						ValueIndex = 0;
					}

					GfVec2f UV( 0.f, 0.f );

					if ( UVSet.UVIndices.IsSet() )
					{
						if ( ensure( UVSet.UVIndices.GetValue().size() > ValueIndex ) )
						{
							UV = UVSet.UVs[ UVSet.UVIndices.GetValue()[ ValueIndex ] ];
						}
					}
					else if ( ensure( UVSet.UVs.size() > ValueIndex ) )
					{
						UV = UVSet.UVs[ ValueIndex ];
					}

					// Flip V for Unreal uv's which match directx
					FVector2D FinalUVVector( UV[0], 1.f - UV[1] );
					SkelMeshWedge.UVs[ UVLayerIndex ] = FinalUVVector;

					++UVLayerIndex;
				}

				Triangle.TangentX[ FinalCornerIndex ] = FVector::ZeroVector;
				Triangle.TangentY[ FinalCornerIndex ] = FVector::ZeroVector;
				Triangle.TangentZ[ FinalCornerIndex ] = FVector::ZeroVector;

				Triangle.WedgeIndex[ FinalCornerIndex ] = WedgeIndex;
			}
		}

		NumProcessedFaceVertexIndices += NumOriginalFaceVertices;
	}

	// Convert joint influences into the SkeletalMeshImportData

	// ComputeJointInfluences returns the influences per bone that applies to all the points of the mesh
	// ComputeVaryingJointInfluences returns the joint influences for each points, expanding the influences to all points if the mesh is rigidly deformed
	VtArray<int> JointIndices;
	VtArray<float> JointWeights;
	SkinningQuery.ComputeVaryingJointInfluences(NumPoints, &JointIndices, &JointWeights);

	// Recompute the joint influences if it's above the limit
	uint32 NumInfluencesPerComponent = SkinningQuery.GetNumInfluencesPerComponent();
	if (NumInfluencesPerComponent > MAX_INFLUENCES_PER_STREAM)
	{
		UsdSkelResizeInfluences(&JointIndices, NumInfluencesPerComponent, MAX_INFLUENCES_PER_STREAM);
		UsdSkelResizeInfluences(&JointWeights, NumInfluencesPerComponent, MAX_INFLUENCES_PER_STREAM);
		NumInfluencesPerComponent = MAX_INFLUENCES_PER_STREAM;
	}

	// We keep track of which influences we added because we combine many Mesh prim (each with potentially a different
	// explicit joint order) into the same skeletal mesh asset
	const int32 NumInfluencesBefore = SkelMeshImportData.Influences.Num();
	if ( JointWeights.size() > ( NumPoints - 1 ) * ( NumInfluencesPerComponent - 1 ) )
	{
		uint32 JointIndex = 0;
		SkelMeshImportData.Influences.Reserve(NumPoints);
		for (uint32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
		{
			// The JointIndices/JointWeights contain the influences data for NumPoints * NumInfluencesPerComponent
			for (uint32 InfluenceIndex = 0; InfluenceIndex < NumInfluencesPerComponent; ++InfluenceIndex, ++JointIndex)
			{
				// BoneWeight could be 0 if the actual number of influences were less than NumInfluencesPerComponent for a given point so just ignore it
				float BoneWeight = JointWeights[JointIndex];
				if (BoneWeight != 0.f)
				{
					SkelMeshImportData.Influences.AddUninitialized();
					SkelMeshImportData.Influences.Last().BoneIndex = JointIndices[JointIndex];
					SkelMeshImportData.Influences.Last().Weight = BoneWeight;
					SkelMeshImportData.Influences.Last().VertexIndex = NumExistingPoints + PointIndex;
				}
			}
		}
	}
	const int32 NumInfluencesAfter = SkelMeshImportData.Influences.Num();

	// If we have a joint mapper this Mesh has an explicit joint ordering, so we need to map joint indices to the skeleton's bone indices
	if ( pxr::UsdSkelAnimMapperRefPtr AnimMapper = SkinningQuery.GetJointMapper() )
	{
		VtArray<int> SkeletonBoneIndices;
		if ( pxr::UsdSkelSkeleton BoundSkeleton = SkelBinding.GetInheritedSkeleton() )
		{
			if ( pxr::UsdAttribute SkeletonJointsAttr = BoundSkeleton.GetJointsAttr() )
			{
				VtArray<TfToken> SkeletonJoints;
				if ( SkeletonJointsAttr.Get( &SkeletonJoints ) )
				{
					// If the skeleton has N bones, this will just contain { 0, 1, 2, ..., N-1 }
					int NumSkeletonBones = static_cast< int >( SkeletonJoints.size() );
					for ( int SkeletonBoneIndex = 0; SkeletonBoneIndex < NumSkeletonBones; ++SkeletonBoneIndex )
					{
						SkeletonBoneIndices.push_back( SkeletonBoneIndex );
					}

					// Use the AnimMapper to produce the indices of the Mesh's joints within the Skeleton's list of joints.
					// Example: Imagine skeleton had { "Root", "Root/Hip", "Root/Hip/Shoulder", "Root/Hip/Shoulder/Arm", "Root/Hip/Shoulder/Arm/Elbow" }, and so
					// BoneIndexRemapping was { 0, 1, 2, 3, 4 }. Consider a Mesh that specifies the explicit joints { "Root/Hip/Shoulder", "Root/Hip/Shoulder/Arm" },
					// and so uses the indices 0 and 1 to refer to Shoulder and Arm. After the Remap call SkeletonBoneIndices will hold { 2, 3 }, as those are the
					// indices of Shoulder and Arm within the skeleton's bones
					VtArray<int> BoneIndexRemapping;
					if ( AnimMapper->Remap( SkeletonBoneIndices, &BoneIndexRemapping ) )
					{
						for ( int32 AddedInfluenceIndex = NumInfluencesBefore; AddedInfluenceIndex < NumInfluencesAfter; ++AddedInfluenceIndex )
						{
							SkeletalMeshImportData::FRawBoneInfluence& Influence = SkelMeshImportData.Influences[ AddedInfluenceIndex ];
							Influence.BoneIndex = BoneIndexRemapping[ Influence.BoneIndex ];
						}
					}
				}
			}
		}
	}


	return true;
}

// Using UsdSkelSkeletonQuery instead of UsdSkelAnimQuery as it automatically does the joint remapping when we ask it to compute joint transforms.
// It also initializes the joint transforms with the rest pose, if available, in case the animation doesn't provide data for all joints.
bool UsdToUnreal::ConvertSkelAnim( const pxr::UsdSkelSkeletonQuery& InUsdSkeletonQuery, const pxr::VtArray<pxr::UsdSkelSkinningQuery>* InSkinningTargets, const UsdUtils::FBlendShapeMap* InBlendShapes, bool bInInterpretLODs, UAnimSequence* OutSkeletalAnimationAsset )
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::ConvertSkelAnim );

	FScopedUnrealAllocs UEAllocs;

	if ( !InUsdSkeletonQuery || !OutSkeletalAnimationAsset )
	{
		return false;
	}

	// If we have no skeleton we can't add animation data to the AnimSequence, so we may as well just return
	USkeleton* Skeleton = OutSkeletalAnimationAsset->GetSkeleton();
	if ( !Skeleton )
	{
		return false;
	}

	TUsdStore<pxr::UsdSkelAnimQuery> AnimQuery = InUsdSkeletonQuery.GetAnimQuery();
	if ( !AnimQuery.Get() )
	{
		return false;
	}

	TUsdStore<pxr::UsdStageWeakPtr> Stage( InUsdSkeletonQuery.GetPrim().GetStage() );
	FUsdStageInfo StageInfo{ Stage.Get() };
	double TimeCodesPerSecond = Stage.Get()->GetTimeCodesPerSecond();
	if ( FMath::IsNearlyZero( TimeCodesPerSecond ) )
	{
		FUsdLogManager::LogMessage( EMessageSeverity::Warning,
									LOCTEXT("TimeCodesPerSecondIsZero", "Cannot bake skeletal animations as the stage has timeCodesPerSecond set to zero!") );
		return false;
	}
	double FramesPerSecond = Stage.Get()->GetFramesPerSecond();
	if ( FMath::IsNearlyZero( FramesPerSecond ) )
	{
		FUsdLogManager::LogMessage( EMessageSeverity::Warning,
									LOCTEXT("FramesPersecondIsZero", "Cannot bake skeletal animations as the stage has framesPerSecond set to zero!") );
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const TArray<FMeshBoneInfo>& BoneInfo = RefSkeleton.GetRawRefBoneInfo();
	int32 NumBones = BoneInfo.Num();
	if ( InUsdSkeletonQuery.GetJointOrder().size() != NumBones )
	{
		return false;
	}

	// In a regular import workflow this NameMapping will exist and be populated with the blend shape names we imported, if any
	const FSmartNameMapping* NameMapping = Skeleton->GetSmartNameContainer( USkeleton::AnimCurveMappingName );
	if ( !NameMapping )
	{
		return false;
	}

	OutSkeletalAnimationAsset->CleanAnimSequenceForImport();
	const bool bSourceDataExists = OutSkeletalAnimationAsset->HasSourceRawData();

	TUsdStore<std::vector<double>> UsdJointTransformTimeSamples;
	AnimQuery.Get().GetJointTransformTimeSamples( &( UsdJointTransformTimeSamples.Get() ) );
	int32 NumJointTransformSamples = UsdJointTransformTimeSamples.Get().size();
	double FirstJointSampleTimeCode = 0;
	double LastJointSampleTimeCode = 0;
	if ( UsdJointTransformTimeSamples.Get().size() > 0 )
	{
		const std::vector<double>& JointTransformTimeSamples = UsdJointTransformTimeSamples.Get();
		FirstJointSampleTimeCode = JointTransformTimeSamples[ 0 ];
		LastJointSampleTimeCode = JointTransformTimeSamples[ JointTransformTimeSamples.size() - 1 ];
	}

	TUsdStore<std::vector<double>> UsdBlendShapeTimeSamples;
	AnimQuery.Get().GetBlendShapeWeightTimeSamples( &( UsdBlendShapeTimeSamples.Get() ) );
	int32 NumBlendShapeSamples = UsdBlendShapeTimeSamples.Get().size();
	double FirstBlendShapeSampleTimeCode = 0;
	double LastBlendShapeSampleTimeCode = 0;
	if ( UsdBlendShapeTimeSamples.Get().size() > 0 )
	{
		const std::vector<double>& BlendShapeTimeSamples = UsdBlendShapeTimeSamples.Get();
		FirstBlendShapeSampleTimeCode = BlendShapeTimeSamples[ 0 ];
		LastBlendShapeSampleTimeCode = BlendShapeTimeSamples[ BlendShapeTimeSamples.size() - 1 ];
	}

	const double StartTimeCode = FMath::Min( FirstJointSampleTimeCode, FirstBlendShapeSampleTimeCode );
	const double EndTimeCode = FMath::Max( LastJointSampleTimeCode, LastBlendShapeSampleTimeCode );
	const double StartSeconds = StartTimeCode / TimeCodesPerSecond;
	const double SequenceLengthTimeCodes = EndTimeCode - StartTimeCode;
	const double SequenceLengthSeconds = FMath::Max<double>( SequenceLengthTimeCodes / TimeCodesPerSecond, MINIMUM_ANIMATION_LENGTH );
	const int32 NumBakedFrames = FMath::CeilToInt( FMath::Max( SequenceLengthSeconds * FramesPerSecond + 1.0, 1.0 ) );
	const double IntervalTimeCodes = ( NumBakedFrames > 1 ) ? ( SequenceLengthTimeCodes / ( NumBakedFrames - 1 ) ) : MINIMUM_ANIMATION_LENGTH;

	// Bake the animation for each frame.
	// An alternative route would be to convert the time samples into TransformCurves, add them to UAnimSequence::RawCurveData,
	// and then call UAnimSequence::BakeTrackCurvesToRawAnimation. Doing it this way provides a few benefits though: The main one is that the way with which
	// UAnimSequence bakes can lead to artifacts on problematic joints (e.g. 90 degree rotation joints children of -1 scale joints, etc.) as it compounds the
	// transformation with the rest pose. Another benefit is that that doing it this way lets us offload the interpolation to USD, so that it can do it
	// however it likes, and we can just sample the joints at the target framerate
	if ( NumJointTransformSamples >= 2 )
	{
		FScopedUsdAllocs Allocs;

		TArray<FRawAnimSequenceTrack> JointTracks;
		JointTracks.SetNum(NumBones);

		for ( int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex )
		{
			FRawAnimSequenceTrack& JointTrack = JointTracks[ BoneIndex ];
			JointTrack.PosKeys.Reserve(NumBakedFrames);
			JointTrack.RotKeys.Reserve(NumBakedFrames);
			JointTrack.ScaleKeys.Reserve(NumBakedFrames);
		}

		pxr::VtArray<pxr::GfMatrix4d> UsdJointTransforms;
		for ( int32 FrameIndex = 0; FrameIndex < NumBakedFrames; ++FrameIndex )
		{
			const double FrameTimeCodes = StartTimeCode + FrameIndex * IntervalTimeCodes;

			InUsdSkeletonQuery.ComputeJointLocalTransforms( &UsdJointTransforms, FrameTimeCodes );
			for ( int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex )
			{
				pxr::GfMatrix4d& UsdJointTransform = UsdJointTransforms[ BoneIndex ];
				FTransform UEJointTransform = UsdToUnreal::ConvertMatrix( StageInfo, UsdJointTransform );

				FRawAnimSequenceTrack& JointTrack = JointTracks[ BoneIndex ];
				JointTrack.PosKeys.Add( UEJointTransform.GetTranslation() );
				JointTrack.RotKeys.Add( UEJointTransform.GetRotation() );
				JointTrack.ScaleKeys.Add( UEJointTransform.GetScale3D() );
			}
		}

		for ( int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex )
		{
			OutSkeletalAnimationAsset->AddNewRawTrack( BoneInfo[ BoneIndex ].Name, &JointTracks[ BoneIndex ] );
		}
	}

	// Add float tracks to animate morph target weights
	if ( InBlendShapes && InSkinningTargets )
	{
		FScopedUsdAllocs Allocs;

		pxr::UsdSkelAnimQuery UsdAnimQuery = AnimQuery.Get();

		pxr::VtTokenArray SkelAnimChannelOrder = UsdAnimQuery.GetBlendShapeOrder();
		int32 NumSkelAnimChannels = SkelAnimChannelOrder.size();

		if ( NumSkelAnimChannels > 0 )
		{
			// Create a float curve for each blend shape channel. These will be copied for each blend shape that uses it
			// Don't remove redundant keys because if there are blendshapes with inbetweens that use this channel,
			// we want to make sure that we don't miss the frames where the curve would have reached the exact weight of a blend shape
			ERichCurveInterpMode CurveInterpMode = Stage.Get()->GetInterpolationType() == pxr::UsdInterpolationTypeHeld ? ERichCurveInterpMode::RCIM_Constant : ERichCurveInterpMode::RCIM_Linear;
			TArray<FRichCurve> SkelAnimChannelCurves;
			SkelAnimChannelCurves.SetNum( NumSkelAnimChannels );
			pxr::VtArray< float > WeightsForFrame;
			for ( int32 FrameIndex = 0; FrameIndex < NumBakedFrames; ++FrameIndex )
			{
				const double FrameTimeCodes = StartTimeCode + FrameIndex * IntervalTimeCodes;
				const double FrameSeconds = FrameTimeCodes / TimeCodesPerSecond - StartSeconds; // We want the animation to start at 0 seconds

				UsdAnimQuery.ComputeBlendShapeWeights( &WeightsForFrame, pxr::UsdTimeCode( FrameTimeCodes ) );

				for ( int32 SkelAnimChannelIndex = 0; SkelAnimChannelIndex < NumSkelAnimChannels; ++SkelAnimChannelIndex )
				{
					FRichCurve& Curve = SkelAnimChannelCurves[ SkelAnimChannelIndex ];

					FKeyHandle NewKeyHandle = Curve.AddKey( FrameSeconds, WeightsForFrame[ SkelAnimChannelIndex ] );
					Curve.SetKeyInterpMode( NewKeyHandle, CurveInterpMode );
				}
			}

			TSet<FString> ProcessedLODParentPaths;

			// Since we may need to switch variants to parse LODs, we could invalidate references to SkinningQuery objects, so we need
			// to keep track of these by path and construct one whenever we need them
			TArray<pxr::SdfPath> PathsToSkinnedPrims;
			for ( const pxr::UsdSkelSkinningQuery& SkinningQuery : *InSkinningTargets )
			{
				// In USD, the skinning target need not be a mesh, but for Unreal we are only interested in skinning meshes
				if ( pxr::UsdGeomMesh SkinningMesh = pxr::UsdGeomMesh( SkinningQuery.GetPrim() ) )
				{
					PathsToSkinnedPrims.Add( SkinningMesh.GetPrim().GetPath() );
				}
			}

			TFunction<bool( const pxr::UsdGeomMesh&, int32 )> CreateCurvesForLOD =
			[ &InUsdSkeletonQuery, InBlendShapes, NumSkelAnimChannels, &SkelAnimChannelOrder, &SkelAnimChannelCurves, OutSkeletalAnimationAsset ]
			( const pxr::UsdGeomMesh& LODMesh, int32 LODIndex )
			{
				pxr::UsdSkelSkinningQuery SkinningQuery = UsdUtils::CreateSkinningQuery( LODMesh, InUsdSkeletonQuery );
				if ( !SkinningQuery )
				{
					return true; // Continue trying other LODs
				}

				pxr::VtTokenArray MeshChannelOrder;
				if ( !SkinningQuery.GetBlendShapeOrder( &MeshChannelOrder ) )
				{
					return true;
				}

				pxr::SdfPathVector BlendShapeTargets;
				const pxr::UsdRelationship& BlendShapeTargetsRel = SkinningQuery.GetBlendShapeTargetsRel();
				BlendShapeTargetsRel.GetTargets( &BlendShapeTargets );

				// USD will already show a warning if this happens, so let's just continue
				int32 NumMeshChannels = static_cast< int32 >( MeshChannelOrder.size() );
				if ( NumMeshChannels != static_cast< int32 >( BlendShapeTargets.size() ) )
				{
					return true;
				}

				pxr::SdfPath MeshPath = SkinningQuery.GetPrim().GetPath();
				for ( int32 MeshChannelIndex = 0; MeshChannelIndex < NumMeshChannels; ++MeshChannelIndex )
				{
					FString PrimaryBlendShapePath = UsdToUnreal::ConvertPath( BlendShapeTargets[ MeshChannelIndex ].MakeAbsolutePath( MeshPath ) );

					if ( const UsdUtils::FUsdBlendShape* FoundPrimaryBlendShape = InBlendShapes->Find( PrimaryBlendShapePath ) )
					{
						// Find a float curve for the primary blend shape
						FRichCurve* PrimaryBlendShapeCurve = nullptr;
						pxr::TfToken& MeshChannel = MeshChannelOrder[ MeshChannelIndex ];
						for ( int32 SkelAnimChannelIndex = 0; SkelAnimChannelIndex < NumSkelAnimChannels; ++SkelAnimChannelIndex )
						{
							const pxr::TfToken& SkelAnimChannel = SkelAnimChannelOrder[ SkelAnimChannelIndex ];
							if ( SkelAnimChannel == MeshChannel )
							{
								PrimaryBlendShapeCurve = &SkelAnimChannelCurves[ SkelAnimChannelIndex ];
								break;
							}
						}

						if ( !PrimaryBlendShapeCurve )
						{
							FUsdLogManager::LogMessage(
								EMessageSeverity::Warning,
								FText::Format( LOCTEXT( "NoChannelForPrimary", "Could not find a float channel to apply to primary blend shape '{0}'" ),
								FText::FromString( PrimaryBlendShapePath ) )
							);
							continue;
						}

						// Primary blend shape has no inbetweens, so we can just use the skel anim channel curve directly
						if ( FoundPrimaryBlendShape->Inbetweens.Num() == 0 )
						{
							SkelDataConversionImpl::SetFloatCurveData( OutSkeletalAnimationAsset, *FoundPrimaryBlendShape->Name, *PrimaryBlendShapeCurve );
						}
						// Blend shape has inbetweens --> Need to map these to multiple float curves. This can be different for each mesh, so we need to do it for each
						else
						{
							TArray<FRichCurve> RemappedBlendShapeCurves = SkelDataConversionImpl::ResolveWeightsForBlendShapeCurve( *FoundPrimaryBlendShape, *PrimaryBlendShapeCurve );
							if ( RemappedBlendShapeCurves.Num() != FoundPrimaryBlendShape->Inbetweens.Num() + 1 )
							{
								FUsdLogManager::LogMessage(
									EMessageSeverity::Warning,
									FText::Format( LOCTEXT( "FailedToRemapInbetweens", "Failed to remap inbetween float curves for blend shape '{0}'" ),
									FText::FromString( PrimaryBlendShapePath ) )
								);
								continue;
							}

							SkelDataConversionImpl::SetFloatCurveData( OutSkeletalAnimationAsset, *FoundPrimaryBlendShape->Name, RemappedBlendShapeCurves[ 0 ] );

							for ( int32 InbetweenIndex = 0; InbetweenIndex < FoundPrimaryBlendShape->Inbetweens.Num(); ++InbetweenIndex )
							{
								const UsdUtils::FUsdBlendShapeInbetween& Inbetween = FoundPrimaryBlendShape->Inbetweens[ InbetweenIndex ];
								const FRichCurve& InbetweenCurve = RemappedBlendShapeCurves[ InbetweenIndex + 1 ]; // Index 0 is the primary

								SkelDataConversionImpl::SetFloatCurveData( OutSkeletalAnimationAsset, *Inbetween.Name, InbetweenCurve );
							}
						}
					}
				}

				return true;
			};

			for ( const pxr::SdfPath& SkinnedPrimPath : PathsToSkinnedPrims )
			{
				pxr::UsdPrim SkinnedPrim = Stage.Get()->GetPrimAtPath( SkinnedPrimPath );
				if ( !SkinnedPrim )
				{
					continue;
				}

				pxr::UsdGeomMesh SkinnedMesh{ SkinnedPrim };
				if ( !SkinnedMesh )
				{
					continue;
				}

				pxr::UsdPrim ParentPrim = SkinnedMesh.GetPrim().GetParent();
				FString ParentPrimPath = UsdToUnreal::ConvertPath( ParentPrim.GetPath() );

				bool bInterpretedLODs = false;
				if ( bInInterpretLODs && ParentPrim && !ProcessedLODParentPaths.Contains( ParentPrimPath ) )
				{
					// At the moment we only consider a single mesh per variant, so if multiple meshes tell us to process the same parent prim, we skip.
					// This check would also prevent us from getting in here in case we just have many meshes children of a same prim, outside
					// of a variant. In this case they don't fit the "one mesh per variant" pattern anyway, and we want to fallback to ignoring LODs
					ProcessedLODParentPaths.Add( ParentPrimPath );

					// WARNING: After this is called, references to objects that were inside any of the LOD Meshes will be invalidated!
					bInterpretedLODs = UsdUtils::IterateLODMeshes( ParentPrim, CreateCurvesForLOD );
				}

				if ( !bInterpretedLODs )
				{
					// Refresh reference to this prim as it could have been inside a variant that was temporarily switched by IterateLODMeshes
					CreateCurvesForLOD( SkinnedMesh, 0 );
				}
			}
		}
	}

	OutSkeletalAnimationAsset->Interpolation = Stage.Get()->GetInterpolationType() == pxr::UsdInterpolationTypeHeld ? EAnimInterpolationType::Step : EAnimInterpolationType::Linear;
	OutSkeletalAnimationAsset->ImportFileFramerate = Stage.Get()->GetFramesPerSecond();
	OutSkeletalAnimationAsset->ImportResampleFramerate = FramesPerSecond;
	OutSkeletalAnimationAsset->SequenceLength = SequenceLengthSeconds;
	OutSkeletalAnimationAsset->SetRawNumberOfFrame( NumBakedFrames );
	OutSkeletalAnimationAsset->MarkRawDataAsModified();
	if ( bSourceDataExists )
	{
		OutSkeletalAnimationAsset->BakeTrackCurvesToRawAnimation();
	}
	else
	{
		OutSkeletalAnimationAsset->PostProcessSequence();
	}
	OutSkeletalAnimationAsset->PostEditChange();
	OutSkeletalAnimationAsset->MarkPackageDirty();

	return true;
}

bool UsdToUnreal::ConvertBlendShape( const pxr::UsdSkelBlendShape& UsdBlendShape, const FUsdStageInfo& StageInfo, const FTransform& AdditionalTransform, uint32 PointIndexOffset, TSet<FString>& UsedMorphTargetNames, UsdUtils::FBlendShapeMap& OutBlendShapes )
{
	return ConvertBlendShape(UsdBlendShape, StageInfo, 0, AdditionalTransform, PointIndexOffset, UsedMorphTargetNames, OutBlendShapes);
}

bool UsdToUnreal::ConvertBlendShape( const pxr::UsdSkelBlendShape& UsdBlendShape, const FUsdStageInfo& StageInfo, int32 LODIndex, const FTransform& AdditionalTransform, uint32 PointIndexOffset, TSet<FString>& UsedMorphTargetNames, UsdUtils::FBlendShapeMap& OutBlendShapes )
{
	FScopedUsdAllocs Allocs;

	pxr::UsdAttribute OffsetsAttr = UsdBlendShape.GetOffsetsAttr();
	pxr::VtArray< pxr::GfVec3f > Offsets;
	OffsetsAttr.Get( &Offsets );

	pxr::UsdAttribute IndicesAttr = UsdBlendShape.GetPointIndicesAttr();
	pxr::VtArray< int > PointIndices;
	IndicesAttr.Get( &PointIndices );

	pxr::UsdAttribute NormalsAttr = UsdBlendShape.GetNormalOffsetsAttr();
	pxr::VtArray< pxr::GfVec3f > Normals;
	NormalsAttr.Get( &Normals );

	// We need to guarantee blend shapes have unique names because these will be used as UMorphTarget names
	// Note that we can't just use the prim path here and need an index to guarantee uniqueness,
	// because although the path is usually unique, USD has case sensitive paths and the FNames of the
	// UMorphTargets are case insensitive
	FString PrimaryName = SkelDataConversionImpl::GetUniqueName(
		SkelDataConversionImpl::SanitizeObjectName( UsdToUnreal::ConvertString( UsdBlendShape.GetPrim().GetName() ) ),
		UsedMorphTargetNames );
	FString PrimaryPath = UsdToUnreal::ConvertPath( UsdBlendShape.GetPrim().GetPath() );
	if ( UsdUtils::FUsdBlendShape* ExistingBlendShape = OutBlendShapes.Find( PrimaryPath ) )
	{
		ExistingBlendShape->LODIndicesThatUseThis.Add( LODIndex );
		return true;
	}

	UsdUtils::FUsdBlendShape PrimaryBlendShape;
	if ( !SkelDataConversionImpl::CreateUsdBlendShape( PrimaryName, Offsets, Normals, PointIndices, StageInfo, AdditionalTransform, PointIndexOffset, LODIndex, PrimaryBlendShape ) )
	{
		return false;
	}
	UsedMorphTargetNames.Add( PrimaryBlendShape.Name );

	UsdUtils::FBlendShapeMap InbetweenBlendShapes;
	for ( const pxr::UsdSkelInbetweenShape& Inbetween : UsdBlendShape.GetInbetweens() )
	{
		if ( !Inbetween )
		{
			continue;
		}

		float Weight = 0.0f;
		if ( !Inbetween.GetWeight( &Weight ) )
		{
			continue;
		}

		FString OrigInbetweenName = UsdToUnreal::ConvertString( Inbetween.GetAttr().GetName() );
		FString InbetweenPath = FString::Printf(TEXT("%s_%s"), *PrimaryPath, *OrigInbetweenName );
		FString InbetweenName = SkelDataConversionImpl::GetUniqueName(
			SkelDataConversionImpl::SanitizeObjectName( FPaths::GetCleanFilename( InbetweenPath ) ),
			UsedMorphTargetNames );

		if ( Weight > 1.0f || Weight < 0.0f || FMath::IsNearlyZero(Weight) || FMath::IsNearlyEqual(Weight, 1.0f) )
		{
			//UE_LOG(LogUsd, Warning, TEXT("Inbetween shape '%s' for blend shape '%s' has invalid weight '%f' and will be ignored. Valid weights are > 0.0f and < 1.0f!"),
			//	*OrigInbetweenName,
			//	*PrimaryPath,
			//	Weight);
			continue;
		}

		pxr::VtArray< pxr::GfVec3f > InbetweenPointsOffsets;
		pxr::VtArray< pxr::GfVec3f > InbetweenNormalOffsets;

		Inbetween.GetOffsets(&InbetweenPointsOffsets);
		Inbetween.GetNormalOffsets(&InbetweenNormalOffsets);

		// Create separate blend shape for the inbetween
		// Now how the inbetween always shares the same point indices as the parent
		UsdUtils::FUsdBlendShape InbetweenShape;
		if ( !SkelDataConversionImpl::CreateUsdBlendShape( InbetweenName, InbetweenPointsOffsets, InbetweenNormalOffsets, PointIndices, StageInfo, AdditionalTransform, PointIndexOffset, LODIndex, InbetweenShape ) )
		{
			continue;
		}
		UsedMorphTargetNames.Add( InbetweenShape.Name );
		InbetweenBlendShapes.Add( InbetweenPath, InbetweenShape );

		// Keep track of it in the PrimaryBlendShape so we can resolve weights later
		UsdUtils::FUsdBlendShapeInbetween& ConvertedInbetween = PrimaryBlendShape.Inbetweens.Emplace_GetRef();
		ConvertedInbetween.Name = InbetweenShape.Name;
		ConvertedInbetween.InbetweenWeight = Weight;
	}

	// Sort according to weight so they're easier to resolve later
	PrimaryBlendShape.Inbetweens.Sort([](const UsdUtils::FUsdBlendShapeInbetween& Lhs, const UsdUtils::FUsdBlendShapeInbetween& Rhs)
	{
		return Lhs.InbetweenWeight < Rhs.InbetweenWeight;
	});

	OutBlendShapes.Add( PrimaryPath, PrimaryBlendShape );
	OutBlendShapes.Append( MoveTemp(InbetweenBlendShapes) );

	return true;
}

USkeletalMesh* UsdToUnreal::GetSkeletalMeshFromImportData(
	TArray<FSkeletalMeshImportData>& LODIndexToSkeletalMeshImportData,
	const TArray<SkeletalMeshImportData::FBone>& InSkeletonBones,
	UsdUtils::FBlendShapeMap& InBlendShapesByPath,
	EObjectFlags ObjectFlags
)
{
	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::GetSkeletalMeshFromImportData );

	if ( LODIndexToSkeletalMeshImportData.Num() == 0 || InSkeletonBones.Num() == 0 )
	{
		return nullptr;
	}

	// A SkeletalMesh could be retrieved for re-use and updated for animations
	// For now, create a new USkeletalMesh
	// Note: Remember to initialize UsedMorphTargetNames with existing morph targets, whenever the SkeletalMesh is reused
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, ObjectFlags | EObjectFlags::RF_Public);

	// Process reference skeleton from import data
	int32 SkeletalDepth = 0;
	FSkeletalMeshImportData DummyData;
	DummyData.RefBonesBinary = InSkeletonBones;
	if ( !SkeletalMeshImportUtils::ProcessImportMeshSkeleton( SkeletalMesh->GetSkeleton(), SkeletalMesh->GetRefSkeleton(), SkeletalDepth, DummyData ) )
	{
		return nullptr;
	}
	if ( SkeletalMesh->GetRefSkeleton().GetRawBoneNum() == 0 )
	{
		SkeletalMesh->MarkPendingKill();
		return nullptr;
	}

	// This prevents PostEditChange calls when it is alive, also ensuring it is called once when we return from this function.
	// This is required because we must ensure the morphtargets are in the SkeletalMesh before the first call to PostEditChange(),
	// or else they will be effectively discarded
	FScopedSkeletalMeshPostEditChange ScopedPostEditChange( SkeletalMesh );
	SkeletalMesh->PreEditChange( nullptr );

	// Create initial bounding box based on expanded version of reference pose for meshes without physics assets
	const FSkeletalMeshImportData& LowestLOD = LODIndexToSkeletalMeshImportData[0];
	FBox BoundingBox( LowestLOD.Points.GetData(), LowestLOD.Points.Num() );
	FBox Temp = BoundingBox;
	FVector MidMesh = 0.5f*(Temp.Min + Temp.Max);
	BoundingBox.Min = Temp.Min + 1.0f*(Temp.Min - MidMesh);
	BoundingBox.Max = Temp.Max + 1.0f*(Temp.Max - MidMesh);
	BoundingBox.Min[2] = Temp.Min[2] + 0.1f*(Temp.Min[2] - MidMesh[2]);
	const FVector BoundingBoxSize = BoundingBox.GetSize();
	if ( LowestLOD.Points.Num() > 2 && BoundingBoxSize.X < THRESH_POINTS_ARE_SAME && BoundingBoxSize.Y < THRESH_POINTS_ARE_SAME && BoundingBoxSize.Z < THRESH_POINTS_ARE_SAME )
	{
		return nullptr;
	}

#if WITH_EDITOR
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>( "MeshUtilities" );
#endif // WITH_EDITOR

	FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
	ImportedResource->LODModels.Empty();
	SkeletalMesh->ResetLODInfo();
	bool bHasVertexColors = false;
	for ( int32 LODIndex = 0; LODIndex < LODIndexToSkeletalMeshImportData.Num(); ++LODIndex )
	{
		FSkeletalMeshImportData& LODImportData = LODIndexToSkeletalMeshImportData[LODIndex];
		ImportedResource->LODModels.Add( new FSkeletalMeshLODModel() );
		FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels.Last();

		// Process bones influence (normalization and optimization) (optional)
		SkeletalMeshImportUtils::ProcessImportMeshInfluences(LODImportData, SkeletalMesh->GetPathName());

		FSkeletalMeshLODInfo& NewLODInfo = SkeletalMesh->AddLODInfo();
		NewLODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
		NewLODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
		NewLODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
		NewLODInfo.LODHysteresis = 0.02f;

		bHasVertexColors |= LODImportData.bHasVertexColors;

		LODModel.NumTexCoords = FMath::Max<uint32>(1, LODImportData.NumTexCoords);

		// Data needed by BuildSkeletalMesh
		LODImportData.PointToRawMap.AddUninitialized( LODImportData.Points.Num() );
		for ( int32 PointIndex = 0; PointIndex < LODImportData.Points.Num(); ++PointIndex )
		{
			LODImportData.PointToRawMap[ PointIndex ] = PointIndex;
		}

		TArray<FVector> LODPoints;
		TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
		TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
		TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
		TArray<int32> LODPointToRawMap;
		LODImportData.CopyLODImportData( LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap );

#if WITH_EDITOR
		IMeshUtilities::MeshBuildOptions BuildOptions;
		BuildOptions.TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
		// #ueent_todo: Normals and tangents shouldn't need to be recomputed when they are retrieved from USD
		//BuildOptions.bComputeNormals = !LODImportData.bHasNormals;
		//BuildOptions.bComputeTangents = !LODImportData.bHasTangents;
		BuildOptions.bUseMikkTSpace = true;

		TArray<FText> WarningMessages;
		TArray<FName> WarningNames;

		bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(LODModel, SkeletalMesh->GetPathName(), SkeletalMesh->GetRefSkeleton(), LODInfluences, LODWedges, LODFaces, LODPoints, LODPointToRawMap, BuildOptions, &WarningMessages, &WarningNames );

		for ( int32 WarningIndex = 0; WarningIndex < FMath::Max( WarningMessages.Num(), WarningNames.Num() ); ++WarningIndex )
		{
			const FText& Text = WarningMessages.IsValidIndex( WarningIndex ) ? WarningMessages[ WarningIndex ] : FText::GetEmpty();
			const FName& Name = WarningNames.IsValidIndex( WarningIndex ) ? WarningNames[ WarningIndex ] : NAME_None;

			if ( bBuildSuccess )
			{
				UE_LOG( LogUsd, Warning, TEXT( "Warning when trying to build skeletal mesh from USD: '%s': '%s'" ), *Name.ToString(), *Text.ToString() );
			}
			else
			{
				UE_LOG( LogUsd, Error, TEXT( "Error when trying to build skeletal mesh from USD: '%s': '%s'" ), *Name.ToString(), *Text.ToString() );
			}
		}

		if ( !bBuildSuccess )
		{
			SkeletalMesh->MarkPendingKill();
			return nullptr;
		}

		// This is important because it will fill in the LODModel's RawSkeletalMeshBulkDataID,
		// which is the part of the skeletal mesh's DDC key that is affected by the actual mesh data
		SkeletalMesh->SaveLODImportedData( LODIndex, LODImportData );
#endif // WITH_EDITOR
	}

	SkeletalMesh->SetImportedBounds( FBoxSphereBounds( BoundingBox ) );
	SkeletalMesh->SetHasVertexColors(bHasVertexColors);
	SkeletalMesh->SetVertexColorGuid(SkeletalMesh->GetHasVertexColors() ? FGuid::NewGuid() : FGuid());
	SkeletalMesh->CalculateInvRefMatrices();

	// Generate a Skeleton and associate it to the SkeletalMesh
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, ObjectFlags | EObjectFlags::RF_Public );
	Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);
	Skeleton->SetPreviewMesh(SkeletalMesh);
	SkeletalMesh->SetSkeleton(Skeleton);

	UsdToUnrealImpl::CreateMorphTargets(InBlendShapesByPath, LODIndexToSkeletalMeshImportData, SkeletalMesh);

	return SkeletalMesh;
}

#endif // #if USE_USD_SDK && WITH_EDITOR

void UsdUtils::ResolveWeightsForBlendShape( const UsdUtils::FUsdBlendShape& InBlendShape, float InWeight, float& OutMainWeight, TArray<float>& OutInbetweenWeights )
{
	int32 NumInbetweens = InBlendShape.Inbetweens.Num();
	if ( NumInbetweens == 0 )
	{
		OutMainWeight = InWeight;
		return;
	}

	OutInbetweenWeights.SetNumUninitialized( NumInbetweens );
	for ( float& OutInbetweenWeight : OutInbetweenWeights )
	{
		OutInbetweenWeight = 0.0f;
	}

	if ( FMath::IsNearlyEqual( InWeight, 0.0f ) )
	{
		OutMainWeight = 0.0f;
		return;
	}
	else if ( FMath::IsNearlyEqual( InWeight, 1.0f ) )
	{
		OutMainWeight = 1.0f;
		return;
	}

	// Note how we don't care if UpperIndex/LowerIndex are beyond the bounds of the array here,
	// as that signals when we're above/below all inbetweens
	int32 UpperIndex = Algo::UpperBoundBy( InBlendShape.Inbetweens, InWeight, []( const UsdUtils::FUsdBlendShapeInbetween& Inbetween )
	{
		return Inbetween.InbetweenWeight;
	} );
	int32 LowerIndex = UpperIndex - 1;

	float UpperWeight = 1.0f;
	if ( UpperIndex <= NumInbetweens - 1 )
	{
		UpperWeight = InBlendShape.Inbetweens[ UpperIndex ].InbetweenWeight;
	}

	float LowerWeight = 0.0f;
	if ( LowerIndex >= 0 )
	{
		LowerWeight = InBlendShape.Inbetweens[ LowerIndex ].InbetweenWeight;
	}

	UpperWeight = ( InWeight - LowerWeight ) / ( UpperWeight - LowerWeight );
	LowerWeight = ( 1.0f - UpperWeight );

	// We're between upper inbetween and the 1.0 weight
	if ( UpperIndex > NumInbetweens - 1 )
	{
		OutMainWeight = UpperWeight;
		OutInbetweenWeights[ NumInbetweens - 1 ] = LowerWeight;
	}
	// We're between 0.0 and the first inbetween weight
	else if ( LowerIndex < 0 )
	{
		OutMainWeight = 0;
		OutInbetweenWeights[ 0 ] = UpperWeight;
	}
	// We're between two inbetweens
	else
	{
		OutInbetweenWeights[ UpperIndex ] = UpperWeight;
		OutInbetweenWeights[ LowerIndex ] = LowerWeight;
	}
}

#if USE_USD_SDK && WITH_EDITOR

// Adapted from UsdSkel_CacheImpl::ReadScope::_FindOrCreateSkinningQuery because we need to manually create these on UsdGeomMeshes we already have
pxr::UsdSkelSkinningQuery UsdUtils::CreateSkinningQuery( const pxr::UsdGeomMesh& SkinnedMesh, const pxr::UsdSkelSkeletonQuery& SkeletonQuery )
{
	pxr::UsdPrim SkinnedPrim = SkinnedMesh.GetPrim();
	if ( !SkinnedPrim )
	{
		return {};
	}

	const pxr::UsdSkelAnimQuery& AnimQuery = SkeletonQuery.GetAnimQuery();

	pxr::UsdSkelBindingAPI SkelBindingAPI{ SkinnedPrim };

	return pxr::UsdSkelSkinningQuery(
		SkinnedPrim,
		SkeletonQuery ? SkeletonQuery.GetJointOrder() : pxr::VtTokenArray(),
		AnimQuery ? AnimQuery.GetBlendShapeOrder() : pxr::VtTokenArray(),
		SkelBindingAPI.GetJointIndicesAttr(),
		SkelBindingAPI.GetJointWeightsAttr(),
		SkelBindingAPI.GetGeomBindTransformAttr(),
		SkelBindingAPI.GetJointsAttr(),
		SkelBindingAPI.GetBlendShapesAttr(),
		SkelBindingAPI.GetBlendShapeTargetsRel()
	);
}

bool UnrealToUsd::ConvertSkeleton( const FReferenceSkeleton& ReferenceSkeleton, pxr::UsdSkelSkeleton& UsdSkeleton )
{
	FScopedUsdAllocs Allocs;

	pxr::UsdStageRefPtr Stage = UsdSkeleton.GetPrim().GetStage();
	if ( !Stage )
	{
		return false;
	}

	FUsdStageInfo StageInfo{ Stage };

	// Joints
	{
		UnrealToUsdImpl::SetJoinsAttr( ReferenceSkeleton, UsdSkeleton.CreateJointsAttr() );
	}

	pxr::VtArray<pxr::GfMatrix4d> LocalSpaceJointTransforms;
	LocalSpaceJointTransforms.reserve( ReferenceSkeleton.GetRefBonePose().Num() );
	for ( const FTransform& BonePose : ReferenceSkeleton.GetRefBonePose() )
	{
		LocalSpaceJointTransforms.push_back( UnrealToUsd::ConvertTransform( StageInfo, BonePose ) );
	}

	TArray<FTransform> WorldSpaceUEJointTransforms;
	FAnimationRuntime::FillUpComponentSpaceTransforms(
		ReferenceSkeleton,
		ReferenceSkeleton.GetRefBonePose(),
		WorldSpaceUEJointTransforms
	);

	pxr::VtArray<pxr::GfMatrix4d> WorldSpaceJointTransforms;
	WorldSpaceJointTransforms.reserve( WorldSpaceUEJointTransforms.Num() );
	for ( const FTransform& WorldSpaceUETransform : WorldSpaceUEJointTransforms )
	{
		WorldSpaceJointTransforms.push_back( UnrealToUsd::ConvertTransform( StageInfo, WorldSpaceUETransform ) );
	}

	// Rest transforms
	{
		pxr::UsdAttribute RestTransformsAttr = UsdSkeleton.CreateRestTransformsAttr();
		RestTransformsAttr.Set( LocalSpaceJointTransforms );
	}

	// Bind transforms
	{
		pxr::UsdAttribute BindTransformsAttr = UsdSkeleton.CreateBindTransformsAttr();
		BindTransformsAttr.Set( WorldSpaceJointTransforms );
	}

	return true;
}

bool UnrealToUsd::ConvertSkeleton( const USkeleton* Skeleton, pxr::UsdSkelSkeleton& UsdSkeleton )
{
	if ( !Skeleton )
	{
		return false;
	}

	return UnrealToUsd::ConvertSkeleton( Skeleton->GetReferenceSkeleton(), UsdSkeleton );
}

bool UnrealToUsd::ConvertSkeletalMesh( const USkeletalMesh* SkeletalMesh, pxr::UsdPrim& SkelRootPrim, const pxr::UsdTimeCode TimeCode, UE::FUsdStage* StageForMaterialAssignments )
{
	pxr::UsdSkelRoot SkelRoot{ SkelRootPrim };
	if ( !SkeletalMesh || !SkeletalMesh->GetSkeleton() || !SkelRoot )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr Stage = SkelRootPrim.GetStage();
	if ( !Stage )
	{
		return false;
	}
	const FUsdStageInfo StageInfo( Stage );

	const FSkeletalMeshModel* SkelMeshResource = SkeletalMesh->GetImportedModel();
	int32 NumLODs = SkelMeshResource->LODModels.Num();
	if ( NumLODs < 1 )
	{
		return false;
	}

	pxr::UsdVariantSets VariantSets = SkelRootPrim.GetVariantSets();
	if ( NumLODs > 1 && VariantSets.HasVariantSet( UnrealIdentifiers::LOD ) )
	{
		UE_LOG( LogUsd, Error, TEXT( "Failed to export higher LODs for skeletal mesh '%s', as the target prim already has a variant set named '%s'!" ), *SkeletalMesh->GetName(), *UsdToUnreal::ConvertToken( UnrealIdentifiers::LOD ) );
		NumLODs = 1;
	}

	bool bExportMultipleLODs = NumLODs > 1;

	pxr::SdfPath ParentPrimPath = SkelRootPrim.GetPath();
	std::string LowestLODAdded = "";

	// Collect all material assignments, referenced by the sections' material indices
	bool bHasMaterialAssignments = false;
	pxr::VtArray< std::string > MaterialAssignments;
	for ( const FSkeletalMaterial& SkeletalMaterial : SkeletalMesh->GetMaterials() )
	{
		FString AssignedMaterialPathName;
		if ( UMaterialInterface* Material = SkeletalMaterial.MaterialInterface )
		{
			if ( Material->GetOutermost() != GetTransientPackage() )
			{
				AssignedMaterialPathName = Material->GetPathName();
				bHasMaterialAssignments = true;
			}
		}

		MaterialAssignments.push_back( UnrealToUsd::ConvertString( *AssignedMaterialPathName ).Get() );
	}
	if ( !bHasMaterialAssignments )
	{
		// Prevent creation of the unrealMaterials attribute in case we don't have any assignments at all
		MaterialAssignments.clear();
	}

	// Create and fill skeleton
	pxr::UsdSkelBindingAPI SkelBindingAPI{ SkelRoot };
	{
		pxr::UsdPrim SkeletonPrim = Stage->DefinePrim(
			SkelRootPrim.GetPath().AppendChild( UnrealToUsd::ConvertToken(TEXT("Skel")).Get() ),
			UnrealToUsd::ConvertToken(TEXT("Skeleton")).Get()
		);
		pxr::UsdSkelSkeleton SkelSkeleton{ SkeletonPrim };

		pxr::UsdRelationship SkelRel = SkelBindingAPI.CreateSkeletonRel();
		SkelRel.SetTargets({SkeletonPrim.GetPath()});

		UnrealToUsd::ConvertSkeleton( SkeletalMesh->GetRefSkeleton(), SkelSkeleton );
	}

	// Actual meshes
	for ( int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex )
	{
		const FSkeletalMeshLODModel& LODModel = SkelMeshResource->LODModels[ LODIndex ];

		if ( LODModel.NumVertices == 0 || LODModel.Sections.Num() == 0 )
		{
			continue;
		}

		// LOD0, LOD1, etc.
		std::string VariantName = UnrealIdentifiers::LOD.GetString() + UnrealToUsd::ConvertString( *LexToString( LODIndex ) ).Get();
		if ( LowestLODAdded.size() == 0 )
		{
			LowestLODAdded = VariantName;
		}

		// Enable the variant edit context, if we are creating variant LODs
		TOptional< pxr::UsdEditContext > EditContext;
		if ( bExportMultipleLODs )
		{
			pxr::UsdVariantSet VariantSet = VariantSets.GetVariantSet( UnrealIdentifiers::LOD );

			if ( !VariantSet.AddVariant( VariantName ) )
			{
				continue;
			}

			VariantSet.SetVariantSelection( VariantName );
			EditContext.Emplace( VariantSet.GetVariantEditContext() );
		}

		pxr::SdfPath MeshPrimPath = ParentPrimPath.AppendPath( pxr::SdfPath( bExportMultipleLODs ? VariantName : UnrealToUsd::ConvertString( *UsdUtils::SanitizeUsdIdentifier( *SkeletalMesh->GetName() ) ).Get() ) );
		pxr::UsdPrim UsdLODPrim = Stage->DefinePrim( MeshPrimPath, UnrealToUsd::ConvertToken( TEXT( "Mesh" ) ).Get() );
		pxr::UsdGeomMesh UsdLODPrimGeomMesh{ UsdLODPrim };

		pxr::UsdPrim MaterialPrim = UsdLODPrim;
		if ( StageForMaterialAssignments )
		{
			pxr::UsdStageRefPtr MaterialStage{ *StageForMaterialAssignments };
			MaterialPrim = MaterialStage->OverridePrim( MeshPrimPath );
		}

		TArray<int32> LODMaterialMap;
		if ( const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo( LODIndex ) )
		{
			LODMaterialMap = LODInfo->LODMaterialMap;
		}

		UnrealToUsdImpl::ConvertSkeletalMeshLOD( SkeletalMesh, LODModel, UsdLODPrimGeomMesh, SkeletalMesh->GetHasVertexColors(), MaterialAssignments, LODMaterialMap, TimeCode, MaterialPrim );

		// Relationships can't target prims inside variants, so if we have BlendShapes to export we have to disable the edit target
		// so that the blend shapes end up outside the variants and the Meshes can have their blendShapeTargets relationships pointing at them
		if ( bExportMultipleLODs && SkeletalMesh->GetMorphTargets().Num() > 0)
		{
			EditContext.Reset();
		}

		pxr::VtArray< pxr::TfToken > AddedBlendShapes;
		pxr::SdfPathVector AddedBlendShapeTargets;
		for ( UMorphTarget* MorphTarget : SkeletalMesh->GetMorphTargets() )
		{
			if ( !MorphTarget || !MorphTarget->HasDataForLOD( LODIndex ) )
			{
				continue;
			}

			int32 NumDeltas = 0;
			FMorphTargetDelta* DeltaArray = MorphTarget->GetMorphTargetDelta(LODIndex, NumDeltas);
			if ( !DeltaArray || NumDeltas == 0 )
			{
				continue;
			}

			pxr::SdfPath ParentPath = bExportMultipleLODs ? SkelRootPrim.GetPath() : UsdLODPrim.GetPath();

			pxr::SdfPath BlendShapePath = ParentPath.AppendPath( UnrealToUsd::ConvertPath( *UsdUtils::SanitizeUsdIdentifier( *MorphTarget->GetName() ) ).Get() );
			pxr::UsdPrim BlendShapePrim = UsdLODPrim.GetStage()->DefinePrim( BlendShapePath, UnrealToUsd::ConvertToken( TEXT( "BlendShape" ) ).Get() );
			pxr::UsdSkelBlendShape BlendShape{ BlendShapePrim };

			bool bCreatedBlendShape = UnrealToUsdImpl::ConvertMorphTargetDeltas(DeltaArray, NumDeltas, BlendShape, TimeCode);
			if ( !bCreatedBlendShape )
			{
				continue;
			}

			AddedBlendShapes.push_back( UnrealToUsd::ConvertToken( *UsdUtils::SanitizeUsdIdentifier( *MorphTarget->GetName() ) ).Get() );
			AddedBlendShapeTargets.push_back( BlendShapePath );
		}

		if ( AddedBlendShapeTargets.size() > 0 )
		{
			// Restore the edit target to the current LOD variant so that the relationship itself ends up inside the mesh, inside the variant
			if ( bExportMultipleLODs )
			{
				EditContext.Emplace( VariantSets.GetVariantSet( UnrealIdentifiers::LOD ).GetVariantEditContext() );
			}

			pxr::UsdSkelBindingAPI LODMeshBindingAPI{ UsdLODPrimGeomMesh };
			LODMeshBindingAPI.CreateBlendShapeTargetsRel().SetTargets( AddedBlendShapeTargets );
			LODMeshBindingAPI.CreateBlendShapesAttr().Set( AddedBlendShapes );
		}
	}

	if ( bExportMultipleLODs )
	{
		VariantSets.GetVariantSet( UnrealIdentifiers::LOD ).SetVariantSelection( LowestLODAdded );
	}

	return true;
}

bool UnrealToUsd::ConvertAnimSequence( UAnimSequence* AnimSequence, pxr::UsdPrim& SkelAnimPrim )
{
	if ( !SkelAnimPrim || !AnimSequence || !AnimSequence->GetSkeleton() )
	{
		return false;
	}

	pxr::UsdSkelAnimation UsdSkelAnim( SkelAnimPrim );

	if ( !UsdSkelAnim )
	{
		return false;
	}

	USkeleton* AnimSkeleton = AnimSequence->GetSkeleton();
	USkeletalMesh* SkeletalMesh = AnimSkeleton->GetAssetPreviewMesh( AnimSequence );

	if ( !SkeletalMesh )
	{
		SkeletalMesh = AnimSkeleton->FindCompatibleMesh();
	}

	if ( !SkeletalMesh )
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	const int32 NumBones = RefSkeleton.GetRefBoneInfo().Num();
	const double TimeCodesPerSecond = SkelAnimPrim.GetStage()->GetTimeCodesPerSecond();
	const int32 NumTimeCodes = AnimSequence->SequenceLength * TimeCodesPerSecond;

	if ( NumBones <= 0 )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;
	pxr::SdfChangeBlock ChangeBlock;

	FUsdStageInfo StageInfo( SkelAnimPrim.GetStage() );

	// Blend shapes
	{
		pxr::VtArray< pxr::TfToken > BlendShapeNames;
		pxr::VtArray< float > BlendShapeWeights;

		const FSmartNameMapping* AnimCurveMapping = AnimSkeleton->GetSmartNameContainer( USkeleton::AnimCurveMappingName );

		if ( AnimCurveMapping )
		{
			TArray< FName > AnimCurveNames;
			AnimCurveMapping->FillNameArray( AnimCurveNames );

			TArray< SmartName::UID_Type > AnimCurveUIDs;
			{
				TArray< FName > UID_ToNameArray;
				AnimCurveMapping->FillUIDToNameArray( UID_ToNameArray );
				AnimCurveUIDs.Reserve( UID_ToNameArray.Num() );
				for ( int32 NameIndex = 0; NameIndex < UID_ToNameArray.Num(); ++NameIndex )
				{
					AnimCurveUIDs.Add( NameIndex );
				}
			}

			// Blend shape names
			for ( const FName& AnimCurveName : AnimCurveNames )
			{
				const FCurveMetaData* CurveMetaData = AnimCurveMapping->GetCurveMetaData( AnimCurveName );

				if ( CurveMetaData && CurveMetaData->Type.bMorphtarget )
				{
					BlendShapeNames.push_back( UnrealToUsd::ConvertToken( *AnimCurveName.ToString() ).Get() );
				}
			}

			// Blend shape weights
			for ( int32 TimeCode = 0; TimeCode < NumTimeCodes; ++TimeCode )
			{
				BlendShapeWeights.clear();
				BlendShapeWeights.reserve( AnimCurveNames.Num() );

				const float AnimTime = TimeCode / TimeCodesPerSecond;

				FBlendedCurve BlendedCurve;
				BlendedCurve.InitFrom( &AnimCurveUIDs );
				const bool bForceUseRawData = true;
				AnimSequence->EvaluateCurveData( BlendedCurve, AnimTime, bForceUseRawData );

				if ( BlendedCurve.IsValid() )
				{
					for ( const FName& AnimCurveName : AnimCurveNames )
					{
						const FCurveMetaData* CurveMetaData = AnimCurveMapping->GetCurveMetaData( AnimCurveName );

						if ( CurveMetaData && CurveMetaData->Type.bMorphtarget )
						{
							SmartName::UID_Type NameUID = AnimSkeleton->GetUIDByName( USkeleton::AnimCurveMappingName, AnimCurveName );
							if ( NameUID != SmartName::MaxUID )
							{
								BlendShapeWeights.push_back( BlendedCurve.Get( NameUID ) );
							}
						}
					}
				};

				UsdSkelAnim.CreateBlendShapeWeightsAttr().Set( BlendShapeWeights, pxr::UsdTimeCode( TimeCode ) );
			}
		}

		if ( !BlendShapeNames.empty() )
		{
			UsdSkelAnim.CreateBlendShapesAttr().Set( BlendShapeNames );
		}
		else
		{
			if ( pxr::UsdAttribute BlendShapesAttr = UsdSkelAnim.GetBlendShapesAttr() )
			{
				BlendShapesAttr.Clear();
			}

			if ( pxr::UsdAttribute BlendShapeWeightsAttr = UsdSkelAnim.GetBlendShapeWeightsAttr() )
			{
				BlendShapeWeightsAttr.Clear();
			}
		}
	}

	// Joints
	{
		UnrealToUsdImpl::SetJoinsAttr( RefSkeleton, UsdSkelAnim.CreateJointsAttr() );
	}

	// Translations, Rotations & Scales
	{
		pxr::UsdAttribute TranslationsAttr = UsdSkelAnim.CreateTranslationsAttr();
		pxr::UsdAttribute RotationsAttr = UsdSkelAnim.CreateRotationsAttr();
		pxr::UsdAttribute ScalesAttr = UsdSkelAnim.CreateScalesAttr();

		UDebugSkelMeshComponent* DebugSkelMeshComponent = NewObject< UDebugSkelMeshComponent >();
		DebugSkelMeshComponent->RegisterComponentWithWorld( GWorld );
		DebugSkelMeshComponent->EmptyOverrideMaterials();
		DebugSkelMeshComponent->SetSkeletalMesh( SkeletalMesh );

		const bool bEnable = true;
		DebugSkelMeshComponent->EnablePreview( bEnable, AnimSequence );

		for ( int32 TimeCode = 0; TimeCode < NumTimeCodes; ++TimeCode )
		{
			const float AnimTime = TimeCode / TimeCodesPerSecond;

			const bool bFireNotifies = false;
			DebugSkelMeshComponent->SetPosition( AnimTime, bFireNotifies );
			DebugSkelMeshComponent->RefreshBoneTransforms();

			pxr::VtVec3fArray Translations;
			Translations.reserve( NumBones );

			pxr::VtQuatfArray Rotations;
			Rotations.reserve( NumBones );

			pxr::VtVec3hArray Scales;
			Scales.reserve( NumBones );

			TArray< FTransform > LocalBoneTransforms = DebugSkelMeshComponent->GetBoneSpaceTransforms();

			for ( int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex )
			{
				FTransform BoneTransform = LocalBoneTransforms[ BoneIndex ];
				BoneTransform = UsdUtils::ConvertAxes( StageInfo.UpAxis == EUsdUpAxis::ZAxis, BoneTransform );

				Translations.push_back( UnrealToUsd::ConvertVector( BoneTransform.GetTranslation() ) );
				Rotations.push_back( UnrealToUsd::ConvertQuat( BoneTransform.GetRotation() ).GetNormalized() );
				Scales.push_back( pxr::GfVec3h( UnrealToUsd::ConvertVector( BoneTransform.GetScale3D() ) ) );
			}

			TranslationsAttr.Set( Translations, pxr::UsdTimeCode( TimeCode ) );
			RotationsAttr.Set( Rotations, pxr::UsdTimeCode( TimeCode ) );
			ScalesAttr.Set( Scales, pxr::UsdTimeCode( TimeCode ) );
		}

		// Actively delete it or else it will remain visible on the viewport
		DebugSkelMeshComponent->DestroyComponent();
	}

	const int32 StageEndTimeCode = SkelAnimPrim.GetStage()->GetEndTimeCode();

	if ( NumTimeCodes > StageEndTimeCode )
	{
		SkelAnimPrim.GetStage()->SetEndTimeCode( NumTimeCodes - 1 );
	}

	return true;
}

void UsdUtils::BindAnimationSource( pxr::UsdPrim& Prim, const pxr::UsdPrim& AnimationSource )
{
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdSkelBindingAPI SkelBindingAPI = pxr::UsdSkelBindingAPI::Apply( Prim );
	SkelBindingAPI.CreateAnimationSourceRel().SetTargets( pxr::SdfPathVector( { AnimationSource.GetPath() }) );
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK && WITH_EDITOR
