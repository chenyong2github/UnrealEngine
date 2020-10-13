// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMCore/RigVMRegistry.h"
#include "RigVMPin.generated.h"

class URigVMGraph;
class URigVMNode;
class URigVMStructNode;
class URigVMPin;
class URigVMLink;

/**
 * The Injected Info is used for injecting a node on a pin.
 * Injected nodes are not visible to the user, but they are normal
 * nodes on the graph.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMInjectionInfo: public UObject
{
	GENERATED_BODY()

public:

	URigVMInjectionInfo()
	{
		bInjectedAsInput = true;
	}

	UPROPERTY()
	URigVMStructNode* StructNode;

	UPROPERTY()
	bool bInjectedAsInput;

	UPROPERTY()
	URigVMPin* InputPin;

	UPROPERTY()
	URigVMPin* OutputPin;

	// Returns the graph of this injected node.
	UFUNCTION(BlueprintCallable, Category = RigVMInjectionInfo)
	URigVMGraph* GetGraph() const;

	// Returns the pin of this injected node.
	UFUNCTION(BlueprintCallable, Category = RigVMInjectionInfo)
	URigVMPin* GetPin() const;
};


/**
 * The Visual Debugging Info is used for visually displaying
 * Data flowing through a pin. Typically this is attached to an input
 * pin causes the pin to inject a node on the link driving it
 */


/**
 * The Pin represents a single connector / pin on a node in the RigVM model.
 * Pins can be connected based on rules. Pins also provide access to a 'PinPath',
 * which essentially represents . separated list of names to reach the pin within
 * the owning graph. PinPaths are unique.
 * In comparison to the EdGraph Pin the URigVMPin supports the concept of 'SubPins',
 * so child / parent relationships between pins. A FVector Pin for example might
 * have its X, Y and Z components as SubPins. Array Pins will have its elements as
 * SubPins, and so on.
 * A URigVMPin is owned solely by a URigVMNode.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMPin : public UObject
{
	GENERATED_BODY()

public:

	// A map used to override pin default values
	typedef TMap<URigVMPin*, FString> FDefaultValueOverride;

	// Splits a PinPath at the start, so for example "Node.Color.R" becomes "Node" and "Color.R"
	static bool SplitPinPathAtStart(const FString& InPinPath, FString& LeftMost, FString& Right);

	// Splits a PinPath at the start, so for example "Node.Color.R" becomes "Node.Color" and "R"
	static bool SplitPinPathAtEnd(const FString& InPinPath, FString& Left, FString& RightMost);

	// Splits a PinPath into all segments, so for example "Node.Color.R" becomes ["Node", "Color", "R"]
	static bool SplitPinPath(const FString& InPinPath, TArray<FString>& Parts);

	// Joins a PinPath from to segments, so for example "Node.Color" and "R" becomes "Node.Color.R"
	static FString JoinPinPath(const FString& Left, const FString& Right);

	// Joins a PinPath from to segments, so for example ["Node", "Color", "R"] becomes "Node.Color.R"
	static FString JoinPinPath(const TArray<FString>& InParts);

	// Default constructor
	URigVMPin();

	// Returns a . separated path containing all names of the pin and its owners,
	// this includes the node name, for example "Node.Color.R"
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	FString GetPinPath() const;

	// Returns a . separated path containing all names of the pin within its main
	// memory owner / storage. This is typically used to create an offset pointer
	// within memory (FRigVMRegisterOffset).
	// So for example for a PinPath such as "Node.Transform.Translation.X" the 
	// corresponding SegmentPath is "Translation.X", since the transform is the
	// storage / memory.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	FString GetSegmentPath() const;

	// Returns the display label of the pin
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	FName GetDisplayName() const;

	// Returns the direction of the pin
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	ERigVMPinDirection GetDirection() const;

	// Returns true if the pin is currently expanded
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsExpanded() const;

	// Returns true if the pin is defined as a constant value / literal
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsDefinedAsConstant() const;

	// Returns true if the pin should be watched
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool RequiresWatch() const;

	// Returns true if the data type of the Pin is a struct
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsStruct() const;

	// Returns true if the Pin is a SubPin within a struct
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsStructMember() const;

	// Returns true if the data type of the Pin is an array
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsArray() const;

	// Returns true if the Pin is a SubPin within an array
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsArrayElement() const;

	// Returns true if this pin represents a dynamic array
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsDynamicArray() const;

	// Returns the index of the Pin within the node / parent Pin
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	int32 GetPinIndex() const;

	// Returns the number of elements within an array Pin
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	int32 GetArraySize() const;

	// Returns the C++ data type of the pin
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	FString GetCPPType() const;

	// Returns the C++ data type of an element of the Pin array
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	FString GetArrayElementCppType() const;

	// Returns true if the C++ data type is FString or FName
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsStringType() const;

	// Returns true if the C++ data type is an execute context
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsExecuteContext() const;

	// Returns the default value of the Pin as a string.
	// Note that this value is computed based on the Pin's
	// SubPins - so for example for a FVector typed Pin
	// the default value is actually composed out of the
	// default values of the X, Y and Z SubPins.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	FString GetDefaultValue() const;

	// Returns the default value with an additional override ma
	FString GetDefaultValue(const FDefaultValueOverride& InDefaultValueOverride) const;

	// Returns the name of a custom widget to be used
	// for editing the Pin.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	FName GetCustomWidgetName() const;

	// Returns the tooltip of this pin
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	FText GetToolTipText() const;

	// Returns the struct of the data type of the Pin,
	// or nullptr otherwise.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	UObject* GetCPPTypeObject() const;

	// Returns the struct of the data type of the Pin,
	// or nullptr otherwise.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	UScriptStruct* GetScriptStruct() const;

	// Returns the enum of the data type of the Pin,
	// or nullptr otherwise.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	UEnum* GetEnum() const;

	// Returns the parent Pin - or nullptr if the Pin
	// is nested directly below a node.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	URigVMPin* GetParentPin() const;

	// Returns the top-most parent Pin, so for example
	// for "Node.Transform.Translation.X" this returns
	// the Pin for "Node.Transform".
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	URigVMPin* GetRootPin() const;

	// Returns the pin to be used for a link.
	// This might differ from this actual pin, since
	// the pin might contain injected nodes.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	URigVMPin* GetPinForLink() const;

	// Returns the original pin for a pin on an injected
	// node. This can be used to determine where a link
	// should go in the user interface
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	URigVMPin* GetOriginalPinFromInjectedNode() const;

	// Returns all of the SubPins of this one.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	const TArray<URigVMPin*>& GetSubPins() const;

	// Returns a SubPin given a name / path or nullptr.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	URigVMPin* FindSubPin(const FString& InPinPath) const;

	// Returns true if this Pin is linked to another Pin
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	bool IsLinkedTo(URigVMPin* InPin) const;

	// Returns all of the links linked to this Pin.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	const TArray<URigVMLink*>& GetLinks() const;

	// Returns all of the linked source Pins,
	// using this Pin as the target.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	TArray<URigVMPin*> GetLinkedSourcePins(bool bRecursive = false) const;

	// Returns all of the linked target Pins,
	// using this Pin as the source.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	TArray<URigVMPin*> GetLinkedTargetPins(bool bRecursive = false) const;

	// Returns all of the source pins
	// using this Pin as the target.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	TArray<URigVMLink*> GetSourceLinks(bool bRecursive = false) const;

	// Returns all of the target links,
	// using this Pin as the source.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	TArray<URigVMLink*> GetTargetLinks(bool bRecursive = false) const;

	// Returns the node of this Pin.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	URigVMNode* GetNode() const;

	// Returns the graph of this Pin.
	UFUNCTION(BlueprintCallable, Category = RigVMPin)
	URigVMGraph* GetGraph() const;

	// Returns true is the two provided source and target Pins
	// can be linked to one another.
	static bool CanLink(URigVMPin* InSourcePin, URigVMPin* InTargetPin, FString* OutFailureReason = nullptr);

	// Returns true if this pin has injected nodes
	bool HasInjectedNodes() const { return InjectionInfos.Num() > 0; }

	// Returns the injected nodes this pin contains.
	const TArray<URigVMInjectionInfo*> GetInjectedNodes() const { return InjectionInfos; }

	// helper function to retrieve an object from a path
	static UObject* FindObjectFromCPPTypeObjectPath(const FString& InObjectPath);
	template<class T>
	FORCEINLINE static T* FindObjectFromCPPTypeObjectPath(const FString& InObjectPath)
	{
		return Cast<T>(FindObjectFromCPPTypeObjectPath(InObjectPath));
	}

	// Returns the name of the context this pin belongs to
	FName GetSliceContext(const FRigVMUserDataArray& InUserData);

	// Returns the number of slices in memory exist for this pin
	int32 GetNumSlices(const FRigVMUserDataArray& InUserData);

	// Returns true if the pin should not show up on a node, but in the details panel
	bool ShowInDetailsPanelOnly() const;

private:

	void UpdateCPPTypeObjectIfRequired() const;
	void SetNameFromIndex();

	UPROPERTY()
	FName DisplayName;

	UPROPERTY()
	ERigVMPinDirection Direction;

	UPROPERTY()
	bool bIsExpanded;

	UPROPERTY()
	bool bIsConstant;

	UPROPERTY(transient)
	bool bRequiresWatch;

	UPROPERTY()
	bool bIsDynamicArray;

	UPROPERTY()
	FString CPPType;

	UPROPERTY(transient)
	UObject* CPPTypeObject;

	UPROPERTY()
	FName CPPTypeObjectPath;

	UPROPERTY()
	FString DefaultValue;

	UPROPERTY()
	FName CustomWidgetName;

	UPROPERTY()
	TArray<URigVMPin*> SubPins;

	UPROPERTY(transient)
	TArray<URigVMLink*> Links;

	UPROPERTY()
	TArray<URigVMInjectionInfo*> InjectionInfos;

	friend class URigVMController;
	friend class URigVMGraph;
	friend class URigVMNode;
	friend class FRigVMParserAST;
};
