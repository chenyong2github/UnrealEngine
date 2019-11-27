// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceVectorCurve.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveFloat.h"
#include "NiagaraTypes.h"

#if WITH_EDITORONLY_DATA
#include "Interfaces/ITargetPlatform.h"
#endif

//////////////////////////////////////////////////////////////////////////
//Vector Curve

const FName UNiagaraDataInterfaceVectorCurve::SampleCurveName(TEXT("SampleVectorCurve"));

UNiagaraDataInterfaceVectorCurve::UNiagaraDataInterfaceVectorCurve(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	UpdateLUT();
#endif
}

void UNiagaraDataInterfaceVectorCurve::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we register data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}

#if WITH_EDITORONLY_DATA
	UpdateLUT();
#endif
}

void UNiagaraDataInterfaceVectorCurve::Serialize(FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if ( bUseLUT && Ar.IsCooking() && Ar.CookingTarget()->RequiresCookedData() )
	{
		UpdateLUT();

		FRichCurve TempXCurve;
		FRichCurve TempYCurve;
		FRichCurve TempZCurve;
		Exchange(XCurve, TempXCurve);
		Exchange(YCurve, TempYCurve);
		Exchange(ZCurve, TempZCurve);

		Super::Serialize(Ar);

		Exchange(XCurve, TempXCurve);
		Exchange(YCurve, TempYCurve);
		Exchange(ZCurve, TempZCurve);
	}
	else
#endif
	{
		Super::Serialize(Ar);
		PushToRenderThread();
	}
}

void UNiagaraDataInterfaceVectorCurve::UpdateTimeRanges()
{
	if ((XCurve.GetNumKeys() > 0 || YCurve.GetNumKeys() > 0 || ZCurve.GetNumKeys() > 0))
	{
		LUTMinTime = FLT_MAX;
		LUTMinTime = FMath::Min(XCurve.GetNumKeys() > 0 ? XCurve.GetFirstKey().Time : LUTMinTime, LUTMinTime);
		LUTMinTime = FMath::Min(YCurve.GetNumKeys() > 0 ? YCurve.GetFirstKey().Time : LUTMinTime, LUTMinTime);
		LUTMinTime = FMath::Min(ZCurve.GetNumKeys() > 0 ? ZCurve.GetFirstKey().Time : LUTMinTime, LUTMinTime);

		LUTMaxTime = FLT_MIN;
		LUTMaxTime = FMath::Max(XCurve.GetNumKeys() > 0 ? XCurve.GetLastKey().Time : LUTMaxTime, LUTMaxTime);
		LUTMaxTime = FMath::Max(YCurve.GetNumKeys() > 0 ? YCurve.GetLastKey().Time : LUTMaxTime, LUTMaxTime);
		LUTMaxTime = FMath::Max(ZCurve.GetNumKeys() > 0 ? ZCurve.GetLastKey().Time : LUTMaxTime, LUTMaxTime);
		LUTInvTimeRange = 1.0f / (LUTMaxTime - LUTMinTime);
	}
	else
	{
		LUTMinTime = 0.0f;
		LUTMaxTime = 1.0f;
		LUTInvTimeRange = 1.0f;
	}
}

TArray<float> UNiagaraDataInterfaceVectorCurve::BuildLUT(int32 NumEntries) const
{
	TArray<float> OutputLUT;
	const float NumEntriesMinusOne = NumEntries - 1;

	OutputLUT.Reserve(NumEntries * 3);
	for (int32 i=0; i < NumEntries; i++)
	{
		float X = UnnormalizeTime(i / NumEntriesMinusOne);
		FVector C(XCurve.Eval(X), YCurve.Eval(X), ZCurve.Eval(X));
		OutputLUT.Add(C.X);
		OutputLUT.Add(C.Y);
		OutputLUT.Add(C.Z);
	}
	return OutputLUT;
}

bool UNiagaraDataInterfaceVectorCurve::CopyToInternal(UNiagaraDataInterface* Destination) const 
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceVectorCurve* DestinationVectorCurve = CastChecked<UNiagaraDataInterfaceVectorCurve>(Destination);
	DestinationVectorCurve->XCurve = XCurve;
	DestinationVectorCurve->YCurve = YCurve;
	DestinationVectorCurve->ZCurve = ZCurve;
#if WITH_EDITORONLY_DATA
	DestinationVectorCurve->UpdateLUT();

	if (!CompareLUTS(DestinationVectorCurve->ShaderLUT))
	{
		UE_LOG(LogNiagara, Log, TEXT("Post CopyToInternal LUT generation is out of sync. Please investigate. %s"), *GetPathName());
	}
#endif
	return true;
}

bool UNiagaraDataInterfaceVectorCurve::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceVectorCurve* OtherVectorCurve = CastChecked<const UNiagaraDataInterfaceVectorCurve>(Other);
	return OtherVectorCurve->XCurve == XCurve &&
		OtherVectorCurve->YCurve == YCurve &&
		OtherVectorCurve->ZCurve == ZCurve;
}

void UNiagaraDataInterfaceVectorCurve::GetCurveData(TArray<FCurveData>& OutCurveData)
{
	OutCurveData.Add(FCurveData(&XCurve, TEXT("X"), FLinearColor::Red));
	OutCurveData.Add(FCurveData(&YCurve, TEXT("Y"), FLinearColor::Green));
	OutCurveData.Add(FCurveData(&ZCurve, TEXT("Z"), FLinearColor::Blue));
}

void UNiagaraDataInterfaceVectorCurve::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = SampleCurveName;
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("VectorCurve")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("X")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Value")));
	//Sig.Owner = *GetName();

	OutFunctions.Add(Sig);
}

// build the shader function HLSL; function name is passed in, as it's defined per-DI; that way, configuration could change
// the HLSL in the spirit of a static switch
// TODO: need a way to identify each specific function here
// 
bool UNiagaraDataInterfaceVectorCurve::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FString TimeToLUTFrac = TEXT("TimeToLUTFraction_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString Sample = TEXT("SampleCurve_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString NumSamples = TEXT("CurveLUTNumMinusOne_") + ParamInfo.DataInterfaceHLSLSymbol;
	OutHLSL += FString::Printf(TEXT("\
void %s(in float In_X, out float3 Out_Value) \n\
{ \n\
	float RemappedX = %s(In_X) * %s; \n\
	float Prev = floor(RemappedX); \n\
	float Next = Prev < %s ? Prev + 1.0 : Prev; \n\
	float Interp = RemappedX - Prev; \n\
	Prev *= %u; \n\
	Next *= %u; \n\
	float3 A = float3(%s(Prev), %s(Prev + 1), %s(Prev + 2)); \n\
	float3 B = float3(%s(Next), %s(Next + 1), %s(Next + 2)); \n\
	Out_Value = lerp(A, B, Interp); \n\
}\n") 
, *InstanceFunctionName, *TimeToLUTFrac, *NumSamples, *NumSamples, CurveLUTNumElems, CurveLUTNumElems
, *Sample, *Sample, *Sample, *Sample, *Sample, *Sample);

	return true;
}

DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceVectorCurve, SampleCurve);
void UNiagaraDataInterfaceVectorCurve::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == SampleCurveName && BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3)
	{
		TCurveUseLUTBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceVectorCurve, SampleCurve)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function.\n\tExpected Name: SampleVectorCurve  Actual Name: %s\n\tExpected Inputs: 1  Actual Inputs: %i\n\tExpected Outputs: 3  Actual Outputs: %i"),
			*BindingInfo.Name.ToString(), BindingInfo.GetNumInputs(), BindingInfo.GetNumOutputs());
	}
}

template<>
FORCEINLINE_DEBUGGABLE FVector UNiagaraDataInterfaceVectorCurve::SampleCurveInternal<TIntegralConstant<bool, true>>(float X)
{
	float RemappedX = FMath::Clamp(NormalizeTime(X) * LUTNumSamplesMinusOne, 0.0f, LUTNumSamplesMinusOne);
	float PrevEntry = FMath::TruncToFloat(RemappedX);
	float NextEntry = PrevEntry < LUTNumSamplesMinusOne ? PrevEntry + 1.0f : PrevEntry;
	float Interp = RemappedX - PrevEntry;

	int32 AIndex = PrevEntry * CurveLUTNumElems;
	int32 BIndex = NextEntry * CurveLUTNumElems;
	FVector A = FVector(ShaderLUT[AIndex], ShaderLUT[AIndex + 1], ShaderLUT[AIndex + 2]);
	FVector B = FVector(ShaderLUT[BIndex], ShaderLUT[BIndex + 1], ShaderLUT[BIndex + 2]);
	return FMath::Lerp(A, B, Interp);
}

template<>
FORCEINLINE_DEBUGGABLE FVector UNiagaraDataInterfaceVectorCurve::SampleCurveInternal<TIntegralConstant<bool, false>>(float X)
{
	return FVector(XCurve.Eval(X), YCurve.Eval(X), ZCurve.Eval(X));
}

template<typename UseLUT>
void UNiagaraDataInterfaceVectorCurve::SampleCurve(FVectorVMContext& Context)
{
	//TODO: Create some SIMDable optimized representation of the curve to do this faster.
	VectorVM::FExternalFuncInputHandler<float> XParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSampleZ(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		float X = XParam.GetAndAdvance();
		FVector V = SampleCurveInternal<UseLUT>(X);
		*OutSampleX.GetDestAndAdvance() = V.X;
		*OutSampleY.GetDestAndAdvance() = V.Y;
		*OutSampleZ.GetDestAndAdvance() = V.Z;
	}
}
