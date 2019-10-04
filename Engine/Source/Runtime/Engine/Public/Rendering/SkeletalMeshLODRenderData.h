// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "Rendering/SkeletalMeshVertexBuffer.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Rendering/SkeletalMeshDuplicatedVerticesBuffer.h"
#include "Rendering/SkeletalMeshVertexClothBuffer.h"
#include "Rendering/MorphTargetVertexInfoBuffers.h"
#include "SkeletalMeshTypes.h"
#include "BoneIndices.h"
#include "StaticMeshResources.h"
#include "GPUSkinVertexFactory.h"
#include "Animation/SkinWeightProfile.h"

#if WITH_EDITOR
class FSkeletalMeshLODModel;
#endif // WITH_EDITOR

struct FSkelMeshRenderSection
{
	/** Material (texture) used for this section. */
	uint16 MaterialIndex;

	/** The offset of this section's indices in the LOD's index buffer. */
	uint32 BaseIndex;

	/** The number of triangles in this section. */
	uint32 NumTriangles;

	/** This section will recompute tangent in runtime */
	bool bRecomputeTangent;

	/** This section will cast shadow */
	bool bCastShadow;

	/** The offset into the LOD's vertex buffer of this section's vertices. */
	uint32 BaseVertexIndex;

	/** The extra vertex data for mapping to an APEX clothing simulation mesh. */
	TArray<FMeshToMeshVertData> ClothMappingData;


	/** The bones which are used by the vertices of this section. Indices of bones in the USkeletalMesh::RefSkeleton array */
	TArray<FBoneIndexType> BoneMap;

	/** The number of triangles in this section. */
	uint32 NumVertices;

	/** max # of bones used to skin the vertices in this section */
	int32 MaxBoneInfluences;

	// INDEX_NONE if not set
	int16 CorrespondClothAssetIndex;

	/** Clothing data for this section, clothing is only present if ClothingData.IsValid() returns true */
	FClothingSectionData ClothingData;

	/** Index Buffer containting all duplicated vertices in the section and a buffer containing which indices into the index buffer are relevant per vertex **/
	FDuplicatedVerticesBuffer DuplicatedVerticesBuffer;

	/** Disabled sections will not be collected when rendering, controlled from the source section in the skeletal mesh asset */
	bool bDisabled;

	FSkelMeshRenderSection()
		: MaterialIndex(0)
		, BaseIndex(0)
		, NumTriangles(0)
		, bRecomputeTangent(false)
		, bCastShadow(true)
		, BaseVertexIndex(0)
		, NumVertices(0)
		, MaxBoneInfluences(4)
		, CorrespondClothAssetIndex(-1)
		, bDisabled(false)
	{}

	FORCEINLINE bool HasClothingData() const
	{
		return (ClothMappingData.Num() > 0);
	}

	FORCEINLINE int32 GetVertexBufferIndex() const
	{
		return BaseVertexIndex;
	}

	FORCEINLINE int32 GetNumVertices() const
	{
		return NumVertices;
	}

	friend FArchive& operator<<(FArchive& Ar, FSkelMeshRenderSection& S);
};

class FSkeletalMeshLODRenderData
{
public:

	/** Info about each section of this LOD for rendering */
	TArray<FSkelMeshRenderSection>	RenderSections;

	// Index Buffer (MultiSize: 16bit or 32bit)
	FMultiSizeIndexContainer	MultiSizeIndexContainer;

	/** Resources needed to render the model using PN-AEN */
	FMultiSizeIndexContainer	AdjacencyMultiSizeIndexContainer;

	/** static vertices from chunks for skinning on GPU */
	FStaticMeshVertexBuffers	StaticVertexBuffers;

	/** Skin weights for skinning */
	FSkinWeightVertexBuffer		SkinWeightVertexBuffer;

	/** A buffer for cloth mesh-mesh mapping */
	FSkeletalMeshVertexClothBuffer	ClothVertexBuffer;

	/** GPU friendly access data for MorphTargets for an LOD */
	FMorphTargetVertexInfoBuffers	MorphTargetVertexInfoBuffers;

	/** Skin weight profile data structures, can contain multiple profiles and their runtime FSkinWeightVertexBuffer */
	FSkinWeightProfilesData SkinWeightProfilesData;

	TArray<FBoneIndexType> ActiveBoneIndices;

	TArray<FBoneIndexType> RequiredBones;

	uint32 BuffersSize;

#if USE_BULKDATA_STREAMING_TOKEN
	FBulkDataStreamingToken BulkDataStreamingToken;
#else
	FByteBulkData StreamingBulkData;
#endif

	/** Whether buffers of this LOD is inlined (i.e. stored in .uexp instead of .ubulk) */
	uint32 bStreamedDataInlined : 1;
	/** Whether this LOD is below MinLod */
	uint32 bIsLODOptional : 1;

#if WITH_EDITOR
	FByteBulkData BulkData;
#endif

	/**
	* Initialize the LOD's render resources.
	*
	* @param Parent Parent mesh
	*/
	void InitResources(bool bNeedsVertexColors, int32 LODIndex, TArray<class UMorphTarget*>& InMorphTargets, USkeletalMesh* Owner);

	/**
	* Releases the LOD's render resources.
	*/
	ENGINE_API void ReleaseResources();

	/**
	* Releases the LOD's CPU render resources.
	*/
	void ReleaseCPUResources(bool bForStreaming = false);

	/** Constructor (default) */
	FSkeletalMeshLODRenderData()
		: BuffersSize(0)
		, bStreamedDataInlined(false)
		, bIsLODOptional(false)
	{
	}

	/**
	* Special serialize function passing the owning UObject along as required by FUnytpedBulkData
	* serialization.
	*
	* @param	Ar		Archive to serialize with
	* @param	Owner	UObject this structure is serialized within
	* @param	Idx		Index of current array entry being serialized
	*/
	void Serialize(FArchive& Ar, UObject* Owner, int32 Idx);

	/**
	* Serialize the portion of data that might be streamed
	* @param bNeedsCPUAcces - Whether something requires keeping CPU copy of resources (see ShouldKeepCPUResources)
	* @param bForceKeepCPUResources - Whether we want to force keeping CPU resources
	* @return Size of streamed LOD data in bytes
	*/
	void SerializeStreamedData(FArchive& Ar, USkeletalMesh* Owner, int32 LODIdx, uint8 ClassStripFlags, bool bNeedsCPUAccess, bool bForceKeepCPUResources);

	void SerializeAvailabilityInfo(FArchive& Ar, USkeletalMesh* Owner, int32 LODIdx, bool bHasAdjacencyData, bool bNeedsCPUAccess);

#if WITH_EDITOR
	/**
	 * Initialize render data (e.g. vertex buffers) from model info
	 * @param BuildFlags See ESkeletalMeshVertexFlags.
	 */
	void BuildFromLODModel(const FSkeletalMeshLODModel* LODModel, uint32 BuildFlags);
#endif // WITH_EDITOR

	uint32 GetNumVertices() const
	{
		return StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
	}

	bool DoesVertexBufferHaveExtraBoneInfluences() const
	{
		return SkinWeightVertexBuffer.HasExtraBoneInfluences();
	}

	uint32 GetNumTexCoords() const
	{
		return StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
	}

	/** Checks whether or not the skin weight buffer has been overridden 'by default' and if not return the original Skin Weight buffer */
	FSkinWeightVertexBuffer* GetSkinWeightVertexBuffer() 
	{
		FSkinWeightVertexBuffer* OverrideBuffer = SkinWeightProfilesData.GetDefaultOverrideBuffer();
		return OverrideBuffer != nullptr ? OverrideBuffer : &SkinWeightVertexBuffer;
	}
	
	/** Checks whether or not the skin weight buffer has been overridden 'by default' and if not return the original Skin Weight buffer */
	const FSkinWeightVertexBuffer* GetSkinWeightVertexBuffer() const
	{
		FSkinWeightVertexBuffer* OverrideBuffer = SkinWeightProfilesData.GetDefaultOverrideBuffer();
		return OverrideBuffer != nullptr ? OverrideBuffer : &SkinWeightVertexBuffer;
	}

	/** Utility function for returning total number of faces in this LOD. */
	ENGINE_API int32 GetTotalFaces() const;

	/**
	* @return true if any chunks have cloth data.
	*/
	ENGINE_API bool HasClothData() const;

	/** Utility for finding the section that a particular vertex is in. */
	ENGINE_API void GetSectionFromVertexIndex(int32 InVertIndex, int32& OutSectionIndex, int32& OutVertIndex) const;

	/**
	* Get Resource Size
	*/
	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const;

	// O(1)
	// @return -1 if not found
	uint32 FindSectionIndex(const FSkelMeshRenderSection& Section) const;

	ENGINE_API int32 NumNonClothingSections() const;

	void IncrementMemoryStats(bool bNeedsVertexColors);
	void DecrementMemoryStats();

	static bool ShouldForceKeepCPUResources();
	static bool ShouldKeepCPUResources(const USkeletalMesh* SkeletalMesh, int32 LODIdx, bool bForceKeep);

private:
	enum EClassDataStripFlag : uint8
	{
		CDSF_AdjacencyData = 1,
		CDSF_MinLodData = 2
	};

	static int32 GetPlatformMinLODIdx(const ITargetPlatform* TargetPlatform, const USkeletalMesh* SkeletalMesh);

	static uint8 GenerateClassStripFlags(FArchive& Ar, const USkeletalMesh* OwnerMesh, int32 LODIdx);

	static bool IsLODCookedOut(const ITargetPlatform* TargetPlatform, const USkeletalMesh* SkeletalMesh, bool bIsBelowMinLOD);

	static bool IsLODInlined(const ITargetPlatform* TargetPlatform, const USkeletalMesh* SkeletalMesh, int32 LODIdx, bool bIsBelowMinLOD);

	static int32 GetNumOptionalLODsAllowed(const ITargetPlatform* TargetPlatform, const USkeletalMesh* SkeletalMesh);

	friend class FSkeletalMeshRenderData;
};