// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using StatsdClient;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Empty implementation that does nothing but still satisfies the interface
	/// Used when DogStatsD is not available.
	/// </summary>
	public sealed class NoOpDogStatsd : IDogStatsd
	{
		/// <inheritdoc />
		public ITelemetryCounters TelemetryCounters { get; } = null!;
		
		/// <inheritdoc />
		public void Configure(StatsdConfig Config)
		{
		}

		/// <inheritdoc />
		public void Counter(string StatName, double Value, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public void Decrement(string StatName, int Value = 1, double SampleRate = 1, params string[] Tags)
		{
		}

		/// <inheritdoc />
		public void Event(string Title, string Text, string AlertType = null!, string AggregationKey = null!, string SourceType = null!,
			int? DateHappened = null, string Priority = null!, string Hostname = null!, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public void Gauge(string StatName, double Value, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public void Histogram(string StatName, double Value, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public void Distribution(string StatName, double Value, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public void Increment(string StatName, int Value = 1, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public void Set<T>(string StatName, T Value, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public void Set(string StatName, string Value, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public IDisposable StartTimer(string Name, double SampleRate = 1, string[] Tags = null!)
		{
			// Some random object to satisfy IDisposable
			return new MemoryStream();
		}

		/// <inheritdoc />
		public void Time(Action Action, string StatName, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public T Time<T>(Func<T> Func, string StatName, double SampleRate = 1, string[] Tags = null!)
		{
			return Func();
		}

		/// <inheritdoc />
		public void Timer(string StatName, double Value, double SampleRate = 1, string[] Tags = null!)
		{
		}

		/// <inheritdoc />
		public void ServiceCheck(string Name, Status Status, int? Timestamp = null, string Hostname = null!, string[] Tags = null!, string Message = null!)
		{
		}
		
		/// <inheritdoc />
		[SuppressMessage("Design", "CA1063:Implement IDisposable correctly")]
		[SuppressMessage("Usage", "CA1816:Call GC.SuppressFinalize correctly")]
		public void Dispose()
		{
		}
	}
}