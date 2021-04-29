// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Utilities;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	/// <summary>
	/// Fingerprint for an issue
	/// </summary>
	public interface IIssueFingerprint
	{
		/// <summary>
		/// The type of issue
		/// </summary>
		public string Type { get; }

		/// <summary>
		/// List of keys which identify this issue.
		/// </summary>
		public CaseInsensitiveStringSet Keys { get; }

		/// <summary>
		/// Set of keys which should trigger a negative match
		/// </summary>
		public CaseInsensitiveStringSet? RejectKeys { get; }
	}

	/// <summary>
	/// Extension methods for <see cref="IIssueFingerprint"/>
	/// </summary>
	public static class IssueFingerprintExtensions
	{
		/// <summary>
		/// Checks if a fingerprint matches another fingerprint
		/// </summary>
		/// <param name="Fingerprint">The first fingerprint to compare</param>
		/// <param name="Other">The other fingerprint to compare to</param>
		/// <returns>True is the fingerprints match</returns>
		public static bool IsMatch(this IIssueFingerprint Fingerprint, IIssueFingerprint Other)
		{
			if (!Fingerprint.Type.Equals(Other.Type, StringComparison.Ordinal))
			{
				return false;
			}
			if (Fingerprint.Keys.Count > 0 && Other.Keys.Count > 0 && !Fingerprint.Keys.Any(x => Other.Keys.Contains(x)))
			{
				return false;
			}
			if (Fingerprint.RejectKeys != null && Fingerprint.RejectKeys.Any(x => Other.Keys.Contains(x)))
			{
				return false;
			}
			return true;
		}

		/// <summary>
		/// Checks if a fingerprint matches another fingerprint for creating a new span
		/// </summary>
		/// <param name="Fingerprint">The first fingerprint to compare</param>
		/// <param name="Other">The other fingerprint to compare to</param>
		/// <returns>True is the fingerprints match</returns>
		public static bool IsMatchForNewSpan(this IIssueFingerprint Fingerprint, IIssueFingerprint Other)
		{
			if (!Fingerprint.Type.Equals(Other.Type, StringComparison.Ordinal))
			{
				return false;
			}
			if (Fingerprint.RejectKeys != null && Fingerprint.RejectKeys.Any(x => Other.Keys.Contains(x)))
			{
				return false;
			}
			return true;
		}
	}
}
