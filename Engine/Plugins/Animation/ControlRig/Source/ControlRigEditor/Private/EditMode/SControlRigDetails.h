// Copyright Epic Games, Inc. All Rights Reserved.
/**
* View for containing details for various controls
*/
#pragma once

#include "CoreMinimal.h"
#include "ControlRigBaseDockableView.h"
#include "Widgets/SWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/FrameNumber.h"
#include "IDetailsView.h"
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "IDetailKeyframeHandler.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

class ISequencer;

class FControlRigEditModeGenericDetails : public IDetailCustomization
{
public:
	FControlRigEditModeGenericDetails() = delete;
	FControlRigEditModeGenericDetails(FEditorModeTools* InModeTools) : ModeTools(InModeTools) {}

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance(FEditorModeTools* InModeTools)
	{
		return MakeShareable(new FControlRigEditModeGenericDetails(InModeTools));
	}

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(class IDetailLayoutBuilder& DetailLayout) override;

protected:
	FEditorModeTools* ModeTools = nullptr;
};

class SControlRigDetails: public SCompoundWidget, public FControlRigBaseDockableView, public IDetailKeyframeHandler
{

	SLATE_BEGIN_ARGS(SControlRigDetails)
	{}
	SLATE_END_ARGS()
	~SControlRigDetails();

	void Construct(const FArguments& InArgs, FControlRigEditMode& InEditMode);

	/** Set the objects to be displayed in the details panel */
	void SetSettingsDetailsObject(const TWeakObjectPtr<>& InObject);
	void SetEulerTransformDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);
	void SetTransformDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);
	void SetTransformNoScaleDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);
	void SetFloatDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);
	void SetBoolDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);
	void SetIntegerDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);
	void SetEnumDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);
	void SetVectorDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);
	void SetVector2DDetailsObjects(const TArray<TWeakObjectPtr<>>& InObjects);

	// IDetailKeyframeHandler interface
	virtual bool IsPropertyKeyable(const UClass* InObjectClass, const class IPropertyHandle& PropertyHandle) const override;
	virtual bool IsPropertyKeyingEnabled() const override;
	virtual void OnKeyPropertyClicked(const IPropertyHandle& KeyedPropertyHandle) override;
	virtual bool IsPropertyAnimated(const class IPropertyHandle& PropertyHandle, UObject *ParentObject) const override;

	/** Display or edit set up for property */
	bool ShouldShowPropertyOnDetailCustomization(const struct FPropertyAndParent& InPropertyAndParent) const;
	bool IsReadOnlyPropertyOnDetailCustomization(const struct FPropertyAndParent& InPropertyAndParent) const;

private:
	void UpdateProxies();
	virtual void HandleControlSelected(UControlRig* Subject, FRigControlElement* InControl, bool bSelected) override;
	virtual void HandleControlAdded(UControlRig* ControlRig, bool bIsAdded) override;

	TSharedPtr<IDetailsView> ControlEulerTransformDetailsView;
	TSharedPtr<IDetailsView> ControlTransformDetailsView;
	TSharedPtr<IDetailsView> ControlTransformNoScaleDetailsView;
	TSharedPtr<IDetailsView> ControlFloatDetailsView;
	TSharedPtr<IDetailsView> ControlBoolDetailsView;
	TSharedPtr<IDetailsView> ControlIntegerDetailsView;
	TSharedPtr<IDetailsView> ControlEnumDetailsView;
	TSharedPtr<IDetailsView> ControlVector2DDetailsView;
	TSharedPtr<IDetailsView> ControlVectorDetailsView;

};

