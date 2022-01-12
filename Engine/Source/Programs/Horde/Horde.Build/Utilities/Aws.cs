// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;

namespace Horde.Build.Utilities
{
	/// <summary>
	/// Helper functions for interacting with Amazon Web Services (AWS) 
	/// </summary>
	public static class AwsHelper
	{
		/// <summary>
		/// Reads AWS credentials from 'credentials' file
		///
		/// The AWS SDK provided functionality was proven too much work, so this is a simplified version.
		/// </summary>
		/// <param name="ProfileName">Name of the AWS profile</param>
		/// <param name="CredentialsFilePath">Override for the credentials file path, set to null for default path</param>
		/// <returns>Credentials as a tuple</returns>
		/// <exception cref="Exception"></exception>
		public static (string AccessKey, string SecretAccessKey, string SecretToken) ReadAwsCredentials(string ProfileName, string? CredentialsFilePath = null)
		{
			CredentialsFilePath ??= Path.Join(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".aws", "credentials");
			string[] Lines = File.ReadAllLines(CredentialsFilePath);
		    
			string ReadValue(string Line, string ExpectedKey)
			{
				if (Line.StartsWith(ExpectedKey, StringComparison.Ordinal)) return Line.Split("=")[1].Trim();
				throw new Exception($"Unable to read key/value on line {Line} for key {ExpectedKey}");
			}

			for (int i = 0; i < Lines.Length; i++)
			{
				string Line = Lines[i];
				if (Line == $"[{ProfileName}]")
				{
					string AccessKey = ReadValue(Lines[i + 1], "aws_access_key_id");
					string SecretAccessKey = ReadValue(Lines[i + 2], "aws_secret_access_key");
					string SessionToken = ReadValue(Lines[i + 3], "aws_session_token");

					return (AccessKey, SecretAccessKey, SessionToken);
				}
			}

			throw new Exception($"Unable to find profile {ProfileName} in file {CredentialsFilePath}");
		}
	}
}