// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCustomVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FNiagaraCustomVersion::GUID(0xFCF57AFA, 0x50764283, 0xB9A9E658, 0xFFA02D32);

// Register the custom version with core
FCustomVersionRegistration GRegisterNiagaraCustomVersion(FNiagaraCustomVersion::GUID, FNiagaraCustomVersion::LatestVersion, TEXT("NiagaraVer"));

// Note: When encountering a conflict on this file please generate a new GUID
const FGuid FNiagaraCustomVersion::LatestScriptCompileVersion(0xE50787A5, 0x5670FE47, 0x8D2A1264, 0x7ED7E281);