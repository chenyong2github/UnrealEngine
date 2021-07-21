// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMMemoryCommon.h"

#if DEBUG_RIGVMMEMORY
	DEFINE_LOG_CATEGORY(LogRigVMMemory);
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FRigVMOperand::Serialize(FArchive& Ar)
{
	Ar << MemoryType;
	Ar << RegisterIndex;
	Ar << RegisterOffset;
}
