// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;

namespace UnrealBuildTool
{
	/// <summary>
	/// Maps a unique string to an integer
	/// </summary>
	class UniqueStringRegistry
	{
		// protect the InstanceMap
		private object LockObject = new object();

		// holds a mapping of string name to single instance
		private Dictionary<string, int> StringToInstanceMap = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);

		// holds a mapping of string aliases to original string name
		private Dictionary<string, string> AliasToStringMap = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

		public UniqueStringRegistry()
		{
		}

		public bool HasString(string Name)
		{
			return StringToInstanceMap.ContainsKey(Name);
		}

		public int FindOrAddByName(string Name)
		{
			// look for existing one
			int Instance = -1;
			if (!StringToInstanceMap.TryGetValue(Name, out Instance))
			{
				lock (LockObject)
				{
					if (!StringToInstanceMap.TryGetValue(Name, out Instance))
					{
						// copy over the dictionary and add, so that other threads can keep reading out of the old one until it's time
						Dictionary<string, int> NewStringToInstanceMap = new Dictionary<string, int>(StringToInstanceMap, StringComparer.OrdinalIgnoreCase);

						// make and add a new instance number
						Instance = StringToInstanceMap.Count;
						NewStringToInstanceMap[Name] = Instance;

						// replace the class's map
						StringToInstanceMap = NewStringToInstanceMap;
					}
				}
			}
			return Instance;
		}

		public string[] GetStringNames()
		{
			return StringToInstanceMap.Keys.ToArray();
		}

		public int[] GetStringIds()
		{
			return StringToInstanceMap.Values.ToArray();
		}

		public string GetStringForId(int Id)
		{
			return StringToInstanceMap.First(x => x.Value == Id).Key;
		}


		public bool HasAlias(string Alias)
		{
			return AliasToStringMap.ContainsKey(Alias);
		}

		public int FindExistingAlias(string Alias)
		{
			string? Name;
			if (!AliasToStringMap.TryGetValue(Alias, out Name) || Name == null)
			{
				throw new BuildException($"Alias {Alias} not found");
			}

			return FindOrAddByName(Name);
		}

		public int FindOrAddAlias(string Alias, string OriginalName)
		{
			string? Name;
			if (!AliasToStringMap.TryGetValue(Alias, out Name))
			{
				lock (LockObject)
				{
					// copy over the dictionary and add, so that other threads can keep reading out of the old one until it's time
					Dictionary<string, string> NewAliasToStringMap = new Dictionary<string, string>(AliasToStringMap, StringComparer.OrdinalIgnoreCase);

					// add a new alias
					NewAliasToStringMap[Alias] = OriginalName;

					// replace the instance map
					AliasToStringMap = NewAliasToStringMap;
				}
				Name = OriginalName;
			}
			else
			{
				if (Name != OriginalName || Name == null)
				{
					throw new BuildException($"{Alias} is already an alias for {Name}. Can't associate with {OriginalName}");
				}
			}

			return FindOrAddByName(Name);
		}
	}
}