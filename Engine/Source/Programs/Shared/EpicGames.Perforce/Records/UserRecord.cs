// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Contains information about a Perforce user
	/// </summary>
	public class UserRecord
	{
		/// <summary>
		/// The name for the user
		/// </summary>
		[PerforceTag("User")]
		public string UserName;

		/// <summary>
		/// Registered email address for reviews
		/// </summary>
		[PerforceTag("Email")]
		public string Email;

		/// <summary>
		/// Last time the user's information was updated
		/// </summary>
		[PerforceTag("Update", Optional = true)]
		public DateTime Update;

		/// <summary>
		/// Last time the user's information was accessed
		/// </summary>
		[PerforceTag("Access", Optional = true)]
		public DateTime Access;

		/// <summary>
		/// The user's full name
		/// </summary>
		[PerforceTag("FullName")]
		public string FullName;

		/// <summary>
		/// Paths which the user is watching
		/// </summary>
		[PerforceTag("Reviews", Optional = true)]
		public List<string> Reviews = new List<string>();

		/// <summary>
		/// The type of user
		/// </summary>
		[PerforceTag("Type", Optional = true)]
		public string? Type;

		/// <summary>
		/// Method used to authenticate
		/// </summary>
		[PerforceTag("AuthMethod")]
		public string? AuthMethod;

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private UserRecord()
		{
			UserName = null!;
			Email = null!;
			FullName = null!;
			Type = null!;
		}

		/// <summary>
		/// Summarize this record for display in the debugger
		/// </summary>
		/// <returns>Formatted record</returns>
		public override string ToString()
		{
			return FullName;
		}
	}
}
