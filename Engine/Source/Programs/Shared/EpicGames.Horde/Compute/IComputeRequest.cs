// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Interface for a compute request
	/// </summary>
	public interface IComputeRequest<TResult>
	{
		/// <summary>
		/// Task to await completion of this request
		/// </summary>
		Task<TResult> Result { get; }

		/// <summary>
		/// Cancel the current request
		/// </summary>
		void Cancel();
	}
}
