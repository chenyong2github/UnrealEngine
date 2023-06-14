// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/IO/PCGExternalData.h"

#include "PCGLoadAlembicElement.generated.h"

struct FPCGExternalDataContext;

UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGEXTERNALDATAINTEROP_API UPCGLoadAlembicSettings : public UPCGExternalDataSettings
{
	GENERATED_BODY()

public:
	// ~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("LoadAlembic")); }
	virtual FText GetDefaultNodeTitle() const override;
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const override;
	// ~End UPCGSettings interface

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Alembic", meta = (FilePathFilter = "Alembic files (*.abc)|*.abc", PCG_Overridable))
	FFilePath AlembicFilePath;

	// To prevent a dependency on the alembic editor module in this class, we'll keep around only the types we need
	/** Scale to apply during import. Note that for both Max/Maya presets the value flips the Y axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Alembic", meta = (PCG_Overridable))
	FVector ConversionScale = FVector(1.0f, -1.0f, 1.0f);

	/** Rotation in Euler angles applied during import. For Max, use (90, 0, 0). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Alembic", meta = (PCG_Overridable))
	FVector ConversionRotation = FVector::ZeroVector;
};

class PCGEXTERNALDATAINTEROP_API FPCGLoadAlembicElement : public FPCGExternalDataElement
{
public:
	virtual FPCGContext* Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node) override;

protected:
	virtual bool PrepareLoad(FPCGExternalDataContext* Context) const override;
	virtual bool ExecuteLoad(FPCGExternalDataContext* Context) const override;
};