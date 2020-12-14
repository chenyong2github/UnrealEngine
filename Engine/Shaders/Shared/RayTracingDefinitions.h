// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================================
	RayTracingDefinitions.ush: used in ray tracing shaders and C++ code to define common constants
	!!! Changing this file requires recompilation of the engine !!!
=================================================================================================*/

#pragma once

// Change this to force recompilation of all ray tracing shaders (use https://www.random.org/cgi-bin/randbyte?nbytes=4&format=h)
// This avoids changing the global ShaderVersion.ush and forcing recompilation of all shaders in the engine (only RT shaders will be affected)
#define RAY_TRACING_SHADER_VERSION 0x7034a1ef 

#define RAY_TRACING_REGISTER_SPACE_LOCAL  0 // default register space for hit group (closest hit, any hit, intersection) shader resources
#define RAY_TRACING_REGISTER_SPACE_GLOBAL 1 // register space for ray generation and miss shaders 
#define RAY_TRACING_REGISTER_SPACE_SYSTEM 2 // register space for "system" parameters (index buffer, vertex buffer, fetch parameters)

#define RAY_TRACING_MASK_OPAQUE						0x01    // Opaque and alpha tested meshes and particles (e.g. used by reflection, shadow, AO and GI tracing passes)
#define RAY_TRACING_MASK_TRANSLUCENT				0x02    // Opaque and alpha tested meshes and particles (e.g. used by translucency tracing pass)
#define RAY_TRACING_MASK_THIN_SHADOW				0x04    // Whether the thin geometry (e.g. hair) is visible for shadow rays
#define RAY_TRACING_MASK_SHADOW						0x08    // Whether the geometry is visible for shadow rays
#define RAY_TRACING_MASK_ALL						0xFF

#define RAY_TRACING_SHADER_SLOT_MATERIAL	0
#define RAY_TRACING_SHADER_SLOT_SHADOW		1
#define RAY_TRACING_NUM_SHADER_SLOTS		2

#define RAY_TRACING_MISS_SHADER_SLOT_DEFAULT	0 // Main miss shader that simply sets HitT to a "miss" value (see FMinimalPayload::SetMiss())
#define RAY_TRACING_MISS_SHADER_SLOT_LIGHTING	1 // Miss shader that may be used to evaluate light source radiance in ray traced reflections and translucency
#define RAY_TRACING_NUM_MISS_SHADER_SLOTS		2

#define RAY_TRACING_LIGHT_COUNT_MAXIMUM		256

#define RAY_TRACING_MAX_ALLOWED_RECURSION_DEPTH 1   // Only allow ray tracing from RayGen shader
#define RAY_TRACING_MAX_ALLOWED_ATTRIBUTE_SIZE  8   // Sizeof 2 floats (barycentrics)
#define RAY_TRACING_MAX_ALLOWED_PAYLOAD_SIZE    64  // Our maximum allowed payload size (sizeof FPackedMaterialClosestHitPayload)

// Constants for the 'Flags' field of FPathTracingLightData
#define PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK				(7 << 0)	// Which lighting channel is this light assigned to?
#define PATHTRACER_FLAG_TRANSMISSION_MASK					(1 << 3)	// Does the light affect the transmission side?
#define PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK		(1 << 4)	// Does the light have a non-inverse square decay?
#define PATHTRACER_FLAG_STATIONARY_MASK						(1 << 5)	// Only used by GPULightmass
#define PATHTRACER_FLAG_TYPE_MASK							(7 << 6)
#define PATHTRACING_LIGHT_SKY								(0 << 6)
#define PATHTRACING_LIGHT_DIRECTIONAL						(1 << 6)
#define PATHTRACING_LIGHT_POINT								(2 << 6)
#define PATHTRACING_LIGHT_SPOT								(3 << 6)
#define PATHTRACING_LIGHT_RECT								(4 << 6)
