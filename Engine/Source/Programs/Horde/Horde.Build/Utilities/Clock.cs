// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace HordeCommon
{
	/// <summary>
	/// Interface representing time to make it pluggable during testing
	/// In normal use, the Clock implementation below is used. 
	/// </summary>
	public interface IClock
	{
		/// <summary>
		/// Return time expressed as the Coordinated Universal Time (UTC)
		/// </summary>
		/// <returns></returns>
		DateTime UtcNow { get; }
	}
	
	/// <summary>
	/// Implementation of <see cref="IClock"/> which returns the current time
	/// </summary>
	public class Clock : IClock
	{
		/// <inheritdoc/>
		public DateTime UtcNow => DateTime.UtcNow;
	}
	
	/// <summary>
	/// Fake clock that doesn't advance by wall block time
	/// Requires manual ticking to progress. Used in tests.
	/// </summary>
	public class FakeClock : IClock
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public FakeClock()
		{
			UtcNow = DateTime.UtcNow;
		}

		/// <summary>
		/// Advance time by given amount
		/// Useful for letting time progress during tests
		/// </summary>
		/// <param name="Period">Time span to advance</param>
		public void Advance(TimeSpan Period)
		{
			UtcNow = UtcNow.Add(Period);
		}

		/// <inheritdoc/>
		public DateTime UtcNow { get; set; }
	}
}