// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCustomVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FNiagaraCustomVersion::GUID(0xFCF57AFA, 0x50764283, 0xB9A9E658, 0xFFA02D32);

// Register the custom version with core
FCustomVersionRegistration GRegisterNiagaraCustomVersion(FNiagaraCustomVersion::GUID, FNiagaraCustomVersion::LatestVersion, TEXT("NiagaraVer"));

// Note: When encountering a conflict on this file please generate a new GUID
const FGuid FNiagaraCustomVersion::LatestScriptCompileVersion(0x98862397, 0xB732490C, 0x9D9C8B1A, 0x6A4DA05F);
