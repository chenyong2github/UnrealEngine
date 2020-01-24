// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "RenderUtils.h"
#include "RenderingThread.h"

#if (!UE_BUILD_SHIPPING)

DEFINE_LOG_CATEGORY_STATIC(LogRHIUnitTestCommandlet, Log, All);

#define RUN_TEST(x) do { bool Ret = x; bResult = bResult && Ret; } while (false)

namespace RHIUnitTest
{
	template <typename ValueType>
	static inline FString ClearValueToString(const ValueType& ClearValue)
	{
		if (TAreTypesEqual<ValueType, FVector4>::Value)
		{
			return FString::Printf(TEXT("%f %f %f %f"), ClearValue.X, ClearValue.Y, ClearValue.Z, ClearValue.W);
		}
		else
		{
			return FString::Printf(TEXT("0x%08x 0x%08x 0x%08x 0x%08x"), ClearValue.X, ClearValue.Y, ClearValue.Z, ClearValue.W);
		}
	}

	static inline bool IsZeroMem(const void* Ptr, uint32 Size)
	{
		uint8* Start = (uint8*)Ptr;
		uint8* End = Start + Size;
		while (Start < End)
		{
			if ((*Start++) != 0)
				return false;
		}

		return true;
	}

	// Copies data in the specified vertex buffer back to the CPU, and passes a pointer to that data to the provided verification lambda.
	static bool VerifyBufferContents(const TCHAR* TestName, FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* Buffer, TFunctionRef<bool(void* Ptr, uint32 NumBytes)> VerifyCallback)
	{
		bool Result;
		{
			uint32 NumBytes = Buffer->GetSize();

			FStagingBufferRHIRef StagingBuffer = RHICreateStagingBuffer();
			RHICmdList.CopyToStagingBuffer(Buffer, StagingBuffer, 0, NumBytes);

			// @todo - readback API is inconsistent across RHIs
			RHICmdList.SubmitCommandsAndFlushGPU();
			RHICmdList.BlockUntilGPUIdle();

			void* Memory = RHILockStagingBuffer(StagingBuffer, 0, NumBytes);
			Result = VerifyCallback(Memory, NumBytes);
			RHIUnlockStagingBuffer(StagingBuffer);
		}

		// Immediate flush to clean up the staging buffer / other resources
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

		if (!Result)
		{
			UE_LOG(LogRHIUnitTestCommandlet, Error, TEXT("Test failed. \"%s\""), TestName);
		}
		else
		{
			UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("Test passed. \"%s\""), TestName);
		}

		return Result;
	}

	// Copies data in the specified vertex buffer back to the CPU, and passes a pointer to that data to the provided verification lambda.
	static bool VerifyBufferContents(const TCHAR* TestName, FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* Buffer, TFunctionRef<bool(void* Ptr, uint32 NumBytes)> VerifyCallback)
	{
		bool Result;
		{
			uint32 NumBytes = Buffer->GetSize();

			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);
			void* Memory = RHICmdList.LockStructuredBuffer(Buffer, 0, NumBytes, EResourceLockMode::RLM_ReadOnly);
			Result = VerifyCallback(Memory, NumBytes);
			RHICmdList.UnlockStructuredBuffer(Buffer);
		}

		// Immediate flush to clean up the staging buffer / other resources
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

		if (!Result)
		{
			UE_LOG(LogRHIUnitTestCommandlet, Error, TEXT("Test failed. \"%s\""), TestName);
		}
		else
		{
			UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("Test passed. \"%s\""), TestName);
		}

		return Result;
	}

	template <typename BufferType, typename ValueType, uint32 NumTestBytes>
	static bool RunTest_UAVClear_Buffer(FRHICommandListImmediate& RHICmdList, const FString& TestName, BufferType* BufferRHI, FRHIUnorderedAccessView* UAV, uint32 BufferSize, const ValueType& ClearValue, void(FRHIComputeCommandList::*ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult0, bResult1;

		checkf(BufferSize % NumTestBytes == 0, TEXT("BufferSize must be a multiple of NumTestBytes."));

		// Test clear buffer to zero
		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, UAV);

		ValueType ZerosValue;
		FMemory::Memset(&ZerosValue, 0, sizeof(ZerosValue));
		(RHICmdList.*ClearPtr)(UAV, ZerosValue);

		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, UAV);
		bResult0 = VerifyBufferContents(*FString::Printf(TEXT("%s - clear to zero"), *TestName), RHICmdList, BufferRHI, [&](void* Ptr, uint32 Size)
		{
			check(Size == BufferSize);
			return IsZeroMem(Ptr, Size);
		});

		FString ClearValueStr;
		if (TAreTypesEqual<ValueType, FVector4>::Value)
		{
			ClearValueStr = FString::Printf(TEXT("%f %f %f %f"), ClearValue.X, ClearValue.Y, ClearValue.Z, ClearValue.W);
		}
		else
		{
			ClearValueStr = FString::Printf(TEXT("0x%08x 0x%08x 0x%08x 0x%08x"), ClearValue.X, ClearValue.Y, ClearValue.Z, ClearValue.W);
		}

		// Clear the buffer to the provided value
		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, UAV);
		(RHICmdList.*ClearPtr)(UAV, ClearValue);
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, UAV);
		bResult1 = VerifyBufferContents(*FString::Printf(TEXT("%s - clear to (%s)"), *TestName, *ClearValueStr), RHICmdList, BufferRHI, [&](void* Ptr, uint32 Size)
		{
			check(Size == BufferSize);

			uint32 NumElements = BufferSize / NumTestBytes;

			for (uint32 Index = 0; Index < NumElements; ++Index)
			{
				uint8* Element = ((uint8*)Ptr) + Index * NumTestBytes;
				if (FMemory::Memcmp(Element, TestValue, NumTestBytes) != 0)
					return false;
			}

			return true;
		});

		return bResult0 && bResult1;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool RunTest_UAVClear_VertexBuffer(FRHICommandListImmediate& RHICmdList, uint32 BufferSize, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::*ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{		
		FString TestName = FString::Printf(TEXT("RunTest_UAVClear_VertexBuffer, Format: %s"), GPixelFormats[Format].Name);

		if (!GPixelFormats[Format].Supported)
		{
			UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("Test skipped. \"%s\". Unsupported format."), *TestName);
			return true;
		}

		FRHIResourceCreateInfo Info;
		FVertexBufferRHIRef VertexBuffer = RHICreateVertexBuffer(BufferSize, BUF_ShaderResource | BUF_UnorderedAccess, Info);
		FUnorderedAccessViewRHIRef UAV = RHICreateUnorderedAccessView(VertexBuffer, Format);
		bool bResult = RunTest_UAVClear_Buffer(RHICmdList, TestName, VertexBuffer.GetReference(), UAV, BufferSize, ClearValue, ClearPtr, TestValue);

		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

		return bResult;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool RunTest_UAVClear_StructuredBuffer(FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 BufferSize, const ValueType& ClearValue, void(FRHIComputeCommandList::*ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		check(NumTestBytes == Stride);

		FRHIResourceCreateInfo Info;
		FStructuredBufferRHIRef StructuredBuffer = RHICreateStructuredBuffer(Stride, BufferSize, BUF_ShaderResource | BUF_UnorderedAccess, Info);
		FUnorderedAccessViewRHIRef UAV = RHICreateUnorderedAccessView(StructuredBuffer, false, false);
		bool bResult = RunTest_UAVClear_Buffer(RHICmdList, *FString::Printf(TEXT("RunTest_UAVClear_StructuredBuffer, Stride: %d, Size: %d"), Stride, BufferSize), StructuredBuffer.GetReference(), UAV, BufferSize, ClearValue, ClearPtr, TestValue);

		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

		return bResult;
	}

	static bool Test_RHIClearUAVUint_VertexBuffer(FRHICommandListImmediate& RHICmdList)
	{
		bool bResult = true;

		{
			// Unsigned int tests (values within range of underlying format, so no conversion should happen)
			const FUintVector4 ClearValueUint8(0x01, 0x23, 0x45, 0x67);
			const FUintVector4 ClearValueUint16(0x0123, 0x4567, 0x89ab, 0xcdef);
			const FUintVector4 ClearValueUint32(0x01234567, 0x89abcdef, 0x8899aabb, 0xccddeeff);

			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R8_UINT          , ClearValueUint8 , &FRHICommandListImmediate::ClearUAVUint, { 0x01 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R8G8B8A8_UINT    , ClearValueUint8 , &FRHICommandListImmediate::ClearUAVUint, { 0x01, 0x23, 0x45, 0x67 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16_UINT         , ClearValueUint16, &FRHICommandListImmediate::ClearUAVUint, { 0x23, 0x01 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16G16_UINT      , ClearValueUint16, &FRHICommandListImmediate::ClearUAVUint, { 0x23, 0x01, 0x67, 0x45 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16G16B16A16_UINT, ClearValueUint16, &FRHICommandListImmediate::ClearUAVUint, { 0x23, 0x01, 0x67, 0x45, 0xab, 0x89, 0xef, 0xcd }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32_UINT         , ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32G32_UINT      , ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01, 0xef, 0xcd, 0xab, 0x89 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32G32B32A32_UINT, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01, 0xef, 0xcd, 0xab, 0x89, 0xbb, 0xaa, 0x99, 0x88, 0xff, 0xee, 0xdd, 0xcc }));

			// Signed integer
			const FUintVector4 ClearValueInt16_Positive(0x1122, 0x3344, 0x5566, 0x7788);
			const FUintVector4 ClearValueInt32_Positive(0x10112233, 0x44556677, 0x0899aabb, 0x4cddeeff);
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16_SINT         , ClearValueInt16_Positive, &FRHICommandListImmediate::ClearUAVUint, { 0x22, 0x11 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16G16B16A16_SINT, ClearValueInt16_Positive, &FRHICommandListImmediate::ClearUAVUint, { 0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32_SINT         , ClearValueInt32_Positive, &FRHICommandListImmediate::ClearUAVUint, { 0x33, 0x22, 0x11, 0x10 }));

			const FUintVector4 ClearValueInt16_Negative(0xffff9122, 0xffffb344, 0xffffd566, 0xfffff788);
			const FUintVector4 ClearValueInt32_Negative(0x80112233, 0xc4556677, 0x8899aabb, 0xccddeeff);
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16_SINT         , ClearValueInt16_Negative, &FRHICommandListImmediate::ClearUAVUint, { 0x22, 0x91 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16G16B16A16_SINT, ClearValueInt16_Negative, &FRHICommandListImmediate::ClearUAVUint, { 0x22, 0x91, 0x44, 0xb3, 0x66, 0xd5, 0x88, 0xf7 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32_SINT         , ClearValueInt32_Negative, &FRHICommandListImmediate::ClearUAVUint, { 0x33, 0x22, 0x11, 0x80 }));
		}

		{
			// Clamping unsigned int tests (components of ClearValueUint are > 0xffff, so will be clamped by the format conversion for formats < 32 bits per channel wide).
			const FUintVector4 ClearValueUint(0xeeffccdd, 0xaabb8899, 0x66774455, 0x22330011);
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R8_UINT          , ClearValueUint, &FRHICommandListImmediate::ClearUAVUint, { 0xff }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16_UINT         , ClearValueUint, &FRHICommandListImmediate::ClearUAVUint, { 0xff, 0xff }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16G16_UINT      , ClearValueUint, &FRHICommandListImmediate::ClearUAVUint, { 0xff, 0xff, 0xff, 0xff }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16G16B16A16_UINT, ClearValueUint, &FRHICommandListImmediate::ClearUAVUint, { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R8G8B8A8_UINT    , ClearValueUint, &FRHICommandListImmediate::ClearUAVUint, { 0xff, 0xff, 0xff, 0xff }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32_UINT         , ClearValueUint, &FRHICommandListImmediate::ClearUAVUint, { 0xdd, 0xcc, 0xff, 0xee }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32G32_UINT      , ClearValueUint, &FRHICommandListImmediate::ClearUAVUint, { 0xdd, 0xcc, 0xff, 0xee, 0x99, 0x88, 0xbb, 0xaa }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32G32B32A32_UINT, ClearValueUint, &FRHICommandListImmediate::ClearUAVUint, { 0xdd, 0xcc, 0xff, 0xee, 0x99, 0x88, 0xbb, 0xaa, 0x55, 0x44, 0x77, 0x66, 0x11, 0x00, 0x33, 0x22 }));

			// Signed integer
			const FUintVector4 ClearValueInt16_ClampToMaxInt16(0x8001, 0x8233, 0x8455, 0x8677);
			const FUintVector4 ClearValueInt16_ClampToMinInt16(0xfabc7123, 0x80123456, 0x80203040, 0x8a0b0c0d);
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16_SINT         , ClearValueInt16_ClampToMaxInt16, &FRHICommandListImmediate::ClearUAVUint, { 0xff, 0x7f }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16_SINT         , ClearValueInt16_ClampToMinInt16, &FRHICommandListImmediate::ClearUAVUint, { 0x00, 0x80 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16G16B16A16_SINT, ClearValueInt16_ClampToMaxInt16, &FRHICommandListImmediate::ClearUAVUint, { 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0x7f }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16G16B16A16_SINT, ClearValueInt16_ClampToMinInt16, &FRHICommandListImmediate::ClearUAVUint, { 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80 }));

			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32_SINT         , ClearValueUint,                  &FRHICommandListImmediate::ClearUAVUint, { 0xdd, 0xcc, 0xff, 0xee }));
		}

		return bResult;
	}

	static bool Test_RHIClearUAVFloat_VertexBuffer(FRHICommandListImmediate& RHICmdList)
	{
		bool bResult = true;

		{
			// Float       32-bit     16-bit
			// 0.2345  = 0x3e7020c5 | 0x3381
			// 0.8499  = 0x3f59930c | 0x3acc
			// 0.00145 = 0x3abe0ded | 0x15f0
			// 0.417   = 0x3ed58106 | 0x36ac
			const FVector4 ClearValueFloat(0.2345f, 0.8499f, 0.417f, 0.00145f);

			// Half precision float tests
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16F          , ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0x81, 0x33 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R16F_FILTER   , ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0x81, 0x33 }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_G16R16F       , ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0x81, 0x33, 0xcc, 0x3a }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_G16R16F_FILTER, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0x81, 0x33, 0xcc, 0x3a }));

			// Full precision float tests
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_R32_FLOAT     , ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0xc5, 0x20, 0x70, 0x3e }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_G32R32F       , ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0xc5, 0x20, 0x70, 0x3e, 0x0c, 0x93, 0x59, 0x3f }));
			RUN_TEST(RunTest_UAVClear_VertexBuffer(RHICmdList, 256, PF_A32B32G32R32F , ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0xc5, 0x20, 0x70, 0x3e, 0x0c, 0x93, 0x59, 0x3f, 0x06, 0x81, 0xd5, 0x3e, 0xed, 0x0d, 0xbe, 0x3a }));

			// @todo - 11,11,10 formats etc.
		}

		return bResult;
	}

	static bool Test_RHIClearUAVUint_StructuredBuffer(FRHICommandListImmediate& RHICmdList)
	{
		bool bResult = true;

		// Structured buffer clears should memset the whole resource to (uint32)ClearValue.X, ignoring other channels.
		const FUintVector4 ClearValueUint8(0x01, 0x23, 0x45, 0x67);
		const FUintVector4 ClearValueUint16(0x0123, 0x4567, 0x89ab, 0xcdef);
		const FUintVector4 ClearValueUint32(0x01234567, 0x89abcdef, 0x8899aabb, 0xccddeeff);

		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  4, 256, ClearValueUint8 , &FRHICommandListImmediate::ClearUAVUint, { 0x01, 0x00, 0x00, 0x00 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  8, 256, ClearValueUint8 , &FRHICommandListImmediate::ClearUAVUint, { 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 12, 264, ClearValueUint8 , &FRHICommandListImmediate::ClearUAVUint, { 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 16, 256, ClearValueUint8 , &FRHICommandListImmediate::ClearUAVUint, { 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 }));

		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  4, 256, ClearValueUint16, &FRHICommandListImmediate::ClearUAVUint, { 0x23, 0x01, 0x00, 0x00 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  8, 256, ClearValueUint16, &FRHICommandListImmediate::ClearUAVUint, { 0x23, 0x01, 0x00, 0x00, 0x23, 0x01, 0x00, 0x00 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 12, 264, ClearValueUint16, &FRHICommandListImmediate::ClearUAVUint, { 0x23, 0x01, 0x00, 0x00, 0x23, 0x01, 0x00, 0x00, 0x23, 0x01, 0x00, 0x00 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 16, 256, ClearValueUint16, &FRHICommandListImmediate::ClearUAVUint, { 0x23, 0x01, 0x00, 0x00, 0x23, 0x01, 0x00, 0x00, 0x23, 0x01, 0x00, 0x00, 0x23, 0x01, 0x00, 0x00 }));

		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  4, 256, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  8, 256, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 12, 264, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 16, 256, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01 }));

		// Large stride
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 32, 256, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint,
		{
			0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01,
			0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01, 0x67, 0x45, 0x23, 0x01
		}));

		// Signed integer
		const FUintVector4 ClearValueInt32_Negative(0x80112233, 0xc4556677, 0x8899aabb, 0xccddeeff);
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  4, 256, ClearValueInt32_Negative, &FRHICommandListImmediate::ClearUAVUint, { 0x33, 0x22, 0x11, 0x80 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  8, 256, ClearValueInt32_Negative, &FRHICommandListImmediate::ClearUAVUint, { 0x33, 0x22, 0x11, 0x80, 0x33, 0x22, 0x11, 0x80 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 12, 264, ClearValueInt32_Negative, &FRHICommandListImmediate::ClearUAVUint, { 0x33, 0x22, 0x11, 0x80, 0x33, 0x22, 0x11, 0x80, 0x33, 0x22, 0x11, 0x80 }));
		RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 16, 256, ClearValueInt32_Negative, &FRHICommandListImmediate::ClearUAVUint, { 0x33, 0x22, 0x11, 0x80, 0x33, 0x22, 0x11, 0x80, 0x33, 0x22, 0x11, 0x80, 0x33, 0x22, 0x11, 0x80 }));

		return bResult;
	}

	static bool Test_RHIClearUAVFloat_StructuredBuffer(FRHICommandListImmediate& RHICmdList)
	{
		bool bResult = true;

		{
			// Float       32-bit  
			// 0.2345  = 0x3e7020c5
			// 0.8499  = 0x3f59930c
			// 0.00145 = 0x3abe0ded
			// 0.417   = 0x3ed58106
			const FVector4 ClearValueFloat(0.2345f, 0.8499f, 0.417f, 0.00145f);

			// Full precision float tests
			RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  4, 256, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0xc5, 0x20, 0x70, 0x3e }));
			RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList,  8, 256, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0xc5, 0x20, 0x70, 0x3e, 0xc5, 0x20, 0x70, 0x3e }));
			RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 12, 264, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0xc5, 0x20, 0x70, 0x3e, 0xc5, 0x20, 0x70, 0x3e, 0xc5, 0x20, 0x70, 0x3e }));
			RUN_TEST(RunTest_UAVClear_StructuredBuffer(RHICmdList, 16, 256, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0xc5, 0x20, 0x70, 0x3e, 0xc5, 0x20, 0x70, 0x3e, 0xc5, 0x20, 0x70, 0x3e, 0xc5, 0x20, 0x70, 0x3e }));
		}

		return bResult;
	}

	static bool VerifyTextureContents(const TCHAR* TestName, FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, TFunctionRef<bool(void* Ptr, uint32 MipWidth, uint32 MipHeight, uint32 Width, uint32 Height, uint32 MipIndex, uint32 SliceIndex)> VerifyCallback)
	{
		bool bResult = true;
		check(Texture->GetNumSamples() == 1); // @todo - support multi-sampled textures
		{
			FIntVector Size = Texture->GetSizeXYZ();

			for (uint32 MipIndex = 0; MipIndex < Texture->GetNumMips(); ++MipIndex)
			{
				uint32 MipWidth = FMath::Max(Size.X >> MipIndex, 1);
				uint32 MipHeight = FMath::Max(Size.Y >> MipIndex, 1);
				uint32 MipDepth = FMath::Max(Size.Z >> MipIndex, 1);

				for (uint32 Z = 0; Z < MipDepth; ++Z)
				{
					{
						FRHIResourceCreateInfo CreateInfo;
						FTexture2DRHIRef StagingTexture = RHICreateTexture2D(MipWidth, MipHeight, Texture->GetFormat(), 1, 1, TexCreate_CPUReadback, CreateInfo);

						FRHICopyTextureInfo CopyInfo = {};
						CopyInfo.Size = FIntVector(MipWidth, MipHeight, 1); // @todo - required for D3D11RHI to prevent crash in FD3D11DynamicRHI::RHICopyTexture
						CopyInfo.SourceMipIndex = MipIndex;
						if (Texture->GetTexture3D())
						{
							CopyInfo.SourceSliceIndex = 0;
							CopyInfo.SourcePosition.Z = Z;
						}
						else
						{
							CopyInfo.SourceSliceIndex = Z;
						}
						CopyInfo.NumSlices = 1;
						CopyInfo.NumMips = 1;
						RHICmdList.CopyTexture(Texture, StagingTexture, CopyInfo);

						RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, StagingTexture);

						FGPUFenceRHIRef GPUFence = RHICreateGPUFence(TEXT("ReadbackFence"));
						RHICmdList.WriteGPUFence(GPUFence);

						RHICmdList.SubmitCommandsAndFlushGPU(); // @todo - refactor RHI readback API. This shouldn't be necessary
						RHICmdList.BlockUntilGPUIdle();

						int32 Width, Height;
						void* Ptr;
						RHICmdList.MapStagingSurface(StagingTexture, GPUFence, Ptr, Width, Height);

						if (!VerifyCallback(Ptr, MipWidth, MipHeight, Width, Height, MipIndex, Z))
						{
							UE_LOG(LogRHIUnitTestCommandlet, Error, TEXT("Test failed. \"%s\" - Mip %d, Slice %d"), TestName, MipIndex, Z);;
							bResult = false;
						}

						RHICmdList.UnmapStagingSurface(StagingTexture);
					}
					RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);
				}
			}
		}
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

		if (bResult)
		{
			UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("Test passed. \"%s\""), TestName);
		}

		return bResult;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool RunTest_UAVClear_Texture(FRHICommandListImmediate& RHICmdList, const FString& TestName, FRHITexture* TextureRHI, uint32 MipIndex, const ValueType& ClearValue, void(FRHIComputeCommandList::*ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult0, bResult1;
		{
			// Test clear whole resource to zero
			for (uint32 Mip = 0; Mip < TextureRHI->GetNumMips(); ++Mip)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, TextureRHI);
				FUnorderedAccessViewRHIRef MipUAV = RHICreateUnorderedAccessView(TextureRHI, Mip);

				ValueType ZerosValue;
				FMemory::Memset(&ZerosValue, 0, sizeof(ZerosValue));
				(RHICmdList.*ClearPtr)(MipUAV, ZerosValue);
			}
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, TextureRHI);

			auto VerifyMip = [&](void* Ptr, uint32 MipWidth, uint32 MipHeight, uint32 Width, uint32 Height, uint32 CurrentMipIndex, uint32 CurrentSliceIndex, bool bShouldBeZero)
			{
				uint32 BytesPerPixel = GPixelFormats[TextureRHI->GetFormat()].BlockBytes;
				uint32 NumBytes = Width * Height * BytesPerPixel;
				check(NumBytes % NumTestBytes == 0);

				// This is the specific mip we're targeting.
				// Verify the mip entirely matches the clear value.

				for (uint32 Y = 0; Y < MipHeight; ++Y)
				{
					uint8* Row = ((uint8*)Ptr) + (Y * Width * BytesPerPixel);

					// Verify row within mip stride bounds matches the expected clear value
					for (uint32 X = 0; X < MipWidth; ++X)
					{
						uint8* Pixel = Row + X * BytesPerPixel;

						if (bShouldBeZero)
						{
							if (!IsZeroMem(Pixel, NumTestBytes))
								return false;
						}
						else
						{
							if (FMemory::Memcmp(Pixel, TestValue, NumTestBytes) != 0)
								return false;
						}
					}
				}

				return true;
			};

			auto VerifyMipIsZero = [&](void* Ptr, uint32 MipWidth, uint32 MipHeight, uint32 Width, uint32 Height, uint32 CurrentMipIndex, uint32 CurrentSliceIndex)
			{
				return VerifyMip(Ptr, MipWidth, MipHeight, Width, Height, CurrentMipIndex, CurrentSliceIndex, true);
			};
			bResult0 = VerifyTextureContents(*FString::Printf(TEXT("%s - clear whole resource to zero"), *TestName), RHICmdList, TextureRHI, VerifyMipIsZero);

			// Clear the selected mip index to the provided value
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, TextureRHI);
			FUnorderedAccessViewRHIRef SpecificMipUAV = RHICreateUnorderedAccessView(TextureRHI, MipIndex);
			(RHICmdList.*ClearPtr)(SpecificMipUAV, ClearValue);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, TextureRHI);
			bResult1 = VerifyTextureContents(*FString::Printf(TEXT("%s - clear mip %d to (%s)"), *TestName, MipIndex, *ClearValueToString(ClearValue)), RHICmdList, TextureRHI, 
				[&](void* Ptr, uint32 MipWidth, uint32 MipHeight, uint32 Width, uint32 Height, uint32 CurrentMipIndex, uint32 CurrentSliceIndex)
			{
				return VerifyMip(Ptr, MipWidth, MipHeight, Width, Height, CurrentMipIndex, CurrentSliceIndex, CurrentMipIndex != MipIndex);
			});
		}

		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

		return bResult0 && bResult1;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool Test_RHIClearUAV_Texture2D(FRHICommandListImmediate& RHICmdList, uint32 NumMips, uint32 NumSlices, uint32 Width, uint32 Height, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::*ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult = true;
		FString TestName = FString::Printf(TEXT("Test_RHIClearUAV_Texture2D (%dx%d, %d Slice(s), %d Mip(s)) - %s"), Width, Height, NumMips, NumSlices, *ClearValueToString(ClearValue));

		{
			FRHIResourceCreateInfo CreateInfo;
			FTextureRHIRef Texture;
			if (NumSlices == 1)
			{
				Texture = RHICreateTexture2D(Width, Height, Format, NumMips, 1, TexCreate_UAV | TexCreate_ShaderResource, CreateInfo);
			}
			else
			{
				Texture = RHICreateTexture2DArray(Width, Height, NumSlices, Format, NumMips, 1, TexCreate_UAV | TexCreate_ShaderResource, CreateInfo);
			}

			for (uint32 Mip = 0; Mip < NumMips; ++Mip)
			{
				RUN_TEST(RunTest_UAVClear_Texture(RHICmdList, *TestName, Texture.GetReference(), Mip, ClearValue, ClearPtr, TestValue));
			}
		}
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

		return bResult;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool Test_RHIClearUAV_Texture2D(FRHICommandListImmediate& RHICmdList, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::*ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult = true;

		// Single Mip, Square
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 1, 1, 32, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 1, 4, 32, 32, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, Square
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 1, 32, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 4, 32, 32, Format, ClearValue, ClearPtr, TestValue));

		// Single Mip, pow2 Rectangle
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 1, 1, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 1, 1, 32, 16, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 1, 4, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 1, 4, 32, 16, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, pow2 Rectangle
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 1, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 1, 32, 16, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 4, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 4, 32, 16, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, Odd-sized
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 1, 17, 23, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 1, 23, 17, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 4, 17, 23, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, 4, 4, 23, 17, Format, ClearValue, ClearPtr, TestValue));

		return bResult;
	}

	static bool Test_RHIClearUAV_Texture2D(FRHICommandListImmediate& RHICmdList)
	{
		bool bResult = true;

		// Float       32-bit     16-bit
		// 0.2345  = 0x3e7020c5 | 0x3381
		// 0.8499  = 0x3f59930c | 0x3acc
		// 0.00145 = 0x3abe0ded | 0x15f0
		// 0.417   = 0x3ed58106 | 0x36ac
		const FVector4 ClearValueFloat(0.2345f, 0.8499f, 0.417f, 0.00145f);
		const FUintVector4 ClearValueUint32(0x01234567, 0x89abcdef, 0x8899aabb, 0xccddeeff);

		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, PF_FloatRGBA, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0x81, 0x33, 0xcc, 0x3a, 0xac, 0x36, 0xf0, 0x15 }));
		RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList, PF_R32_UINT, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01 }));

		return bResult;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool Test_RHIClearUAV_Texture3D(FRHICommandListImmediate& RHICmdList, uint32 NumMips, uint32 Width, uint32 Height, uint32 Depth, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::*ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		FString TestName = FString::Printf(TEXT("Test_RHIClearUAVUint_Texture3D (%dx%dx%d, %d Mip(s))"), Width, Height, Depth, NumMips);

		bool bResult = true;

		{
			FRHIResourceCreateInfo CreateInfo;
			FTexture3DRHIRef Texture = RHICreateTexture3D(Width, Height, Depth, Format, NumMips, TexCreate_UAV | TexCreate_ShaderResource, CreateInfo);

			for (uint32 Mip = 0; Mip < NumMips; ++Mip)
			{
				RUN_TEST(RunTest_UAVClear_Texture(RHICmdList, *TestName, Texture.GetReference(), Mip, ClearValue, ClearPtr, TestValue));
			}
		}
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResourcesFlushDeferredDeletes);

		return bResult;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool Test_RHIClearUAV_Texture3D(FRHICommandListImmediate& RHICmdList, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::*ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult = true;

		// Single Mip, Cube
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 1, 32, 32, 32, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, Cube
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 4, 32, 32, 32, Format, ClearValue, ClearPtr, TestValue));

		// Single Mip, pow2 Cuboid
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 1, 16, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 1, 16, 32, 16, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 1, 32, 16, 16, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, pow2 Cuboid
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 4, 16, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 4, 16, 32, 16, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 4, 32, 16, 16, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, Odd-sized cuboid
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 4, 17, 23, 29, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 4, 29, 17, 23, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, 4, 23, 29, 17, Format, ClearValue, ClearPtr, TestValue));

		return bResult;
	}

	static bool Test_RHIClearUAV_Texture3D(FRHICommandListImmediate& RHICmdList)
	{
		bool bResult = true;

		// Float       32-bit     16-bit
		// 0.2345  = 0x3e7020c5 | 0x3381
		// 0.8499  = 0x3f59930c | 0x3acc
		// 0.00145 = 0x3abe0ded | 0x15f0
		// 0.417   = 0x3ed58106 | 0x36ac
		const FVector4 ClearValueFloat(0.2345f, 0.8499f, 0.417f, 0.00145f);
		const FUintVector4 ClearValueUint32(0x01234567, 0x89abcdef, 0x8899aabb, 0xccddeeff);

		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, PF_FloatRGBA, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0x81, 0x33, 0xcc, 0x3a, 0xac, 0x36, 0xf0, 0x15 }));
		RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList, PF_R32_UINT, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01 }));

		return bResult;
	}

	static bool RunTests_RenderThread(FRHICommandListImmediate& RHICmdList)
	{
		bool bResult = true;

		// ------------------------------------------------
		// RHIClearUAVUint / RHIClearUAVFloat tests
		// ------------------------------------------------

		// Vertex/Structured Buffer
		{
			RUN_TEST(Test_RHIClearUAVUint_VertexBuffer(RHICmdList));
			RUN_TEST(Test_RHIClearUAVFloat_VertexBuffer(RHICmdList));

			RUN_TEST(Test_RHIClearUAVUint_StructuredBuffer(RHICmdList));
			RUN_TEST(Test_RHIClearUAVFloat_StructuredBuffer(RHICmdList));
		}

		// Texture2D/3D
		{
			RUN_TEST(Test_RHIClearUAV_Texture2D(RHICmdList));
			RUN_TEST(Test_RHIClearUAV_Texture3D(RHICmdList));
		}

		// @todo - add more tests
		return bResult;
	}
}

ENGINE_API void RunRHIUnitTest()
{
	bool bResult = false;

	// Enqueue a single rendering command to hand control of the tests over to the rendering thread.
	ENQUEUE_RENDER_COMMAND(RunRHIUnitTestsCommand)([&](FRHICommandListImmediate& RHICmdList)
	{
		bResult = RHIUnitTest::RunTests_RenderThread(RHICmdList);
	});

	// Flush to wait for the above rendering command to complete.
	FlushRenderingCommands(true);

	if (bResult)
	{
		UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("RHI unit tested completed. All tests passed."));
	}
	else
	{
		UE_LOG(LogRHIUnitTestCommandlet, Error, TEXT("RHI unit tested completed. At least one test failed."));
	}
}

#endif // (!UE_BUILD_SHIPPING)
