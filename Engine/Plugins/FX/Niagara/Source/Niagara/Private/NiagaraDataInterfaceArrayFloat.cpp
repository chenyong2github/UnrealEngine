// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayImpl.h"
#include "NiagaraDataInterfaceArrayImplHelpers.h"
#include "NiagaraRenderer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraDataInterfaceArrayFloat)

NDIARRAY_GENERATE_IMPL(UNiagaraDataInterfaceArrayFloat, float, FloatData)
NDIARRAY_GENERATE_IMPL_LWC(UNiagaraDataInterfaceArrayFloat2, FVector2f, FloatData)
NDIARRAY_GENERATE_IMPL_LWC(UNiagaraDataInterfaceArrayFloat3, FVector3f, FloatData)
NDIARRAY_GENERATE_IMPL(UNiagaraDataInterfaceArrayPosition, FNiagaraPosition, PositionData)
NDIARRAY_GENERATE_IMPL_LWC(UNiagaraDataInterfaceArrayFloat4, FVector4f, FloatData)
NDIARRAY_GENERATE_IMPL(UNiagaraDataInterfaceArrayColor, FLinearColor, ColorData)
NDIARRAY_GENERATE_IMPL_LWC(UNiagaraDataInterfaceArrayQuat, FQuat4f, QuatData)
