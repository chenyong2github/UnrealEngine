// Copyright Epic Games, Inc. All Rights Reserved.

//#pragma once

#include "ShaderParameterUtils.h"
#include "RenderGraphUtils.h"

// HLSL runtime and DirectML runtime use different shader parameter access patterns
#ifdef NNXRT_RDG_DML
	#define NNXRT_RDG_BUFFER_SRV(View, BufferName) RDG_BUFFER_ACCESS(BufferName, ERHIAccess::UAVCompute)
	#define NNXRT_RDG_BUFFER_UAV(View, BufferName) RDG_BUFFER_ACCESS(BufferName, ERHIAccess::UAVCompute)
#else
	#define NNXRT_RDG_BUFFER_SRV(View, BufferName) SHADER_PARAMETER_RDG_BUFFER_SRV(View, BufferName)
	#define NNXRT_RDG_BUFFER_UAV(View, BufferName) SHADER_PARAMETER_RDG_BUFFER_UAV(View, BufferName)
#endif

#ifdef NNXRT_RDG_DML
	#define NNXRT_BEGIN_SHADER_PARAMETER_STRUCT(Name) BEGIN_SHADER_PARAMETER_STRUCT(Name, )
#else
	#define NNXRT_BEGIN_SHADER_PARAMETER_STRUCT(Name) BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
#endif

#define NNXRT_END_SHADER_PARAMETER_STRUCT END_SHADER_PARAMETER_STRUCT

//Unary element wise operators
#define NNXRT_ELEMENTWISEUNARY_PARAMETER_STRUCT() \
	NNXRT_BEGIN_SHADER_PARAMETER_STRUCT(FMLElementWiseUnaryParameters) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, Input) \
		NNXRT_RDG_BUFFER_UAV(RWBuffer<float>, Output) \
		SHADER_PARAMETER(uint32, Num) \
		SHADER_PARAMETER(uint32, ThreadCountX) \
		SHADER_PARAMETER(float, Alpha) \
		SHADER_PARAMETER(float, Beta) \
		SHADER_PARAMETER(float, Gamma) \
	NNXRT_END_SHADER_PARAMETER_STRUCT()

#ifdef NNXRT_RDG_DML
NNXRT_ELEMENTWISEUNARY_PARAMETER_STRUCT()
#endif

//Variadic element wise operators
#define NNXRT_ELEMENTWISEVARIADIC_PARAMETER_STRUCT() \
	NNXRT_BEGIN_SHADER_PARAMETER_STRUCT(FMLElementWiseVariadicParameters) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, Input0) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, Input1) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, Input2) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, Input3) \
		NNXRT_RDG_BUFFER_UAV(RWBuffer<float>, Output) \
		SHADER_PARAMETER(FUint32Vector4, Input0Info0) \
		SHADER_PARAMETER(FUint32Vector4, Input0Info1) \
		SHADER_PARAMETER(FUint32Vector4, Input1Info0) \
		SHADER_PARAMETER(FUint32Vector4, Input1Info1) \
		SHADER_PARAMETER(FUint32Vector4, Input2Info0) \
		SHADER_PARAMETER(FUint32Vector4, Input2Info1) \
		SHADER_PARAMETER(FUint32Vector4, Input3Info0) \
		SHADER_PARAMETER(FUint32Vector4, Input3Info1) \
		SHADER_PARAMETER(FUint32Vector4, OutInfo0) \
		SHADER_PARAMETER(FUint32Vector4, OutInfo1) \
		SHADER_PARAMETER(uint32, OutRank) \
		SHADER_PARAMETER(uint32, Num) \
		SHADER_PARAMETER(uint32, ThreadCountX) \
		SHADER_PARAMETER(float, Scale) \
	NNXRT_END_SHADER_PARAMETER_STRUCT()

#ifdef NNXRT_RDG_DML
NNXRT_ELEMENTWISEVARIADIC_PARAMETER_STRUCT()
#endif

//Binary element wise operators
#define NNXRT_ELEMENTWISEBINARY_PARAMETER_STRUCT() \
	NNXRT_BEGIN_SHADER_PARAMETER_STRUCT(FMLElementWiseBinaryParameters) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, LHSInput) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, RHSInput) \
		NNXRT_RDG_BUFFER_UAV(RWBuffer<float>, Output) \
		SHADER_PARAMETER(FUint32Vector4, LHSInfo0) \
		SHADER_PARAMETER(FUint32Vector4, LHSInfo1) \
		SHADER_PARAMETER(FUint32Vector4, RHSInfo0) \
		SHADER_PARAMETER(FUint32Vector4, RHSInfo1) \
		SHADER_PARAMETER(FUint32Vector4, OutInfo0) \
		SHADER_PARAMETER(FUint32Vector4, OutInfo1) \
		SHADER_PARAMETER(uint32, OutRank) \
		SHADER_PARAMETER(uint32, Num) \
		SHADER_PARAMETER(uint32, ThreadCountX) \
	NNXRT_END_SHADER_PARAMETER_STRUCT()

#ifdef NNXRT_RDG_DML
NNXRT_ELEMENTWISEBINARY_PARAMETER_STRUCT()
#endif

//Gemm
#define NNXRT_GEMM_MAX_NUM_STACK_DIMENSIONS 8

#define NNXRT_GEMM_PARAMETER_STRUCT() \
	NNXRT_BEGIN_SHADER_PARAMETER_STRUCT(FMLGemmParameters) \
		SHADER_PARAMETER(float, Alpha) \
		SHADER_PARAMETER(float, Beta) \
		SHADER_PARAMETER(int32, TransA) \
		SHADER_PARAMETER(int32, TransB) \
		SHADER_PARAMETER(uint32, M) \
		SHADER_PARAMETER(uint32, N) \
		SHADER_PARAMETER(uint32, K) \
		SHADER_PARAMETER(uint32, MxK) \
		SHADER_PARAMETER(uint32, KxN) \
		SHADER_PARAMETER(uint32, MxN) \
		SHADER_PARAMETER(uint32, CWidth) \
		SHADER_PARAMETER(uint32, CHeight) \
		SHADER_PARAMETER(float, CScalar) \
		SHADER_PARAMETER_ARRAY(FUint32Vector4, StackShapeA_StackShapeB_StackStrideA_StackStrideB, [NNXRT_GEMM_MAX_NUM_STACK_DIMENSIONS]) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, A) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, B) \
		NNXRT_RDG_BUFFER_SRV(Buffer<float>, C) \
		NNXRT_RDG_BUFFER_UAV(RWBuffer<float>, Y) \
	NNXRT_END_SHADER_PARAMETER_STRUCT()

#ifdef NNXRT_RDG_DML
NNXRT_GEMM_PARAMETER_STRUCT()
#endif
