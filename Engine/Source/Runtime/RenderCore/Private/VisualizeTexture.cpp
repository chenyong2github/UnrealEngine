// Copyright Epic Games, Inc. All Rights Reserved.

#include "VisualizeTexture.h"
#include "ShaderParameters.h"
#include "RHIStaticStates.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "RenderTargetPool.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"
#include "Misc/FileHelper.h"

void FVisualizeTexture::ParseCommands(const TCHAR* Cmd, FOutputDevice &Ar)
{
#if SUPPORTS_VISUALIZE_TEXTURE
	Config = {};
	uint32 ParameterCount = 0;
	for (;;)
	{
		FString Parameter = FParse::Token(Cmd, 0);

		if (Parameter.IsEmpty())
		{
			break;
		}
		else if (ParameterCount == 0)
		{
			if (!FChar::IsDigit(**Parameter))
			{
				const TCHAR* AfterAt = *Parameter;

				while (*AfterAt != 0 && *AfterAt != TCHAR('@'))
				{
					++AfterAt;
				}

				if (*AfterAt == TCHAR('@'))
				{
					FString NameWithoutAt = Parameter.Left(AfterAt - *Parameter);
					Visualize(*NameWithoutAt, FCString::Atoi(AfterAt + 1));
				}
				else
				{
					Visualize(*Parameter);
				}
			}
			else
			{
				Visualize({});
			}
		}
		else if (Parameter == TEXT("fulllist") || Parameter == TEXT("full"))
		{
			Config.Flags |= EFlags::FullList;
		}
		else if (Parameter == TEXT("byname"))
		{
			Config.SortBy = ESortBy::Name;
		}
		else if (Parameter == TEXT("bysize"))
		{
			Config.SortBy = ESortBy::Size;
		}
		else if (Parameter == TEXT("uv0"))
		{
			Config.InputUVMapping = EInputUVMapping::LeftTop;
		}
		else if (Parameter == TEXT("uv1"))
		{
			Config.InputUVMapping = EInputUVMapping::Whole;
		}
		else if (Parameter == TEXT("uv2"))
		{
			Config.InputUVMapping = EInputUVMapping::PixelPerfectCenter;
		}
		else if (Parameter == TEXT("pip"))
		{
			Config.InputUVMapping = EInputUVMapping::PictureInPicture;
		}
		else if (Parameter == TEXT("bmp"))
		{
			Config.Flags |= EFlags::SaveBitmap;
		}
		else if (Parameter == TEXT("stencil"))
		{
			Config.Flags |= EFlags::SaveBitmapAsStencil;
		}
		else if (Parameter == TEXT("frac"))
		{
			Config.ShaderOp = EShaderOp::Frac;
		}
		else if (Parameter == TEXT("sat"))
		{
			Config.ShaderOp = EShaderOp::Saturate;
		}
		else if (Parameter.Left(3) == TEXT("mip"))
		{
			Parameter.RightInline(Parameter.Len() - 3, false);
			Config.MipIndex = FCString::Atoi(*Parameter);
		}
		else if (Parameter.Left(5) == TEXT("index"))
		{
			Parameter.RightInline(Parameter.Len() - 5, false);
			Config.ArrayIndex = FCString::Atoi(*Parameter);
		}
		// e.g. RGB*6, A, *22, /2.7, A*7
		else if (Parameter.Left(3) == TEXT("rgb")
			|| Parameter.Left(1) == TEXT("a")
			|| Parameter.Left(1) == TEXT("r")
			|| Parameter.Left(1) == TEXT("g")
			|| Parameter.Left(1) == TEXT("b")
			|| Parameter.Left(1) == TEXT("*")
			|| Parameter.Left(1) == TEXT("/"))
		{
			Config.SingleChannel = -1;

			if (Parameter.Left(3) == TEXT("rgb"))
			{
				Parameter.RightInline(Parameter.Len() - 3, false);
			}
			else if (Parameter.Left(1) == TEXT("r")) Config.SingleChannel = 0;
			else if (Parameter.Left(1) == TEXT("g")) Config.SingleChannel = 1;
			else if (Parameter.Left(1) == TEXT("b")) Config.SingleChannel = 2;
			else if (Parameter.Left(1) == TEXT("a")) Config.SingleChannel = 3;
			if (Config.SingleChannel >= 0)
			{
				Parameter.RightInline(Parameter.Len() - 1, false);
				Config.SingleChannelMul = 1.0f;
				Config.RGBMul = 0.0f;
			}

			float Mul = 1.0f;

			// * or /
			if (Parameter.Left(1) == TEXT("*"))
			{
				Parameter.RightInline(Parameter.Len() - 1, false);
				Mul = FCString::Atof(*Parameter);
			}
			else if (Parameter.Left(1) == TEXT("/"))
			{
				Parameter.RightInline(Parameter.Len() - 1, false);
				Mul = 1.0f / FCString::Atof(*Parameter);
			}
			Config.RGBMul *= Mul;
			Config.SingleChannelMul *= Mul;
			Config.AMul *= Mul;
		}
		else
		{
			Ar.Logf(TEXT("Error: parameter \"%s\" not recognized"), *Parameter);
		}

		++ParameterCount;
	}

	if (!ParameterCount)
	{
		Ar.Logf(TEXT("VisualizeTexture/Vis <CheckpointName> [<Mode>] [PIP/UV0/UV1/UV2] [BMP] [FRAC/SAT] [FULL]:"));
		Ar.Logf(TEXT("Mode (examples):"));
		Ar.Logf(TEXT("  RGB      = RGB in range 0..1 (default)"));
		Ar.Logf(TEXT("  *8       = RGB * 8"));
		Ar.Logf(TEXT("  A        = alpha channel in range 0..1"));
		Ar.Logf(TEXT("  R        = red channel in range 0..1"));
		Ar.Logf(TEXT("  G        = green channel in range 0..1"));
		Ar.Logf(TEXT("  B        = blue channel in range 0..1"));
		Ar.Logf(TEXT("  A*16     = Alpha * 16"));
		Ar.Logf(TEXT("  RGB/2    = RGB / 2"));
		Ar.Logf(TEXT("SubResource:"));
		Ar.Logf(TEXT("  MIP5     = Mip level 5 (0 is default)"));
		Ar.Logf(TEXT("  INDEX5   = Array Element 5 (0 is default)"));
		Ar.Logf(TEXT("InputMapping:"));
		Ar.Logf(TEXT("  PIP      = like UV1 but as picture in picture with normal rendering  (default)"));
		Ar.Logf(TEXT("  UV0      = UV in left top"));
		Ar.Logf(TEXT("  UV1      = full texture"));
		Ar.Logf(TEXT("  UV2      = pixel perfect centered"));
		Ar.Logf(TEXT("Flags:"));
		Ar.Logf(TEXT("  BMP      = save out bitmap to the screenshots folder (not on console, normalized)"));
		Ar.Logf(TEXT("STENCIL    = Stencil normally displayed in alpha channel of depth.  This option is used for BMP to get a stencil only BMP."));
		Ar.Logf(TEXT("  FRAC     = use frac() in shader (default)"));
		Ar.Logf(TEXT("  SAT      = use saturate() in shader"));
		Ar.Logf(TEXT("  FULLLIST = show full list, otherwise we hide some textures in the printout"));
		Ar.Logf(TEXT("  BYNAME   = sort list by name"));
		Ar.Logf(TEXT("  BYSIZE   = show list by size"));
		Ar.Logf(TEXT("TextureId:"));
		Ar.Logf(TEXT("  0        = <off>"));

		DebugLog(EDebugLogVerbosity::Extended);
	}
#endif
}

void FVisualizeTexture::DebugLogOnCrash()
{
#if SUPPORTS_VISUALIZE_TEXTURE
	Config.SortBy = ESortBy::Size;
	Config.Flags |= EFlags::FullList;
	DebugLog(EDebugLogVerbosity::Default);
#endif
}

void FVisualizeTexture::GetTextureInfos_GameThread(TArray<FString>& Infos) const
{
	check(IsInGameThread());
	FlushRenderingCommands();

	for (uint32 Index = 0, Num = GRenderTargetPool.GetElementCount(); Index < Num; ++Index)
	{
		FPooledRenderTarget* RenderTarget = GRenderTargetPool.GetElementById(Index);

		if (!RenderTarget)
		{
			continue;
		}

		FPooledRenderTargetDesc Desc = RenderTarget->GetDesc();
		uint32 SizeInKB = (RenderTarget->ComputeMemorySize() + 1023) / 1024;
		FString Entry = FString::Printf(TEXT("%s %d %s %d"),
			*Desc.GenerateInfoString(),
			Index + 1,
			Desc.DebugName ? Desc.DebugName : TEXT("<Unnamed>"),
			SizeInKB);
		Infos.Add(Entry);
	}
}

TGlobalResource<FVisualizeTexture> GVisualizeTexture;

#if SUPPORTS_VISUALIZE_TEXTURE

void FVisualizeTexture::DebugLog(EDebugLogVerbosity Verbosity)
{
	{
		struct FSortedLines
		{
			FString Line;
			int32   SortIndex = 0;
			uint32  PoolIndex = 0;

			bool operator < (const FSortedLines &B) const
			{
				// first large ones
				if (SortIndex < B.SortIndex)
				{
					return true;
				}
				if (SortIndex > B.SortIndex)
				{
					return false;
				}

				return Line < B.Line;
			}
		};

		TArray<FSortedLines> SortedLines;

		for (uint32 Index = 0, Num = GRenderTargetPool.GetElementCount(); Index < Num; ++Index)
		{
			FPooledRenderTarget* RenderTarget = GRenderTargetPool.GetElementById(Index);

			if (!RenderTarget)
			{
				continue;
			}

			const bool bFullList = EnumHasAnyFlags(Config.Flags, EFlags::FullList);

			const FPooledRenderTargetDesc Desc = RenderTarget->GetDesc();

			if (bFullList || (Desc.Flags & TexCreate_HideInVisualizeTexture) == 0)
			{
				const uint32 SizeInKB = (RenderTarget->ComputeMemorySize() + 1023) / 1024;

				FString UnusedStr;

				if (RenderTarget->GetUnusedForNFrames() > 0)
				{
					if (!bFullList)
					{
						continue;
					}

					UnusedStr = FString::Printf(TEXT(" unused(%d)"), RenderTarget->GetUnusedForNFrames());
				}

				FSortedLines Element;
				Element.PoolIndex = Index;
				Element.SortIndex = Index;

				FString InfoString = Desc.GenerateInfoString();

				switch (Config.SortBy)
				{
				case ESortBy::Index:
				{
					// Constant works well with the average name length
					const uint32 TotalSpacerSize = 36;
					const uint32 SpaceCount = FMath::Max<int32>(0, TotalSpacerSize - InfoString.Len());

					for (uint32 Space = 0; Space < SpaceCount; ++Space)
					{
						InfoString.AppendChar((TCHAR)' ');
					}

					// Sort by index
					Element.Line = FString::Printf(TEXT("%s %s %d KB%s"), *InfoString, Desc.DebugName, SizeInKB, *UnusedStr);
				}
				break;

				case ESortBy::Name:
				{
					Element.Line = FString::Printf(TEXT("%s %s %d KB%s"), Desc.DebugName, *InfoString, SizeInKB, *UnusedStr);
					Element.SortIndex = 0;
				}
				break;

				case ESortBy::Size:
				{
					Element.Line = FString::Printf(TEXT("%d KB %s %s%s"), SizeInKB, *InfoString, Desc.DebugName, *UnusedStr);
					Element.SortIndex = -(int32)SizeInKB;
				}
				break;

				default:
					checkNoEntry();
				}

				if (Desc.Flags & TexCreate_FastVRAM)
				{
					FRHIResourceInfo Info;

					FTextureRHIRef Texture = RenderTarget->GetRenderTargetItem().ShaderResourceTexture;

					if (!IsValidRef(Texture))
					{
						Texture = RenderTarget->GetRenderTargetItem().TargetableTexture;
					}

					if (IsValidRef(Texture))
					{
						RHIGetResourceInfo(Texture, Info);
					}

					if (Info.VRamAllocation.AllocationSize)
					{
						// Note we do KB for more readable numbers but this can cause quantization loss
						Element.Line += FString::Printf(TEXT(" VRamInKB(Start/Size):%d/%d"),
							Info.VRamAllocation.AllocationStart / 1024,
							(Info.VRamAllocation.AllocationSize + 1023) / 1024);
					}
					else
					{
						Element.Line += TEXT(" VRamInKB(Start/Size):<NONE>");
					}
				}

				SortedLines.Add(Element);
			}
		}

		SortedLines.Sort();

		for (int32 Index = 0; Index < SortedLines.Num(); Index++)
		{
			const FSortedLines& Entry = SortedLines[Index];

			UE_LOG(LogConsoleResponse, Log, TEXT("   %3d = %s"), Entry.PoolIndex + 1, *Entry.Line);
		}
	}

	UE_LOG(LogConsoleResponse, Log, TEXT(""));

	if (Verbosity == EDebugLogVerbosity::Extended)
	{
		UE_LOG(LogConsoleResponse, Log, TEXT("CheckpointName (what was rendered this frame, use <Name>@<Number> to get intermediate versions):"));

		TArray<FString> Entries;
		Entries.Reserve(VersionCountMap.Num());
		for (auto KV : VersionCountMap)
		{
			Entries.Add(KV.Key);
		}
		Entries.Sort();

		// Magic number works well with the name length we have
		const uint32 ColumnCount = 5;
		const uint32 SpaceBetweenColumns = 1;
		const uint32 ColumnHeight = FMath::DivideAndRoundUp((uint32)Entries.Num(), ColumnCount);

		// Width of the column in characters, init with 0
		uint32 ColumnWidths[ColumnCount] = {};

		for (uint32 Index = 0, Count = Entries.Num(); Index < Count; ++Index)
		{
			const FString& Entry = *Entries[Index];
			const uint32 Column = Index / ColumnHeight;
			ColumnWidths[Column] = FMath::Max(ColumnWidths[Column], (uint32)Entry.Len());
		}

		// Print them sorted, if possible multiple in a line
		{
			FString Line;

			for (uint32 OutputIndex = 0, Count = Entries.Num(); OutputIndex < Count; ++OutputIndex)
			{
				// [0, ColumnCount-1]
				const uint32 Column = OutputIndex % ColumnCount;
				const uint32 Row = OutputIndex / ColumnCount;
				const uint32 Index = Row + Column * ColumnHeight;

				bool bLineEnd = true;

				if (Index < Count)
				{
					bLineEnd = (Column + 1 == ColumnCount);

					// Order per column for readability.
					const FString& Entry = *Entries[Index];

					Line += Entry;

					const int32 SpaceCount = ColumnWidths[Column] + SpaceBetweenColumns - Entry.Len();
					checkf(SpaceCount >= 0, TEXT("A previous pass produced bad data"));

					for (int32 Space = 0; Space < SpaceCount; ++Space)
					{
						Line.AppendChar((TCHAR)' ');
					}
				}

				if (bLineEnd)
				{
					Line.TrimEndInline();
					UE_LOG(LogConsoleResponse, Log, TEXT("   %s"), *Line);
					Line.Empty();
				}
			}
		}
	}

	uint32 WholeCount;
	uint32 WholePoolInKB;
	uint32 UsedInKB;
	GRenderTargetPool.GetStats(WholeCount, WholePoolInKB, UsedInKB);

	UE_LOG(LogConsoleResponse, Log, TEXT("Pool: %d/%d MB (referenced/allocated)"), (UsedInKB + 1023) / 1024, (WholePoolInKB + 1023) / 1024);
}

static TAutoConsoleVariable<int32> CVarAllowBlinking(
	TEXT("r.VisualizeTexture.AllowBlinking"), 1,
	TEXT("Whether to allow blinking when visualizing NaN or inf that can become irritating over time.\n"),
	ECVF_RenderThreadSafe);

enum class EVisualisePSType
{
	Cube = 0,
	Texture1D = 1, //not supported
	Texture2DNoMSAA = 2,
	Texture3D = 3,
	CubeArray = 4,
	Texture2DMSAA = 5,
	Texture2DDepthStencilNoMSAA = 6,
	Texture2DUINT8 = 7,
	Texture2DUINT32 = 8,
	MAX
};

/** A pixel shader which filters a texture. */
// @param TextureType 0:Cube, 1:1D(not yet supported), 2:2D no MSAA, 3:3D, 4:Cube[], 5:2D MSAA, 6:2D DepthStencil no MSAA (needed to avoid D3DDebug error)
class FVisualizeTexturePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeTexturePS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeTexturePS, FGlobalShader);

	class FVisualisePSTypeDim : SHADER_PERMUTATION_ENUM_CLASS("TEXTURE_TYPE", EVisualisePSType);

	using FPermutationDomain = TShaderPermutationDomain<FVisualisePSTypeDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		return PermutationVector.Get<FVisualisePSTypeDim>() != EVisualisePSType::Texture1D;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector, TextureExtent)
		SHADER_PARAMETER_ARRAY(FVector4, VisualizeParam, [3])

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VisualizeTexture2D)
		SHADER_PARAMETER_SAMPLER(SamplerState, VisualizeTexture2DSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VisualizeTexture3D)
		SHADER_PARAMETER_SAMPLER(SamplerState, VisualizeTexture3DSampler)
		SHADER_PARAMETER_RDG_TEXTURE(TextureCube, VisualizeTextureCube)
		SHADER_PARAMETER_SAMPLER(SamplerState, VisualizeTextureCubeSampler)
		SHADER_PARAMETER_RDG_TEXTURE(TextureCubeArray, VisualizeTextureCubeArray)
		SHADER_PARAMETER_SAMPLER(SamplerState, VisualizeTextureCubeArraySampler)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint4>, VisualizeDepthStencil)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DMS<float4>, VisualizeTexture2DMS)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, VisualizeUINT8Texture2D)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeTexturePS, "/Engine/Private/Tools/VisualizeTexture.usf", "VisualizeTexturePS", SF_Pixel);

static EVisualisePSType GetVisualizePSType(const FRDGTextureDesc& Desc)
{
	if(Desc.IsTexture2D())
	{
		// 2D		
		if(Desc.NumSamples > 1)
		{
			// MSAA
			return EVisualisePSType::Texture2DMSAA;
		}
		else
		{
			if(Desc.Format == PF_DepthStencil)
			{
				// DepthStencil non MSAA (needed to avoid D3DDebug error)
				return EVisualisePSType::Texture2DDepthStencilNoMSAA;
			}
			else if (Desc.Format == PF_R8_UINT)
			{
				return EVisualisePSType::Texture2DUINT8;
			}
			else if (Desc.Format == PF_R32_UINT)
			{
				return EVisualisePSType::Texture2DUINT32;
			}
			else
			{
				// non MSAA
				return EVisualisePSType::Texture2DNoMSAA;
			}
		}
	}
	else if(Desc.IsTextureCube())
	{
		if(Desc.IsTextureArray())
		{
			// Cube[]
			return EVisualisePSType::CubeArray;
		}
		else
		{
			// Cube
			return EVisualisePSType::Cube;
		}
	}

	check(Desc.IsTexture3D());
	return EVisualisePSType::Texture3D;
}

void FVisualizeTexture::ReleaseDynamicRHI()
{
	Config = {};
	Requested = {};
	Captured = {};
}

void FVisualizeTexture::CreateContentCapturePass(FRDGBuilder& GraphBuilder, const FRDGTextureRef InputTexture, uint32 CaptureId)
{
	if (!InputTexture)
	{
		return;
	}

	const FRDGTextureDesc& InputDesc = InputTexture->Desc;
	const FIntPoint InputExtent = InputDesc.Extent;

	if (EnumHasAnyFlags(InputDesc.Flags, TexCreate_CPUReadback))
	{
		return;
	}

	FIntPoint OutputExtent = InputExtent;

	// Clamp to reasonable value to prevent crash
	OutputExtent.X = FMath::Max(OutputExtent.X, 1);
	OutputExtent.Y = FMath::Max(OutputExtent.Y, 1);

	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(OutputExtent, PF_B8G8R8A8, FClearValueBinding(FLinearColor(1, 1, 0, 1)), TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("VisualizeTexture"));

	EInputValueMapping InputValueMapping = EInputValueMapping::Color;

	{
		if (InputDesc.Format == PF_ShadowDepth)
		{
			InputValueMapping = EInputValueMapping::Shadow;
		}
		else if (EnumHasAnyFlags(InputDesc.Flags, TexCreate_DepthStencilTargetable))
		{
			InputValueMapping = EInputValueMapping::Depth;
		}

		const EVisualisePSType VisualizeType = GetVisualizePSType(InputDesc);

		FVisualizeTexturePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeTexturePS::FParameters>();

		{
			PassParameters->TextureExtent = FVector(InputExtent.X, InputExtent.Y, InputDesc.Depth);

			{
				// Alternates between 0 and 1 with a short pause
				const float FracTimeScale = 2.0f;
				float FracTime = FApp::GetCurrentTime() * FracTimeScale - floor(FApp::GetCurrentTime() * FracTimeScale);
				float BlinkState = (FracTime > 0.5f) ? 1.0f : 0.0f;

				FVector4 VisualizeParamValue[3];

				float Add = 0.0f;
				float FracScale = 1.0f;

				// w * almost_1 to avoid frac(1) => 0
				PassParameters->VisualizeParam[0] = FVector4(Config.RGBMul, Config.SingleChannelMul, Add, FracScale * 0.9999f);
				PassParameters->VisualizeParam[1] = FVector4(CVarAllowBlinking.GetValueOnRenderThread() ? BlinkState : 1.0f, (Config.ShaderOp == EShaderOp::Saturate) ? 1.0f : 0.0f, Config.ArrayIndex, Config.MipIndex);
				PassParameters->VisualizeParam[2] = FVector4((float)InputValueMapping, 0.0f, Config.SingleChannel);
			}

			FRHISamplerState* PointSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			PassParameters->VisualizeTexture2D = InputTexture;
			PassParameters->VisualizeTexture2DSampler = PointSampler;
			PassParameters->VisualizeTexture3D = InputTexture;
			PassParameters->VisualizeTexture3DSampler = PointSampler;
			PassParameters->VisualizeTextureCube = InputTexture;
			PassParameters->VisualizeTextureCubeSampler = PointSampler;
			PassParameters->VisualizeTextureCubeArray = InputTexture;
			PassParameters->VisualizeTextureCubeArraySampler = PointSampler;

			if (VisualizeType == EVisualisePSType::Texture2DDepthStencilNoMSAA)
			{
				FRDGTextureSRVDesc SRVDesc = FRDGTextureSRVDesc::CreateWithPixelFormat(InputTexture, PF_X24_G8);
				PassParameters->VisualizeDepthStencil = GraphBuilder.CreateSRV(SRVDesc);
			}

			PassParameters->VisualizeTexture2DMS = InputTexture;
			PassParameters->VisualizeUINT8Texture2D = InputTexture;

			PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::EClear);
		}

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
		FVisualizeTexturePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FVisualizeTexturePS::FVisualisePSTypeDim>(VisualizeType);

		TShaderMapRef<FVisualizeTexturePS> PixelShader(ShaderMap, PermutationVector);

		FString ExtendedDrawEvent;
		if (GetEmitRDGEvents())
		{
			if (InputDesc.IsTexture3D())
			{
				ExtendedDrawEvent += FString::Printf(TEXT("x%d CapturedSlice=%d"), InputDesc.Depth, Config.ArrayIndex);
			}

			// Precise the mip level being captured in the mip level when there is a mip chain.
			if (InputDesc.IsMipChain())
			{
				ExtendedDrawEvent += FString::Printf(TEXT(" Mips=%d CapturedMip=%d"), InputDesc.NumMips, Config.MipIndex);
			}
		}

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			ShaderMap,
			RDG_EVENT_NAME("VisualizeTextureCapture(%s@%d %s %dx%d%s)",
				InputTexture->Name, CaptureId,
				GPixelFormats[InputDesc.Format].Name,
				InputExtent.X, InputExtent.Y,
				*ExtendedDrawEvent),
			PixelShader,
			PassParameters,
			FIntRect(0, 0, OutputExtent.X, OutputExtent.Y));
	}

	{
		Captured.Desc = Translate(InputDesc);
		Captured.Desc.DebugName = InputTexture->Name;
		Captured.PooledRenderTarget = nullptr;
		Captured.Texture = OutputTexture;
		Captured.InputValueMapping = InputValueMapping;

		GraphBuilder.QueueTextureExtraction(OutputTexture, &Captured.PooledRenderTarget);
	}

	if (EnumHasAnyFlags(Config.Flags, EFlags::SaveBitmap | EFlags::SaveBitmapAsStencil))
	{
		uint32 MipAdjustedExtentX = FMath::Clamp(OutputExtent.X >> Config.MipIndex, 0, OutputExtent.X);
		uint32 MipAdjustedExtentY = FMath::Clamp(OutputExtent.Y >> Config.MipIndex, 0, OutputExtent.Y);
		FIntPoint Extent(MipAdjustedExtentX, MipAdjustedExtentY);

		FReadSurfaceDataFlags ReadDataFlags;
		ReadDataFlags.SetLinearToGamma(false);
		ReadDataFlags.SetOutputStencil(EnumHasAnyFlags(Config.Flags, EFlags::SaveBitmapAsStencil));
		ReadDataFlags.SetMip(Config.MipIndex);

		const TCHAR* DebugName = Captured.Desc.DebugName;

		AddReadbackTexturePass(GraphBuilder, RDG_EVENT_NAME("SaveBitmap"), OutputTexture,
			[OutputTexture, Extent, ReadDataFlags, DebugName](FRHICommandListImmediate& RHICmdList)
		{
			TArray<FColor> Bitmap;
			RHICmdList.ReadSurfaceData(OutputTexture->GetRHI(), FIntRect(0, 0, Extent.X, Extent.Y), Bitmap, ReadDataFlags);

			// if the format and texture type is supported
			if (Bitmap.Num())
			{
				// Create screenshot folder if not already present.
				IFileManager::Get().MakeDirectory(*FPaths::ScreenShotDir(), true);

				const FString Filename(FPaths::ScreenShotDir() / TEXT("VisualizeTexture"));

				uint32 ExtendXWithMSAA = Bitmap.Num() / Extent.Y;

				// Save the contents of the array to a bitmap file. (24bit only so alpha channel is dropped)
				FFileHelper::CreateBitmap(*Filename, ExtendXWithMSAA, Extent.Y, Bitmap.GetData());

				UE_LOG(LogRendererCore, Display, TEXT("Content was saved to \"%s\""), *FPaths::ScreenShotDir());
			}
			else
			{
				UE_LOG(LogRendererCore, Error, TEXT("Failed to save BMP for VisualizeTexture, format or texture type is not supported"));
			}
		});
	}
}

TOptional<uint32> FVisualizeTexture::ShouldCapture(const TCHAR* InName, uint32 InMipIndex)
{
	TOptional<uint32> CaptureId;
	uint32& VersionCount = VersionCountMap.FindOrAdd(InName);
	if (!Requested.Name.IsEmpty() && Requested.Name == InName)
	{
		if (!Requested.Version || VersionCount == Requested.Version.GetValue())
		{
			CaptureId = VersionCount;
		}
	}
	++VersionCount;
	return CaptureId;
}

uint32 FVisualizeTexture::GetVersionCount(const TCHAR* InName) const
{
	if (const uint32* VersionCount = VersionCountMap.Find(InName))
	{
		return *VersionCount;
	}
	return 0;
}

void FVisualizeTexture::SetCheckPoint(FRHICommandListImmediate& RHICmdList, IPooledRenderTarget* PooledRenderTarget)
{
	check(IsInRenderingThread());

	if (!PooledRenderTarget)
	{
		return;
	}

	const FPooledRenderTargetDesc& Desc = PooledRenderTarget->GetDesc();

	if ((Desc.TargetableFlags & TexCreate_ShaderResource) == 0)
	{
		return;
	}

	TOptional<uint32> CaptureId = ShouldCapture(Desc.DebugName, Config.MipIndex);
	if (!CaptureId)
	{
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);
	FRDGTextureRef TextureToCapture = GraphBuilder.RegisterExternalTexture(PooledRenderTarget, ERenderTargetTexture::Targetable);
	CreateContentCapturePass(GraphBuilder, TextureToCapture, CaptureId.GetValue());
	GraphBuilder.Execute();
}

void FVisualizeTexture::Visualize(const FString& InName, TOptional<uint32> InVersion)
{
	Requested.Name = InName;
	Requested.Version = InVersion;
}

#endif // SUPPORTS_VISUALIZE_TEXTURE
