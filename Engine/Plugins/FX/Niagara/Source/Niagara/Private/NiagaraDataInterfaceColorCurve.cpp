// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceColorCurve.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveFloat.h"
#include "NiagaraTypes.h"

#if WITH_EDITORONLY_DATA
#include "Interfaces/ITargetPlatform.h"
#endif

//////////////////////////////////////////////////////////////////////////
//Color Curve

const FName UNiagaraDataInterfaceColorCurve::SampleCurveName(TEXT("SampleColorCurve"));

UNiagaraDataInterfaceColorCurve::UNiagaraDataInterfaceColorCurve(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetDefaultLUT();
}

void UNiagaraDataInterfaceColorCurve::PostInitProperties()
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

void UNiagaraDataInterfaceColorCurve::Serialize(FArchive& Ar)
{
#if WITH_EDITORONLY_DATA
	if (bUseLUT && Ar.IsCooking() && Ar.CookingTarget()->RequiresCookedData())
	{
		UpdateLUT();

		FRichCurve TempRCurve;
		FRichCurve TempGCurve;
		FRichCurve TempBCurve;
		FRichCurve TempACurve;
		Exchange(RedCurve, TempRCurve);
		Exchange(GreenCurve, TempGCurve);
		Exchange(BlueCurve, TempBCurve);
		Exchange(AlphaCurve, TempACurve);

		Super::Serialize(Ar);

		Exchange(RedCurve, TempRCurve);
		Exchange(GreenCurve, TempGCurve);
		Exchange(BlueCurve, TempBCurve);
		Exchange(AlphaCurve, TempACurve);
	}
	else
#endif
	{
		Super::Serialize(Ar);
	}
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceColorCurve::UpdateTimeRanges()
{
	if ((RedCurve.GetNumKeys() > 0 || GreenCurve.GetNumKeys() > 0 || BlueCurve.GetNumKeys() > 0 || AlphaCurve.GetNumKeys() > 0))
	{
		LUTMinTime = FLT_MAX;
		LUTMinTime = FMath::Min(RedCurve.GetNumKeys() > 0 ? RedCurve.GetFirstKey().Time : LUTMinTime, LUTMinTime);
		LUTMinTime = FMath::Min(GreenCurve.GetNumKeys() > 0 ? GreenCurve.GetFirstKey().Time : LUTMinTime, LUTMinTime);
		LUTMinTime = FMath::Min(BlueCurve.GetNumKeys() > 0 ? BlueCurve.GetFirstKey().Time : LUTMinTime, LUTMinTime);
		LUTMinTime = FMath::Min(AlphaCurve.GetNumKeys() > 0 ? AlphaCurve.GetFirstKey().Time : LUTMinTime, LUTMinTime);

		LUTMaxTime = FLT_MIN;
		LUTMaxTime = FMath::Max(RedCurve.GetNumKeys() > 0 ? RedCurve.GetLastKey().Time : LUTMaxTime, LUTMaxTime);
		LUTMaxTime = FMath::Max(GreenCurve.GetNumKeys() > 0 ? GreenCurve.GetLastKey().Time : LUTMaxTime, LUTMaxTime);
		LUTMaxTime = FMath::Max(BlueCurve.GetNumKeys() > 0 ? BlueCurve.GetLastKey().Time : LUTMaxTime, LUTMaxTime);
		LUTMaxTime = FMath::Max(AlphaCurve.GetNumKeys() > 0 ? AlphaCurve.GetLastKey().Time : LUTMaxTime, LUTMaxTime);
		LUTInvTimeRange = 1.0f / (LUTMaxTime - LUTMinTime);
	}
	else
	{
		LUTMinTime = 0.0f;
		LUTMaxTime = 1.0f;
		LUTInvTimeRange = 1.0f;
	}
}

TArray<float> UNiagaraDataInterfaceColorCurve::BuildLUT(int32 NumEntries) const
{
	TArray<float> OutputLUT;
	const float NumEntriesMinusOne = NumEntries - 1;

	OutputLUT.Reserve(NumEntries * 4);
	for (int32 i = 0; i < NumEntries; i++)
	{
		float X = UnnormalizeTime(float(i) / NumEntriesMinusOne);
		FLinearColor C(RedCurve.Eval(X), GreenCurve.Eval(X), BlueCurve.Eval(X), AlphaCurve.Eval(X));
		OutputLUT.Add(C.R);
		OutputLUT.Add(C.G);
		OutputLUT.Add(C.B);
		OutputLUT.Add(C.A);
	}
	return OutputLUT;
}
#endif

bool UNiagaraDataInterfaceColorCurve::CopyToInternal(UNiagaraDataInterface* Destination) const 
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}
	UNiagaraDataInterfaceColorCurve* DestinationColorCurve = CastChecked<UNiagaraDataInterfaceColorCurve>(Destination);
	DestinationColorCurve->RedCurve = RedCurve;
	DestinationColorCurve->GreenCurve = GreenCurve;
	DestinationColorCurve->BlueCurve = BlueCurve;
	DestinationColorCurve->AlphaCurve = AlphaCurve;
#if WITH_EDITORONLY_DATA
	DestinationColorCurve->UpdateLUT();

	if (!CompareLUTS(DestinationColorCurve->ShaderLUT))
	{
		UE_LOG(LogNiagara, Log, TEXT("CopyToInternal LUT generation is out of sync. Please investigate. %s to %s"), *GetPathName(), *DestinationColorCurve->GetPathName());
	}
#endif
	return true;
}

bool UNiagaraDataInterfaceColorCurve::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceColorCurve* OtherColoRedCurve = CastChecked<const UNiagaraDataInterfaceColorCurve>(Other);
	return OtherColoRedCurve->RedCurve == RedCurve &&
		OtherColoRedCurve->GreenCurve == GreenCurve &&
		OtherColoRedCurve->BlueCurve == BlueCurve &&
		OtherColoRedCurve->AlphaCurve == AlphaCurve;
}

void UNiagaraDataInterfaceColorCurve::GetCurveData(TArray<FCurveData>& OutCurveData)
{
	OutCurveData.Add(FCurveData(&RedCurve, TEXT("Red"), FLinearColor::Red));
	OutCurveData.Add(FCurveData(&GreenCurve, TEXT("Green"), FLinearColor::Green));
	OutCurveData.Add(FCurveData(&BlueCurve, TEXT("Blue"), FLinearColor::Blue));
	OutCurveData.Add(FCurveData(&AlphaCurve, TEXT("Alpha"), FLinearColor::White));
}

void UNiagaraDataInterfaceColorCurve::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = SampleCurveName;
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("ColorCurve")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("X")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), TEXT("Value")));
	//Sig.Owner = *GetName();

	OutFunctions.Add(Sig);
}

// build the shader function HLSL; function name is passed in, as it's defined per-DI; that way, configuration could change
// the HLSL in the spirit of a static switch
// TODO: need a way to identify each specific function here
// 
bool UNiagaraDataInterfaceColorCurve::GetFunctionHLSL(const FName& DefinitionFunctionName, FString InstanceFunctionName, FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	FString TimeToLUTFrac = TEXT("TimeToLUTFraction_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString Sample = TEXT("SampleCurve_") + ParamInfo.DataInterfaceHLSLSymbol;
	FString NumSamples = TEXT("CurveLUTNumMinusOne_") + ParamInfo.DataInterfaceHLSLSymbol;
	OutHLSL += FString::Printf(TEXT("\
void %s(in float In_X, out float4 Out_Value) \n\
{ \n\
	float RemappedX = %s(In_X) * %s; \n\
	float Prev = floor(RemappedX); \n\
	float Next = Prev < %s ? Prev + 1.0 : Prev; \n\
	float Interp = RemappedX - Prev; \n\
	Prev *= %u; \n\
	Next *= %u; \n\
	float4 A = float4(%s(Prev), %s(Prev + 1), %s(Prev + 2), %s(Prev + 3)); \n\
	float4 B = float4(%s(Next), %s(Next + 1), %s(Next + 2), %s(Next + 3)); \n\
	Out_Value = lerp(A, B, Interp); \n\
}\n")
, *InstanceFunctionName, *TimeToLUTFrac, *NumSamples, *NumSamples, CurveLUTNumElems, CurveLUTNumElems
, *Sample, *Sample, *Sample, *Sample, *Sample, *Sample, *Sample, *Sample);

	return true;
}

DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceColorCurve, SampleCurve);
void UNiagaraDataInterfaceColorCurve::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == SampleCurveName && BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 4)
	{
		TCurveUseLUTBinder<NDI_FUNC_BINDER(UNiagaraDataInterfaceColorCurve, SampleCurve)>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function.\n\tExpected Name: SampleColorCurve  Actual Name: %s\n\tExpected Inputs: 1  Actual Inputs: %i\n\tExpected Outputs: 4  Actual Outputs: %i"),
			*BindingInfo.Name.ToString(), BindingInfo.GetNumInputs(), BindingInfo.GetNumOutputs());
		OutFunc = FVMExternalFunction();
	}
}

template<>
FORCEINLINE_DEBUGGABLE FLinearColor UNiagaraDataInterfaceColorCurve::SampleCurveInternal<TIntegralConstant<bool, true>>(float X)
{
	float RemappedX = FMath::Clamp(NormalizeTime(X) * LUTNumSamplesMinusOne, 0.0f, LUTNumSamplesMinusOne);
	float PrevEntry = FMath::TruncToFloat(RemappedX);
	float NextEntry = PrevEntry < LUTNumSamplesMinusOne ? PrevEntry + 1.0f : PrevEntry;
	float Interp = RemappedX - PrevEntry;
	
	int32 AIndex = PrevEntry * CurveLUTNumElems;
	int32 BIndex = NextEntry * CurveLUTNumElems;
	FLinearColor A = FLinearColor(ShaderLUT[AIndex], ShaderLUT[AIndex + 1], ShaderLUT[AIndex + 2], ShaderLUT[AIndex + 3]);
	FLinearColor B = FLinearColor(ShaderLUT[BIndex], ShaderLUT[BIndex + 1], ShaderLUT[BIndex + 2], ShaderLUT[BIndex + 3]);
	return FMath::Lerp(A, B, Interp);
}

template<>
FORCEINLINE_DEBUGGABLE FLinearColor UNiagaraDataInterfaceColorCurve::SampleCurveInternal<TIntegralConstant<bool, false>>(float X)
{
	return FLinearColor(RedCurve.Eval(X), GreenCurve.Eval(X), BlueCurve.Eval(X), AlphaCurve.Eval(X));
}

template<typename UseLUT>
void UNiagaraDataInterfaceColorCurve::SampleCurve(FVectorVMContext& Context)
{
	//TODO: Create some SIMDable optimized representation of the curve to do this faster.
	VectorVM::FExternalFuncInputHandler<float> XParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> SamplePtrR(Context);
	VectorVM::FExternalFuncRegisterHandler<float> SamplePtrG(Context);
	VectorVM::FExternalFuncRegisterHandler<float> SamplePtrB(Context);
	VectorVM::FExternalFuncRegisterHandler<float> SamplePtrA(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		float X = XParam.GetAndAdvance();
		FLinearColor C = SampleCurveInternal<UseLUT>(X);
		*SamplePtrR.GetDestAndAdvance() = C.R;
		*SamplePtrG.GetDestAndAdvance() = C.G;
		*SamplePtrB.GetDestAndAdvance() = C.B;
		*SamplePtrA.GetDestAndAdvance() = C.A;
	}
}
