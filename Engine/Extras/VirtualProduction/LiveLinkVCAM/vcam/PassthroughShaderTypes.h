//  Copyright Epic Games, Inc. All Rights Reserved.

#ifndef AAPLShaderTypes_h
#define AAPLShaderTypes_h

#include <simd/simd.h>

// Buffer index values shared between shader and C code to ensure Metal shader buffer inputs
// match Metal API buffer set calls.
typedef enum PassthroughVertexInputIndex
{
    PassthroughVertexInputIndexVertices     = 0,
    PassthroughVertexInputIndexUniforms
} PassthroughVertexInputIndex;

struct PassthroughVertexUniforms {
    float aspectRatio;
};

// Texture index values shared between shader and C code to ensure Metal shader buffer inputs match
//   Metal API texture set calls
typedef enum BasicTextureInputIndex
{
    BasicTextureInputIndexBaseColor = 0,
    BasicTextureInputIndexUniforms
} BasicTextureIndex;

struct BasicTextureUniforms {
    
    int isARGB;
    int flipY;
};



//  This structure defines the layout of vertices sent to the vertex
//  shader. This header is shared between the .metal shader and C code, to guarantee that
//  the layout of the vertex array in the C code matches the layout that the .metal
//  vertex shader expects.
typedef struct
{
    vector_float2 position;
    vector_float2 textureCoord;
} PassthroughVertex;

#endif /* AAPLShaderTypes_h */
