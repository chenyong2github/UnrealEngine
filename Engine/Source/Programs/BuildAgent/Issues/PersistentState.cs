// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent.Issues
{
	/// <summary>
	/// State tracked by the program between runs.
	/// </summary>
	[DataContract]
	class PersistentState
	{
		/// <summary>
		/// All open issues.
		/// </summary>
		[DataMember]
		public List<Issue> Issues = new List<Issue>();

		/// <summary>
		/// Map of stream name to list of builds within it. Only retains a few builds in order to determine the last succesful CL for a build.
		/// </summary>
		[DataMember]
		public Dictionary<string, List<IssueBuild>> Streams = new Dictionary<string, List<IssueBuild>>();

		/// <summary>
		/// Adds a new build for tracking
		/// </summary>
		/// <param name="Stream">The stream containing the build</param>
		/// <param name="Build">The build to add</param>
		public void AddBuild(string Stream, IssueBuild Build)
		{
			// Get the list of tracked builds for this stream
			List<IssueBuild> Builds;
			if (!Streams.TryGetValue(Stream, out Builds))
			{
				Builds = new List<IssueBuild>();
				Streams.Add(Stream, Builds);
			}

			// Add this build to the tracked state data
			int BuildIdx = Builds.BinarySearch(Build);
			if (BuildIdx < 0)
			{
				BuildIdx = ~BuildIdx;
				Builds.Insert(BuildIdx, Build);
			}
		}

		/// <summary>
		/// Finds the last build before the one given which executes the same steps. Used to determine the last succesful build before a failure.
		/// </summary>
		/// <param name="Stream">The stream to search for</param>
		/// <param name="Change">The change to search for</param>
		/// <param name="StepNames">The step names which the job step needs to have</param>
		/// <returns>The last build known before the given changelist</returns>
		public IssueBuild FindBuildBefore(string Stream, int Change, string StepName)
		{
			IssueBuild Result = null;

			List<IssueBuild> Builds;
			if(Streams.TryGetValue(Stream, out Builds))
			{
				for(int Idx = 0; Idx < Builds.Count && Builds[Idx].Change < Change; Idx++)
				{
					if(Builds[Idx].JobStepName == StepName)
					{
						Result = Builds[Idx];
					}
				}
			}

			return Result;
		}

		/// <summary>
		/// Finds the first build after the one given which executes the same steps. Used to determine the last succesful build before a failure.
		/// </summary>
		/// <param name="Stream">The stream to search for</param>
		/// <param name="Change">The change to search for</param>
		/// <param name="StepName">Name of the job step to find</param>
		/// <returns>The last build known before the given changelist</returns>
		public IssueBuild FindBuildAfter(string Stream, int Change, string StepName)
		{
			IssueBuild Result = null;

			List<IssueBuild> Builds;
			if (Streams.TryGetValue(Stream, out Builds))
			{
				for (int Idx = Builds.Count - 1; Idx >= 0 && Builds[Idx].Change > Change; Idx--)
				{
					if (Builds[Idx].JobStepName == StepName)
					{
						Result = Builds[Idx];
					}
				}
			}

			return Result;
		}
	}
}
