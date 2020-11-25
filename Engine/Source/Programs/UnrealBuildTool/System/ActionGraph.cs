// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Diagnostics;
using System.Linq;
using System.Runtime.Serialization;
using Tools.DotNETCommon;
using System.Text;
using System.Threading.Tasks;

namespace UnrealBuildTool
{
	static class ActionGraph
	{
		/// <summary>
		/// Links the actions together and sets up their dependencies
		/// </summary>
		/// <param name="Actions">List of actions in the graph</param>
		public static void Link(List<LinkedAction> Actions)
		{
			// Build a map from item to its producing action
			Dictionary<FileItem, LinkedAction> ItemToProducingAction = new Dictionary<FileItem, LinkedAction>();
			foreach (LinkedAction Action in Actions)
			{
				foreach (FileItem ProducedItem in Action.ProducedItems)
				{
					ItemToProducingAction[ProducedItem] = Action;
				}
			}

			// Check for cycles
			DetectActionGraphCycles(Actions, ItemToProducingAction);

			// Use this map to add all the prerequisite actions
			foreach (LinkedAction Action in Actions)
			{
				Action.PrerequisiteActions = new HashSet<LinkedAction>();
				foreach(FileItem PrerequisiteItem in Action.PrerequisiteItems)
				{
					LinkedAction PrerequisiteAction;
					if(ItemToProducingAction.TryGetValue(PrerequisiteItem, out PrerequisiteAction))
					{
						Action.PrerequisiteActions.Add(PrerequisiteAction);
					}
				}
			}

			// Sort the action graph
			SortActionList(Actions);
		}

		/// <summary>
		/// Checks a set of actions for conflicts (ie. different actions producing the same output items)
		/// </summary>
		/// <param name="Actions">The set of actions to check</param>
		public static void CheckForConflicts(IEnumerable<IAction> Actions)
		{
			bool bResult = true;

			Dictionary<FileItem, IAction> ItemToProducingAction = new Dictionary<FileItem, IAction>();
			foreach(IAction Action in Actions)
			{
				foreach(FileItem ProducedItem in Action.ProducedItems)
				{
					IAction ExistingAction;
					if(ItemToProducingAction.TryGetValue(ProducedItem, out ExistingAction))
					{
						bResult &= CheckForConflicts(ExistingAction, Action);
					}
					else
					{
						ItemToProducingAction.Add(ProducedItem, Action);
					}
				}
			}

			if(!bResult)
			{
				throw new BuildException("Action graph is invalid; unable to continue. See log for additional details.");
			}
		}

		/// <summary>
		/// Finds conflicts betwee two actions, and prints them to the log
		/// </summary>
		/// <param name="A">The first action</param>
		/// <param name="B">The second action</param>
		/// <returns>True if any conflicts were found, false otherwise.</returns>
		public static bool CheckForConflicts(IAction A, IAction B)
		{
			bool bResult = true;
			if (A.ActionType != B.ActionType)
			{
				LogConflict(A, "action type is different", A.ActionType.ToString(), B.ActionType.ToString());
				bResult = false;
			}
			if (!Enumerable.SequenceEqual(A.PrerequisiteItems, B.PrerequisiteItems))
			{
				LogConflict(A, "prerequisites are different", String.Join(", ", A.PrerequisiteItems.Select(x => x.Location)), String.Join(", ", B.PrerequisiteItems.Select(x => x.Location)));
				bResult = false;
			}
			if (!Enumerable.SequenceEqual(A.DeleteItems, B.DeleteItems))
			{
				LogConflict(A, "deleted items are different", String.Join(", ", A.DeleteItems.Select(x => x.Location)), String.Join(", ", B.DeleteItems.Select(x => x.Location)));
				bResult = false;
			}
			if (A.DependencyListFile != B.DependencyListFile)
			{
				LogConflict(A, "dependency list is different", (A.DependencyListFile == null) ? "(none)" : A.DependencyListFile.AbsolutePath, (B.DependencyListFile == null) ? "(none)" : B.DependencyListFile.AbsolutePath);
				bResult = false;
			}
			if (A.WorkingDirectory != B.WorkingDirectory)
			{
				LogConflict(A, "working directory is different", A.WorkingDirectory.FullName, B.WorkingDirectory.FullName);
				bResult = false;
			}
			if (A.CommandPath != B.CommandPath)
			{
				LogConflict(A, "command path is different", A.CommandPath.FullName, B.CommandPath.FullName);
				bResult = false;
			}
			if (A.CommandArguments != B.CommandArguments)
			{
				LogConflict(A, "command arguments are different", A.CommandArguments, B.CommandArguments);
				bResult = false;
			}
			return bResult;
		}

		/// <summary>
		/// Adds the description of a merge error to an output message
		/// </summary>
		/// <param name="Action">The action with the conflict</param>
		/// <param name="Description">Description of the difference</param>
		/// <param name="OldValue">Previous value for the field</param>
		/// <param name="NewValue">Conflicting value for the field</param>
		static void LogConflict(IAction Action, string Description, string OldValue, string NewValue)
		{
			Log.TraceError("Unable to merge actions producing {0}: {1}", Action.ProducedItems.First().Location.GetFileName(), Description);
			Log.TraceLog("  Previous: {0}", OldValue);
			Log.TraceLog("  Conflict: {0}", NewValue);
		}

		/// <summary>
		/// Builds a list of actions that need to be executed to produce the specified output items.
		/// </summary>
		public static List<LinkedAction> GetActionsToExecute(List<LinkedAction> Actions, CppDependencyCache CppDependencies, ActionHistory History, bool bIgnoreOutdatedImportLibraries)
		{
			using (Timeline.ScopeEvent("ActionGraph.GetActionsToExecute()"))
			{
				// For all targets, build a set of all actions that are outdated.
				Dictionary<LinkedAction, bool> OutdatedActionDictionary = new Dictionary<LinkedAction, bool>();
				GatherAllOutdatedActions(Actions, History, OutdatedActionDictionary, CppDependencies, bIgnoreOutdatedImportLibraries);

				// Build a list of actions that are both needed for this target and outdated.
				return Actions.Where(Action => Action.CommandPath != null && OutdatedActionDictionary[Action]).ToList();
			}
		}

		/// <summary>
		/// Checks that there aren't any intermediate files longer than the max allowed path length
		/// </summary>
		/// <param name="BuildConfiguration">The build configuration</param>
		/// <param name="Actions">List of actions in the graph</param>
		public static void CheckPathLengths(BuildConfiguration BuildConfiguration, IEnumerable<IAction> Actions)
		{
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				const int MAX_PATH = 260;

				List<FileReference> FailPaths = new List<FileReference>();
				List<FileReference> WarnPaths = new List<FileReference>();
				foreach (IAction Action in Actions)
				{
					foreach (FileItem PrerequisiteItem in Action.PrerequisiteItems)
					{
						if (PrerequisiteItem.Location.FullName.Length >= MAX_PATH)
						{
							FailPaths.Add(PrerequisiteItem.Location);
						}
					}
					foreach (FileItem ProducedItem in Action.ProducedItems)
					{
						if (ProducedItem.Location.FullName.Length >= MAX_PATH)
						{
							FailPaths.Add(ProducedItem.Location);
						}
						if (ProducedItem.Location.FullName.Length > UnrealBuildTool.RootDirectory.FullName.Length + BuildConfiguration.MaxNestedPathLength && ProducedItem.Location.IsUnderDirectory(UnrealBuildTool.RootDirectory))
						{
							WarnPaths.Add(ProducedItem.Location);
						}
					}
				}

				if (FailPaths.Count > 0)
				{
					StringBuilder Message = new StringBuilder();
					Message.AppendFormat("The following output paths are longer than {0} characters. Please move the engine to a directory with a shorter path.", MAX_PATH);
					foreach (FileReference Path in FailPaths)
					{
						Message.AppendFormat("\n[{0} characters] {1}", Path.FullName.Length, Path);
					}
					throw new BuildException(Message.ToString());
				}

				if (WarnPaths.Count > 0)
				{
					StringBuilder Message = new StringBuilder();
					Message.AppendFormat("Detected paths more than {0} characters below UE root directory. This may cause portability issues due to the {1} character maximum path length on Windows:\n", BuildConfiguration.MaxNestedPathLength, MAX_PATH);
					foreach (FileReference Path in WarnPaths)
					{
						string RelativePath = Path.MakeRelativeTo(UnrealBuildTool.RootDirectory);
						Message.AppendFormat("\n[{0} characters] {1}", RelativePath.Length, RelativePath);
					}
					Message.AppendFormat("\n\nConsider setting {0} = ... in module *.Build.cs files to use alternative names for intermediate paths.", nameof(ModuleRules.ShortName));
					Log.TraceWarning(Message.ToString());
				}
			}
		}

		/// <summary>
		/// Executes a list of actions.
		/// </summary>
		public static void ExecuteActions(BuildConfiguration BuildConfiguration, List<LinkedAction> ActionsToExecute)
		{
			if(ActionsToExecute.Count == 0)
			{
				Log.TraceInformation("Target is up to date");
			}
			else
			{
				// Figure out which executor to use
				ActionExecutor Executor;
				if (BuildConfiguration.bAllowHybridExecutor && HybridExecutor.IsAvailable())
				{
					Executor = new HybridExecutor(BuildConfiguration.MaxParallelActions);
				}
				else if (BuildConfiguration.bAllowXGE && XGE.IsAvailable())
				{
					Executor = new XGE();
				}
				else if (BuildConfiguration.bAllowFASTBuild && FASTBuild.IsAvailable())
				{
					Executor = new FASTBuild();
				}
				else if(BuildConfiguration.bAllowSNDBS && SNDBS.IsAvailable())
				{
					Executor = new SNDBS();
				}
				else if(BuildConfiguration.bAllowParallelExecutor && ParallelExecutor.IsAvailable())
				{
					Executor = new ParallelExecutor(BuildConfiguration.MaxParallelActions);
				}
				else
				{
					Executor = new LocalExecutor(BuildConfiguration.MaxParallelActions);
				}

				// Execute the build
				Stopwatch Timer = Stopwatch.StartNew();
				if(!Executor.ExecuteActions(ActionsToExecute))
				{
					throw new CompilationResultException(CompilationResult.OtherCompilationError);
				}
				Log.TraceInformation("Total time in {0} executor: {1:0.00} seconds", Executor.Name, Timer.Elapsed.TotalSeconds);

				// Reset the file info for all the produced items
				foreach (LinkedAction BuildAction in ActionsToExecute)
				{
					foreach(FileItem ProducedItem in BuildAction.ProducedItems)
					{
						ProducedItem.ResetCachedInfo();
					}
				}

				// Verify the link outputs were created (seems to happen with Win64 compiles)
				foreach (LinkedAction BuildAction in ActionsToExecute)
				{
					if (BuildAction.ActionType == ActionType.Link)
					{
						foreach (FileItem Item in BuildAction.ProducedItems)
						{
							if(!Item.Exists)
							{
								throw new BuildException("Failed to produce item: {0}", Item.AbsolutePath);
							}
						}
					}
				}
			}
		}

		/// <summary>
		/// Sorts the action list for improved parallelism with local execution.
		/// </summary>
		static void SortActionList(List<LinkedAction> Actions)
		{
			// Clear the current dependent count
			foreach(LinkedAction Action in Actions)
			{
				Action.NumTotalDependentActions = 0;
			}

			// Increment all the dependencies
			foreach(LinkedAction Action in Actions)
			{
				Action.IncrementDependentCount(new HashSet<LinkedAction>());
			}

			// Sort actions by number of actions depending on them, descending. Secondary sort criteria is file size.
			Actions.Sort(LinkedAction.Compare);
		}

		/// <summary>
		/// Checks for cycles in the action graph.
		/// </summary>
		static void DetectActionGraphCycles(List<LinkedAction> Actions, Dictionary<FileItem, LinkedAction> ItemToProducingAction)
		{
			// Starting with actions that only depend on non-produced items, iteratively expand a set of actions that are only dependent on
			// non-cyclical actions.
			Dictionary<LinkedAction, bool> ActionIsNonCyclical = new Dictionary<LinkedAction, bool>();
			Dictionary<LinkedAction, List<LinkedAction>> CyclicActions = new Dictionary<LinkedAction, List<LinkedAction>>();
			while (true)
			{
				bool bFoundNewNonCyclicalAction = false;

				foreach (LinkedAction Action in Actions)
				{
					if (!ActionIsNonCyclical.ContainsKey(Action))
					{
						// Determine if the action depends on only actions that are already known to be non-cyclical.
						bool bActionOnlyDependsOnNonCyclicalActions = true;
						foreach (FileItem PrerequisiteItem in Action.PrerequisiteItems)
						{
							LinkedAction ProducingAction;
							if (ItemToProducingAction.TryGetValue(PrerequisiteItem, out ProducingAction))
							{
								if (!ActionIsNonCyclical.ContainsKey(ProducingAction))
								{
									bActionOnlyDependsOnNonCyclicalActions = false;
									if (!CyclicActions.ContainsKey(Action))
									{
										CyclicActions.Add(Action, new List<LinkedAction>());
									}

									List<LinkedAction> CyclicPrereq = CyclicActions[Action];
									if (!CyclicPrereq.Contains(ProducingAction))
									{
										CyclicPrereq.Add(ProducingAction);
									}
								}
							}
						}

						// If the action only depends on known non-cyclical actions, then add it to the set of known non-cyclical actions.
						if (bActionOnlyDependsOnNonCyclicalActions)
						{
							ActionIsNonCyclical.Add(Action, true);
							bFoundNewNonCyclicalAction = true;
							if (CyclicActions.ContainsKey(Action))
							{
								CyclicActions.Remove(Action);
							}
						}
					}
				}

				// If this iteration has visited all actions without finding a new non-cyclical action, then all non-cyclical actions have
				// been found.
				if (!bFoundNewNonCyclicalAction)
				{
					break;
				}
			}

			// If there are any cyclical actions, throw an exception.
			if (ActionIsNonCyclical.Count < Actions.Count)
			{
				// Find the index of each action
				Dictionary<LinkedAction, int> ActionToIndex = new Dictionary<LinkedAction, int>();
				for(int Idx = 0; Idx < Actions.Count; Idx++)
				{
					ActionToIndex[Actions[Idx]] = Idx;
				}

				// Describe the cyclical actions.
				string CycleDescription = "";
				foreach (LinkedAction Action in Actions)
				{
					if (!ActionIsNonCyclical.ContainsKey(Action))
					{
						CycleDescription += string.Format("Action #{0}: {1}\n", ActionToIndex[Action], Action.CommandPath);
						CycleDescription += string.Format("\twith arguments: {0}\n", Action.CommandArguments);
						foreach (FileItem PrerequisiteItem in Action.PrerequisiteItems)
						{
							CycleDescription += string.Format("\tdepends on: {0}\n", PrerequisiteItem.AbsolutePath);
						}
						foreach (FileItem ProducedItem in Action.ProducedItems)
						{
							CycleDescription += string.Format("\tproduces:   {0}\n", ProducedItem.AbsolutePath);
						}
						CycleDescription += string.Format("\tDepends on cyclic actions:\n");
						if (CyclicActions.ContainsKey(Action))
						{
							foreach (LinkedAction CyclicPrerequisiteAction in CyclicActions[Action])
							{
								if (CyclicActions.ContainsKey(CyclicPrerequisiteAction))
								{
									List<FileItem> CyclicProducedItems = CyclicPrerequisiteAction.ProducedItems.ToList();
									if (CyclicProducedItems.Count == 1)
									{
										CycleDescription += string.Format("\t\t{0} (produces: {1})\n", ActionToIndex[CyclicPrerequisiteAction], CyclicProducedItems[0].AbsolutePath);
									}
									else
									{
										CycleDescription += string.Format("\t\t{0}\n", ActionToIndex[CyclicPrerequisiteAction]);
										foreach (FileItem CyclicProducedItem in CyclicProducedItems)
										{
											CycleDescription += string.Format("\t\t\tproduces:   {0}\n", CyclicProducedItem.AbsolutePath);
										}
									}
								}
							}
							CycleDescription += "\n";
						}
						else
						{
							CycleDescription += string.Format("\t\tNone?? Coding error!\n");
						}
						CycleDescription += "\n\n";
					}
				}

				throw new BuildException("Action graph contains cycle!\n\n{0}", CycleDescription);
			}
		}

		/// <summary>
		/// Determines the full set of actions that must be built to produce an item.
		/// </summary>
		/// <param name="Actions">All the actions in the graph</param>
		/// <param name="OutputItems">Set of output items to be built</param>
		/// <returns>Set of prerequisite actions</returns>
		public static List<LinkedAction> GatherPrerequisiteActions(List<LinkedAction> Actions, HashSet<FileItem> OutputItems)
		{
			HashSet<LinkedAction> PrerequisiteActions = new HashSet<LinkedAction>();
			foreach(LinkedAction Action in Actions)
			{
				if(Action.ProducedItems.Any(x => OutputItems.Contains(x)))
				{
					GatherPrerequisiteActions(Action, PrerequisiteActions);
				}
			}
			return PrerequisiteActions.ToList();
		}

		/// <summary>
		/// Determines the full set of actions that must be built to produce an item.
		/// </summary>
		/// <param name="Action">The root action to scan</param>
		/// <param name="PrerequisiteActions">Set of prerequisite actions</param>
		private static void GatherPrerequisiteActions(LinkedAction Action, HashSet<LinkedAction> PrerequisiteActions)
		{
			if(PrerequisiteActions.Add(Action))
			{
				foreach(LinkedAction PrerequisiteAction in Action.PrerequisiteActions)
				{
					GatherPrerequisiteActions(PrerequisiteAction, PrerequisiteActions);
				}
			}
		}

		/// <summary>
		/// Determines whether an action is outdated based on the modification times for its prerequisite
		/// and produced items.
		/// </summary>
		/// <param name="RootAction">- The action being considered.</param>
		/// <param name="OutdatedActionDictionary">-</param>
		/// <param name="ActionHistory"></param>
		/// <param name="CppDependencies"></param>
		/// <param name="bIgnoreOutdatedImportLibraries"></param>
		/// <returns>true if outdated</returns>
		public static bool IsActionOutdated(LinkedAction RootAction, Dictionary<LinkedAction, bool> OutdatedActionDictionary, ActionHistory ActionHistory, CppDependencyCache CppDependencies, bool bIgnoreOutdatedImportLibraries)
		{
			// Only compute the outdated-ness for actions that don't aren't cached in the outdated action dictionary.
			bool bIsOutdated = false;
			lock(OutdatedActionDictionary)
			{
				if (OutdatedActionDictionary.TryGetValue(RootAction, out bIsOutdated))
				{
					return bIsOutdated;
				}
			}

			// Determine the last time the action was run based on the write times of its produced files.
			string LatestUpdatedProducedItemName = null;
			DateTimeOffset LastExecutionTimeUtc = DateTimeOffset.MaxValue;
			foreach (FileItem ProducedItem in RootAction.ProducedItems)
			{
				// Check if the command-line of the action previously used to produce the item is outdated.
				string NewProducingCommandLine = RootAction.CommandPath.FullName + " " + RootAction.CommandArguments;
				if (ActionHistory.UpdateProducingCommandLine(ProducedItem, NewProducingCommandLine))
				{
					if(ProducedItem.Exists)
					{
						Log.TraceLog(
							"{0}: Produced item \"{1}\" was produced by outdated command-line.\n  New command-line: {2}",
							RootAction.StatusDescription,
							Path.GetFileName(ProducedItem.AbsolutePath),
							NewProducingCommandLine
							);
					}

					bIsOutdated = true;
				}

				// If the produced file doesn't exist or has zero size, consider it outdated.  The zero size check is to detect cases
				// where aborting an earlier compile produced invalid zero-sized obj files, but that may cause actions where that's
				// legitimate output to always be considered outdated.
				if (ProducedItem.Exists && (RootAction.ActionType != ActionType.Compile || ProducedItem.Length > 0 || (!ProducedItem.Location.HasExtension(".obj") && !ProducedItem.Location.HasExtension(".o"))))
				{
					// Use the oldest produced item's time as the last execution time.
					if (ProducedItem.LastWriteTimeUtc < LastExecutionTimeUtc)
					{
						LastExecutionTimeUtc = ProducedItem.LastWriteTimeUtc;
						LatestUpdatedProducedItemName = ProducedItem.AbsolutePath;
					}
				}
				else
				{
					// If any of the produced items doesn't exist, the action is outdated.
					Log.TraceLog(
						"{0}: Produced item \"{1}\" doesn't exist.",
						RootAction.StatusDescription,
						Path.GetFileName(ProducedItem.AbsolutePath)
						);
					bIsOutdated = true;
				}
			}

			// Check if any of the prerequisite actions are out of date
			if (!bIsOutdated)
			{
				foreach (LinkedAction PrerequisiteAction in RootAction.PrerequisiteActions)
				{
					if (IsActionOutdated(PrerequisiteAction, OutdatedActionDictionary, ActionHistory, CppDependencies, bIgnoreOutdatedImportLibraries))
					{
						// Only check for outdated import libraries if we were configured to do so.  Often, a changed import library
						// won't affect a dependency unless a public header file was also changed, in which case we would be forced
						// to recompile anyway.  This just allows for faster iteration when working on a subsystem in a DLL, as we
						// won't have to wait for dependent targets to be relinked after each change.
						if(!bIgnoreOutdatedImportLibraries || !IsImportLibraryDependency(RootAction, PrerequisiteAction))
						{
							Log.TraceLog("{0}: Prerequisite {1} is produced by outdated action.", RootAction.StatusDescription, PrerequisiteAction.StatusDescription);
							bIsOutdated = true;
							break;
						}
					}
				}
			} 

			// Check if any prerequisite item has a newer timestamp than the last execution time of this action
			if(!bIsOutdated)
			{
				foreach (FileItem PrerequisiteItem in RootAction.PrerequisiteItems)
				{
					if (PrerequisiteItem.Exists)
					{
						// allow a 1 second slop for network copies
						TimeSpan TimeDifference = PrerequisiteItem.LastWriteTimeUtc - LastExecutionTimeUtc;
						bool bPrerequisiteItemIsNewerThanLastExecution = TimeDifference.TotalSeconds > 1;
						if (bPrerequisiteItemIsNewerThanLastExecution)
						{
							// Need to check for import libraries here too
							if(!bIgnoreOutdatedImportLibraries || !IsImportLibraryDependency(RootAction, PrerequisiteItem))
							{
								Log.TraceLog("{0}: Prerequisite {1} is newer than the last execution of the action: {2} vs {3}", RootAction.StatusDescription, Path.GetFileName(PrerequisiteItem.AbsolutePath), PrerequisiteItem.LastWriteTimeUtc.ToLocalTime(), LastExecutionTimeUtc.LocalDateTime);
								bIsOutdated = true;
								break;
							}
						}
					}
				}
			}

			// Check the dependency list
			if(!bIsOutdated && RootAction.DependencyListFile != null)
			{
				List<FileItem> DependencyFiles;
				if(!CppDependencies.TryGetDependencies(RootAction.DependencyListFile, out DependencyFiles))
				{
					Log.TraceLog("{0}: Missing dependency list file \"{1}\"", RootAction.StatusDescription, RootAction.DependencyListFile);
					bIsOutdated = true;
				}
				else
				{
					foreach(FileItem DependencyFile in DependencyFiles)
					{
						if(!DependencyFile.Exists || DependencyFile.LastWriteTimeUtc > LastExecutionTimeUtc)
						{
							Log.TraceLog(
								"{0}: Dependency {1} is newer than the last execution of the action: {2} vs {3}",
								RootAction.StatusDescription,
								Path.GetFileName(DependencyFile.AbsolutePath),
								DependencyFile.LastWriteTimeUtc.ToLocalTime(),
								LastExecutionTimeUtc.LocalDateTime
								);
							bIsOutdated = true;
							break;
						}
					}
				}
			}

			// Cache the outdated-ness of this action.
			lock(OutdatedActionDictionary)
			{
				if(!OutdatedActionDictionary.ContainsKey(RootAction))
				{
					OutdatedActionDictionary.Add(RootAction, bIsOutdated);
				}
			}

			return bIsOutdated;
		}

		/// <summary>
		/// Determines if the dependency between two actions is only for an import library
		/// </summary>
		/// <param name="RootAction">The action to check</param>
		/// <param name="PrerequisiteAction">The action that it depends on</param>
		/// <returns>True if the only dependency between two actions is for an import library</returns>
		static bool IsImportLibraryDependency(LinkedAction RootAction, LinkedAction PrerequisiteAction)
		{
			if(PrerequisiteAction.bProducesImportLibrary)
			{
				return PrerequisiteAction.ProducedItems.All(x => x.Location.HasExtension(".lib") || !RootAction.PrerequisiteItems.Contains(x));
			}
			else
			{
				return false;
			}
		}

		/// <summary>
		/// Determines if the dependency on a between two actions is only for an import library
		/// </summary>
		/// <param name="RootAction">The action to check</param>
		/// <param name="PrerequisiteItem">The dependency that is out of date</param>
		/// <returns>True if the only dependency between two actions is for an import library</returns>
		static bool IsImportLibraryDependency(LinkedAction RootAction, FileItem PrerequisiteItem)
		{
			if(PrerequisiteItem.Location.HasExtension(".lib"))
			{
				foreach(LinkedAction PrerequisiteAction in RootAction.PrerequisiteActions)
				{
					if(PrerequisiteAction.bProducesImportLibrary && PrerequisiteAction.ProducedItems.Contains(PrerequisiteItem))
					{
						return true;
					}
				}
			}
			return false;
		}

		/// <summary>
		/// Builds a dictionary containing the actions from AllActions that are outdated by calling
		/// IsActionOutdated.
		/// </summary>
		public static void GatherAllOutdatedActions(IEnumerable<LinkedAction> Actions, ActionHistory ActionHistory, Dictionary<LinkedAction, bool> OutdatedActions, CppDependencyCache CppDependencies, bool bIgnoreOutdatedImportLibraries)
		{
			using(Timeline.ScopeEvent("Prefetching include dependencies"))
			{
				List<FileItem> Dependencies = new List<FileItem>();
				foreach(LinkedAction Action in Actions)
				{
					if(Action.DependencyListFile != null)
					{
						Dependencies.Add(Action.DependencyListFile);
					}
				}
				Parallel.ForEach(Dependencies, File => { List<FileItem> Temp; CppDependencies.TryGetDependencies(File, out Temp); });
			}

			using(Timeline.ScopeEvent("Cache outdated actions"))
			{
				Parallel.ForEach(Actions, Action => IsActionOutdated(Action, OutdatedActions, ActionHistory, CppDependencies, bIgnoreOutdatedImportLibraries));
			}
		}
		/// <summary>
		/// Deletes all the items produced by actions in the provided outdated action dictionary.
		/// </summary>
		/// <param name="OutdatedActions">List of outdated actions</param>
		public static void DeleteOutdatedProducedItems(List<LinkedAction> OutdatedActions)
		{
			foreach(LinkedAction OutdatedAction in OutdatedActions)
			{
				foreach (FileItem DeleteItem in OutdatedAction.DeleteItems)
				{
					if (DeleteItem.Exists)
					{
						Log.TraceLog("Deleting outdated item: {0}", DeleteItem.AbsolutePath);
						DeleteItem.Delete();
					}
				}
			}
		}

		/// <summary>
		/// Creates directories for all the items produced by actions in the provided outdated action
		/// dictionary.
		/// </summary>
		public static void CreateDirectoriesForProducedItems(List<LinkedAction> OutdatedActions)
		{
			HashSet<DirectoryReference> OutputDirectories = new HashSet<DirectoryReference>();
			foreach(LinkedAction OutdatedAction in OutdatedActions)
			{
				foreach(FileItem ProducedItem in OutdatedAction.ProducedItems)
				{
					OutputDirectories.Add(ProducedItem.Location.Directory);
				}
			}
			foreach(DirectoryReference OutputDirectory in OutputDirectories)
			{
				if(!DirectoryReference.Exists(OutputDirectory))
				{
					DirectoryReference.CreateDirectory(OutputDirectory);
				}
			}
		}

		/// <summary>
		/// Imports an action graph from a JSON file
		/// </summary>
		/// <param name="InputFile">The file to read from</param>
		/// <returns>List of actions</returns>
		public static List<Action> ImportJson(FileReference InputFile)
		{
			JsonObject Object = JsonObject.Read(InputFile);

			JsonObject EnvironmentObject = Object.GetObjectField("Environment");
			foreach(string KeyName in EnvironmentObject.KeyNames)
			{
				Environment.SetEnvironmentVariable(KeyName, EnvironmentObject.GetStringField(KeyName));
			}

			List<Action> Actions = new List<Action>();
			foreach (JsonObject ActionObject in Object.GetObjectArrayField("Actions"))
			{
				Actions.Add(Action.ImportJson(ActionObject));
			}
			return Actions;
		}

		/// <summary>
		/// Exports an action graph to a JSON file
		/// </summary>
		/// <param name="Actions">The actions to write</param>
		/// <param name="OutputFile">Output file to write the actions to</param>
		public static void ExportJson(IEnumerable<IAction> Actions, FileReference OutputFile)
		{
			DirectoryReference.CreateDirectory(OutputFile.Directory);
			using (JsonWriter Writer = new JsonWriter(OutputFile))
			{
				Writer.WriteObjectStart();

				Writer.WriteObjectStart("Environment");
				foreach (System.Collections.DictionaryEntry Pair in Environment.GetEnvironmentVariables())
				{
					if (!UnrealBuildTool.InitialEnvironment.Contains(Pair.Key) || (string)(UnrealBuildTool.InitialEnvironment[Pair.Key]) != (string)(Pair.Value))
					{
						Writer.WriteValue((string)Pair.Key, (string)Pair.Value);
					}
				}
				Writer.WriteObjectEnd();

				Writer.WriteArrayStart("Actions");
				foreach (IAction Action in Actions)
				{
					Writer.WriteObjectStart();
					Action.ExportJson(Writer);
					Writer.WriteObjectEnd();
				}
				Writer.WriteArrayEnd();
				Writer.WriteObjectEnd();
			}
		}
	}
}
