// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCustomVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FNiagaraCustomVersion::GUID(0xFCF57AFA, 0x50764283, 0xB9A9E658, 0xFFA02D32);

// Register the custom version with core
FCustomVersionRegistration GRegisterNiagaraCustomVersion(FNiagaraCustomVersion::GUID, FNiagaraCustomVersion::LatestVersion, TEXT("NiagaraVer"));

const FGuid FNiagaraCustomVersion::LatestScriptCompileVersion(0x37BF6E63, 0x7F1144F2, 0xAD4EC385, 0x0A16472D);

