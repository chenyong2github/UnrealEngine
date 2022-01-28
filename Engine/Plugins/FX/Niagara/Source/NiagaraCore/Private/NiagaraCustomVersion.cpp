// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCustomVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FNiagaraCustomVersion::GUID(0xFCF57AFA, 0x50764283, 0xB9A9E658, 0xFFA02D32);

// Register the custom version with core
FCustomVersionRegistration GRegisterNiagaraCustomVersion(FNiagaraCustomVersion::GUID, FNiagaraCustomVersion::LatestVersion, TEXT("NiagaraVer"));

// Note: When encountering a conflict on this file please generate a new GUID
const FGuid FNiagaraCustomVersion::LatestScriptCompileVersion(0x0C09B728, 0x5EE44F4A, 0x9E135A16, 0xAA8A0160);