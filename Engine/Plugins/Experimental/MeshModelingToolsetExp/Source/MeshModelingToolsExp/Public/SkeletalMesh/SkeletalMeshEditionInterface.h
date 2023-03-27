// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "UObject/WeakInterfacePtr.h"
#include "SkeletalMeshNotifier.h"

#include "HitProxies.h"
#include "GenericPlatform/ICursor.h"

#include "SkeletalMeshEditionInterface.generated.h"

class FSkeletalMeshToolNotifier;

/**
 * USkeletalMeshEditionInterface
 */

UINTERFACE()
class MESHMODELINGTOOLSEXP_API USkeletalMeshEditionInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * ISkeletalMeshEditionInterface
 */

class MESHMODELINGTOOLSEXP_API ISkeletalMeshEditionInterface
{
	GENERATED_BODY()

public:
	ISkeletalMeshNotifier& GetNotifier();
	bool NeedsNotification() const;

	void BindTo(TSharedPtr<ISkeletalMeshEditorBinding> InBinding);
	void Unbind();
	
protected:
	virtual void HandleSkeletalMeshModified(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType) = 0;
	TOptional<FName> GetBoneName(HHitProxy* InHitProxy) const;
	
	TWeakPtr<ISkeletalMeshEditorBinding> Binding;

private:
	TUniquePtr<FSkeletalMeshToolNotifier> Notifier;
	
	friend FSkeletalMeshToolNotifier;
};

/**
 * FSkeletalMeshToolNotifier
 */

class FSkeletalMeshToolNotifier: public ISkeletalMeshNotifier
{
public:
	FSkeletalMeshToolNotifier(TWeakInterfacePtr<ISkeletalMeshEditionInterface> InInterface);
	virtual void HandleNotification(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType) override;
	
protected:
	TWeakInterfacePtr<ISkeletalMeshEditionInterface> Interface;
};

/**
 * HBoneHitProxy
 */

struct HBoneHitProxy : public HHitProxy
{
	DECLARE_HIT_PROXY()

	int32 BoneIndex;
	FName BoneName;

	HBoneHitProxy(int32 InBoneIndex, FName InBoneName)
		: HHitProxy(HPP_Foreground)
		, BoneIndex(InBoneIndex)
		, BoneName(InBoneName)
	{}

	virtual EMouseCursor::Type GetMouseCursor() override { return EMouseCursor::Crosshairs; }
};
