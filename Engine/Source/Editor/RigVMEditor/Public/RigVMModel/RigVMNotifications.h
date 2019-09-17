// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMNotifications.generated.h"

class URigVMGraph;

/**
 * The Graph Notification Type is used to differentiate
 * between all of the changes that can happen within a graph.
 */
UENUM(BlueprintType)
enum class ERigVMGraphNotifType : uint8
{
	GraphChanged, // The graph has changed / a new graph has been picked (Subject == nullptr)
	NodeAdded, // A node has been added to the graph (Subject == URigVMNode)
	NodeRemoved, // A node has been removed from the graph (Subject == URigVMNode)
	NodeSelected, // A node has been selected (Subject == URigVMNode)
	NodeDeselected, // A node has been deselected (Subject == URigVMNode)
	NodePositionChanged, // A node's position has changed (Subject == URigVMNode)
	NodeSizeChanged, // A node's size has changed (Subject == URigVMNode)
	NodeColorChanged, // A node's color has changed (Subject == URigVMNode)
	PinArraySizeChanged, // An array pin's size has changed (Subject == URigVMPin)
	PinDefaultValueChanged, // A pin's default value has changed (Subject == URigVMPin)
	PinDirectionChanged, // A pin's direction has changed (Subject == URigVMPin)
	PinTypeChanged, // A pin's data type has changed (Subject == URigVMPin)
	LinkAdded, // A link has been added (Subject == URigVMLink)
	LinkRemoved, // A link has been removed (Subject == URigVMLink)
	CommentTextChanged, // A comment node's text has changed (Subject == URigVMCommentNode)
	RerouteCompactnessChanged, // A reroute node's compactness has changed (Subject == URigVMRerouteNode)
	VariableRenamed, // A variable has been renamed (Subject == URigVMVariableNode)
	ParameterRenamed, // A parameter has been renamed (Subject == URigVMParameterNode)
	Invalid // The max for this enum (used for guarding)
};

// A delegate for subscribing / reacting to graph modifications.
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigVMGraphModifiedEvent, ERigVMGraphNotifType /* type */, URigVMGraph* /* graph */, UObject* /* subject */);

// A dynamic delegate for subscribing / reacting to graph modifications (used for Python integration).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRigVMGraphModifiedDynamicEvent, ERigVMGraphNotifType, NotifType, URigVMGraph*, Graph, UObject*, Subject);