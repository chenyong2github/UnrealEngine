// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "SubobjectDataFactory.h"

/**
* This subobject data factory will create native and blueprint inherited subobjects
*/
class FInheritedSubobjectDataFactory : public ISubobjectDataFactory
{
public:
	
	// Begin ISubobjectDataFactory interface
	virtual FName GetID() const { return TEXT("InheritedSubobjectFactory"); }
	virtual TSharedPtr<FSubobjectData> CreateSubobjectData(const FCreateSubobjectParams& Params) override;
	virtual bool ShouldCreateSubobjectData(const FCreateSubobjectParams& Params) const override;
	// End ISubobjectDataFactory
};