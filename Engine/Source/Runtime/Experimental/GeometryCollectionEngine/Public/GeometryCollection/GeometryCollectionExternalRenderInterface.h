// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "GeometryCollectionExternalRenderInterface.generated.h"

class UGeometryCollection;
class UGeometryCollectionComponent;

UINTERFACE()
class GEOMETRYCOLLECTIONENGINE_API UGeometryCollectionExternalRenderInterface : public UInterface
{
	GENERATED_BODY()
};

class GEOMETRYCOLLECTIONENGINE_API IGeometryCollectionExternalRenderInterface
{
	GENERATED_BODY()

public:
	virtual void OnRegisterGeometryCollection(UGeometryCollectionComponent const& InComponent) = 0;
	virtual void OnUnregisterGeometryCollection() = 0;
	virtual void UpdateState(UGeometryCollection const& InGeometryCollection, bool bInIsBroken) = 0;
	virtual void UpdateRootTransform(UGeometryCollection const& InGeometryCollection, FTransform const& InBaseTransform, FTransform const& InRootTransform) = 0;
	virtual void UpdateTransforms(UGeometryCollection const& InGeometryCollection, FTransform const& InBaseTransform, TArrayView<const FMatrix> InMatrices) = 0;
};
