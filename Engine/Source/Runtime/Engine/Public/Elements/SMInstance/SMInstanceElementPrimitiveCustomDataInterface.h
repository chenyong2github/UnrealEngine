// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Interfaces/TypedElementPrimitiveCustomDataInterface.h"
#include "SMInstanceElementPrimitiveCustomDataInterface.generated.h"

UCLASS()
class ENGINE_API USMInstanceElementPrimitiveCustomDataInterface : public UObject, public ITypedElementPrimitiveCustomDataInterface
{
	GENERATED_BODY()

public:
	virtual void SetCustomData(const FTypedElementHandle& InElementHandle, TArrayView<const float> CustomDataFloats,  bool bMarkRenderStateDirty) override;
	virtual void SetCustomDataValue(const FTypedElementHandle& InElementHandle, int32 CustomDataIndex, float CustomDataValue, bool bMarkRenderStateDirty) override;
};
