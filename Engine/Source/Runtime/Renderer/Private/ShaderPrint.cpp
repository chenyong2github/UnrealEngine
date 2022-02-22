// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShaderPrint.h"
#include "ShaderPrintParameters.h"

#include "ShaderParameterStruct.h"
#include "CommonRenderResources.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "Engine/Engine.h"
#include "GlobalShader.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "SceneRendering.h"
#include "SystemTextures.h"
#include "ScenePrivate.h"

namespace ShaderPrint
{
	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Console variables

	TAutoConsoleVariable<int32> CVarEnable(
		TEXT("r.ShaderPrint"),
		0,
		TEXT("ShaderPrint debugging toggle.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarFontSize(
		TEXT("r.ShaderPrint.FontSize"),
		8,
		TEXT("ShaderPrint font size.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarFontSpacingX(
		TEXT("r.ShaderPrint.FontSpacingX"),
		0,
		TEXT("ShaderPrint horizontal spacing between symbols.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarFontSpacingY(
		TEXT("r.ShaderPrint.FontSpacingY"),
		8,
		TEXT("ShaderPrint vertical spacing between symbols.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarMaxCharacterCount(
		TEXT("r.ShaderPrint.MaxCharacters"),
		2000,
		TEXT("ShaderPrint output buffer size.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarMaxWidgetCount(
		TEXT("r.ShaderPrint.MaxWidget"),
		32,
		TEXT("ShaderPrint max widget count.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarMaxLineCount(
		TEXT("r.ShaderPrint.MaxLine"),
		32,
		TEXT("ShaderPrint max line count.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarDrawLock(
		TEXT("r.ShaderPrint.Lock"),
		0,
		TEXT("Lock the line drawing.\n"),
		ECVF_Cheat | ECVF_RenderThreadSafe);

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Global states

	static uint32 GWidgetRequestCount = 0;
	static uint32 GCharacterRequestCount = 0;
	static uint32 GLineRequestCount = 0;
	static bool GShaderPrintEnableOverride = false;
	static FViewInfo* GDefaultView = nullptr;

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Struct & Functions
	
	// Structure used by shader buffers to store values and symbols
	struct ShaderPrintItem
	{
		FVector2D ScreenPos;
		int32 Value;
		int32 Type;
	};

	// Empty buffer for binding when ShaderPrint is disabled
	class FEmptyBuffer : public FBufferWithRDG
	{
	public:
		void InitRHI() override
		{
			if (!Buffer.IsValid())
			{
				FRHICommandList* UnusedCmdList = new FRHICommandList(FRHIGPUMask::All());
				GetPooledFreeBuffer(*UnusedCmdList, FRDGBufferDesc::CreateStructuredDesc(sizeof(ShaderPrintItem), 1), Buffer, TEXT("ShaderPrint.EmptyValueBuffer"));
				delete UnusedCmdList;
				UnusedCmdList = nullptr;
			}
		}
	};

	FBufferWithRDG* GEmptyBuffer = new TGlobalResource<FEmptyBuffer>();

	// Get value buffer size
	// Note that if the ShaderPrint system is disabled we still want to bind a minimal buffer
	static uint32 GetMaxValueCount()
	{
		uint32 MaxValueCount = FMath::Max(CVarMaxCharacterCount.GetValueOnRenderThread() + int32(GCharacterRequestCount), 0);
		return IsEnabled() ? MaxValueCount : 0;
	}

	static uint32 GetMaxWidgetCount()
	{
		uint32 MaxValueCount = FMath::Max(CVarMaxWidgetCount.GetValueOnRenderThread() + int32(GWidgetRequestCount), 0);
		return IsEnabled() ? MaxValueCount : 0;
	}

	static uint32 GetMaxLineCount()
	{
		uint32 MaxValueCount = FMath::Max(CVarMaxLineCount.GetValueOnRenderThread() + int32(GLineRequestCount), 0);
		return IsEnabled() ? MaxValueCount : 0;
	}

	// Get symbol buffer size
	// This is some multiple of the value buffer size to allow for maximum value->symbol expansion
	static uint32 GetMaxSymbolCount()
	{
		return GetMaxValueCount() * 12u;
	}

	static bool IsDrawLocked()
	{
		return CVarDrawLock.GetValueOnRenderThread() > 0;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Uniform buffer
	
	// ShaderPrint uniform buffer
	IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FShaderPrintCommonParameters, "ShaderPrint");
	
	// Fill the uniform buffer parameters
	// Return a uniform buffer with values filled and with single frame lifetime
	static TUniformBufferRef<ShaderPrint::FShaderPrintCommonParameters> CreateUniformBuffer(const FShaderPrintData& Data)
	{		
		FShaderPrintCommonParameters Out;
		Out.FontSize = Data.FontSize;
		Out.FontSpacing = Data.FontSpacing;
		Out.Resolution = Data.OutputRect.Size();
		Out.CursorCoord = Data.CursorCoord;
		Out.MaxValueCount = Data.MaxValueCount;
		Out.MaxSymbolCount = Data.MaxSymbolCount;
		Out.MaxStateCount = Data.MaxStateCount;
		Out.MaxLineCount = Data.MaxLineCount;
		Out.TranslatedWorldOffset = Data.TranslatedWorldOffset;
		return TUniformBufferRef<ShaderPrint::FShaderPrintCommonParameters>::CreateUniformBufferImmediate(Out, UniformBuffer_SingleFrame);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Accessors

	void SetParameters(FRDGBuilder& GraphBuilder, FShaderParameters& OutParameters)
	{
		if (GDefaultView != nullptr)
		{
			SetParameters(GraphBuilder, GDefaultView->ShaderPrintData, OutParameters);
		}
	}

	// Fill the FShaderParameters parameters
	void SetParameters(FRDGBuilder& GraphBuilder, const FViewInfo & View, FShaderParameters& OutParameters)
	{
		SetParameters(GraphBuilder, View.ShaderPrintData, OutParameters);
	}

	// Fill the FShaderParameters parameters
	void SetParameters(FRDGBuilder& GraphBuilder, const FShaderPrintData& Data, FShaderParameters& OutParameters)
	{
		OutParameters.Common = Data.UniformBuffer;
		OutParameters.ShaderPrint_StateBuffer = GraphBuilder.CreateSRV(Data.ShaderPrintStateBuffer);
		OutParameters.ShaderPrint_RWValuesBuffer = GraphBuilder.CreateUAV(Data.ShaderPrintValueBuffer);
		OutParameters.ShaderPrint_RWLinesBuffer = GraphBuilder.CreateUAV(Data.ShaderPrintLineBuffer);
	}

	// Supported platforms
	bool IsSupported(EShaderPlatform InShaderPlatform)
	{
		return RHISupportsComputeShaders(InShaderPlatform) && !IsHlslccShaderPlatform(InShaderPlatform);
	}

	void SetEnabled(bool bInEnabled)
	{
		if (IsInGameThread())
		{
			CVarEnable->Set(bInEnabled);
		}
		else
		{
			GShaderPrintEnableOverride = bInEnabled;
		}
	}

	void SetFontSize(int32 InFontSize)
	{
		CVarFontSize->Set(FMath::Clamp(InFontSize, 6, 128));
	}

	void RequestSpaceForCharacters(uint32 InCount)
	{
		GCharacterRequestCount += InCount;
	}

	void RequestSpaceForLines(uint32 InCount)
	{
		GLineRequestCount += InCount;
	}

	bool IsEnabled()
	{
		return CVarEnable.GetValueOnAnyThread() != 0 || GShaderPrintEnableOverride;
	}

	bool IsEnabled(const FViewInfo& View)
	{
		return IsEnabled() && IsSupported(View.GetShaderPlatform());
	}

	// Returns true if the default view exists and has shader debug rendering enabled (this needs to be checked before using a permutation that requires the shader draw parameters)
	bool IsDefaultViewEnabled()
	{
		return GDefaultView != nullptr && IsEnabled(*GDefaultView);
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Widget/Characters Shaders
	 
	// Shader to initialize the output value buffer
	class FShaderInitValueBufferCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FShaderInitValueBufferCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderInitValueBufferCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<ShaderPrintItem>, RWValuesBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderInitValueBufferCS, "/Engine/Private/ShaderPrintDraw.usf", "InitValueBufferCS", SF_Compute);

	// Shader to fill the indirect parameter arguments ready for the value->symbol compute pass
	class FShaderBuildIndirectDispatchArgsCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FShaderBuildIndirectDispatchArgsCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderBuildIndirectDispatchArgsCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_REF(FShaderPrintCommonParameters, Common)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<ShaderPrintItem>, ValuesBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<ShaderPrintItem>, RWSymbolsBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWIndirectDispatchArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderBuildIndirectDispatchArgsCS, "/Engine/Private/ShaderPrintDraw.usf", "BuildIndirectDispatchArgsCS", SF_Compute);

	// Shader to clean & compact widget state
	class FShaderCompactStateBufferCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FShaderCompactStateBufferCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderCompactStateBufferCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(uint32, FrameIndex)
			SHADER_PARAMETER(uint32, FrameThreshold)
			SHADER_PARAMETER_STRUCT_REF(FShaderPrintCommonParameters, Common)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWStateBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderCompactStateBufferCS, "/Engine/Private/ShaderPrintDraw.usf", "CompactStateBufferCS", SF_Compute);

	// Shader to read the values buffer and convert to the symbols buffer
	class FShaderBuildSymbolBufferCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FShaderBuildSymbolBufferCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderBuildSymbolBufferCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(uint32, FrameIndex)
			SHADER_PARAMETER_STRUCT_REF(FShaderPrintCommonParameters, Common)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<ShaderPrintItem>, ValuesBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<ShaderPrintItem>, RWSymbolsBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWStateBuffer)
			RDG_BUFFER_ACCESS(IndirectDispatchArgsBuffer, ERHIAccess::IndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderBuildSymbolBufferCS, "/Engine/Private/ShaderPrintDraw.usf", "BuildSymbolBufferCS", SF_Compute);

	// Shader to fill the indirect parameter arguments ready for draw pass
	class FShaderBuildIndirectDrawArgsCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FShaderBuildIndirectDrawArgsCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderBuildIndirectDrawArgsCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_REF(FShaderPrintCommonParameters, Common)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<ShaderPrintItem>, SymbolsBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWIndirectDrawArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderBuildIndirectDrawArgsCS, "/Engine/Private/ShaderPrintDraw.usf", "BuildIndirectDrawArgsCS", SF_Compute);

	// Shader for draw pass to render each symbol
	class FShaderDrawSymbols : public FGlobalShader
	{
	public:
		FShaderDrawSymbols()
		{}

		FShaderDrawSymbols(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
			: FGlobalShader(Initializer)
		{}

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			RENDER_TARGET_BINDING_SLOTS()
			SHADER_PARAMETER_STRUCT_REF(FShaderPrintCommonParameters, Common)
			SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<ShaderPrintItem>, SymbolsBuffer)
			RDG_BUFFER_ACCESS(IndirectDrawArgsBuffer, ERHIAccess::IndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}
	};

	class FShaderDrawSymbolsVS : public FShaderDrawSymbols
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawSymbolsVS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawSymbolsVS, FShaderDrawSymbols);
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawSymbolsVS, "/Engine/Private/ShaderPrintDraw.usf", "DrawSymbolsVS", SF_Vertex);

	class FShaderDrawSymbolsPS : public FShaderDrawSymbols
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawSymbolsPS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawSymbolsPS, FShaderDrawSymbols);
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawSymbolsPS, "/Engine/Private/ShaderPrintDraw.usf", "DrawSymbolsPS", SF_Pixel);

	//////////////////////////////////////////////////////////////////////////
	// Line Shaders

	class FShaderDrawDebugClearCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugClearCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugClearCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, RWElementBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_CLEAR_CS"), 1);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawDebugClearCS, "/Engine/Private/ShaderPrintDrawPrimitive.usf", "ShaderDrawDebugClearCS", SF_Compute);
	
	class FShaderDrawDebugCopyCS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugCopyCS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugCopyCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, ElementBuffer)
			SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, RWIndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_COPY_CS"), 1);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawDebugCopyCS, "/Engine/Private/ShaderPrintDrawPrimitive.usf", "ShaderDrawDebugCopyCS", SF_Compute);

	class FShaderDrawDebugVS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugVS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugVS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector3f, TranslatedWorldOffsetConversion)
			SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
			SHADER_PARAMETER_SRV(StructuredBuffer, LockedShaderDrawDebugPrimitive)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, ShaderDrawDebugPrimitive)
			RDG_BUFFER_ACCESS(IndirectBuffer, ERHIAccess::IndirectArgs)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_VS"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_PS"), 0);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawDebugVS, "/Engine/Private/ShaderPrintDrawPrimitive.usf", "ShaderDrawDebugVS", SF_Vertex);


	class FShaderDrawDebugPS : public FGlobalShader
	{
		DECLARE_GLOBAL_SHADER(FShaderDrawDebugPS);
		SHADER_USE_PARAMETER_STRUCT(FShaderDrawDebugPS, FGlobalShader);
		
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER(FVector2f, OutputInvResolution)
			SHADER_PARAMETER(FVector2f, OriginalViewRectMin)
			SHADER_PARAMETER(FVector2f, OriginalViewSize)
			SHADER_PARAMETER(FVector2f, OriginalBufferInvSize)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		using FPermutationDomain = TShaderPermutationDomain<>;

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsSupported(Parameters.Platform);
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING"), 1);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_VS"), 0);
			OutEnvironment.SetDefine(TEXT("GPU_DEBUG_RENDERING_PS"), 1);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FShaderDrawDebugPS, "/Engine/Private/ShaderPrintDrawPrimitive.usf", "ShaderDrawDebugPS", SF_Pixel);

	BEGIN_SHADER_PARAMETER_STRUCT(FShaderDrawVSPSParameters , )
		SHADER_PARAMETER_STRUCT_INCLUDE(FShaderDrawDebugVS::FParameters, VS)
		SHADER_PARAMETER_STRUCT_INCLUDE(FShaderDrawDebugPS::FParameters, PS)
	END_SHADER_PARAMETER_STRUCT()

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Drawing/Rendering API

	void BeginView(FRDGBuilder& GraphBuilder, FViewInfo& View)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ShaderPrint::BeginView);

		View.ShaderPrintData = FShaderPrintData();
		if (!IsSupported(View.GetShaderPlatform()))
		{
			return;
		}

		const FIntPoint ViewSize(FMath::Max(View.UnconstrainedViewRect.Size().X, 1), FMath::Max(View.UnconstrainedViewRect.Size().Y, 1));

		float FontSize = float(FMath::Max(CVarFontSize.GetValueOnRenderThread(), 1)) * View.Family->DebugDPIScale;
		float FontSpacingX = float(FMath::Max(CVarFontSpacingX.GetValueOnRenderThread(), 1)) * View.Family->DebugDPIScale;
		float FontSpacingY = float(FMath::Max(CVarFontSpacingY.GetValueOnRenderThread(), 1)) * View.Family->DebugDPIScale;

		const float FontWidth = FontSize / float(ViewSize.X);
		const float FontHeight = FontSize / float(ViewSize.Y);
		const float SpaceWidth = FontSpacingX / float(ViewSize.X);
		const float SpaceHeight = FontSpacingY / float(ViewSize.Y);

		View.ShaderPrintData.FontSize = FVector2f(FontWidth, FontHeight);
		View.ShaderPrintData.FontSpacing = FVector2f(SpaceWidth + FontWidth, SpaceHeight + FontHeight);		
		View.ShaderPrintData.OutputRect = View.UnconstrainedViewRect;
		View.ShaderPrintData.MaxValueCount = GetMaxValueCount();
		View.ShaderPrintData.MaxSymbolCount = GetMaxSymbolCount();
		View.ShaderPrintData.MaxStateCount = GetMaxWidgetCount();
		View.ShaderPrintData.MaxLineCount = GetMaxLineCount();
		View.ShaderPrintData.CursorCoord = View.CursorPos;
		View.ShaderPrintData.TranslatedWorldOffset = View.ViewMatrices.GetPreViewTranslation();
		GCharacterRequestCount = 0;
		GWidgetRequestCount = 0;
		GLineRequestCount = 0;

		// Early out if system is disabled
		// Note that we still bind a dummy ShaderPrintValueBuffer 
		// This is in case some debug shader code is still active (we don't want an unbound buffer!)
		if (!IsEnabled())
		{
			View.ShaderPrintData.UniformBuffer = CreateUniformBuffer(View.ShaderPrintData);
			View.ShaderPrintData.ShaderPrintValueBuffer = GraphBuilder.RegisterExternalBuffer(GEmptyBuffer->Buffer);
			View.ShaderPrintData.ShaderPrintStateBuffer = GraphBuilder.RegisterExternalBuffer(GEmptyBuffer->Buffer);
			View.ShaderPrintData.ShaderPrintLineBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, 8), TEXT("ShaderPrint.LineBuffer(Dummy)"), ERDGBufferFlags::None);
			return;
		}

		// Invalid to call begin twice for the same view.
		check(GDefaultView != &View);
		if (GDefaultView == nullptr)
		{
			GDefaultView = &View;
		}

		// Primitives/Lines
		// ----------------
		{			
			FSceneViewState* ViewState = View.ViewState;
			const bool bLockBufferThisFrame = IsDrawLocked() && ViewState && !ViewState->ShaderPrintStateData.bIsLocked;
			ERDGBufferFlags Flags = bLockBufferThisFrame ? ERDGBufferFlags::MultiFrame : ERDGBufferFlags::None;

			View.ShaderPrintData.ShaderPrintLineBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, 8 * View.ShaderPrintData.MaxLineCount), TEXT("ShaderPrint.LineBuffer"), Flags);

			// Clear buffer counter
			{
				FShaderDrawDebugClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FShaderDrawDebugClearCS::FParameters>();
				Parameters->RWElementBuffer = GraphBuilder.CreateUAV(View.ShaderPrintData.ShaderPrintLineBuffer);

				TShaderMapRef<FShaderDrawDebugClearCS> ComputeShader(View.ShaderMap);
				ClearUnusedGraphResources(ComputeShader, Parameters);
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("ShaderPrint::BeginView(Clear lines)"),
					Parameters,
					ERDGPassFlags::Compute,
					[Parameters, ComputeShader](FRHICommandList& RHICmdList)
					{
						FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, FIntVector(1, 1, 1));
					});
			}

			if (IsDrawLocked() && ViewState && !ViewState->ShaderPrintStateData.bIsLocked)
			{
				ViewState->ShaderPrintStateData.LineBuffer = GraphBuilder.ConvertToExternalBuffer(View.ShaderPrintData.ShaderPrintLineBuffer);
				ViewState->ShaderPrintStateData.PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
				ViewState->ShaderPrintStateData.bIsLocked = true;
			}

			if (!IsDrawLocked() && ViewState && ViewState->ShaderPrintStateData.bIsLocked)
			{
				ViewState->ShaderPrintStateData.LineBuffer = nullptr;
				ViewState->ShaderPrintStateData.PreViewTranslation = FVector::ZeroVector;
				ViewState->ShaderPrintStateData.bIsLocked = false;
			}
		}

		// Characters & Widgets
		// --------------------
		{
			// Initialize output buffer and store in the view info
			// Values buffer contains Count + 1 elements. The first element is only used as a counter.
			View.ShaderPrintData.ShaderPrintValueBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(ShaderPrintItem), View.ShaderPrintData.MaxValueCount + 1), TEXT("ShaderPrint.ValueBuffer"));

			// State buffer is retrieved from the view state, or created if it does not exist
			View.ShaderPrintData.ShaderPrintStateBuffer = nullptr;
			if (FSceneViewState* ViewState = View.ViewState)
			{
				if (ViewState->ShaderPrintStateData.StateBuffer)
				{
					View.ShaderPrintData.ShaderPrintStateBuffer = GraphBuilder.RegisterExternalBuffer(ViewState->ShaderPrintStateData.StateBuffer);
				}
				else
				{
					View.ShaderPrintData.ShaderPrintStateBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), (3*View.ShaderPrintData.MaxStateCount) + 1), TEXT("ShaderPrint.StateBuffer"));
					AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(View.ShaderPrintData.ShaderPrintStateBuffer, PF_R32_UINT), 0u);
					ViewState->ShaderPrintStateData.StateBuffer = GraphBuilder.ConvertToExternalBuffer(View.ShaderPrintData.ShaderPrintStateBuffer);
				}
			}
			else
			{
				View.ShaderPrintData.MaxStateCount = 0;
				View.ShaderPrintData.ShaderPrintStateBuffer = GraphBuilder.RegisterExternalBuffer(GEmptyBuffer->Buffer);
			}

			// Clear the output buffer internal counter ready for use
			const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();
			FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
			TShaderMapRef< FShaderInitValueBufferCS > ComputeShader(GlobalShaderMap);

			auto* PassParameters = GraphBuilder.AllocParameters<FShaderInitValueBufferCS::FParameters>();
			PassParameters->RWValuesBuffer = GraphBuilder.CreateUAV(View.ShaderPrintData.ShaderPrintValueBuffer);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ShaderPrint::BeginView(Clear characters)"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));
		}

		// Create common parameters uniform buffer
		View.ShaderPrintData.UniformBuffer = CreateUniformBuffer(View.ShaderPrintData);
	}

	static void InternalDrawView_Characters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture OutputTexture)
	{
		const FIntRect Viewport = OutputTexture.ViewRect;
	
		// Initialize graph managed resources
		// Symbols buffer contains Count + 1 elements. The first element is only used as a counter.
		FRDGBufferRef SymbolBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(ShaderPrintItem), GetMaxSymbolCount() + 1), TEXT("ShaderPrint.SymbolBuffer"));
		FRDGBufferRef IndirectDispatchArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("ShaderPrint.IndirectDispatchArgs"));
		FRDGBufferRef IndirectDrawArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(5), TEXT("ShaderPrint.IndirectDrawArgs"));

		// Non graph managed resources
		FRDGBufferSRVRef ValuesBuffer = GraphBuilder.CreateSRV(View.ShaderPrintData.ShaderPrintValueBuffer);
		FRDGBufferSRVRef StateBuffer = GraphBuilder.CreateSRV(View.ShaderPrintData.ShaderPrintStateBuffer);
		FTextureRHIRef FontTexture = GSystemTextures.AsciiTexture->GetRenderTargetItem().ShaderResourceTexture;

		const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);

		// BuildIndirectDispatchArgs
		{
			typedef FShaderBuildIndirectDispatchArgsCS SHADER;
			TShaderMapRef<SHADER> ComputeShader(GlobalShaderMap);

			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->Common = View.ShaderPrintData.UniformBuffer;
			PassParameters->ValuesBuffer = ValuesBuffer;
			PassParameters->RWSymbolsBuffer = GraphBuilder.CreateUAV(SymbolBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->RWIndirectDispatchArgsBuffer = GraphBuilder.CreateUAV(IndirectDispatchArgsBuffer, EPixelFormat::PF_R32_UINT);

			FComputeShaderUtils::AddPass(
				GraphBuilder, 
				RDG_EVENT_NAME("ShaderPrint::BuildIndirectDispatchArgs"), 
				ComputeShader, PassParameters,
				FIntVector(1, 1, 1));
		}

		// BuildSymbolBuffer
		{
			typedef FShaderBuildSymbolBufferCS SHADER;
			TShaderMapRef<SHADER> ComputeShader(GlobalShaderMap);

			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->FrameIndex = View.Family ? View.Family->FrameNumber : 0u;
			PassParameters->Common = View.ShaderPrintData.UniformBuffer;
			PassParameters->ValuesBuffer = ValuesBuffer;
			PassParameters->RWSymbolsBuffer = GraphBuilder.CreateUAV(SymbolBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->RWStateBuffer = GraphBuilder.CreateUAV(View.ShaderPrintData.ShaderPrintStateBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->IndirectDispatchArgsBuffer = IndirectDispatchArgsBuffer;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ShaderPrint::BuildSymbolBuffer"),
				ComputeShader, PassParameters,
				IndirectDispatchArgsBuffer, 0);
		}

		// CompactStateBuffer
		{
			typedef FShaderCompactStateBufferCS SHADER;
			TShaderMapRef<SHADER> ComputeShader(GlobalShaderMap);

			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->FrameIndex = View.Family ? View.Family->FrameNumber : 0u;
			PassParameters->FrameThreshold = 300u;
			PassParameters->Common = View.ShaderPrintData.UniformBuffer;
			PassParameters->RWStateBuffer = GraphBuilder.CreateUAV(View.ShaderPrintData.ShaderPrintStateBuffer, EPixelFormat::PF_R32_UINT);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ShaderPrint::CompactStateBuffer"),
				ComputeShader, PassParameters, FIntVector(1,1,1));
		}

		// BuildIndirectDrawArgs
		{
			typedef FShaderBuildIndirectDrawArgsCS SHADER;
			TShaderMapRef<SHADER> ComputeShader(GlobalShaderMap);

			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->Common = View.ShaderPrintData.UniformBuffer;
			PassParameters->SymbolsBuffer = GraphBuilder.CreateSRV(SymbolBuffer);
			PassParameters->RWIndirectDrawArgsBuffer = GraphBuilder.CreateUAV(IndirectDrawArgsBuffer, EPixelFormat::PF_R32_UINT);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ShaderPrint::BuildIndirectDrawArgs"),
				ComputeShader, PassParameters,
				FIntVector(1, 1, 1));
		}

		// DrawSymbols
		{
			typedef FShaderDrawSymbols SHADER;
			TShaderMapRef< FShaderDrawSymbolsVS > VertexShader(GlobalShaderMap);
			TShaderMapRef< FShaderDrawSymbolsPS > PixelShader(GlobalShaderMap);

			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture.Texture, ERenderTargetLoadAction::ELoad);
			PassParameters->Common = View.ShaderPrintData.UniformBuffer;
			PassParameters->MiniFontTexture = FontTexture;
			PassParameters->SymbolsBuffer = GraphBuilder.CreateSRV(SymbolBuffer);
			PassParameters->IndirectDrawArgsBuffer = IndirectDrawArgsBuffer;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ShaderPrint::DrawSymbols"),
				PassParameters,
				ERDGPassFlags::Raster,
				[VertexShader, PixelShader, PassParameters, Viewport](FRHICommandList& RHICmdList)
			{
				
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *PassParameters);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				RHICmdList.DrawIndexedPrimitiveIndirect(GTwoTrianglesIndexBuffer.IndexBufferRHI, PassParameters->IndirectDrawArgsBuffer->GetIndirectRHICallBuffer(), 0);
			});
		}
	}

	static void InternalDrawView_Lines(
		FRDGBuilder& GraphBuilder,
		const FViewInfo& View,
		const FVector& TranslatedWorldOffsetConversion,
		FRDGBufferRef DataBuffer,
		FRDGTextureRef OutputTexture,
		FRDGTextureRef DepthTexture)
	{
		FRDGBufferRef IndirectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("ShaderDraw.IndirectBuffer"), ERDGBufferFlags::None);
		{
			FShaderDrawDebugCopyCS::FParameters* Parameters = GraphBuilder.AllocParameters<FShaderDrawDebugCopyCS::FParameters>();
			Parameters->ElementBuffer = GraphBuilder.CreateSRV(DataBuffer);
			Parameters->RWIndirectArgs = GraphBuilder.CreateUAV(IndirectBuffer, PF_R32_UINT);

			TShaderMapRef<FShaderDrawDebugCopyCS> ComputeShader(View.ShaderMap);
			ClearUnusedGraphResources(ComputeShader, Parameters);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ShaderPrint::CopyLineArgs"),
				Parameters,
				ERDGPassFlags::Compute,
				[Parameters, ComputeShader](FRHICommandList& RHICmdList)
				{
					FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *Parameters, FIntVector(1, 1, 1));
				});
		}

		TShaderMapRef<FShaderDrawDebugVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FShaderDrawDebugPS> PixelShader(View.ShaderMap);

		FShaderDrawVSPSParameters* PassParameters = GraphBuilder.AllocParameters<FShaderDrawVSPSParameters >();
		PassParameters->VS.View = View.ViewUniformBuffer;
		PassParameters->VS.TranslatedWorldOffsetConversion = FVector3f(TranslatedWorldOffsetConversion);
		PassParameters->PS.RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->PS.OutputInvResolution = FVector2f(1.f / View.UnscaledViewRect.Width(), 1.f / View.UnscaledViewRect.Height());
		PassParameters->PS.OriginalViewRectMin = FVector2f(View.ViewRect.Min);
		PassParameters->PS.OriginalViewSize = FVector2f(View.ViewRect.Width(), View.ViewRect.Height());
		PassParameters->PS.OriginalBufferInvSize = FVector2f(1.f / DepthTexture->Desc.Extent.X, 1.f / DepthTexture->Desc.Extent.Y);
		PassParameters->PS.DepthTexture = DepthTexture;
		PassParameters->PS.DepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->VS.ShaderDrawDebugPrimitive = GraphBuilder.CreateSRV(DataBuffer);
		PassParameters->VS.IndirectBuffer = IndirectBuffer;

		ValidateShaderParameters(PixelShader, PassParameters->PS);
		ClearUnusedGraphResources(PixelShader, &PassParameters->PS, { IndirectBuffer });
		ValidateShaderParameters(VertexShader, PassParameters->VS);
		ClearUnusedGraphResources(VertexShader, &PassParameters->VS, { IndirectBuffer });

		const FIntRect Viewport = View.UnscaledViewRect;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ShaderPrint::DrawLines"),
			PassParameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, PassParameters, IndirectBuffer, Viewport](FRHICommandList& RHICmdList)
			{
				// Marks the indirect draw parameter as used by the pass, given it's not used directly by any of the shaders.
				PassParameters->VS.IndirectBuffer->MarkResourceAsUsed();

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI(); // Premultiplied-alpha composition
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None, true>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_LineList;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

				// Marks the indirect draw parameter as used by the pass, given it's not used directly by any of the shaders.
				FRHIBuffer* IndirectBufferRHI = PassParameters->VS.IndirectBuffer->GetIndirectRHICallBuffer();
				check(IndirectBufferRHI != nullptr);
				RHICmdList.DrawPrimitiveIndirect(IndirectBufferRHI, 0);
			});
	}

	void DrawView(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FScreenPassTexture& OutputTexture, const FScreenPassTexture& DepthTexture)
	{
		check(OutputTexture.IsValid());

		RDG_EVENT_SCOPE(GraphBuilder, "ShaderPrint::DrawView");

		// Lines
		{
			FRDGBufferRef DataBuffer = View.ShaderPrintData.ShaderPrintLineBuffer;
			InternalDrawView_Lines(GraphBuilder, View, FVector::ZeroVector, DataBuffer, OutputTexture.Texture, DepthTexture.Texture);
		}

		// Locked lines
		if (View.ViewState && View.ViewState->ShaderPrintStateData.bIsLocked)
		{
			const FVector LockedToCurrentTranslatedOffset = View.ViewMatrices.GetPreViewTranslation() - View.ViewState->ShaderPrintStateData.PreViewTranslation;
			FRDGBufferRef DataBuffer = GraphBuilder.RegisterExternalBuffer(View.ViewState->ShaderPrintStateData.LineBuffer);
			InternalDrawView_Lines(GraphBuilder, View, LockedToCurrentTranslatedOffset, DataBuffer, OutputTexture.Texture, DepthTexture.Texture);
		}

		// Characters
		{
			InternalDrawView_Characters(GraphBuilder, View, OutputTexture);
		}
	}

	void EndView(FViewInfo& View)
	{
		View.ShaderPrintData = FShaderPrintData();
		GDefaultView = nullptr;
	}
}
