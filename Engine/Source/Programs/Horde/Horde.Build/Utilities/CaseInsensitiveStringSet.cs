// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Serializers;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Case insensitive set of strings
	/// </summary>
	public class CaseInsensitiveStringSet : HashSet<string>
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public CaseInsensitiveStringSet()
			: base(StringComparer.OrdinalIgnoreCase)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public CaseInsensitiveStringSet(IEnumerable<string> Items)
			: base(Items, StringComparer.OrdinalIgnoreCase)
		{
		}
	}
}
