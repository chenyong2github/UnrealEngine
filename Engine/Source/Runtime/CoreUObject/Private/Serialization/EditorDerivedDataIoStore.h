// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#if WITH_EDITORONLY_DATA

#include "IO/IoDispatcher.h"
#include "IO/IoDispatcherBackend.h"
#include "Templates/SharedPointer.h"

namespace UE::DerivedData::Private { class FEditorDerivedData; }

namespace UE::DerivedData::IoStore
{

class IEditorDerivedDataIoStore : public IIoDispatcherBackend
{
public:
	virtual FIoChunkId AddData(const Private::FEditorDerivedData* Data) = 0;
	virtual const Private::FEditorDerivedData* RemoveData(const FIoChunkId& ChunkId) = 0;
};

TSharedRef<IEditorDerivedDataIoStore> CreateEditorDerivedDataIoStore();

} // UE::DerivedData::IoStore

#endif // WITH_EDITORONLY_DATA
