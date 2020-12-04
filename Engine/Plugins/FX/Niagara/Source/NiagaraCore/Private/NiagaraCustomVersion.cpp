// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCustomVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FNiagaraCustomVersion::GUID(0xFCF57AFA, 0x50764283, 0xB9A9E658, 0xFFA02D32);

// Register the custom version with core
FCustomVersionRegistration GRegisterNiagaraCustomVersion(FNiagaraCustomVersion::GUID, FNiagaraCustomVersion::LatestVersion, TEXT("NiagaraVer"));


const FGuid FNiagaraCustomVersion::LatestScriptCompileVersion(0xFDC087A9, 0x4CE44C8C, 0xB0FFCD9A, 0x6A8BD5D4);
