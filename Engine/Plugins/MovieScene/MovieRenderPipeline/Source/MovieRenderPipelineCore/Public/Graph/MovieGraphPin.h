// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "MovieGraphPin.generated.h"

// Forward Declares
class UMovieGraphNode;
class UMovieGraphEdge;

USTRUCT(BlueprintType)
struct FMovieGraphPinProperties
{
	GENERATED_BODY()

	FMovieGraphPinProperties() = default;
	explicit FMovieGraphPinProperties(const FName& InLabel, bool bInAllowMultipleConnections)
		: Label(InLabel)
		, bAllowMultipleConnections(bInAllowMultipleConnections)
	{}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	FName Label = NAME_None;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	bool bAllowMultipleConnections = true;

	bool operator==(const FMovieGraphPinProperties& Other) const
	{
		return Label == Other.Label
			&& bAllowMultipleConnections == Other.bAllowMultipleConnections;
	}

	bool operator !=(const FMovieGraphPinProperties& Other) const
	{
		return !(*this == Other);
	}
};


UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphPin : public UObject
{
	GENERATED_BODY()

public:
	bool AddEdgeTo(UMovieGraphPin* InOtherPin);
	bool BreakEdgeTo(UMovieGraphPin* InOtherPin);
	bool BreakAllEdges();
	bool IsConnected() const;
	bool IsOutputPin() const;
	int32 EdgeCount() const;
public:
	// The node that this pin belongs to.
	UPROPERTY(BlueprintReadOnly, Category = "Properties")
	TObjectPtr<UMovieGraphNode> Node = nullptr;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Properties", meta = (ShowOnlyInnerProperties))
	FMovieGraphPinProperties Properties;

	UPROPERTY(BlueprintReadOnly, Category = "Properties")
	TArray<TObjectPtr<UMovieGraphEdge>> Edges;
};