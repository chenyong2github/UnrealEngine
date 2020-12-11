// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/LazyObjectPtr.h"
#include "TransformNoScale.h"
#include "EulerTransform.h"

#if WITH_EDITOR
#include "IPropertyTypeCustomization.h"
#endif

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
	UPROPERTY(VisibleAnywhere, Category = "Control")
	FName ControlName;
protected:
	void CheckEditModeOnSelectionChange(UControlRig* InControlRig);
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
	
	UPROPERTY(EditAnywhere, Interp, Category = "Control")
	FTransform Transform;
};


UCLASS()
class UControlRigEulerTransformControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigEulerTransformControlProxy() {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:

	UPROPERTY(EditAnywhere, Interp, Category = "Control")
	FEulerTransform Transform;
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

	UPROPERTY(EditAnywhere, Interp, Category = "Control")
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

	UPROPERTY(EditAnywhere, Interp, Category = "Control")
	float Float;
};

UCLASS()
class UControlRigIntegerControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigIntegerControlProxy() {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:

	UPROPERTY(EditAnywhere, Interp, Category = "Control")
	int32 Integer;
};

USTRUCT(BlueprintType)
struct FControlRigEnumControlProxyValue
{
	GENERATED_USTRUCT_BODY()

	FControlRigEnumControlProxyValue()
	{
		EnumType = nullptr;
		EnumIndex = INDEX_NONE;
	}
	
	UPROPERTY()
	UEnum* EnumType;

	UPROPERTY(EditAnywhere, Category = Enum)
	int32 EnumIndex;
};

#if WITH_EDITOR

class FControlRigEnumControlProxyValueDetails : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FControlRigEnumControlProxyValueDetails);
	}

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	int32 GetEnumValue() const;
	void OnEnumValueChanged(int32 InValue, ESelectInfo::Type InSelectInfo, TSharedRef<IPropertyHandle> InStructHandle);

	UControlRigEnumControlProxy* ProxyBeingCustomized;
};
#endif

UCLASS()
class UControlRigEnumControlProxy : public UControlRigControlsProxy
{
	GENERATED_BODY()
	UControlRigEnumControlProxy() {}

	// UObject interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	//UControlRigControlsProxy
	virtual void ValueChanged() override;
	virtual void SetKey(const IPropertyHandle& KeyedPropertyHandle) override;

public:

	UPROPERTY(EditAnywhere, Interp, Category = "Control")
	FControlRigEnumControlProxyValue Enum;
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

	UPROPERTY(EditAnywhere, Interp, Category = "Control")
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

	UPROPERTY(EditAnywhere, Interp, Category = "Control")
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

	UPROPERTY(EditAnywhere, Interp, Category = "Control")
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
