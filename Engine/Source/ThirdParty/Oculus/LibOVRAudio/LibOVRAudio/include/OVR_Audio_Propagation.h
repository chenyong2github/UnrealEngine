/********************************************************************************//**
\file      OVR_Audio_Propagation.h
\brief     OVR Audio SDK public header file
\copyright Copyright (c) Facebook Technologies, LLC and its affiliates. All rights reserved.
************************************************************************************/
#ifndef OVR_Audio_Propagation_h
#define OVR_Audio_Propagation_h

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

  #pragma pack(push, 1) // Make sure all structs are packed (for debugger visualization)

    /***********************************************************************************/
    /* Geometry API */

    typedef struct ovrAudioMaterial_* ovrAudioMaterial;

    typedef enum
    {
        ovrAudioScalarType_Int8,
        ovrAudioScalarType_UInt8,
        ovrAudioScalarType_Int16,
        ovrAudioScalarType_UInt16,
        ovrAudioScalarType_Int32,
        ovrAudioScalarType_UInt32,
        ovrAudioScalarType_Int64,
        ovrAudioScalarType_UInt64,
        ovrAudioScalarType_Float16,
        ovrAudioScalarType_Float32,
        ovrAudioScalarType_Float64,
    } ovrAudioScalarType;

    typedef enum
    {
        ovrAudioFaceType_Triangles = 0,
        ovrAudioFaceType_Quads = 1,
        ovrAudioFaceType_COUNT
    } ovrAudioFaceType;

	  typedef enum
	  {
		  ovrAudioMaterialProperty_Absorption = 0,
		  ovrAudioMaterialProperty_Transmission = 1,
		  ovrAudioMaterialProperty_Scattering = 2,
		  ovrAudioMaterialProperty_COUNT
	  } ovrAudioMaterialProperty;

    typedef struct
    {
        const void* vertices;
        size_t byteOffset; // offset in bytes of 0th vertex within buffer.
        size_t vertexCount;
        size_t vertexStride; // if non-zero, the stride in bytes between consecutive vertices.
        ovrAudioScalarType vertexType;
    } ovrAudioMeshVertices;

    typedef struct
    {
        const void* indices;
        size_t byteOffset; // offset in bytes of 0th index within buffer.
        size_t indexCount; // number of indices
        ovrAudioScalarType indexType;
    } ovrAudioMeshIndices;

    typedef struct
    {
        size_t indexOffset;
        size_t faceCount; // number of faces
        ovrAudioFaceType faceType;
        ovrAudioMaterial material;
    } ovrAudioMeshGroup;

    typedef struct
    {
        ovrAudioMeshVertices vertices;
        ovrAudioMeshIndices indices;
        const ovrAudioMeshGroup* groups;
        size_t groupCount;
    } ovrAudioMesh;

    typedef struct ovrAudioGeometry_* ovrAudioGeometry;

    typedef size_t(*ovrAudioSerializerReadCallback)(void* userData, void* bytes, size_t byteCount);
    typedef size_t(*ovrAudioSerializerWriteCallback)(void* userData, const void* bytes, size_t byteCount);
    typedef int64_t(*ovrAudioSerializerSeekCallback)(void* userData, int64_t seekOffset);
    typedef struct
    {
        ovrAudioSerializerReadCallback read;
        ovrAudioSerializerWriteCallback write;
        ovrAudioSerializerSeekCallback seek;
        void* userData;
    } ovrAudioSerializer;

  #pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif // OVR_Audio_Propagation_h
