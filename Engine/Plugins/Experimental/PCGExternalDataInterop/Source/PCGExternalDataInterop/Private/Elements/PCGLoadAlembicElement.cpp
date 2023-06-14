// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGLoadAlembicElement.h"

#include "Alembic/PCGAlembicInterop.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "PCGLoadAlembic"

#if WITH_EDITOR
FText UPCGLoadAlembicSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Load Alembic");
}

FText UPCGLoadAlembicSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Loads data from an Alembic file");
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGLoadAlembicSettings::CreateElement() const
{
	return MakeShared<FPCGLoadAlembicElement>();
}

FPCGContext* FPCGLoadAlembicElement::Initialize(const FPCGDataCollection& InputData, TWeakObjectPtr<UPCGComponent> SourceComponent, const UPCGNode* Node)
{
	FPCGLoadAlembicContext* Context = new FPCGLoadAlembicContext();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	return Context;
}

bool FPCGLoadAlembicElement::PrepareLoad(FPCGExternalDataContext* InContext) const
{
	check(InContext);
	FPCGLoadAlembicContext* Context = static_cast<FPCGLoadAlembicContext*>(InContext);

	check(Context);
	const UPCGLoadAlembicSettings* Settings = Context->GetInputSettings<UPCGLoadAlembicSettings>();
	check(Settings);

#if WITH_EDITOR
	const FString FileName = Settings->AlembicFilePath.FilePath;
	PCGAlembicInterop::LoadFromAlembicFile(Context, FileName);

	if (!Context->PointDataAccessorsMapping.IsEmpty())
	{
		for (const FPCGExternalDataContext::FPointDataAccessorsMapping& DataMapping : Context->PointDataAccessorsMapping)
		{
			FPCGTaggedData& OutData = Context->OutputData.TaggedData.Emplace_GetRef();
			OutData.Data = DataMapping.PointData;
		}

		Context->bDataPrepared = true;
	}
#else
	PCGE_LOG(Error, GraphAndLog, LOCTEXT("NotSupportedInGameMode", "The Load Alembic node is not support in non-editor builds."));
#endif

	return true;
}

bool FPCGLoadAlembicElement::ExecuteLoad(FPCGExternalDataContext* InContext) const
{
	if (!FPCGExternalDataElement::ExecuteLoad(InContext))
	{
		return false;
	}

	// Finally, apply conversion
	check(InContext);
	FPCGLoadAlembicContext* Context = static_cast<FPCGLoadAlembicContext*>(InContext);
	check(Context);
	const UPCGLoadAlembicSettings* Settings = Context->GetInputSettings<UPCGLoadAlembicSettings>();
	check(Settings);

	const FVector& ConversionScale = Settings->ConversionScale;
	const FVector& ConversionRotation = Settings->ConversionRotation;

	const FTransform ConversionTransform(FRotator::MakeFromEuler(ConversionRotation), FVector::ZeroVector, ConversionScale);
	if (!ConversionTransform.Equals(FTransform::Identity))
	{
		for (const FPCGExternalDataContext::FPointDataAccessorsMapping& DataMapping : Context->PointDataAccessorsMapping)
		{
			UPCGPointData* PointData = DataMapping.PointData;

			if (!PointData)
			{
				continue;
			}

			TArray<FPCGPoint>& Points = PointData->GetMutablePoints();

			for (FPCGPoint& Point : Points)
			{
				Point.Transform = Point.Transform * ConversionTransform;
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE