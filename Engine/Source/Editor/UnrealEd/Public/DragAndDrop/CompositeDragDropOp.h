// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

class FCompositeDragDropOp : public FDecoratedDragDropOp
{
public:
	static const FString& GetTypeId() { static FString Type = TEXT("FCompositeDragDropOp"); return Type; }
	virtual bool IsOfTypeImpl(const FString& Type) const override 
	{
		return GetTypeId() == Type || FDecoratedDragDropOp::IsOfTypeImpl(Type) || GetSubOpPtr(Type) != nullptr; 
	}

	FCompositeDragDropOp() : FDecoratedDragDropOp() {}

	void AddSubOp(const TSharedPtr<FDragDropOperation>& SubOp)
	{
		check(!SubOp->IsOfType<FCompositeDragDropOp>());
		SubOps.Add(SubOp);
	}

	template <typename T>
	TSharedPtr<T> GetSubOp()
	{
		for (auto& SubOp : SubOps)
		{
			if (SubOp->IsOfType<T>())
			{
				return StaticCastSharedPtr<T>(SubOp);
			}
		}
		return nullptr;
	}

	template <typename T>
	const TSharedPtr<const T> GetSubOp() const
	{
		for (const auto& SubOp : SubOps)
		{
			if (SubOp->IsOfType<T>())
			{
				return StaticCastSharedPtr<const T>(SubOp);
			}
		}
		return nullptr;
	}

	virtual TSharedPtr<FDragDropOperation> ConvertTo(const FString& TypeId) override
	{
		// Attempt to convert this before getting subops
		if (FDecoratedDragDropOp::IsOfTypeImpl(TypeId))
		{
			return AsShared();
		}
		else
		{
			// Will be nullptr if failed to convert
			TSharedPtr<FDragDropOperation> SubOpAs = GetSubOpPtr(TypeId);
			return SubOpAs;
		}
	}

	virtual void ResetToDefaultToolTip() override
	{
		FDecoratedDragDropOp::ResetToDefaultToolTip();

		for (const auto& SubOp : SubOps)
		{
			if (SubOp->IsOfType<FDecoratedDragDropOp>())
			{
				auto DecoratedSubOp = SubOp->CastTo<FDecoratedDragDropOp>();
				if (DecoratedSubOp.IsValid())
				{
					DecoratedSubOp->ResetToDefaultToolTip();
				}
			}
		}
	}
private:
	TSharedPtr<FDragDropOperation> GetSubOpPtr(const FString& TypeId) const
	{
		for (const auto& SubOp : SubOps)
		{
			if (SubOp->IsOfTypeImpl(TypeId))
			{
				return SubOp;
			}
		}
		return nullptr;
	}
protected:
	TArray<TSharedPtr<FDragDropOperation>> SubOps;
};