// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCurve.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveFloat.h"
#include "NiagaraTypes.h"

#if WITH_EDITORONLY_DATA
#include "Interfaces/ITargetPlatform.h"
#endif

const FName UNiagaraDataInterfaceCurve::SampleCurveName(TEXT("SampleCurve"));

UNiagaraDataInterfaceCurve::UNiagaraDataInterfaceCurve(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetDefaultLUT();
}

void UNiagaraDataInterfaceCurve::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}

#if WITH_EDITORONLY_DATA
	UpdateLUT();
#endif
}

void UNiagaraDataInterfaceCurve::Serialize(FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (bUseLUT && Ar.IsCooking() && Ar.CookingTarget()->RequiresCookedData())
	{
		UpdateLUT();

		FRichCurve TempCurve;
		Exchange(Curve, TempCurve);

		Super::Serialize(Ar);

		Exchange(Curve, TempCurve);
	}
	else
#endif
	{
		Super::Serialize(Ar);
	}
}

bool UNiagaraDataInterfaceCurve::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	CastChecked<UNiagaraDataInterfaceCurve>(Destination)->Curve = Curve;
#if WITH_EDITORONLY_DATA
	CastChecked<UNiagaraDataInterfaceCurve>(Destination)->UpdateLUT();
	if (!CompareLUTS(CastChecked<UNiagaraDataInterfaceCurve>(Destination)->ShaderLUT))
	{
		UE_LOG(LogNiagara, Log, TEXT("Post CopyToInternal LUT generation is out of sync. Please investigate. %s"), *GetPathName());
	}
#endif
	return true;
}

bool UNiagaraDataInterfaceCurve::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	return CastChecked<UNiagaraDataInterfaceCurve>(Other)->Curve == Curve;
}

void UNiagaraDataInterfaceCurve::GetCurveData(TArray<FCurveData>& OutCurveData)
{
	OutCurveData.Add(FCurveData(&Curve, NAME_None, FLinearColor::Red));
}

void UNiagaraDataInterfaceCurve::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = SampleCurveName;
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Curve")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("X")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Value")));
//	Sig.Owner = *GetName();

	OutFunctions.Add(Sig);
}

void UNiagaraDataInterfaceCurve::UpdateTimeRanges()
{
	if (Curve.GetNumKeys() > 0)
	{
		LUTMinTime = Curve.GetFirstKey().Time;
		LUTMaxTime = Curve.GetLastKey().Time;
		LUTInvTimeRange = 1.0f / (LUTMaxTime - LUTMinTime);
	}
	else
	{
		LUTMinTime = 0.0f;
		LUTMaxTime = 1.0f;
		LUTInvTimeRange = 1.0f;
	}
}

TArray<float> UNiagaraDataInterfaceCurve::BuildLUT(int32 NumEntries) const
{
	TArray<float> OutputLUT;
	const float NumEntriesMinusOne = NumEntries - 1;

	OutputLUT.Reserve(NumEntries);
	for (int32 i = 0; i < NumEntries; i++)
	{
		float X = UnnormalizeTime(i / NumEntriesMinusOne);
		float C = Curve.Eval(X);
		OutputLUT.Add(C);
	}
	return OutputLUT;
}

// build the shader function HLSL; function name is passed in, as it's defined per-DI; that way, configuration could change
// the HLSL in the spirit of a static switch
// TODO: need a way to identify each specific function here
// 
bool UNiagaraDataInterfaceCurve::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FString TimeToLUTFrac = TEXT("TimeToLUTFraction_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString Sample = TEXT("SampleCurve_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString NumSamples = TEXT("CurveLUTNumMinusOne_") + ParamInfo.DataInterfaceHLSLSymbol;
	OutHLSL += FString::Printf(TEXT("\
void %s(in float In_X, out float Out_Value) \n\
{ \n\
	float RemappedX = %s(In_X) * %s; \n\
	float Prev = floor(RemappedX); \n\
	float Next = Prev < %s ? Prev + 1.0 : Prev; \n\
	float Interp = RemappedX - Prev; \n\
	float A = %s(Prev); \n\
	float B = %s(Next); \n\
	Out_Value = lerp(A, B, Interp); \n\
}\n")
, *InstanceFunctionName, *TimeToLUTFrac, *NumSamples, *NumSamples, *Sample, *Sample);

	return true;
}

DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceCurve, SampleCurve);
void UNiagaraDataInterfaceCurve::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == SampleCurveName && BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1)
	{
		TCurveUseLUTBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceCurve, SampleCurve)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function.\n\tExpected Name: SampleCurve  Actual Name: %s\n\tExpected Inputs: 1  Actual Inputs: %i\n\tExpected Outputs: 1  Actual Outputs: %i"),
			*BindingInfo.Name.ToString(), BindingInfo.GetNumInputs(), BindingInfo.GetNumOutputs());
	}
}

template<>
FORCEINLINE_DEBUGGABLE float UNiagaraDataInterfaceCurve::SampleCurveInternal<TIntegralConstant<bool, true>>(float X)
{
	float RemappedX = FMath::Clamp(NormalizeTime(X) * LUTNumSamplesMinusOne, 0.0f, LUTNumSamplesMinusOne);
	float PrevEntry = FMath::TruncToFloat(RemappedX);
	float NextEntry = PrevEntry < LUTNumSamplesMinusOne ? PrevEntry + 1.0f : PrevEntry;
	float Interp = RemappedX - PrevEntry;

	int32 AIndex = PrevEntry * CurveLUTNumElems;
	int32 BIndex = NextEntry * CurveLUTNumElems;
	float A = ShaderLUT[AIndex];
	float B = ShaderLUT[BIndex];
	return FMath::Lerp(A, B, Interp);
}

template<>
FORCEINLINE_DEBUGGABLE float UNiagaraDataInterfaceCurve::SampleCurveInternal<TIntegralConstant<bool, false>>(float X)
{
	return Curve.Eval(X);
}

template<typename UseLUT>
void UNiagaraDataInterfaceCurve::SampleCurve(FVectorVMContext& Context)
{
	VectorVM::FExternalFuncInputHandler<float> XParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutSample(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutSample.GetDest() = SampleCurveInternal<UseLUT>(XParam.Get());
		XParam.Advance();
		OutSample.Advance();
	}
}
