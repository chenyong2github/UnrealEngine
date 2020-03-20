// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/ArchiveProxy.h"

/*-----------------------------------------------------------------------------
	FArchiveProxy implementation.
-----------------------------------------------------------------------------*/

FArchiveProxy::FArchiveProxy(FArchive& InInnerArchive)
: FArchive    (InInnerArchive)
, InnerArchive(InInnerArchive)
{
	LinkProxy(&InnerArchive, this);
}

FArchiveProxy::~FArchiveProxy()
{
	UnlinkProxy(&InnerArchive, this);
}
