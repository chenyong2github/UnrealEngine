// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Represents a list of responses from the Perforce server. Within the list, individual responses
	/// may indicate success or failure.
	/// </summary>
	/// <typeparam name="T">Successful response type</typeparam>
	public class PerforceResponseList<T> : List<PerforceResponse<T>> where T : class
	{
		/// <summary>
		/// Whether all responses in this list are successful
		/// </summary>
		public bool Succeeded
		{
			get { return this.All(x => x.Succeeded); }
		}

		/// <summary>
		/// Returns the first error, or null.
		/// </summary>
		public PerforceError FirstError
		{
			get { return Errors.FirstOrDefault(); }
		}

		/// <summary>
		/// Sequence of all the data objects from the responses.
		/// </summary>
		public List<T> Data
		{
			get
			{
				if (Count == 1)
				{
					PerforceError? Error = this[0].Error;
					if (Error != null && Error.Generic == PerforceGenericCode.Empty)
					{
						return new List<T>();
					}
				}
				return this.Where(x => x.Info == null).Select(x => x.Data).Where(x => x != null).ToList();
			}
		}

		/// <summary>
		/// Sequence of all the error responses.
		/// </summary>
		public IEnumerable<PerforceError> Errors
		{
			get
			{
				foreach (PerforceResponse<T> Response in this)
				{
					PerforceError? Error = Response.Error;
					if (Error != null)
					{
						yield return Error;
					}
				}
			}
		}

		/// <summary>
		/// Throws an exception if any response is an error
		/// </summary>
		public void EnsureSuccess()
		{
			foreach (PerforceResponse<T> Response in this)
			{
				Response.EnsureSuccess();
			}
		}
	}
}
