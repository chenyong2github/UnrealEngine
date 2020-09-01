// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if defined(UE_USE_ALLOCATIONS_PROVIDER)

#include "AnalysisSession.h"
#include "CoreTypes.h"
#include "Templates/UniquePtr.h"

#include <new>

/* {{{1

1. *A*    Query(A, A, Crosses_Or)             Show all live at time A
   **A    Query(0, A, Crosses_None)           Show all freed before time A
   A**    Query(A, INF, Crosses_None)         Show all allocated after time A
2. A\B    Query(A, B, Crosses_A)              Live at time A, but not at time B
   B\A    Query(A, B, Crosses_B)              Live at time B, but not at time A
   A&B    Query(A, B, Crosses_And)            Live at time A and also at time B
   A^B    Query(A, A, Crosses_Xor)            Live either at time A or at time B
   A|B    Query(A, B, Crosses_Or)             All at time A and all at time B with diffs highlighted
   A<>B   Query(A, B, Crosses_None)           Allocated and freed during AB time interval
   <AB>   [same as A&B]                       Allocated before time A and freed after time B
   <A>B   [same as A\B]                       Allocated before time A and freed during AB time interval
   A<B>   [same as B\A]                       Allocated during AB time interval and freed after time B
3. A*BC*  Query(B, C, Crosses_And).Start > A  Allocated during AB time interval and still live after time C
   *AB*C  Query(A, B, Crosses_And).End < C    Allocated before A and freed during BC time interval

                     [-]
   (A)        (B)    < >   ~Or             /---- Xor&A
    |          |     A B   None Xor And Or A B-- Xor&B
    | [------] |     0 0   1    0   0   0  0 0
    |      [---+--]  0 1   0    1   0   1  0 1
 [--+---]      |     1 0   0    1   0   1  1 0
  [-+----------+-]   1 1   0    0   1   1  0 0
    |          |

}}} */

////////////////////////////////////////////////////////////////////////////////
class IAllocationsProvider
	: public Trace::IProvider
{
public:
	struct FAllocation
	{
		double		GetStartTime() const;
		double		GetEndTime() const;
		uint64		GetAddress() const;
		uint64		GetSize() const;
		uint64		GetBacktraceId() const;
		uint32		GetTag() const;
	};

	class FAllocations
	{
	public:
		void				operator delete (void* Address);
		uint32				Num() const;
		const FAllocation*	Get(uint32 Index) const;
	};

	typedef TUniquePtr<const FAllocations> QueryResult;

	enum class ECrosses
	{
		None,
		//A, /* ...off temporarily */
		//B, /* ...off temporarily */
		Xor,
		And,
		Or,
	};

	enum class EQueryStatus
	{
		Unknown,
		Done,
		Working,
		Available,
	};

	struct FQueryStatus
	{
		QueryResult			NextResult() const;
		EQueryStatus		Status;
		mutable UPTRINT		Handle;
	};

	typedef UPTRINT				QueryHandle;
	typedef const FQueryStatus	QueryStatus;
	virtual QueryHandle			StartQuery(double TimeA, double TimeB, ECrosses Crosses) = 0;
	virtual void				CancelQuery(QueryHandle Query) = 0;
	virtual QueryStatus			PollQuery(QueryHandle Query) = 0;
};

#endif // UE_USE_ALLOCATIONS_PROVIDER
