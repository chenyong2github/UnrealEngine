// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AllocationsProvider.h"
#include "UObject/NameTypes.h"

#if defined(UE_USE_ALLOCATIONS_PROVIDER)

////////////////////////////////////////////////////////////////////////////////
class FAllocationsProvider
	: public IAllocationsProvider
{
public:
						FAllocationsProvider();
	virtual				~FAllocationsProvider();
	FName				GetName() const;
	virtual QueryHandle	StartQuery(double TimeA, double TimeB, ECrosses Crosses) override;
	virtual void		CancelQuery(QueryHandle Query) override;
	virtual QueryStatus	PollQuery(QueryHandle Query) override;
};

#endif // UE_USE_ALLOCATIONS_PROVIDER
