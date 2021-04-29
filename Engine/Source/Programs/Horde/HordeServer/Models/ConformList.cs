using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// List of machines that are currently conforming
	/// </summary>
	[SingletonDocument("5ff62d06a3bdb49f82da3cac")]
	public class ConformList : SingletonBase
	{
		/// <summary>
		/// Maximum allowed at once
		/// </summary>
		public int MaxCount { get; set; }

		/// <summary>
		/// List of current conforms that are running
		/// </summary>
		public List<ObjectId> LeaseIds { get; set; } = new List<ObjectId>();
	}
}
