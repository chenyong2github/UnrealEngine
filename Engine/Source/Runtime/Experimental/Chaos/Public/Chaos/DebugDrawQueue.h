// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/ChaosDebugDrawDeclares.h"
#include "Containers/List.h"
#include "Math/Vector.h"
#include "Math/Color.h"
#include "Math/Quat.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/IConsoleManager.h"

class AActor;

#if CHAOS_DEBUG_DRAW
namespace Chaos
{
/** Thread-safe single-linked list (lock-free). (Taken from Light Mass, should probably just move into core) */
template<typename ElementType>
class TListThreadSafe
{
public:

	/** Initialization constructor. */
	TListThreadSafe() :
		FirstElement(nullptr)
	{}

	/**
	* Adds an element to the list.
	* @param Element	Newly allocated and initialized list element to add.
	*/
	void AddElement(TList<ElementType>* Element)
	{
		// Link the element at the beginning of the list.
		TList<ElementType>* LocalFirstElement;
		do
		{
			LocalFirstElement = FirstElement;
			Element->Next = LocalFirstElement;
		} while (FPlatformAtomics::InterlockedCompareExchangePointer((void**)&FirstElement, Element, LocalFirstElement) != LocalFirstElement);
	}

	/**
	* Clears the list and returns the elements.
	* @return	List of all current elements. The original list is cleared. Make sure to delete all elements when you're done with them!
	*/
	TList<ElementType>* ExtractAll()
	{
		// Atomically read the complete list and clear the shared head pointer.
		TList<ElementType>* LocalFirstElement;
		do
		{
			LocalFirstElement = FirstElement;
		} while (FPlatformAtomics::InterlockedCompareExchangePointer((void**)&FirstElement, NULL, LocalFirstElement) != LocalFirstElement);
		return LocalFirstElement;
	}

	/**
	*	Clears the list.
	*/
	void Clear()
	{
		while (FirstElement)
		{
			// Atomically read the complete list and clear the shared head pointer.
			TList<ElementType>* Element = ExtractAll();

			// Delete all elements in the local list.
			while (Element)
			{
				TList<ElementType>* NextElement = Element->Next;
				delete Element;
				Element = NextElement;
			};
		};
	}

private:

	TList<ElementType>* FirstElement;
};

struct CHAOS_API FLatentDrawCommand
{
	FVector LineStart;
	FVector LineEnd;
	FColor Color;
	int32 Segments;
	bool bPersistentLines;
	float ArrowSize;
	float LifeTime;
	uint8 DepthPriority;
	float Thickness;
	float Radius;
	float HalfHeight;
	FVector Center;
	FVector Extent;
	FQuat Rotation;
	FVector TextLocation;
	FString Text;
	class AActor* TestBaseActor;
	bool bDrawShadow;
	float FontScale;
	float Duration;
	FMatrix TransformMatrix;
	bool bDrawAxis;
	FVector YAxis;
	FVector ZAxis;

	enum class EDrawType
	{
		Point,
		Line,
		DirectionalArrow,
		Sphere,
		Box,
		String,
		Circle,
		Capsule,
	} Type;

	FLatentDrawCommand()
		: TestBaseActor(nullptr)
	{
	}

	static FLatentDrawCommand DrawPoint(const FVector& Position, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		FLatentDrawCommand Command;
		Command.LineStart = Position;
		Command.Color = Color;
		Command.bPersistentLines = bPersistentLines;
		Command.LifeTime = LifeTime;
		Command.DepthPriority = DepthPriority;
		Command.Thickness = Thickness;
		Command.Type = EDrawType::Point;
		return Command;
	}


	static FLatentDrawCommand DrawLine(const FVector& LineStart, const FVector& LineEnd, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		FLatentDrawCommand Command;
		Command.LineStart = LineStart;
		Command.LineEnd = LineEnd;
		Command.Color = Color;
		Command.bPersistentLines = bPersistentLines;
		Command.LifeTime = LifeTime;
		Command.DepthPriority = DepthPriority;
		Command.Thickness = Thickness;
		Command.Type = EDrawType::Line;
		return Command;
	}

	static FLatentDrawCommand DrawDirectionalArrow(const FVector& LineStart, FVector const& LineEnd, float ArrowSize, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		FLatentDrawCommand Command;
		Command.LineStart = LineStart;
		Command.LineEnd = LineEnd;
		Command.ArrowSize = ArrowSize;
		Command.Color = Color;
		Command.bPersistentLines = bPersistentLines;
		Command.LifeTime = LifeTime;
		Command.DepthPriority = DepthPriority;
		Command.Thickness = Thickness;
		Command.Type = EDrawType::DirectionalArrow;
		return Command;
	}

	static FLatentDrawCommand DrawDebugSphere(const FVector& Center, float Radius, int32 Segments, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		FLatentDrawCommand Command;
		Command.LineStart = Center;
		Command.Radius = Radius;
		Command.Color = Color;
		Command.Segments = Segments;
		Command.bPersistentLines = bPersistentLines;
		Command.LifeTime = LifeTime;
		Command.DepthPriority = DepthPriority;
		Command.Thickness = Thickness;
		Command.Type = EDrawType::Sphere;
		return Command;
	}

	static FLatentDrawCommand DrawDebugBox(const FVector& Center, const FVector& Extent, const FQuat& Rotation, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		FLatentDrawCommand Command;
		Command.Center = Center;
		Command.Extent = Extent;
		Command.Rotation = Rotation;
		Command.Color = Color;
		Command.bPersistentLines = bPersistentLines;
		Command.LifeTime = LifeTime;
		Command.DepthPriority = DepthPriority;
		Command.Thickness = Thickness;
		Command.Type = EDrawType::Box;
		return Command;
	}

	static FLatentDrawCommand DrawDebugString(const FVector& TextLocation, const FString& Text, class AActor* TestBaseActor, const FColor& Color, float Duration, bool bDrawShadow, float FontScale)
	{
		FLatentDrawCommand Command;
		Command.TextLocation = TextLocation;
		Command.Text = Text;
		Command.TestBaseActor = TestBaseActor;
		Command.Color = Color;
		Command.Duration = Duration;
		Command.LifeTime = Duration;
		Command.bDrawShadow = bDrawShadow;
		Command.FontScale = FontScale;
		Command.Type = EDrawType::String;
		return Command;
	}

	static FLatentDrawCommand DrawDebugCircle(const FVector& Center, float Radius, int32 Segments, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness, const FVector& YAxis, const FVector& ZAxis, bool bDrawAxis)
	{
		FLatentDrawCommand Command;
		Command.Center = Center;
		Command.Radius = Radius;
		Command.Segments = Segments;
		Command.Color = Color;
		Command.bPersistentLines = bPersistentLines;
		Command.LifeTime = LifeTime;
		Command.DepthPriority = DepthPriority;
		Command.Thickness = Thickness;
		Command.YAxis = YAxis;
		Command.ZAxis = ZAxis;
		Command.bDrawAxis = bDrawAxis;
		Command.Type = EDrawType::Circle;
		return Command;
	}

	static FLatentDrawCommand DrawDebugCapsule(const FVector& Center, float HalfHeight, float Radius, const FQuat& Rotation, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		FLatentDrawCommand Command;
		Command.Center = Center;
		Command.HalfHeight = HalfHeight;
		Command.Radius = Radius;
		Command.Rotation = Rotation;
		Command.Color = Color;
		Command.bPersistentLines = bPersistentLines;
		Command.LifeTime = LifeTime;
		Command.DepthPriority = DepthPriority;
		Command.Thickness = Thickness;
		Command.Type = EDrawType::Capsule;
		return Command;
	}

};

/** A thread safe way to generate latent debug drawing. (This is picked up later by the geometry collection component which is a total hack for now, but needed to get into an engine world ) */
class CHAOS_API FDebugDrawQueue
{
public:
	enum EBuffer
	{
		Internal = 0,
		External = 1
	};

	void ExtractAllElements(TArray<FLatentDrawCommand>& OutDrawCommands, bool bClear = true)
	{
		// We do not always want to flip and clear the buffers (e.g., when paused)
		if (bClear)
		{
			Flush();
		}

		//make a copy so that alloc/free is all taken care of by debug draw code (avoid dll crossing, and other badness). Also ensures order is the same as it was originally pushed in
		TList<FLatentDrawCommand>* List = Queue[!DoubleBuffer].ExtractAll();
		while(List)
		{
			OutDrawCommands.Insert(List->Element, 0);
			TList<FLatentDrawCommand>* Prev = List;
			List = List->Next;
			//if (bClear)
			//{
			//	delete Prev;
			//}
		}
	}

	void DrawDebugPoint(const FVector& Position, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		if (!IsDebugDrawingEnabled()) { return; }
		Queue[DoubleBuffer].AddElement(new TList<FLatentDrawCommand>(FLatentDrawCommand::DrawPoint(Position, Color, bPersistentLines, LifeTime, DepthPriority, Thickness)));
	}

	void DrawDebugLine(const FVector& LineStart, const FVector& LineEnd, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f)
	{
		if (!IsDebugDrawingEnabled()) { return; }
		Queue[DoubleBuffer].AddElement(new TList<FLatentDrawCommand>(FLatentDrawCommand::DrawLine(LineStart, LineEnd, Color, bPersistentLines, LifeTime, DepthPriority, Thickness)));
	}

	void DrawDebugDirectionalArrow(const FVector& LineStart, const FVector& LineEnd, float ArrowSize, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f)
	{
		if (!IsDebugDrawingEnabled()) { return; }
		Queue[DoubleBuffer].AddElement(new TList<FLatentDrawCommand>(FLatentDrawCommand::DrawDirectionalArrow(LineStart, LineEnd, ArrowSize, Color, bPersistentLines, LifeTime, DepthPriority, Thickness)));
	}

	void DrawDebugSphere(FVector const& Center, float Radius, int32 Segments, const FColor& Color, bool bPersistentLines = false, float LifeTime = -1.f, uint8 DepthPriority = 0, float Thickness = 0.f)
	{
		if (!IsDebugDrawingEnabled()) { return; }
		Queue[DoubleBuffer].AddElement(new TList<FLatentDrawCommand>(FLatentDrawCommand::DrawDebugSphere(Center, Radius, Segments, Color, bPersistentLines, LifeTime, DepthPriority, Thickness)));
	}

	void DrawDebugBox(FVector const& Center, FVector const& Extent, const FQuat& Rotation, FColor const& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		if (IsDebugDrawingEnabled())
		{
			Queue[DoubleBuffer].AddElement(new TList<FLatentDrawCommand>(FLatentDrawCommand::DrawDebugBox(Center, Extent, Rotation, Color, bPersistentLines, LifeTime, DepthPriority, Thickness)));
		}
	}

	void DrawDebugString(FVector const& TextLocation, const FString& Text, class AActor* TestBaseActor, FColor const& Color, float Duration, bool bDrawShadow, float FontScale)
	{
		if (IsDebugDrawingEnabled())
		{
			Queue[DoubleBuffer].AddElement(new TList<FLatentDrawCommand>(FLatentDrawCommand::DrawDebugString(TextLocation, Text, TestBaseActor, Color, Duration, bDrawShadow, FontScale)));
		}
	}

	void DrawDebugCircle(const FVector& Center, float Radius, int32 Segments, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness, const FVector& YAxis, const FVector& ZAxis, bool bDrawAxis)
	{
		if (IsDebugDrawingEnabled())
		{
			Queue[DoubleBuffer].AddElement(new TList<FLatentDrawCommand>(FLatentDrawCommand::DrawDebugCircle(Center, Radius, Segments, Color, bPersistentLines, LifeTime, DepthPriority, Thickness, YAxis, ZAxis, bDrawAxis)));
		}
	}

	void DrawDebugCapsule(const FVector& Center, float HalfHeight, float Radius, const FQuat& Rotation, const FColor& Color, bool bPersistentLines, float LifeTime, uint8 DepthPriority, float Thickness)
	{
		if (IsDebugDrawingEnabled())
		{
			Queue[DoubleBuffer].AddElement(new TList<FLatentDrawCommand>(FLatentDrawCommand::DrawDebugCapsule(Center, HalfHeight, Radius, Rotation, Color, bPersistentLines, LifeTime, DepthPriority, Thickness)));
		}
	}

	void SetConsumerActive(void* Consumer, bool bConsumerActive);

	static FDebugDrawQueue& GetInstance()
	{
		static FDebugDrawQueue Singleton;
		return Singleton;
	}

	static bool IsDebugDrawingEnabled()
	{
		// Don't queue up debug draws unless something is consuming the queue, otherwise it can build up forever
		return !!EnableDebugDrawing && (NumConsumers > 0);
	}

	static FAutoConsoleVariableRef MakeCVarRef();

private:
	friend class FAutoConsoleVariableRef;

	FDebugDrawQueue() {}
	~FDebugDrawQueue() {}

	void Flush()
	{
		DoubleBuffer = !DoubleBuffer;
		TList<FLatentDrawCommand>* List = Queue[DoubleBuffer].ExtractAll();
		while (List)
		{
			TList<FLatentDrawCommand>* Prev = List;
			List = List->Next;
			delete Prev;
		}
	}

	TListThreadSafe<FLatentDrawCommand> Queue[2];
	FThreadSafeBool DoubleBuffer;

	TArray<void*> Consumers;
	FCriticalSection ConsumersCS;

	static int32 EnableDebugDrawing;
	static int32 NumConsumers;
};
}
#endif