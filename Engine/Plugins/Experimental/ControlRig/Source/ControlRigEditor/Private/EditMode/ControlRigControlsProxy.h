// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/LazyObjectPtr.h"
#include "TransformNoScale.h"
#include "ControlRigControlsProxy.generated.h"

struct FRigControl;
class UControlRig;
class IPropertyHandle;

UCLASS(Abstract)
class UControlRigControlsProxy : public UObject
{
	GENERATED_BODY()

public:
	UControlRigControlsProxy() : bSelected(false) {}
	virtual void SetName(const FName& InName)  { ControlName = InName; }
	virtual FName GetName() const { return ControlName; }
	virtual void ValueChanged() {}
	virtual void SelectionChanged(bool bInSelected);
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) {};

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

public:

	FRigControl* RigControl;
	TWeakObjectPtr<UControlRig> ControlRig;

	UPROPERTY()
	bool bSelected;
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Control")
	FName ControlName;
};

UCLASS()
class UControlRigTransformControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigTransformControlProxy() {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:
	
	UPROPERTY(EditAnywhere, Interp, AdvancedDisplay, Category = "Control")
	FTransform Transform;
};

UCLASS()
class UControlRigTransformNoScaleControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigTransformNoScaleControlProxy() {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:

	UPROPERTY(EditAnywhere, Interp, AdvancedDisplay, Category = "Control")
	FTransformNoScale Transform;
};

UCLASS()
class UControlRigFloatControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigFloatControlProxy()  {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:

	UPROPERTY(EditAnywhere, Interp, AdvancedDisplay, Category = "Control")
	float Float;
};


UCLASS()
class UControlRigVectorControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigVectorControlProxy() {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:

	UPROPERTY(EditAnywhere, Interp, AdvancedDisplay, Category = "Control")
	FVector Vector;
};

UCLASS()
class UControlRigVector2DControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigVector2DControlProxy() {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:

	UPROPERTY(EditAnywhere, Interp, AdvancedDisplay, Category = "Control")
	FVector2D Vector2D;
};

UCLASS()
class UControlRigBoolControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigBoolControlProxy() {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:

	UPROPERTY(EditAnywhere, Interp, AdvancedDisplay, Category = "Control")
	bool Bool;
};


/** Proxy in Details Panel */
UCLASS()
class UControlRigDetailPanelControlProxies :public UObject
{
	GENERATED_BODY()

	UControlRigDetailPanelControlProxies() {}
protected:

	UPROPERTY()
	TMap<FName, UControlRigControlsProxy*> AllProxies;

	UPROPERTY()
	TArray< UControlRigControlsProxy*> SelectedProxies;


public:
	UControlRigControlsProxy* FindProxy(const FName& Name) const;
	void AddProxy(const FName& Name, UControlRig* InControlRig, FRigControl* InControl);
	void RemoveProxy(const FName& Name );
	void ProxyChanged(const FName& Name);
	void RemoveAllProxies();
	void RecreateAllProxies(UControlRig* InControlRig);
	void SelectProxy(const FName& Name, bool bSelected);
	const TArray<UControlRigControlsProxy*>& GetSelectedProxies() const { return SelectedProxies;}

};
