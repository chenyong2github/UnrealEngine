// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "ILiveLinkClient.h"
#include "LiveLinkComponentController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLiveLinkTickDelegate, float, DeltaTime);

class ULiveLinkControllerBase;

UCLASS( ClassGroup=(LiveLink), meta=(DisplayName="LiveLink Controller", BlueprintSpawnableComponent) )
class LIVELINKCOMPONENTS_API ULiveLinkComponentController : public UActorComponent
{
	GENERATED_BODY()
	
public:
	ULiveLinkComponentController();

public:
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category="LiveLink")
	FLiveLinkSubjectRepresentation SubjectRepresentation;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Instanced, NoClear)
	ULiveLinkControllerBase* Controller_DEPRECATED;
#endif

	/** Instanced controllers used to control the desired role */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "LiveLink", Instanced, NoClear, meta = (ShowOnlyInnerProperties))
	TMap<TSubclassOf<ULiveLinkRole>, ULiveLinkControllerBase*> ControllerMap;

	UPROPERTY(EditAnywhere, Category="LiveLink", AdvancedDisplay)
	bool bUpdateInEditor;
	
	// This Event is triggered any time new LiveLink data is available, including in the editor
	UPROPERTY(BlueprintAssignable, Category = "LiveLink")
	FLiveLinkTickDelegate OnLiveLinkUpdated;

	UPROPERTY(EditInstanceOnly, Category = "LiveLink", meta = (UseComponentPicker, AllowedClasses = "ActorComponent", DisallowedClasses = "LiveLinkComponentController"))
	FComponentReference ComponentToControl;

protected:
	// Keep track when component gets registered or controller map gets changed
	bool bIsDirty;

public:
	
	/** Creates an instance of the desired controller class for a specified Role class */
	void SetControllerClassForRole(TSubclassOf<ULiveLinkRole> RoleClass, TSubclassOf<ULiveLinkControllerBase> DesiredControllerClass);

	/** Creates an instance of the desired controller class for a specified Role class */
	FLiveLinkSubjectRepresentation GetSubjectRepresentation() const { return SubjectRepresentation; }

	/** Sets SubjectRepresentation and if required, will update the Controllers map associated to the Role */
	void SetSubjectRepresentation(const FLiveLinkSubjectRepresentation& InSubjectRepresentation);
	
	/** Returns true if ControllerMap needs to be updated for the current Role. Useful for customization or C++ modification to the Role */
	bool IsControllerMapOutdated() const;
	
	/** Used to notify that the subject role has changed. Mainly from Customization or C++ modification to the subject's Role */
	void OnSubjectRoleChanged();

	//~ Begin UActorComponent interface
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent interface

	//~ UObject interface
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	//~ End UObject interface

protected:

	/** Returns an array representing the class hierarchy of the given class */
	TArray<TSubclassOf<ULiveLinkRole>> GetSelectedRoleHierarchyClasses(const TSubclassOf<ULiveLinkRole> InCurrentRoleClass) const;
	TSubclassOf<ULiveLinkControllerBase> GetControllerClassForRoleClass(const TSubclassOf<ULiveLinkRole> RoleClass) const;

#if WITH_EDITOR
	/** Called during loading to convert old data to new scheme. */
	void ConvertOldControllerSystem();
#endif //WITH_EDITOR
};