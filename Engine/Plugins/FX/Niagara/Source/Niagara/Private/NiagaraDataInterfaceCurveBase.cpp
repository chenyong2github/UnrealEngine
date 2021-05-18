// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCurveBase.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveFloat.h"
#include "Engine/Texture2D.h"
#include "Internationalization/Internationalization.h"
#include "ShaderParameterUtils.h"

#include "NiagaraCustomVersion.h"
#include "NiagaraEmitter.h"
#include "NiagaraShader.h"
#include "NiagaraTypes.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceCurveBase"

IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_Curve);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceCurveBase, FNiagaraDataInterfaceParametersCS_Curve);

void FNiagaraDataInterfaceParametersCS_Curve::Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
{
	MinTime.Bind(ParameterMap, *(TEXT("MinTime_") + ParameterInfo.DataInterfaceHLSLSymbol));
	MaxTime.Bind(ParameterMap, *(TEXT("MaxTime_") + ParameterInfo.DataInterfaceHLSLSymbol));
	InvTimeRange.Bind(ParameterMap, *(TEXT("InvTimeRange_") + ParameterInfo.DataInterfaceHLSLSymbol));
	CurveLUTNumMinusOne.Bind(ParameterMap, *(TEXT("CurveLUTNumMinusOne_") + ParameterInfo.DataInterfaceHLSLSymbol));
	CurveLUT.Bind(ParameterMap, *(TEXT("CurveLUT_") + ParameterInfo.DataInterfaceHLSLSymbol));
}

void FNiagaraDataInterfaceParametersCS_Curve::Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
{
	check(IsInRenderingThread());

	FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();
	FNiagaraDataInterfaceProxyCurveBase* CurveDI = (FNiagaraDataInterfaceProxyCurveBase*)Context.DataInterface;
	FReadBuffer& CurveLUTBuffer = CurveDI->CurveLUT;

	SetShaderValue(RHICmdList, ComputeShaderRHI, MinTime, CurveDI->LUTMinTime);
	SetShaderValue(RHICmdList, ComputeShaderRHI, MaxTime, CurveDI->LUTMaxTime);
	SetShaderValue(RHICmdList, ComputeShaderRHI, InvTimeRange, CurveDI->LUTInvTimeRange);
	SetShaderValue(RHICmdList, ComputeShaderRHI, CurveLUTNumMinusOne, CurveDI->CurveLUTNumMinusOne);
	SetSRVParameter(RHICmdList, ComputeShaderRHI, CurveLUT, CurveLUTBuffer.SRV);
}



float GNiagaraLUTOptimizeThreshold = UNiagaraDataInterfaceCurveBase::DefaultOptimizeThreshold;
static FAutoConsoleVariableRef CVarNiagaraLUTOptimizeThreshold(
	TEXT("fx.Niagara.LUT.OptimizeThreshold"),
	GNiagaraLUTOptimizeThreshold,
	TEXT("Error Threshold used when optimizing Curve LUTs, setting to 0.0 or below will result in no optimization\n"),
	ECVF_Default
);

int32 GNiagaraLUTVerifyPostLoad = 0;
static FAutoConsoleVariableRef CVarNiagaraLUTVerifyPostLoad(
	TEXT("fx.Niagara.LUT.VerifyPostLoad"),
	GNiagaraLUTVerifyPostLoad,
	TEXT("Enable to verify LUTs match in PostLoad vs the Loaded Data\n"),
	ECVF_Default
);

void UNiagaraDataInterfaceCurveBase::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (HasEditorData)
	{
		const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);

		if (NiagaraVer < FNiagaraCustomVersion::LatestVersion)
		{
			UpdateLUT();
		}
	}
#endif
}

void UNiagaraDataInterfaceCurveBase::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// Push to render thread if loading
	if (Ar.IsLoading())
	{
#if WITH_EDITORONLY_DATA
		HasEditorData = !Ar.IsFilterEditorOnly();
		// Sometimes curves are out of date which needs to be tracked down
		// Temporarily we will make sure they are up to date in editor builds
		if (HasEditorData && GetClass() != UNiagaraDataInterfaceCurveBase::StaticClass())
		{
			UpdateLUT(true);
		}
#endif
		MarkRenderDataDirty();
	}
}

#if WITH_EDITOR
void UNiagaraDataInterfaceCurveBase::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraDataInterfaceCurveBase, bExposeCurve))
	{
		UpdateExposedTexture();
	}
}
#endif

bool UNiagaraDataInterfaceCurveBase::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceCurveBase* DestinationTyped = CastChecked<UNiagaraDataInterfaceCurveBase>(Destination);
	DestinationTyped->bUseLUT = bUseLUT;
	DestinationTyped->ShaderLUT = ShaderLUT;
	DestinationTyped->LUTNumSamplesMinusOne = LUTNumSamplesMinusOne;
#if WITH_EDITORONLY_DATA
	DestinationTyped->bOptimizeLUT = bOptimizeLUT;
	DestinationTyped->bOverrideOptimizeThreshold = bOverrideOptimizeThreshold;
	DestinationTyped->OptimizeThreshold = OptimizeThreshold;
#endif
	DestinationTyped->bExposeCurve = bExposeCurve;
	DestinationTyped->ExposedName = ExposedName;
	DestinationTyped->MarkRenderDataDirty();

	return true;
}


bool UNiagaraDataInterfaceCurveBase::CompareLUTS(const TArray<float>& OtherLUT) const
{
	if (ShaderLUT.Num() == OtherLUT.Num())
	{
		bool bMatched = true;
		for (int32 i = 0; i < ShaderLUT.Num(); i++)
		{
			if (false == FMath::IsNearlyEqual(ShaderLUT[i], OtherLUT[i], 0.0001f))
			{
				bMatched = false;
				UE_LOG(LogNiagara, Log, TEXT("First LUT mismatch found on comparison - LUT[%d] = %.9f  Other = %.9f \t%.9f"), i, ShaderLUT[i], OtherLUT[i], fabsf(ShaderLUT[i] - OtherLUT[i]));
				break;
			}
		}
		return bMatched;
	}
	else
	{
		UE_LOG(LogNiagara, Log, TEXT("Table sizes don't match"));
		return false;
	}
}

void UNiagaraDataInterfaceCurveBase::SetDefaultLUT()
{
	ShaderLUT.Empty(GetCurveNumElems());
	ShaderLUT.AddDefaulted(GetCurveNumElems());
	LUTNumSamplesMinusOne = 0;
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceCurveBase::UpdateLUT(bool bFromSerialize)
{
	UpdateTimeRanges();
	if (bUseLUT)
	{
		ShaderLUT = BuildLUT(CurveLUTDefaultWidth);
		OptimizeLUT();
		LUTNumSamplesMinusOne = float(ShaderLUT.Num() / GetCurveNumElems()) - 1;
	}

	if (!bUseLUT || (ShaderLUT.Num() == 0))
	{
		SetDefaultLUT();
	}

	if (!bFromSerialize)
	{
		UpdateExposedTexture();
	}

	MarkRenderDataDirty();
}

void UNiagaraDataInterfaceCurveBase::OptimizeLUT()
{
	// Do we optimize this LUT?
	if (!bOptimizeLUT)
	{
		return;
	}

	// Check error threshold is valid for us to optimize
	const float ErrorThreshold = bOverrideOptimizeThreshold ? OptimizeThreshold : GNiagaraLUTOptimizeThreshold;
	if (ErrorThreshold <= 0.0f)
	{
		return;
	}

	const int32 NumElements = GetCurveNumElems();
	check((ShaderLUT.Num() % NumElements) == 0);

	const int CurrNumSamples = ShaderLUT.Num() / NumElements;

	for (int32 NewNumSamples = 1; NewNumSamples < CurrNumSamples; ++NewNumSamples)
	{
		TArray<float> ResampledLUT = BuildLUT(NewNumSamples);

		bool bCanUseLUT = true;
		for (int iSample = 0; iSample < CurrNumSamples; ++iSample)
		{
			const float NormalizedSampleTime = float(iSample) / float(CurrNumSamples - 1);

			const float LhsInterp = FMath::Frac(NormalizedSampleTime * CurrNumSamples);
			const int LhsSampleA = FMath::Min(FMath::FloorToInt(NormalizedSampleTime * CurrNumSamples), CurrNumSamples - 1);
			const int LhsSampleB = FMath::Min(LhsSampleA + 1, CurrNumSamples - 1);

			const float RhsInterp = FMath::Frac(NormalizedSampleTime * NewNumSamples);
			const int RhsSampleA = FMath::Min(FMath::FloorToInt(NormalizedSampleTime * NewNumSamples), NewNumSamples - 1);
			const int RhsSampleB = FMath::Min(RhsSampleA + 1, NewNumSamples - 1);

			for (int iElement = 0; iElement < NumElements; ++iElement)
			{
				const float LhsValue = FMath::Lerp(ShaderLUT[LhsSampleA * NumElements + iElement], ShaderLUT[LhsSampleB * NumElements + iElement], LhsInterp);
				const float RhsValue = FMath::Lerp(ResampledLUT[RhsSampleA * NumElements + iElement], ResampledLUT[RhsSampleB * NumElements + iElement], RhsInterp);
				const float Error = FMath::Abs(LhsValue - RhsValue);
				if (Error > ErrorThreshold)
				{
					bCanUseLUT = false;
					break;
				}
			}

			if (!bCanUseLUT)
			{
				break;
			}
		}

		if (bCanUseLUT)
		{
			ShaderLUT = ResampledLUT;
			break;
		}
	}
}

void UNiagaraDataInterfaceCurveBase::UpdateExposedTexture()
{
	if (bExposeCurve == false)
	{
		//-TODO: Do we need to invalidate the owning system to be safe??
		ExposedTexture = nullptr;
		return;
	}

	const int32 CurveWidth = 256;

	if (ExposedTexture == nullptr)
	{
		ExposedTexture = NewObject<UTexture2D>(this);
		ExposedTexture->Source.Init(CurveWidth, 1, 1, 1, ETextureSourceFormat::TSF_RGBA16F);
		ExposedTexture->SRGB = false;
		ExposedTexture->CompressionNone = true;
		ExposedTexture->MipGenSettings = TMGS_NoMipmaps;
		ExposedTexture->AddressX = TA_Clamp;
		ExposedTexture->AddressY = TA_Clamp;
		ExposedTexture->LODGroup = TEXTUREGROUP_EffectsNotFiltered;
	}

	FFloat16Color* TexData = reinterpret_cast<FFloat16Color*>(ExposedTexture->Source.LockMip(0));

	const int32 NumElements = GetCurveNumElems();
	const FLinearColor DefaultColors(1.0f, 1.0f, 1.0f, 1.0f);

	TArray<float> TempLUT = BuildLUT(CurveWidth);
	for (int32 i=0; i < CurveWidth; ++i)
	{
		TexData[i].R = TempLUT[(i * NumElements) + 0];
		TexData[i].G = NumElements >= 2 ? TempLUT[(i * NumElements) + 1] : DefaultColors.G;
		TexData[i].B = NumElements >= 3 ? TempLUT[(i * NumElements) + 2] : DefaultColors.B;
		TexData[i].A = NumElements >= 4 ? TempLUT[(i * NumElements) + 3] : DefaultColors.A;
	}

	ExposedTexture->Source.UnlockMip(0);

	ExposedTexture->PostEditChange();

	// PostEditChange() will assign a random GUID to the texture, which leads to non-deterministic builds.
	ExposedTexture->SetDeterministicLightingGuid();
}
#endif

bool UNiagaraDataInterfaceCurveBase::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceCurveBase* OtherTyped = CastChecked<UNiagaraDataInterfaceCurveBase>(Other);
	bool bEqual = OtherTyped->bUseLUT == bUseLUT;
#if WITH_EDITORONLY_DATA
	bEqual &= OtherTyped->bOptimizeLUT == bOptimizeLUT;
	bEqual &= OtherTyped->bOverrideOptimizeThreshold == bOverrideOptimizeThreshold;
	if (bOverrideOptimizeThreshold)
	{
		bEqual &= OtherTyped->OptimizeThreshold == OptimizeThreshold;
	}
#endif
	bEqual &= OtherTyped->bExposeCurve == bExposeCurve;
	bEqual &= OtherTyped->ExposedName == ExposedName;
	if ( bEqual && bUseLUT )
	{
		bEqual &= OtherTyped->ShaderLUT == ShaderLUT;
	}

	return bEqual;
}


#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceCurveBase::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("\n");
	FString MinTimeStr = TEXT("MinTime_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString MaxTimeStr = TEXT("MaxTime_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString InvTimeRangeStr = TEXT("InvTimeRange_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString CurveLUTNumMinusOneStr = TEXT("CurveLUTNumMinusOne_") + ParamInfo.DataInterfaceHLSLSymbol;

	FString BufferName = "CurveLUT_" + ParamInfo.DataInterfaceHLSLSymbol;
	OutHLSL += TEXT("Buffer<float> ") + BufferName + TEXT(";\n");

	OutHLSL += TEXT("float ") + MinTimeStr + TEXT(";\n");
	OutHLSL += TEXT("float ") + MaxTimeStr + TEXT(";\n");
	OutHLSL += TEXT("float ") + InvTimeRangeStr + TEXT(";\n");
	OutHLSL += TEXT("float ") + CurveLUTNumMinusOneStr + TEXT(";\n");
	OutHLSL += TEXT("\n");

	//TODO: Create a Unitiliy/Common funcitons hlsl def shared between all instances of the same data interface class for these.
	OutHLSL += FString::Printf(TEXT("float TimeToLUTFraction_%s(float T)\n{\n\treturn saturate((T - %s) * %s);\n}\n"), *ParamInfo.DataInterfaceHLSLSymbol, *MinTimeStr, *InvTimeRangeStr);
	OutHLSL += FString::Printf(TEXT("float SampleCurve_%s(float T)\n{\n\treturn %s[(uint)T];\n}\n"), *ParamInfo.DataInterfaceHLSLSymbol, *BufferName);
	OutHLSL += TEXT("\n");
}
#endif

//FReadBuffer& UNiagaraDataInterfaceCurveBase::GetCurveLUTGPUBuffer()
//{
//	//TODO: This isn't really very thread safe. Need to move to a proxy like system where DIs can push data to the RT safely.
//	if (GPUBufferDirty)
//	{
//		int32 ElemSize = GetCurveNumElems();
//		CurveLUT.Release();
//		CurveLUT.Initialize(sizeof(float), CurveLUTWidth * ElemSize, EPixelFormat::PF_R32_FLOAT, BUF_Static);
//		uint32 BufferSize = ShaderLUT.Num() * sizeof(float);
//		int32 *BufferData = static_cast<int32*>(RHILockVertexBuffer(CurveLUT.Buffer, 0, BufferSize, EResourceLockMode::RLM_WriteOnly));
//		FPlatformMemory::Memcpy(BufferData, ShaderLUT.GetData(), BufferSize);
//		RHIUnlockVertexBuffer(CurveLUT.Buffer);
//		GPUBufferDirty = false;
//	}
//
//	return CurveLUT;
//}

void UNiagaraDataInterfaceCurveBase::GetExposedVariables(TArray<FNiagaraVariableBase>& OutVariables) const
{
	if (bExposeCurve)
	{
		OutVariables.Emplace(FNiagaraTypeDefinition(UTexture::StaticClass()), ExposedName);
	}
}

bool UNiagaraDataInterfaceCurveBase::GetExposedVariableValue(const FNiagaraVariableBase& InVariable, void* InPerInstanceData, FNiagaraSystemInstance* InSystemInstance, void* OutData) const
{
	if (bExposeCurve && ExposedTexture != nullptr)
	{
		*reinterpret_cast<UObject**>(OutData) = ExposedTexture;
		return true;
	}

	return false;
}

void UNiagaraDataInterfaceCurveBase::PushToRenderThreadImpl()
{
	if (!GSupportsResourceView)
	{
		return;
	}

	FNiagaraDataInterfaceProxyCurveBase* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyCurveBase>();

	const float rtLUTMinTime = this->LUTMinTime;
	const float rtLUTMaxTime = this->LUTMaxTime;
	const float rtLUTInvTimeRange = this->LUTInvTimeRange;
	const float rtCurveLUTNumMinusOne = this->LUTNumSamplesMinusOne;

	// Push Updates to Proxy.
	ENQUEUE_RENDER_COMMAND(FUpdateDIColorCurve)(
		[RT_Proxy, rtLUTMinTime, rtLUTMaxTime, rtLUTInvTimeRange, rtCurveLUTNumMinusOne, rtShaderLUT = ShaderLUT](FRHICommandListImmediate& RHICmdList)
	{
		RT_Proxy->LUTMinTime = rtLUTMinTime;
		RT_Proxy->LUTMaxTime = rtLUTMaxTime;
		RT_Proxy->LUTInvTimeRange = rtLUTInvTimeRange;
		RT_Proxy->CurveLUTNumMinusOne = rtCurveLUTNumMinusOne;

		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RT_Proxy->CurveLUT.NumBytes);
		RT_Proxy->CurveLUT.Release();

		check(rtShaderLUT.Num());
		RT_Proxy->CurveLUT.Initialize(sizeof(float), rtShaderLUT.Num(), EPixelFormat::PF_R32_FLOAT, BUF_Static);
		uint32 BufferSize = rtShaderLUT.Num() * sizeof(float);
		int32 *BufferData = static_cast<int32*>(RHILockVertexBuffer(RT_Proxy->CurveLUT.Buffer, 0, BufferSize, EResourceLockMode::RLM_WriteOnly));
		FPlatformMemory::Memcpy(BufferData, rtShaderLUT.GetData(), BufferSize);
		RHIUnlockVertexBuffer(RT_Proxy->CurveLUT.Buffer);
		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RT_Proxy->CurveLUT.NumBytes);
	});
}


#if WITH_EDITOR	
/** Refreshes and returns the errors detected with the corresponding data, if any.*/
TArray<FNiagaraDataInterfaceError> UNiagaraDataInterfaceCurveBase::GetErrors()
{
	// Trace down the root emitter (if there is one)
	TArray<FNiagaraDataInterfaceError> Errors;
	UObject* Obj = GetOuter();
	UNiagaraEmitter* Emitter = nullptr;
	while (Obj && !Obj->IsA(UPackage::StaticClass()))
	{
		Emitter = Cast<UNiagaraEmitter>(Obj);
		if (!Emitter)
			Obj = Obj->GetOuter();
		else
			break;
	}

	// If there is a root emitter, assume that we are in its particle stack and point out that we need bUseLUT true for GPU sims.
	if (Emitter && Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim && !bUseLUT)
	{
		FNiagaraDataInterfaceError LUTsNeededForGPUSimsError(LOCTEXT("LUTsNeededForGPUSims", "This Data Interface must have bUseLUT set to true for GPU sims."),
			LOCTEXT("LUTsNeededForGPUSimsSummary", "bUseLUT Required"),
			FNiagaraDataInterfaceFix());

		Errors.Add(LUTsNeededForGPUSimsError);
	}
	return Errors;
}
#endif

#undef LOCTEXT_NAMESPACE