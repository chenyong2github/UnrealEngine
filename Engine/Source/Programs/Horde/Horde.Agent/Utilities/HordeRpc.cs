// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Google.Protobuf.Collections;
using Google.Protobuf.WellKnownTypes;
using HordeCommon;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;

namespace HordeCommon.Rpc
{
#pragma warning disable CS1591
	partial class Property
	{
		public Property(string Name, string Value)
		{
			this.Name = Name;
			this.Value = Value;
		}

		public Property(KeyValuePair<string, string> Pair)
		{
			this.Name = Pair.Key;
			this.Value = Pair.Value;
		}
	}

	partial class PropertyUpdate
	{
		public PropertyUpdate(string Name, string? Value)
		{
			this.Name = Name;
			this.Value = Value;
		}
	}

	static class PropertyExtensions
	{
		public static string GetValue(this RepeatedField<Property> Properties, string Name)
		{
			return Properties.First(x => x.Name == Name).Value;
		}

		public static bool TryGetValue(this RepeatedField<Property> Properties, string Name, [MaybeNullWhen(false)] out string Result)
		{
			Property? Property = Properties.FirstOrDefault(x => x.Name == Name);
			if (Property == null)
			{
				Result = null!;
				return false;
			}
			else
			{
				Result = Property.Value;
				return true;
			}
		}
	}

	partial class GetStreamRequest
	{
		public GetStreamRequest(string StreamId)
		{
			this.StreamId = StreamId;
		}
	}

	partial class UpdateStreamRequest
	{
		public UpdateStreamRequest(string StreamId, Dictionary<string, string?> Properties)
		{
			this.StreamId = StreamId;
			this.Properties.AddRange(Properties.Select(x => new PropertyUpdate(x.Key, x.Value)));
		}
	}

	partial class GetJobRequest
	{
		public GetJobRequest(string JobId)
		{
			this.JobId = JobId;
		}
	}

	partial class BeginBatchRequest
	{
		public BeginBatchRequest(string JobId, string BatchId, string LeaseId)
		{
			this.JobId = JobId;
			this.BatchId = BatchId;
			this.LeaseId = LeaseId;
		}
	}

	partial class FinishBatchRequest
	{
		public FinishBatchRequest(string JobId, string BatchId, string LeaseId)
		{
			this.JobId = JobId;
			this.BatchId = BatchId;
			this.LeaseId = LeaseId;
		}
	}

	partial class BeginStepRequest
	{
		public BeginStepRequest(string JobId, string BatchId, string LeaseId)
		{
			this.JobId = JobId;
			this.BatchId = BatchId;
			this.LeaseId = LeaseId;
		}
	}

	partial class UpdateStepRequest
	{
		public UpdateStepRequest(string JobId, string BatchId, string StepId, JobStepState State, JobStepOutcome Outcome)
		{
			this.JobId = JobId;
			this.BatchId = BatchId;
			this.StepId = StepId;
			this.State = State;
			this.Outcome = Outcome;
		}
	}
	
	partial class GetStepRequest
	{
		public GetStepRequest(string JobId, string BatchId, string StepId)
		{
			this.JobId = JobId;
			this.BatchId = BatchId;
			this.StepId = StepId;
		}
	}
	
	partial class GetStepResponse
	{
		public GetStepResponse(JobStepOutcome Outcome, JobStepState State, bool AbortRequested)
		{
			this.Outcome = Outcome;
			this.State = State;
			this.AbortRequested = AbortRequested;
		}
	}

	partial class CreateEventRequest
	{
		public CreateEventRequest(EventSeverity Severity, string LogId, int LineIndex, int LineCount)
		{
			this.Severity = Severity;
			this.LogId = LogId;
			this.LineIndex = LineIndex;
			this.LineCount = LineCount;
		}
	}

	partial class CreateEventsRequest
	{
		public CreateEventsRequest(IEnumerable<CreateEventRequest> Events)
		{
			this.Events.AddRange(Events);
		}
	}

	partial class WriteOutputRequest
	{
		public WriteOutputRequest(string LogId, long Offset, int LineIndex, byte[] Data, bool Flush)
		{
			this.LogId = LogId;
			this.Offset = Offset;
			this.LineIndex = LineIndex;
			this.Data = ByteString.CopyFrom(Data);
			this.Flush = Flush;
		}
	}

	partial class DownloadSoftwareRequest
	{
		public DownloadSoftwareRequest(string Version)
		{
			this.Version = Version;
		}
	}
#pragma warning restore CS1591
}
