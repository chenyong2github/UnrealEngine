// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	}
}