// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusCoreNotify.h"
#include "OptimusDiagnostic.h"
#include "OptimusTemplates.h"

#include "UObject/Object.h"
#include "CoreMinimal.h"

#include "OptimusNode.generated.h"

struct FOptimusNodePinStorageConfig;
enum class EOptimusNodePinDirection : uint8;
enum class EOptimusNodePinStorageType : uint8;
class UOptimusActionStack;
class UOptimusNodeGraph;
class UOptimusNodePin;
struct FOptimusDataTypeRef;

UCLASS(Abstract)
class OPTIMUSDEVELOPER_API UOptimusNode : public UObject
{
	GENERATED_BODY()
public:
	struct CategoryName
	{
		OPTIMUSDEVELOPER_API static const FName DataProviders;
		OPTIMUSDEVELOPER_API static const FName Deformers;
		OPTIMUSDEVELOPER_API static const FName Resources;
		OPTIMUSDEVELOPER_API static const FName Variables;
		OPTIMUSDEVELOPER_API static const FName Values;
	};

	struct PropertyMeta
	{
		static const FName Category;
		static const FName Input;
		static const FName Output;
		static const FName Resource;
	};
public:
	UOptimusNode();

	/// @brief Returns the node class category.
	/// @return The node class category.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	virtual FName GetNodeCategory() const PURE_VIRTUAL(, return NAME_None;);

	/// @brief Returns the node class name. This name is immutable for the given node class.
	/// @return The node class name.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	FName GetNodeName() const;

	/// @brief Returns the display name to use on the graphical node in the 
	/// @return 
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	virtual FText GetDisplayName() const;

	/// @brief Set the display name for this node.
	/// @param InDisplayName 
	/// @return 
	bool SetDisplayName(FText InDisplayName);

	/// @brief Returns the position in the graph UI that the node should be placed.
	/// @return The coordinates of the node's position.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool SetGraphPosition(const FVector2D& InPosition);

	/// @brief Returns the position in the graph UI that the node should be placed.
	/// @return The coordinates of the node's position.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	FVector2D GetGraphPosition() const { return GraphPosition; }

	/// @brief Set a new position of the node in the graph UI.
	/// @param InPosition The coordinates of the new position.
	/// @return true if the position setting was successful (i.e. the coordinates are valid).
	bool SetGraphPositionDirect(const FVector2D &InPosition);

	/// @brief Returns the absolute path of the node. This can be passed to the root
	/// IOptimusNodeGraphCollectionOwner object to resolve to a node object.
	/// @return The absolute path of this node, rooted within the deformer.
	FString GetNodePath() const;

	/// Returns the owning node graph of this node.
	UOptimusNodeGraph *GetOwningGraph() const;

	const TArray<UOptimusNodePin*>& GetPins() const { return Pins; }

	/// Returns the node's diagnostic level (e.g. error state). For a node, only None, Warning
	/// Error are relevant.
	EOptimusDiagnosticLevel GetDiagnosticLevel() const { return DiagnosticLevel; }

	/// Find the pin associated with the given dot-separated pin path.
	/// @param InPinPath The path of the pin.
	/// @return The pin object, if found, otherwise nullptr.
	UOptimusNodePin* FindPin(const FString &InPinPath) const;

	/// Find the pin from the given path array.
	UOptimusNodePin* FindPinFromPath(const TArray<FName>& InPinPath) const;

	/// Find the pin associated with the given FProperty object(s).
	/// @param InRootProperty The property representing the pin root we're interested in.
	/// @param InSubProperty The property representing the actual pin the value changed on.
	/// @return The pin object, if found, otherwise nullptr.
	UOptimusNodePin* FindPinFromProperty(
	    const FProperty* InRootProperty,
	    const FProperty* InSubProperty
		) const;

	/// @brief Returns the class of all non-deprecated UOptimusNodeBase nodes that are defined, 
	/// in no particular order.
	/// @return List of all classes that derive from UOptimusNodeBase.
	static TArray<UClass*> GetAllNodeClasses();

	/// Called just after the node is created, either via direct creation or deletion undo.
	/// By default it creates the pins representing connectable properties.
	void PostCreateNode();

	/// Returns the current revision number. The number itself has no meaning except that
	/// it monotonically increases each time this node is modified in some way.
	int32 GetRevision() const
	{
		return Revision;
	}

	//== UObject overrides
#if WITH_EDITOR
	bool Modify( bool bInAlwaysMarkDirty=true ) override;
#endif
	void Serialize(FArchive& Ar) override;
	void PostLoad() override;
	
protected:
	friend class UOptimusNodeGraph;
	friend class UOptimusNodePin;
	friend class UOptimusDeformer;
	friend struct FOptimusNodeAction_AddRemovePin;
	friend struct FOptimusNodeAction_SetPinType;
	friend struct FOptimusNodeAction_SetPinName;
	friend struct FOptimusNodeAction_SetPinDataDomain;

	// Return the action stack for this node.
	UOptimusActionStack* GetActionStack() const;

	// Node layout data
	// FIXME: Move to private.
	UPROPERTY(NonTransactional)
	FVector2D GraphPosition;

	// Called when the node is being constructed
	virtual void ConstructNode();

	void EnableDynamicPins();

	UOptimusNodePin* AddPin(
		FName InName,
		EOptimusNodePinDirection InDirection,
		FOptimusNodePinStorageConfig InStorageConfig,
		FOptimusDataTypeRef InDataType,
		UOptimusNodePin* InBeforePin = nullptr
		);
	
	/** Create a pin and add it to the node in the location specified. */ 
	UOptimusNodePin* AddPinDirect(
		FName InName,
		EOptimusNodePinDirection InDirection,
		FOptimusNodePinStorageConfig InStorageConfig,
		FOptimusDataTypeRef InDataType,
		UOptimusNodePin* InBeforePin = nullptr,
		UOptimusNodePin* InParentPin = nullptr
		);

	// Remove a pin.
	bool RemovePin(
		UOptimusNodePin* InPin
		);

	// Remove the pin with no undo.
	bool RemovePinDirect(
		UOptimusNodePin* InPin
		);
	
	/** Set the pin data type. */
	bool SetPinDataType(
		UOptimusNodePin* InPin,
		FOptimusDataTypeRef InDataType
		);
	
	bool SetPinDataTypeDirect(
		UOptimusNodePin* InPin,
		FOptimusDataTypeRef InDataType
		);

	/** Set the pin name. */
	// FIXME: Hoist to public
	bool SetPinName(
		UOptimusNodePin* InPin,
		FName InNewName
		);
	
	bool SetPinNameDirect(
	    UOptimusNodePin* InPin,
	    FName InNewName
		);

	/** Set the pin's resource context names. */
	bool SetPinDataDomain(
		UOptimusNodePin* InPin,
		const TArray<FName>& InDataDomainLevelNames
		);

	bool SetPinDataDomainDirect(
		UOptimusNodePin* InPin,
		const TArray<FName>& InDataDomainLevelNames
		);
	
	void SetPinExpanded(const UOptimusNodePin* InPin, bool bInExpanded);
	bool GetPinExpanded(const UOptimusNodePin* InPin) const;

	// Set the current error state
	void SetDiagnosticLevel(EOptimusDiagnosticLevel InDiagnosticLevel);

	// A sentinel to indicate whether sending notifications is allowed.
	bool bSendNotifications = true;
	
private:
	void IncrementRevision();
	
	void Notify(
		EOptimusGraphNotifyType InNotifyType
	);

	bool CanNotify() const
	{
		return !bConstructingNode && bSendNotifications;
	}
	
	
	void CreatePinsFromStructLayout(
		const UStruct *InStruct, 
		UOptimusNodePin *InParentPin = nullptr
		);

	UOptimusNodePin* CreatePinFromProperty(
	    EOptimusNodePinDirection InDirection,
		const FProperty* InProperty,
		UOptimusNodePin* InParentPin = nullptr
	);

	// The display name to show. This is non-transactional because it is controlled by our 
	// action system rather than the transacting system for undo.
	UPROPERTY(NonTransactional)
	FText DisplayName;

	// The list of pins. Non-transactional for the same reason as above. 
	UPROPERTY(NonTransactional)
	TArray<UOptimusNodePin *> Pins;

	// The list of pins that should be shown as expanded in the graph view.
	UPROPERTY(NonTransactional)
	TSet<FName> ExpandedPins;

	UPROPERTY()
	EOptimusDiagnosticLevel DiagnosticLevel = EOptimusDiagnosticLevel::None;

	// The revision number. Incremented each time Modify is called. Can be used to check
	// if the object is now different and may need to be involved in updating the compute graph.
	int32 Revision = 0;

	// Set to true if the node is dynamic and can have pins arbitrarily added.
	bool bDynamicPins = false;

	// A sentinel to indicate we're doing node construction.
	bool bConstructingNode = false;

	/// Cached pin lookups
	mutable TMap<TArray<FName>, UOptimusNodePin*> CachedPinLookup;
};
