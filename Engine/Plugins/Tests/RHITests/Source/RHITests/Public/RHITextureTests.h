// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHITestsCommon.h"

class FRHITextureTests
{
	static bool VerifyTextureContents(const TCHAR* TestName, FRHICommandListImmediate& RHICmdList, FRHITexture* Texture, TFunctionRef<bool(void* Ptr, uint32 MipWidth, uint32 MipHeight, uint32 Width, uint32 Height, uint32 MipIndex, uint32 SliceIndex)> VerifyCallback);
public:
	template <typename ValueType, uint32 NumTestBytes>
	static bool RunTest_UAVClear_Texture(FRHICommandListImmediate& RHICmdList, const FString& TestName, FRHITexture* TextureRHI, uint32 MipIndex, const ValueType& ClearValue, void(FRHIComputeCommandList::* ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult0, bResult1;
		{
			RHICmdList.Transition(FRHITransitionInfo(TextureRHI, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

			// Test clear whole resource to zero
			for (uint32 Mip = 0; Mip < TextureRHI->GetNumMips(); ++Mip)
			{
				FUnorderedAccessViewRHIRef MipUAV = RHICreateUnorderedAccessView(TextureRHI, Mip);

				ValueType ZerosValue;
				FMemory::Memset(&ZerosValue, 0, sizeof(ZerosValue));
				(RHICmdList.*ClearPtr)(MipUAV, ZerosValue);
			}
			RHICmdList.Transition(FRHITransitionInfo(TextureRHI, ERHIAccess::UAVCompute, ERHIAccess::CopySrc));

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
			RHICmdList.Transition(FRHITransitionInfo(TextureRHI, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			FUnorderedAccessViewRHIRef SpecificMipUAV = RHICreateUnorderedAccessView(TextureRHI, MipIndex);
			(RHICmdList.*ClearPtr)(SpecificMipUAV, ClearValue);
			RHICmdList.Transition(FRHITransitionInfo(TextureRHI, ERHIAccess::UAVCompute, ERHIAccess::CopySrc));
			bResult1 = VerifyTextureContents(*FString::Printf(TEXT("%s - clear mip %d to (%s)"), *TestName, MipIndex, *ClearValueToString(ClearValue)), RHICmdList, TextureRHI,
				[&](void* Ptr, uint32 MipWidth, uint32 MipHeight, uint32 Width, uint32 Height, uint32 CurrentMipIndex, uint32 CurrentSliceIndex)
			{
				return VerifyMip(Ptr, MipWidth, MipHeight, Width, Height, CurrentMipIndex, CurrentSliceIndex, CurrentMipIndex != MipIndex);
			});
		}

		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

		return bResult0 && bResult1;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool Test_RHIClearUAV_Texture2D_WithParams(FRHICommandListImmediate& RHICmdList, uint32 NumMips, uint32 NumSlices, uint32 Width, uint32 Height, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::* ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult = true;
		FString TestName = FString::Printf(TEXT("Test_RHIClearUAV_Texture2D (%dx%d, %d Slice(s), %d Mip(s)) - %s"), Width, Height, NumMips, NumSlices, *ClearValueToString(ClearValue));

		{
			FRHIResourceCreateInfo CreateInfo(*TestName);
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
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

		return bResult;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool Test_RHIClearUAV_Texture2D_Impl(FRHICommandListImmediate& RHICmdList, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::* ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult = true;

		// Single Mip, Square
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 1, 1, 32, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 1, 4, 32, 32, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, Square
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 1, 32, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 4, 32, 32, Format, ClearValue, ClearPtr, TestValue));

		// Single Mip, pow2 Rectangle
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 1, 1, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 1, 1, 32, 16, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 1, 4, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 1, 4, 32, 16, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, pow2 Rectangle
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 1, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 1, 32, 16, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 4, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 4, 32, 16, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, Odd-sized
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 1, 17, 23, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 1, 23, 17, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 4, 17, 23, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture2D_WithParams(RHICmdList, 4, 4, 23, 17, Format, ClearValue, ClearPtr, TestValue));

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
		const FVector4f ClearValueFloat(0.2345f, 0.8499f, 0.417f, 0.00145f);
		const FUintVector4 ClearValueUint32(0x01234567, 0x89abcdef, 0x8899aabb, 0xccddeeff);

		RUN_TEST(Test_RHIClearUAV_Texture2D_Impl(RHICmdList, PF_FloatRGBA, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0x81, 0x33, 0xcc, 0x3a, 0xac, 0x36, 0xf0, 0x15 }));
		RUN_TEST(Test_RHIClearUAV_Texture2D_Impl(RHICmdList, PF_R32_UINT, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01 }));

		return bResult;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool Test_RHIClearUAV_Texture3D_WithParams(FRHICommandListImmediate& RHICmdList, uint32 NumMips, uint32 Width, uint32 Height, uint32 Depth, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::* ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		FString TestName = FString::Printf(TEXT("Test_RHIClearUAVUint_Texture3D (%dx%dx%d, %d Mip(s))"), Width, Height, Depth, NumMips);

		bool bResult = true;

		{
			FRHIResourceCreateInfo CreateInfo(*TestName);
			FTexture3DRHIRef Texture = RHICreateTexture3D(Width, Height, Depth, Format, NumMips, TexCreate_UAV | TexCreate_ShaderResource, CreateInfo);

			for (uint32 Mip = 0; Mip < NumMips; ++Mip)
			{
				RUN_TEST(RunTest_UAVClear_Texture(RHICmdList, *TestName, Texture.GetReference(), Mip, ClearValue, ClearPtr, TestValue));
			}
		}
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

		return bResult;
	}

	template <typename ValueType, uint32 NumTestBytes>
	static bool Test_RHIClearUAV_Texture3D_Impl(FRHICommandListImmediate& RHICmdList, EPixelFormat Format, const ValueType& ClearValue, void(FRHIComputeCommandList::* ClearPtr)(FRHIUnorderedAccessView*, ValueType const&), const uint8(&TestValue)[NumTestBytes])
	{
		bool bResult = true;

		// Single Mip, Cube
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 1, 32, 32, 32, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, Cube
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 4, 32, 32, 32, Format, ClearValue, ClearPtr, TestValue));

		// Single Mip, pow2 Cuboid
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 1, 16, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 1, 16, 32, 16, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 1, 32, 16, 16, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, pow2 Cuboid
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 4, 16, 16, 32, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 4, 16, 32, 16, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 4, 32, 16, 16, Format, ClearValue, ClearPtr, TestValue));

		// Multiple Mip, Odd-sized cuboid
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 4, 17, 23, 29, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 4, 29, 17, 23, Format, ClearValue, ClearPtr, TestValue));
		RUN_TEST(Test_RHIClearUAV_Texture3D_WithParams(RHICmdList, 4, 23, 29, 17, Format, ClearValue, ClearPtr, TestValue));

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
		const FVector4f ClearValueFloat(0.2345f, 0.8499f, 0.417f, 0.00145f);
		const FUintVector4 ClearValueUint32(0x01234567, 0x89abcdef, 0x8899aabb, 0xccddeeff);

		RUN_TEST(Test_RHIClearUAV_Texture3D_Impl(RHICmdList, PF_FloatRGBA, ClearValueFloat, &FRHICommandListImmediate::ClearUAVFloat, { 0x81, 0x33, 0xcc, 0x3a, 0xac, 0x36, 0xf0, 0x15 }));
		RUN_TEST(Test_RHIClearUAV_Texture3D_Impl(RHICmdList, PF_R32_UINT, ClearValueUint32, &FRHICommandListImmediate::ClearUAVUint, { 0x67, 0x45, 0x23, 0x01 }));

		return bResult;
	}

	static bool Test_RHIFormat_WithParams(FRHICommandListImmediate& RHICmdList, EPixelFormat ResourceFormat, EPixelFormat SRVFormat, EPixelFormat UAVFormat, ETextureCreateFlags Flags)
	{
		const uint32 Width = 32;
		const uint32 Height = 32;

		bool bResult = true;
		FString TestName = FString::Printf(TEXT("Test_RHIFormat (%s, %s, %s, %d)"), GPixelFormats[ResourceFormat].Name, GPixelFormats[SRVFormat].Name, GPixelFormats[UAVFormat].Name, Flags);
		{
			FRHIResourceCreateInfo CreateInfo(*TestName);

			FTextureRHIRef Texture;
			Texture = RHICreateTexture2D(Width, Height, ResourceFormat, 1, 1, Flags, CreateInfo);
			bResult = (Texture != nullptr);

			if (Texture && SRVFormat != PF_Unknown)
			{
				FRHITextureSRVCreateInfo ViewInfo(0, 1, SRVFormat);
				FShaderResourceViewRHIRef SRV = RHICreateShaderResourceView(Texture, ViewInfo);
				bResult = (SRV != nullptr);
			}

			// TODO
			if (Texture && UAVFormat != PF_Unknown)
			{
				//RHICreateUnorderedAccessView(Texture, 0, UAVFormat);
			}
		}
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

		if (bResult)
		{
			UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("Test passed. \"%s\""), *TestName);
		}

		return bResult;
	}

	static bool Test_RHIFormat_RenderTargetFormat(FRHICommandListImmediate& RHICmdList, EPixelFormat Format, bool bAllowUAV)
	{
		bool bResult = true;
		if (EnumHasAnyFlags(GPixelFormats[Format].Capabilities, EPixelFormatCapabilities::RenderTarget))
		{
			RUN_TEST(Test_RHIFormat_WithParams(RHICmdList, Format, PF_Unknown, PF_Unknown, TexCreate_RenderTargetable));
			RUN_TEST(Test_RHIFormat_WithParams(RHICmdList, Format, Format, PF_Unknown, TexCreate_RenderTargetable | TexCreate_ShaderResource));
			if (bAllowUAV)
			{
				RUN_TEST(Test_RHIFormat_WithParams(RHICmdList, Format, PF_Unknown, Format, TexCreate_RenderTargetable | TexCreate_UAV));
				RUN_TEST(Test_RHIFormat_WithParams(RHICmdList, Format, Format, Format, TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV));
			}
		}
		else
		{
			UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("Skipping test for lack of format support. \"Test_RHIFormat_RenderTargetFormat (%s)\""), GPixelFormats[Format].Name);
		}
		return bResult;
	}

	static bool Test_RHIFormat_DepthFormat(FRHICommandListImmediate& RHICmdList, EPixelFormat ResourceFormat)
	{
		bool bResult = true;
		if (EnumHasAnyFlags(GPixelFormats[ResourceFormat].Capabilities, EPixelFormatCapabilities::DepthStencil))
		{
			RUN_TEST(Test_RHIFormat_WithParams(RHICmdList, ResourceFormat, PF_Unknown, PF_Unknown, TexCreate_DepthStencilTargetable));
			RUN_TEST(Test_RHIFormat_WithParams(RHICmdList, ResourceFormat, ResourceFormat, PF_Unknown, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource));

			if (ResourceFormat == PF_DepthStencil)
			{
				RUN_TEST(Test_RHIFormat_WithParams(RHICmdList, ResourceFormat, PF_X24_G8, PF_Unknown, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource));
			}
		}
		else
		{
			UE_LOG(LogRHIUnitTestCommandlet, Display, TEXT("Skipping test for lack of format support. \"Test_RHIFormat_DepthFormat (%s)\""), GPixelFormats[ResourceFormat].Name);
		}
		return bResult;
	}

	static bool Test_RHIFormats(FRHICommandListImmediate& RHICmdList)
	{
		bool bResult = true;
		RUN_TEST(Test_RHIFormat_RenderTargetFormat(RHICmdList, PF_R32_FLOAT, true));

		RUN_TEST(Test_RHIFormat_DepthFormat(RHICmdList, PF_DepthStencil));
		RUN_TEST(Test_RHIFormat_DepthFormat(RHICmdList, PF_ShadowDepth));
		RUN_TEST(Test_RHIFormat_DepthFormat(RHICmdList, PF_R32_FLOAT));
		RUN_TEST(Test_RHIFormat_DepthFormat(RHICmdList, PF_D24));

		return bResult;
	}
};
