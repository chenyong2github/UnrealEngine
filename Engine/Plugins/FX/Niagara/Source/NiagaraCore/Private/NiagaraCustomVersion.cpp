// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraCustomVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FNiagaraCustomVersion::GUID(0xFCF57AFA, 0x50764283, 0xB9A9E658, 0xFFA02D32);

// Register the custom version with core
FCustomVersionRegistration GRegisterNiagaraCustomVersion(FNiagaraCustomVersion::GUID, FNiagaraCustomVersion::LatestVersion, TEXT("NiagaraVer"));

const FGuid FNiagaraCustomVersion::LatestScriptCompileVersion(0xDD83EBDB, 0x347040EC, 0x8077BE9F, 0x7635CD04);

