// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Base class for returning untyped response data
	/// </summary>
	public class PerforceResponse
	{
		/// <summary>
		/// Stores the response data
		/// </summary>
		protected object InternalData;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data">The response data</param>
		public PerforceResponse(object Data)
		{
			this.InternalData = Data;
		}

		/// <summary>
		/// True if the response is successful
		/// </summary>
		public bool Succeeded
		{
			get { return !(InternalData is PerforceError); }
		}

		/// <summary>
		/// True if the response is an error
		/// </summary>
		public bool Failed
		{
			get { return InternalData is PerforceError; }
		}

		/// <summary>
		/// Accessor for the succcessful response data. Throws an exception if the response is an error.
		/// </summary>
		public object Data
		{
			get
			{
				EnsureSuccess();
				return InternalData;
			}
		}

		/// <summary>
		/// Returns the info data.
		/// </summary>
		public PerforceInfo? Info
		{
			get { return InternalData as PerforceInfo; }
		}

		/// <summary>
		/// Returns the error data, or null if this is a succesful response.
		/// </summary>
		public PerforceError? Error
		{
			get { return InternalData as PerforceError; }
		}

		/// <summary>
		/// Throws an exception if the response is an error
		/// </summary>
		public void EnsureSuccess()
		{
			PerforceError? Error = InternalData as PerforceError;
			if(Error != null)
			{
				throw new PerforceException(Error);
			}
		}

		/// <summary>
		/// Returns a string representation of this object for debugging
		/// </summary>
		/// <returns>String representation of the object for debugging</returns>
		public override string? ToString()
		{
			return InternalData.ToString();
		}
	}

	/// <summary>
	/// Represents a successful Perforce response of the given type, or an error. Throws a PerforceException with the error
	/// text if the response value is attempted to be accessed and an error has occurred.
	/// </summary>
	/// <typeparam name="T">Type of data returned on success</typeparam>
	public class PerforceResponse<T> : PerforceResponse where T : class
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data">The successful response data</param>
		public PerforceResponse(T Data)
			: base(Data)
		{
		}
		
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Info">The info data</param>
		public PerforceResponse(PerforceInfo Info)
			: base(Info)
		{
		}
		
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Error">The error data</param>
		public PerforceResponse(PerforceError Error)
			: base(Error)
		{
		}

		/// <summary>
		/// Construct a typed response from an untyped response
		/// </summary>
		/// <param name="UntypedResponse">The untyped response</param>
		public PerforceResponse(PerforceResponse UntypedResponse)
			: base(UntypedResponse.Error ?? UntypedResponse.Info ?? (object)(T)UntypedResponse.Data)
		{

		}

		/// <summary>
		/// Accessor for the succcessful response data. Throws an exception if the response is an error.
		/// </summary>
		public new T Data
		{
			get
			{
				T? Result = InternalData as T;
				if (Result == null)
				{
					if (InternalData is PerforceInfo)
					{
						throw new PerforceException($"Expected record of type '{typeof(T).Name}', got info: {InternalData}");
					}
					else if(InternalData is PerforceError)
					{
						throw new PerforceException($"{InternalData}");
					}
					else
					{
						throw new PerforceException($"Expected record of type '{typeof(T).Name}', got: {InternalData}");
					}
				}
				return Result;
			}
		}
	}
}
