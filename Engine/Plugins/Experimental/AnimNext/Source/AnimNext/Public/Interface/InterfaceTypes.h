// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Param/ParamTypeHandle.h"

#define ANIM_NEXT_INTERFACE_RETURN_TYPE(Typename) \
virtual UE::AnimNext::FParamTypeHandle GetReturnTypeHandleImpl() const final override\
{\
	return UE::AnimNext::FParamTypeHandle::GetHandle<Typename>();\
}\
