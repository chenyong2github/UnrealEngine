//  Copyright Epic Games, Inc. All Rights Reserved.

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// Include header shared between this Metal shader code and C code executing Metal API commands.
#import "PassthroughShaderTypes.h"

// Vertex shader outputs and fragment shader inputs
typedef struct
{
    float4 position [[position]];

    float2 textureCoord;

} RasterizerData;

vertex RasterizerData
passthrough_vertex(uint vertexID [[vertex_id]],
             constant PassthroughVertex *vertices [[buffer(PassthroughVertexInputIndexVertices)]],
             constant PassthroughVertexUniforms & uniforms [[buffer(PassthroughVertexInputIndexUniforms)]])
{
    RasterizerData out;

    // To convert from positions in pixel space to positions in clip-space,
    //  divide the pixel coordinates by half the size of the viewport.
    out.position.xy = vertices[vertexID].position.xy * ((uniforms.aspectRatio > 1.0) ? float2(1.0, 1.0 / uniforms.aspectRatio) : float2( uniforms.aspectRatio, 1.0));
    out.position.z = 0;
    out.position.w = 1;

    // Pass the input color directly to the rasterizer.
    out.textureCoord = vertices[vertexID].textureCoord;

    return out;
}

fragment float4 basic_texture(RasterizerData in [[stage_in]],
                              texture2d<half> colorTexture [[ texture(BasicTextureInputIndexBaseColor) ]],
                              constant BasicTextureUniforms & uniforms [[buffer(BasicTextureInputIndexUniforms)]])
{
    constexpr sampler textureSampler (mag_filter::linear,
                                      min_filter::linear);

    float2 texcoord = in.textureCoord;
    if (uniforms.flipY) {
        texcoord.y = 1.0 - texcoord.y;
    }
    
    // Sample the texture to obtain a color
    half4 colorSample = colorTexture.sample(textureSampler, texcoord);

    // return the color of the texture
    return pow(float4(uniforms.isARGB ? colorSample.gbar : colorSample), 2.2);
}

