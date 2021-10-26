// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	using JobId = ObjectId<IJob>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Specifies a pattern of times that this schedule should run. Each schedule may have multiple patterns.
	/// </summary>
	public class SchedulePattern
	{
		/// <summary>
		/// Which days of the week the schedule should run
		/// </summary>
		public List<DayOfWeek>? DaysOfWeek { get; set; }

		/// <summary>
		/// Time during the day for the first schedule to trigger. Measured in minutes from midnight.
		/// </summary>
		public int MinTime { get; set; }

		/// <summary>
		/// Time during the day for the last schedule to trigger. Measured in minutes from midnight.
		/// </summary>
		public int? MaxTime { get; set; }

		/// <summary>
		/// Interval between each schedule triggering
		/// </summary>
		public int? Interval { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private SchedulePattern()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DaysOfWeek">Which days of the week the schedule should run</param>
		/// <param name="MinTime">Time during the day for the first schedule to trigger. Measured in minutes from midnight.</param>
		/// <param name="MaxTime">Time during the day for the last schedule to trigger. Measured in minutes from midnight.</param>
		/// <param name="Interval">Interval between each schedule triggering</param>
		public SchedulePattern(List<DayOfWeek>? DaysOfWeek, int MinTime, int? MaxTime, int? Interval)
		{
			this.DaysOfWeek = DaysOfWeek;
			this.MinTime = MinTime;
			this.MaxTime = MaxTime;
			this.Interval = Interval;
		}

		/// <summary>
		/// Calculates the trigger index based on the given time in minutes
		/// </summary>
		/// <param name="LastTime">Time in minutes for the last trigger</param>
		/// <param name="TimeZone">The timezone for running the schedule</param>
		/// <returns>Index of the trigger</returns>
		public DateTimeOffset GetNextTriggerTime(DateTimeOffset LastTime, TimeZoneInfo TimeZone)
		{
			// Convert last time into the correct timezone for running the scheule
			LastTime = TimeZoneInfo.ConvertTime(LastTime, TimeZone);

			// Get the base time (ie. the start of this day) for anchoring the schedule
			DateTimeOffset BaseTime = new DateTimeOffset(LastTime.Year, LastTime.Month, LastTime.Day, 0, 0, 0, LastTime.Offset);
			for (; ; )
			{
				if (DaysOfWeek == null || DaysOfWeek.Contains(BaseTime.DayOfWeek))
				{
					// Get the last time in minutes from the start of this day
					int LastTimeMinutes = (int)(LastTime - BaseTime).TotalMinutes;

					// Get the time of the first trigger of this day. If the last time is less than this, this is the next trigger.
					if (LastTimeMinutes < MinTime)
					{
						return BaseTime.AddMinutes(MinTime);
					}

					// Otherwise, get the time for the last trigger in the day.
					if (Interval.HasValue && Interval.Value > 0)
					{
						int ActualMaxTime = MaxTime ?? ((24 * 60) - 1);
						if (LastTimeMinutes < ActualMaxTime)
						{
							int LastIndex = (LastTimeMinutes - MinTime) / Interval.Value;
							int NextIndex = LastIndex + 1;

							int NextTimeMinutes = MinTime + (NextIndex * Interval.Value);
							if (NextTimeMinutes <= ActualMaxTime)
							{
								return BaseTime.AddMinutes(NextTimeMinutes);
							}
						}
					}
				}
				BaseTime = BaseTime.AddDays(1.0);
			}
		}
	}

	/// <summary>
	/// Gate allowing a schedule to trigger.
	/// </summary>
	public class ScheduleGate
	{
		/// <summary>
		/// The template containing the dependency
		/// </summary>
		public TemplateRefId TemplateRefId { get; set; }

		/// <summary>
		/// Target to wait for
		/// </summary>
		public string Target { get; set; }

		/// <summary>
		/// Private constructor for serializtaion
		/// </summary>
		[BsonConstructor]
		private ScheduleGate()
		{
			Target = String.Empty;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="TemplateRefId">The template containing the dependency</param>
		/// <param name="Target">Target to wait for</param>
		public ScheduleGate(TemplateRefId TemplateRefId, string Target)
		{
			this.TemplateRefId = TemplateRefId;
			this.Target = Target;
		}
	}

	/// <summary>
	/// Represents a schedule
	/// </summary>
	public class Schedule
	{
		/// <summary>
		/// Whether this schedule is currently enabled
		/// </summary>
		public bool Enabled { get; set; }

		/// <summary>
		/// Maximum number of builds triggered by this schedule active at once. Set to zero for unlimited.
		/// </summary>
		public int MaxActive { get; set; }

		/// <summary>
		/// Maximum number of changes the schedule can fall behind head revision. If greater than zero, builds will be triggered for every submitted changelist until the backlog is this size.
		/// </summary>
		public int MaxChanges { get; set; }

		/// <summary>
		/// Whether the build requires a change to be submitted
		/// </summary>
		[BsonIgnoreIfDefault, BsonDefaultValue(true)]
		public bool RequireSubmittedChange { get; set; } = true;

		/// <summary>
		/// Reference to another job/target which much succeed for this schedule to trigger
		/// </summary>
		public ScheduleGate? Gate { get; set; }

		/// <summary>
		/// Whether this build requires a code change to trigger
		/// </summary>
		public List<ChangeContentFlags>? Filter { get; set; }

		/// <summary>
		/// File paths which should trigger this schedule
		/// </summary>
		public List<string>? Files { get; set; }

		/// <summary>
		/// Parameters for the template
		/// </summary>
		public Dictionary<string, string> TemplateParameters { get; set; }

		/// <summary>
		/// List of patterns for this schedule
		/// </summary>
		public List<SchedulePattern> Patterns { get; set; }

		/// <summary>
		/// Last changelist number that this was triggered for
		/// </summary>
		public int LastTriggerChange { get; set; }

		/// <summary>
		/// Last time that the schedule was triggered
		/// </summary>
		public DateTimeOffset LastTriggerTime { get; set; }

		/// <summary>
		/// List of jobs that are currently active
		/// </summary>
		public List<JobId> ActiveJobs { get; set; } = new List<JobId>();

		/// <summary>
		/// Private constructor for serializtaion
		/// </summary>
		[BsonConstructor]
		private Schedule()
		{
			TemplateParameters = new Dictionary<string, string>();
			Patterns = new List<SchedulePattern>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Enabled">Whether the schedule is currently enabled</param>
		/// <param name="MaxActive">Maximum number of builds that may be active at once</param>
		/// <param name="MaxChanges">Maximum number of changes the schedule can fall behind head revision</param>
		/// <param name="RequireSubmittedChange">Whether a change has to be submitted for the schedule to trigger</param>
		/// <param name="Gate">Reference to another job/target which much succeed for this schedule to trigger</param>
		/// <param name="Filter">Filter for changes to consider</param>
		/// <param name="Files">Files that should trigger the schedule</param>
		/// <param name="TemplateParameters">Parameters for the template to run</param>
		/// <param name="Patterns">List of patterns for the schedule</param>
		public Schedule(bool Enabled = true, int MaxActive = 0, int MaxChanges = 0, bool RequireSubmittedChange = true, ScheduleGate? Gate = null, List<ChangeContentFlags>? Filter = null, List<string>? Files = null, Dictionary<string, string>? TemplateParameters = null, List<SchedulePattern>? Patterns = null)
		{
			this.Enabled = Enabled;
			this.MaxActive = MaxActive;
			this.MaxChanges = MaxChanges;
			this.RequireSubmittedChange = RequireSubmittedChange;
			this.Gate = Gate;
			this.Filter = Filter;
			this.Files = Files;
			this.TemplateParameters = TemplateParameters ?? new Dictionary<string, string>();
			this.Patterns = Patterns ?? new List<SchedulePattern>();
			this.LastTriggerTime = DateTimeOffset.Now;
		}

		/// <summary>
		/// Copies the state fields from another schedule object
		/// </summary>
		/// <param name="Other">The schedule object to copy from</param>
		public void CopyState(Schedule Other)
		{
			LastTriggerChange = Other.LastTriggerChange;
			LastTriggerTime = Other.LastTriggerTime;
			ActiveJobs.AddRange(Other.ActiveJobs);
		}

		/// <summary>
		/// Gets the flags to filter changes by
		/// </summary>
		/// <returns>Set of filter flags</returns>
		public ChangeContentFlags? GetFilterFlags()
		{
			if(Filter == null)
			{
				return null;
			}

			ChangeContentFlags Flags = 0;
			foreach (ChangeContentFlags Flag in Filter)
			{
				Flags |= Flag;
			}
			return Flags;
		}

		/// <summary>
		/// Get the next time that the schedule will trigger
		/// </summary>
		/// <param name="TimeZone">Timezone to evaluate the trigger</param>
		/// <returns>Next time at which the schedule will trigger</returns>
		public DateTimeOffset? GetNextTriggerTime(TimeZoneInfo TimeZone)
		{
			return GetNextTriggerTime(LastTriggerTime, TimeZone);
		}

		/// <summary>
		/// Get the next time that the schedule will trigger
		/// </summary>
		/// <param name="LastTime">Last time at which the schedule triggered</param>
		/// <param name="TimeZone">Timezone to evaluate the trigger</param>
		/// <returns>Next time at which the schedule will trigger</returns>
		public DateTimeOffset? GetNextTriggerTime(DateTimeOffset LastTime, TimeZoneInfo TimeZone)
		{
			DateTimeOffset? NextTriggerTime = null;
			if (Enabled)
			{
				foreach (SchedulePattern Pattern in Patterns)
				{
					DateTimeOffset PatternTriggerTime = Pattern.GetNextTriggerTime(LastTime, TimeZone);
					if (NextTriggerTime == null || NextTriggerTime < PatternTriggerTime)
					{
						NextTriggerTime = PatternTriggerTime;
					}
				}
			}
			return NextTriggerTime;
		}
	}
}
