// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Graph/GraphHandle.h"

#include "GraphElement.generated.h"

UENUM()
enum class EGraphElementType
{
	Node,
	Edge,
	Island,
	Unknown
};

UCLASS(abstract)
class UGraphElement : public UObject
{
	GENERATED_BODY()
public:
	explicit UGraphElement(EGraphElementType InElementType)
		: ElementType(InElementType)
	{}

	EGraphElementType GetElementType() const { return ElementType; }

	friend class UGraph;
protected:
	UGraphElement() = default;

	void SetUniqueIndex(int64 InUniqueIndex) { UniqueIndex = InUniqueIndex; }
	int64 GetUniqueIndex() const { return UniqueIndex; }

	void SetParentGraph(TObjectPtr<UGraph> InGraph) { ParentGraph = InGraph; }
	TObjectPtr<UGraph> GetGraph() const { return ParentGraph.Get(); }

	/** Called when we create this element and prior to setting any properties. */
	virtual void OnCreate() {}

private:
	UPROPERTY()
	EGraphElementType ElementType = EGraphElementType::Unknown;

	/** Will match the UniqueIndex in the UGraphHandle that references this element. */
	UPROPERTY()
	int64 UniqueIndex = INDEX_NONE;

	UPROPERTY()
	TWeakObjectPtr<UGraph> ParentGraph = nullptr;
};