// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "IPropertyTypeCustomization.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;
class UGeometryCollectionComponent;

/** Details customization for rigid body selection. */
class FSelectedRigidBodyCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:
	static void GetSelectedGeometryCollectionCluster(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId, const UGeometryCollectionComponent*& OutGeometryCollectionComponent, int32& OutTransformIndex);

	static int32 GetParentClusterRigidBodyId(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId);
	static int32 GetChildClusterRigidBodyId(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId);
	static int32 GetPreviousClusteredSiblingRigidBodyId(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId);
	static int32 GetNextClusteredSiblingRigidBodyId(TSharedRef<IPropertyHandle> PropertyHandleActor, TSharedRef<IPropertyHandle> PropertyHandleId);

	FReply OnPick(TSharedRef<IPropertyHandle> PropertyHandleId) const;
};
