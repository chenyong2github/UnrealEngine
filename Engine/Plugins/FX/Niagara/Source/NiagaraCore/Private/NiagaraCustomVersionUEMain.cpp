// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCustomVersionUEMain.h"

const FGuid FNiagaraCustomVersionUEMain::GUID(0xFACD84A7, 0xCEFF5F42, 0x8FB5674C, 0x40CE42BB);

// Register the custom version with core
FCustomVersionRegistration GRegisterNiagaraCustomVersionUE5Main(FNiagaraCustomVersionUEMain::GUID, FNiagaraCustomVersionUEMain::LatestVersion, TEXT("NiagaraVerUEMain"));
