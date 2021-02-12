// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/HashBuilder.h"
#include "Templates/SubclassOf.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartitionEditorSpatialHash.generated.h"

UCLASS()
class ENGINE_API UWorldPartitionEditorSpatialHash : public UWorldPartitionEditorHash
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	friend class SWorldPartitionEditorGridSpatialHash;

	struct FCellCoord
	{
		FCellCoord(int64 InX, int64 InY, int64 InZ, int32 InLevel)
			: X(InX)
			, Y(InY)
			, Z(InZ)
			, Level(InLevel)
		{}

		int64 X;
		int64 Y;
		int64 Z;
		int32 Level;

		inline uint32 GetChildIndex() const
		{
			return ((X & 1) << 2) | ((Y & 1) << 1) | (Z & 1);
		}

		inline FCellCoord GetChildCellCoord(uint32 ChildIndex) const
		{
			check(Level);
			check(ChildIndex < 8);

			return FCellCoord(
				(X << 1) | (ChildIndex >> 2),
				(Y << 1) | ((ChildIndex >> 1) & 1),
				(Z << 1) | (ChildIndex & 1),
				Level - 1
			);
		}

		inline FCellCoord GetParentCellCoord() const
		{
			return FCellCoord(X >> 1, Y >> 1, Z >> 1, Level + 1);
		}

		bool operator==(const FCellCoord& Other) const
		{
			return (X == Other.X) && (Y == Other.Y) && (Z == Other.Z) && (Level == Other.Level);
		}

		friend ENGINE_API uint32 GetTypeHash(const FCellCoord& CellCoord)
		{
			FHashBuilder HashBuilder;
			HashBuilder << CellCoord.X << CellCoord.Y << CellCoord.Z << CellCoord.Level;
			return HashBuilder.GetHash();
		}
	};

	inline FCellCoord GetCellCoords(const FVector& InPos, int32 Level) const
	{
		check(Level >= 0);
		const int32 CellSizeForLevel = CellSize * (1 << Level);
		return FCellCoord(
			FMath::FloorToInt(InPos.X / CellSizeForLevel),
			FMath::FloorToInt(InPos.Y / CellSizeForLevel),
			FMath::FloorToInt(InPos.Z / CellSizeForLevel),
			Level
		);
	}

	inline FBox GetCellBounds(const FCellCoord& InCellCoord) const
	{
		check(InCellCoord.Level >= 0);
		const int32 CellSizeForLevel = CellSize * (1 << InCellCoord.Level);
		const FVector Min = FVector(
			InCellCoord.X * CellSizeForLevel, 
			InCellCoord.Y * CellSizeForLevel, 
			InCellCoord.Z * CellSizeForLevel
		);
		const FVector Max = Min + FVector(CellSizeForLevel);
		return FBox(Min, Max);
	}

	inline int32 GetLevelForBox(const FBox& Box) const
	{
		const FVector Extent = Box.GetExtent();
		const float MaxLength = Extent.GetMax() * 2.0;
		return FMath::CeilToInt(FMath::Max<float>(FMath::Log2(MaxLength / CellSize), 0));
	}

	inline int32 ForEachIntersectingCells(const FBox& InBounds, int32 Level, TFunctionRef<void(const FCellCoord&)> InOperation) const
	{
		int32 NumCells = 0;

		FCellCoord MinCellCoords(GetCellCoords(InBounds.Min, Level));
		FCellCoord MaxCellCoords(GetCellCoords(InBounds.Max, Level));

		for (int32 z=MinCellCoords.Z; z<=MaxCellCoords.Z; z++)
		{
			for (int32 y=MinCellCoords.Y; y<=MaxCellCoords.Y; y++)
			{
				for (int32 x=MinCellCoords.X; x<=MaxCellCoords.X; x++)
				{
					InOperation(FCellCoord(x, y, z, Level));
					NumCells++;
				}
			}
		}

		return NumCells;
	}

	struct FCellNode
	{
		FCellNode()
			: ChildNodesMask(0)
			, ChildNodesLoadedMask(0)
		{}

		inline bool HasChildNodes() const
		{
			return !!ChildNodesMask;
		}

		inline bool HasChildNode(uint32 ChildIndex) const
		{
			check(ChildIndex < 8);
			return !!(ChildNodesMask & (1 << ChildIndex));
		}

		inline void AddChildNode(uint32 ChildIndex)
		{
			check(ChildIndex < 8);
			uint32 ChildMask = 1 << ChildIndex;
			check(!(ChildNodesMask & ChildMask));
			ChildNodesMask |= ChildMask;
		}

		inline void RemoveChildNode(uint32 ChildIndex)
		{
			check(ChildIndex < 8);
			uint32 ChildMask = 1 << ChildIndex;
			check(ChildNodesMask & ChildMask);
			ChildNodesMask &= ~ChildMask;
		}

		inline void ForEachChild(TFunctionRef<void(uint32 ChildIndex)> InOperation) const
		{
			int32 CurChildNodesMask = ChildNodesMask;

			while(CurChildNodesMask)
			{
				const int32 ChildIndex = FMath::CountTrailingZeros(CurChildNodesMask);
				
				check(CurChildNodesMask & (1 << ChildIndex));
				CurChildNodesMask &= ~(1 << ChildIndex);

				InOperation(ChildIndex);
			}
		}

		inline bool HasChildLoadedNodes() const
		{
			return !!ChildNodesLoadedMask;
		}

		inline bool HasChildLoadedNode(uint32 ChildIndex) const
		{
			check(ChildIndex < 8);
			return !!(ChildNodesLoadedMask & (1 << ChildIndex));
		}

		inline void AddChildLoadedNode(uint32 ChildIndex)
		{
			check(ChildIndex < 8);
			uint32 ChildMask = 1 << ChildIndex;
			check(ChildNodesMask & ChildMask);
			check(!(ChildNodesLoadedMask & ChildMask));
			ChildNodesLoadedMask |= ChildMask;
		}

		inline void RemoveChildLoadedNode(uint32 ChildIndex)
		{
			check(ChildIndex < 8);
			uint32 ChildMask = 1 << ChildIndex;
			check(ChildNodesMask & ChildMask);
			check(ChildNodesLoadedMask & ChildMask);
			ChildNodesLoadedMask &= ~ChildMask;
		}

		uint8 ChildNodesMask;
		uint8 ChildNodesLoadedMask;
	};

public:
	virtual ~UWorldPartitionEditorSpatialHash() {}

	// UWorldPartitionEditorHash interface begin
	virtual void Initialize() override;
	virtual void SetDefaultValues() override;
	virtual FName GetWorldPartitionEditorName() override;
	virtual FBox GetEditorWorldBounds() const override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void HashActor(FWorldPartitionHandle& InActorHandle) override;
	virtual void UnhashActor(FWorldPartitionHandle& InActorHandle) override;

	virtual void OnCellLoaded(const UWorldPartitionEditorCell* Cell) override;
	virtual void OnCellUnloaded(const UWorldPartitionEditorCell* Cell) override;

	virtual int32 ForEachIntersectingActor(const FBox& Box, TFunctionRef<void(FWorldPartitionActorDesc*)> InOperation) override;
	virtual int32 ForEachIntersectingCell(const FBox& Box, TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation) override;
	virtual int32 ForEachCell(TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation) override;
	virtual UWorldPartitionEditorCell* GetAlwaysLoadedCell() override;
	// UWorldPartitionEditorHash interface end
#endif

#if WITH_EDITORONLY_DATA
private:
	int32 ForEachIntersectingUnloadedRegion(const FBox& Box, TFunctionRef<void(const FCellCoord&)> InOperation);
	int32 ForEachIntersectingUnloadedRegionInner(const FBox& Box, const FCellCoord& CellCoord, TFunctionRef<void(const FCellCoord&)> InOperation);

	FBox GetActorBounds(const FWorldPartitionHandle& InActorHandle) const;
	bool IsActorAlwaysLoaded(const FWorldPartitionHandle& InActorHandle) const;
	int32 ForEachIntersectingCellInner(const FBox& Box, const FCellCoord& CellCoord, TFunctionRef<void(UWorldPartitionEditorCell*)> InOperation);

	UPROPERTY(Config)
	int32 CellSize;

	TMap<FCellCoord, FCellNode> HashNodes;
	TMap<FCellCoord, UWorldPartitionEditorCell*> HashCells;

	UPROPERTY(Transient)
	TSet<TObjectPtr<UWorldPartitionEditorCell>> Cells;
	
	FBox Bounds;
	bool bBoundsDirty;
	
	UPROPERTY(Transient)
	TObjectPtr<UWorldPartitionEditorCell> AlwaysLoadedCell;

public:
	UPROPERTY(Config, meta = (AllowedClasses = "Texture2D, MaterialInterface"))
	FSoftObjectPath WorldImage;

	UPROPERTY(Config)
	FVector2D WorldImageTopLeftW;

	UPROPERTY(Config)
	FVector2D WorldImageBottomRightW;
#endif
};
