// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceSpline.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemInstance.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceSpline"

UNiagaraDataInterfaceSpline::UNiagaraDataInterfaceSpline(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, Source(nullptr)
{
	FNiagaraTypeDefinition Def(UObject::StaticClass());
	SplineUserParameter.Parameter.SetType(Def);
}

void UNiagaraDataInterfaceSpline::PostInitProperties()
{
	Super::PostInitProperties();

	//Can we regitser data interfaces as regular types and fold them into the FNiagaraVariable framework for UI and function calls etc?
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
	}
}

static const FName SampleSplinePositionByUnitDistanceName("SampleSplinePositionByUnitDistance");
static const FName SampleSplinePositionByUnitDistanceWSName("SampleSplinePositionByUnitDistanceWS");

static const FName SampleSplineRotationByUnitDistanceName("SampleSplineRotationByUnitDistance");
static const FName SampleSplineRotationByUnitDistanceWSName("SampleSplineRotationByUnitDistanceWS");

static const FName SampleSplineUpVectorByUnitDistanceName("SampleSplineUpVectorByUnitDistance");
static const FName SampleSplineUpVectorByUnitDistanceWSName("SampleSplineUpVectorByUnitDistanceWS");

static const FName SampleSplineDirectionByUnitDistanceName("SampleSplineDirectionByUnitDistance");
static const FName SampleSplineDirectionByUnitDistanceWSName("SampleSplineDirectionByUnitDistanceWS");

static const FName SampleSplineRightVectorByUnitDistanceName("SampleSplineRightVectorByUnitDistance");
static const FName SampleSplineRightVectorByUnitDistanceWSName("SampleSplineRightVectorByUnitDistanceWS");

static const FName SampleSplineTangentByUnitDistanceName("SampleSplineTangentByUnitDistance");
static const FName SampleSplineTangentByUnitDistanceWSName("SampleSplineTangentByUnitDistanceWS");


static const FName FindClosestUnitDistanceFromPositionWSName("FindClosestUnitDistanceFromPositionWS");


/** Temporary solution for exposing the transform of a mesh. Ideally this would be done by allowing interfaces to add to the uniform set for a simulation. */
static const FName GetSplineLocalToWorldName("GetSplineLocalToWorld");
static const FName GetSplineLocalToWorldInverseTransposedName("GetSplineLocalToWorldInverseTransposed");

void UNiagaraDataInterfaceSpline::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplinePositionByUnitDistanceName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplinePositionByUnitDistance", "Sample the spline Position where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the local space of the referenced USplineComponent."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplinePositionByUnitDistanceWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Position")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplinePositionByUnitDistanceWS", "Sample the spline Position where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the world space of the level."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineRotationByUnitDistanceName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineRotationByUnitDistance", "Sample the spline Rotation where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the local space of the referenced USplineComponent."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineRotationByUnitDistanceWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rotation")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineRotationByUnitDistanceWS", "Sample the spline Rotation where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the world space of the level."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineDirectionByUnitDistanceName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Direction")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineDirectionByUnitDistance", "Sample the spline direction vector where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the local space of the referenced USplineComponent."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineDirectionByUnitDistanceWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Direction")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineDirectionByUnitDistanceWS", "Sample the spline direction vector where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the world space of the level."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineUpVectorByUnitDistanceName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UpVector")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineUpVectorByUnitDistance", "Sample the spline up vector where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the local space of the referenced USplineComponent."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineUpVectorByUnitDistanceWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineUpVectorByUnitDistanceWS", "Sample the spline up vector where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the world space of the level."));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("UpVector")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineRightVectorByUnitDistanceName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineRightVectorByUnitDistance", "Sample the spline right vector where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the local space of the referenced USplineComponent."));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("RightVector")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineRightVectorByUnitDistanceWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineRightVectorByUnitDistanceWS", "Sample the spline right vector where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the world space of the level."));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("RightVector")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineTangentByUnitDistanceName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineTangentVectorByUnitDistance", "Sample the spline tangent vector where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the local space of the referenced USplineComponent."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SampleSplineTangentByUnitDistanceWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Tangent")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_SampleSplineTangentVectorByUnitDistanceWS", "Sample the spline tangent vector where U is a 0 to 1 value representing the start and normalized length of the spline.\nThis is in the world space of the level."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSplineLocalToWorldName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Transform")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_GetSplineLocalToWorld", "Get the transform from the USplineComponent's local space to world space."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSplineLocalToWorldInverseTransposedName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Transform")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_GetSplineLocalToWorldInverseTransposed", "Get the transform from the world space to the USplineComponent's local space."));
		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = FindClosestUnitDistanceFromPositionWSName;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Spline")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("PositionWS")));

		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("U")));
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		//Sig.Owner = *GetName();
		Sig.SetDescription(LOCTEXT("DataInterfaceSpline_FindClosestUnitDistanceFromPositionWS", "Given a world space position, find the closest value 'U' on the USplineComponent to that point."));
		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplinePositionByUnitDistance);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineRotationByUnitDistance);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineUpVectorByUnitDistance);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineRightVectorByUnitDistance);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineDirectionByUnitDistance);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineTangentByUnitDistance);
DEFINE_NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, FindClosestUnitDistanceFromPositionWS);

void UNiagaraDataInterfaceSpline::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == SampleSplinePositionByUnitDistanceName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplinePositionByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplinePositionByUnitDistanceWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandler, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplinePositionByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineRotationByUnitDistanceName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 4);
		TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineRotationByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineRotationByUnitDistanceWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 4);
		TNDIExplicitBinder<FNDITransformHandler, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineRotationByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineUpVectorByUnitDistanceName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineUpVectorByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineUpVectorByUnitDistanceWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandler, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineUpVectorByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineDirectionByUnitDistanceName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineDirectionByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineDirectionByUnitDistanceWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandler, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineDirectionByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineRightVectorByUnitDistanceName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineRightVectorByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineRightVectorByUnitDistanceWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandler, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineRightVectorByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineTangentByUnitDistanceName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandlerNoop, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineTangentByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == SampleSplineTangentByUnitDistanceWSName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		TNDIExplicitBinder<FNDITransformHandler, TNDIParamBinder<1, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, SampleSplineTangentByUnitDistance)>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == FindClosestUnitDistanceFromPositionWSName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		TNDIParamBinder<1, float, TNDIParamBinder<2, float, TNDIParamBinder<3, float, NDI_FUNC_BINDER(UNiagaraDataInterfaceSpline, FindClosestUnitDistanceFromPositionWS)>>>::Bind(this, BindingInfo, InstanceData, OutFunc);
	}
	else if (BindingInfo.Name == GetSplineLocalToWorldName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 16);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceSpline::GetLocalToWorld);
	}
	else if (BindingInfo.Name == GetSplineLocalToWorldInverseTransposedName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 16);
		OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceSpline::GetLocalToWorldInverseTransposed);
	}

	//check(0);
}

bool UNiagaraDataInterfaceSpline::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceSpline* OtherTyped = CastChecked<UNiagaraDataInterfaceSpline>(Destination);
	OtherTyped->Source = Source;
	OtherTyped->SplineUserParameter = SplineUserParameter;
	return true;
}

bool UNiagaraDataInterfaceSpline::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceSpline* OtherTyped = CastChecked<const UNiagaraDataInterfaceSpline>(Other);
	return OtherTyped->Source == Source && OtherTyped->SplineUserParameter == SplineUserParameter;
}

int32 UNiagaraDataInterfaceSpline::PerInstanceDataSize()const
{
	return sizeof(FNDISpline_InstanceData);
}

bool UNiagaraDataInterfaceSpline::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDISpline_InstanceData* InstData = new (PerInstanceData) FNDISpline_InstanceData();

	InstData->Component.Reset();
	InstData->Transform = FMatrix::Identity;
	InstData->TransformInverseTransposed = FMatrix::Identity;

	return true;
}

void UNiagaraDataInterfaceSpline::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDISpline_InstanceData* InstData = (FNDISpline_InstanceData*)PerInstanceData;
	InstData->~FNDISpline_InstanceData();
}

bool UNiagaraDataInterfaceSpline::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	check(SystemInstance);
	FNDISpline_InstanceData* InstData = (FNDISpline_InstanceData*)PerInstanceData;

	if (InstData && InstData->ResetRequired(this, SystemInstance))
	{
		return true;
	}

	if (!InstData)
	{
		return true;
	}

	USplineComponent* SplineComponent = InstData->Component.Get();
	if (SplineComponent == nullptr)
	{
		if (SplineUserParameter.Parameter.IsValid() && InstData && SystemInstance != nullptr)
		{
			// Initialize the binding and retrieve the object. If a valid object is bound, we'll try and retrieve the Spline component from it.
			// If it's not valid yet, we'll reset and do this again when/if a valid object is set on the binding
			UObject* UserParamObject = InstData->UserParamBinding.Init(SystemInstance->GetInstanceParameters(), SplineUserParameter.Parameter);
			InstData->CachedUserParam = UserParamObject;
			if (UserParamObject)
			{
				if (USplineComponent* UserSplineComp = Cast<USplineComponent>(UserParamObject))
				{
					if (!UserSplineComp->IsPendingKill())
					{
						SplineComponent = UserSplineComp;
					}
				}
				else if (AActor* Actor = Cast<AActor>(UserParamObject))
				{
					SplineComponent = Source->FindComponentByClass<USplineComponent>();
				}
				else
				{
					//We have a valid, non-null UObject parameter type but it is not a type we can use to get a skeletal Spline from. 
					UE_LOG(LogNiagara, Warning, TEXT("Spline data interface using object parameter with invalid type. Spline Data Interfaces can only get a valid Spline from SplineComponents or Actors."));
					UE_LOG(LogNiagara, Warning, TEXT("Invalid Parameter : %s"), *UserParamObject->GetFullName());
					UE_LOG(LogNiagara, Warning, TEXT("Niagara Component : %s"), *GetFullNameSafe(Cast<UNiagaraComponent>(SystemInstance->GetAttachComponent())));
					UE_LOG(LogNiagara, Warning, TEXT("System : %s"), *GetFullNameSafe(SystemInstance->GetSystem()));
				}
			}
			else
			{
				// The binding exists, but no object is bound. Not warning here in case the user knows what they're doing.
			}
		}
		else if (Source != nullptr)
		{
			SplineComponent = Source->FindComponentByClass<USplineComponent>();
		}
		else if (USceneComponent* AttachComp = SystemInstance->GetAttachComponent())
		{
			if (AActor* Owner = AttachComp->GetAttachmentRootActor())
			{
				SplineComponent = Owner->FindComponentByClass<USplineComponent>();
			}
		}
		InstData->Component = SplineComponent;
	}

	//Re-evaluate source in case it's changed?
	if (SplineComponent != nullptr)
	{
		InstData->Transform = SplineComponent->GetComponentToWorld().ToMatrixWithScale();
		InstData->TransformInverseTransposed = InstData->Transform.InverseFast().GetTransposed();
		InstData->ComponentTransform = SplineComponent->GetComponentTransform();
		InstData->DefaultUpVector = SplineComponent->DefaultUpVector;
		if (InstData->SplineCurves.Version != SplineComponent->SplineCurves.Version)
			InstData->SplineCurves = SplineComponent->SplineCurves;
	}

	


	//Any situations requiring a rebind?
	return false;
}



bool FNDISpline_InstanceData::ResetRequired(UNiagaraDataInterfaceSpline* Interface, FNiagaraSystemInstance* SystemInstance) const
{
	
	if (Interface->SplineUserParameter.Parameter.IsValid())
	{
		// Reset if the user object ptr has been changed to look at a new object
		if (UserParamBinding.GetValue() != CachedUserParam)
		{
			return true;
		}
	}
	

	return false;
}

float FNDISpline_InstanceData::GetSplineLength() const
{
	return SplineCurves.GetSplineLength();
}

bool FNDISpline_InstanceData::IsValid() const
{
	return Component.IsValid();
}

FVector FNDISpline_InstanceData::GetLocationAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetLocationAtSplineInputKey(Param, CoordinateSpace);
}

FVector FNDISpline_InstanceData::GetLocationAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	FVector Location = SplineCurves.Position.Eval(InKey, FVector::ZeroVector);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Location = ComponentTransform.TransformPosition(Location);
	}

	return Location;
}

FQuat FNDISpline_InstanceData::GetQuaternionAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetQuaternionAtSplineInputKey(Param, CoordinateSpace);
}

FQuat FNDISpline_InstanceData::GetQuaternionAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	FQuat Quat = SplineCurves.Rotation.Eval(InKey, FQuat::Identity);
	Quat.Normalize();

	const FVector Direction = SplineCurves.Position.EvalDerivative(InKey, FVector::ZeroVector).GetSafeNormal();
	const FVector UpVector = Quat.RotateVector(DefaultUpVector);

	FQuat Rot = (FRotationMatrix::MakeFromXZ(Direction, UpVector)).ToQuat();

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Rot = ComponentTransform.GetRotation() * Rot;
	}

	return Rot;
}


FVector FNDISpline_InstanceData::GetUpVectorAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetUpVectorAtSplineInputKey(Param, CoordinateSpace);
}

FVector FNDISpline_InstanceData::GetUpVectorAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FQuat Quat = GetQuaternionAtSplineInputKey(InKey, ESplineCoordinateSpace::Local);
	FVector UpVector = Quat.RotateVector(FVector::UpVector);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		UpVector = ComponentTransform.TransformVectorNoScale(UpVector);
	}

	return UpVector;
}

FVector FNDISpline_InstanceData::GetRightVectorAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetRightVectorAtSplineInputKey(Param, CoordinateSpace);
}

FVector FNDISpline_InstanceData::GetRightVectorAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const FQuat Quat = GetQuaternionAtSplineInputKey(InKey, ESplineCoordinateSpace::Local);
	FVector RightVector = Quat.RotateVector(FVector::RightVector);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		RightVector = ComponentTransform.TransformVectorNoScale(RightVector);
	}

	return RightVector;
}

FVector FNDISpline_InstanceData::GetTangentAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetTangentAtSplineInputKey(Param, CoordinateSpace);
}


FVector FNDISpline_InstanceData::GetDirectionAtDistanceAlongSpline(float Distance, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	const float Param = SplineCurves.ReparamTable.Eval(Distance, 0.0f);
	return GetDirectionAtSplineInputKey(Param, CoordinateSpace);
}

FVector FNDISpline_InstanceData::GetTangentAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	FVector Tangent = SplineCurves.Position.EvalDerivative(InKey, FVector::ZeroVector);

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Tangent = ComponentTransform.TransformVector(Tangent);
	}

	return Tangent;
}


FVector FNDISpline_InstanceData::GetDirectionAtSplineInputKey(float InKey, ESplineCoordinateSpace::Type CoordinateSpace) const
{
	FVector Direction = SplineCurves.Position.EvalDerivative(InKey, FVector::ZeroVector).GetSafeNormal();

	if (CoordinateSpace == ESplineCoordinateSpace::World)
	{
		Direction = ComponentTransform.TransformVector(Direction);
		Direction.Normalize();
	}

	return Direction;
}

float FNDISpline_InstanceData::FindInputKeyClosestToWorldLocation(const FVector& WorldLocation) const
{
	const FVector LocalLocation = ComponentTransform.InverseTransformPosition(WorldLocation);
	float Dummy;
	return SplineCurves.Position.InaccurateFindNearest(LocalLocation, Dummy);
}


template<typename TransformHandlerType, typename SplineSampleType>
void UNiagaraDataInterfaceSpline::SampleSplinePositionByUnitDistance(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	TransformHandlerType TransformHandler;
	SplineSampleType SplineSampleParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosZ(Context);

	if (InstData->IsValid())
	{
		const float SplineLength = InstData->GetSplineLength();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = InstData->GetLocationAtDistanceAlongSpline(DistanceUnitDistance * SplineLength, ESplineCoordinateSpace::Local);
			TransformHandler.TransformPosition(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = FVector(EForceInit::ForceInitToZero);
			TransformHandler.TransformPosition(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
}

template<typename TransformHandlerType, typename SplineSampleType>
void UNiagaraDataInterfaceSpline::SampleSplineRotationByUnitDistance(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	TransformHandlerType TransformHandler;
	SplineSampleType SplineSampleParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutQuatX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutQuatY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutQuatZ(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutQuatW(Context);

	if (InstData->IsValid())
	{
		const FQuat TransformQuat = InstData->Transform.GetMatrixWithoutScale().ToQuat();
		const float SplineLength = InstData->GetSplineLength();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			const float DistanceUnitDistance = SplineSampleParam.GetAndAdvance();

			FQuat Quat = InstData->GetQuaternionAtDistanceAlongSpline(DistanceUnitDistance * SplineLength, ESplineCoordinateSpace::Local);
			TransformHandler.TransformRotation(Quat, TransformQuat);

			*OutQuatX.GetDestAndAdvance() = Quat.X;
			*OutQuatY.GetDestAndAdvance() = Quat.Y;
			*OutQuatZ.GetDestAndAdvance() = Quat.Z;
			*OutQuatW.GetDestAndAdvance() = Quat.W;
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			*OutQuatX.GetDestAndAdvance() = FQuat::Identity.X;
			*OutQuatY.GetDestAndAdvance() = FQuat::Identity.Y;
			*OutQuatZ.GetDestAndAdvance() = FQuat::Identity.Z;
			*OutQuatW.GetDestAndAdvance() = FQuat::Identity.W;
		}
	}
}

template<typename TransformHandlerType, typename SplineSampleType>
void UNiagaraDataInterfaceSpline::SampleSplineUpVectorByUnitDistance(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	TransformHandlerType TransformHandler;
	SplineSampleType SplineSampleParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosZ(Context);

	if (InstData->IsValid())
	{
		const float SplineLength = InstData->GetSplineLength();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = InstData->GetUpVectorAtDistanceAlongSpline(DistanceUnitDistance * SplineLength, ESplineCoordinateSpace::Local);
			TransformHandler.TransformVector(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = FVector(0.0f, 0.0f, 1.0f); 
			TransformHandler.TransformVector(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
}

template<typename TransformHandlerType, typename SplineSampleType>
void UNiagaraDataInterfaceSpline::SampleSplineRightVectorByUnitDistance(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	TransformHandlerType TransformHandler;
	SplineSampleType SplineSampleParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosZ(Context);
	
	
	if (InstData->IsValid())
	{
		const float SplineLength = InstData->GetSplineLength();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = InstData->GetRightVectorAtDistanceAlongSpline(DistanceUnitDistance * SplineLength, ESplineCoordinateSpace::Local);
			TransformHandler.TransformVector(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			FVector Pos = FVector(-1.0f, 0.0f, 0.0f); 
			TransformHandler.TransformVector(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
}

template<typename TransformHandlerType, typename SplineSampleType>
void UNiagaraDataInterfaceSpline::SampleSplineTangentByUnitDistance(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	TransformHandlerType TransformHandler;
	SplineSampleType SplineSampleParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosZ(Context);

	if (InstData->IsValid())
	{
		const float SplineLength = InstData->GetSplineLength();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = InstData->GetTangentAtDistanceAlongSpline(DistanceUnitDistance * SplineLength, ESplineCoordinateSpace::Local);
			TransformHandler.TransformVector(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = FVector(EForceInit::ForceInitToZero); 
			TransformHandler.TransformVector(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
}

template<typename TransformHandlerType, typename SplineSampleType>
void UNiagaraDataInterfaceSpline::SampleSplineDirectionByUnitDistance(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	TransformHandlerType TransformHandler;
	SplineSampleType SplineSampleParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutPosZ(Context);

	if (InstData->IsValid())
	{
		const float SplineLength = InstData->GetSplineLength();
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = InstData->GetDirectionAtDistanceAlongSpline(DistanceUnitDistance * SplineLength, ESplineCoordinateSpace::Local);
			TransformHandler.TransformVector(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float DistanceUnitDistance = SplineSampleParam.Get();

			FVector Pos = FVector(0.0f, 1.0f, 0.0f); 
			TransformHandler.TransformVector(Pos, InstData->Transform);

			*OutPosX.GetDest() = Pos.X;
			*OutPosY.GetDest() = Pos.Y;
			*OutPosZ.GetDest() = Pos.Z;
			SplineSampleParam.Advance();
			OutPosX.Advance();
			OutPosY.Advance();
			OutPosZ.Advance();
		}
	}
}

void UNiagaraDataInterfaceSpline::WriteTransform(const FMatrix& ToWrite, FVectorVMContext& Context)
{
	VectorVM::FExternalFuncRegisterHandler<float> Out00(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out01(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out02(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out03(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out04(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out05(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out06(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out07(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out08(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out09(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out10(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out11(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out12(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out13(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out14(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out15(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*Out00.GetDest() = ToWrite.M[0][0]; Out00.Advance();
		*Out01.GetDest() = ToWrite.M[0][1]; Out01.Advance();
		*Out02.GetDest() = ToWrite.M[0][2]; Out02.Advance();
		*Out03.GetDest() = ToWrite.M[0][3]; Out03.Advance();
		*Out04.GetDest() = ToWrite.M[1][0]; Out04.Advance();
		*Out05.GetDest() = ToWrite.M[1][1]; Out05.Advance();
		*Out06.GetDest() = ToWrite.M[1][2]; Out06.Advance();
		*Out07.GetDest() = ToWrite.M[1][3]; Out07.Advance();
		*Out08.GetDest() = ToWrite.M[2][0]; Out08.Advance();
		*Out09.GetDest() = ToWrite.M[2][1]; Out09.Advance();
		*Out10.GetDest() = ToWrite.M[2][2]; Out10.Advance();
		*Out11.GetDest() = ToWrite.M[2][3]; Out11.Advance();
		*Out12.GetDest() = ToWrite.M[3][0]; Out12.Advance();
		*Out13.GetDest() = ToWrite.M[3][1]; Out13.Advance();
		*Out14.GetDest() = ToWrite.M[3][2]; Out14.Advance();
		*Out15.GetDest() = ToWrite.M[3][3]; Out15.Advance();
	}
}

template<typename PosXType, typename PosYType, typename PosZType>
void UNiagaraDataInterfaceSpline::FindClosestUnitDistanceFromPositionWS(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	PosXType PosXParam(Context);
	PosYType PosYParam(Context);
	PosZType PosZParam(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutUnitDistance(Context);

	if (InstData->IsValid())
	{

		const int32 NumPoints = InstData->GetSplinePointsPosition().Points.Num();
		const float FinalKeyTime = InstData->GetSplinePointsPosition().Points[NumPoints - 1].InVal;

		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float PosX = PosXParam.Get();
			float PosY = PosYParam.Get();
			float PosZ = PosZParam.Get();

			FVector Pos(PosX, PosY, PosZ);

			// This first call finds the key time, but this is not in 0..1 range for the spline. 
			float KeyTime = InstData->FindInputKeyClosestToWorldLocation(Pos);
			// We need to convert into the range by dividing through by the overall duration of the spline according to the keys.
			float UnitDistance = KeyTime / FinalKeyTime;

			*OutUnitDistance.GetDest() = UnitDistance;

			PosXParam.Advance();
			PosYParam.Advance();
			PosZParam.Advance();
			OutUnitDistance.Advance();
		}
	}
	else
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			float PosX = PosXParam.Get();
			float PosY = PosYParam.Get();
			float PosZ = PosZParam.Get();

			*OutUnitDistance.GetDest() = 0.0f;

			PosXParam.Advance();
			PosYParam.Advance();
			PosZParam.Advance();
			OutUnitDistance.Advance();
		}
	}
}

void UNiagaraDataInterfaceSpline::GetLocalToWorld(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	WriteTransform(InstData->Transform, Context);
}

void UNiagaraDataInterfaceSpline::GetLocalToWorldInverseTransposed(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDISpline_InstanceData> InstData(Context);
	WriteTransform(InstData->TransformInverseTransposed, Context);
}

#undef LOCTEXT_NAMESPACE
