// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FieldNotification/FieldId.h"

namespace UE::FieldNotification
{
	
	struct IClassDescriptor
	{
		enum
		{
			Max_IndexOf_ = 0,
		};

		virtual int32 GetNumberOfField() const = 0;
		virtual FFieldId GetField(FName InFieldName) const = 0;
		virtual FFieldId GetField(int32 InFieldNumber) const = 0;
		virtual ~IClassDescriptor() = default;
	};

} // namespace