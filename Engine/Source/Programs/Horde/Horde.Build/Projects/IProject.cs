// Copyright Epic Games, Inc. All Rights Reserved.

using Horde.Build.Utilities;

namespace Horde.Build.Projects
{
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Represents a project
	/// </summary>
	public interface IProject
	{
		/// <summary>
		/// Identifier for the project.
		/// </summary>
		ProjectId Id { get; }

		/// <summary>
		/// Configuration settings for the stream
		/// </summary>
		ProjectConfig Config { get; }
	}
	
	/// <summary>
	/// Interface for a document containing a project logo
	/// </summary>
	public interface IProjectLogo
	{
		/// <summary>
		/// The project id
		/// </summary>
		public ProjectId Id { get; }

		/// <summary>
		/// Path to the logo
		/// </summary>
		public string Path { get; }

		/// <summary>
		/// Revision of the file
		/// </summary>
		public string Revision { get; }

		/// <summary>
		/// Mime type for the image
		/// </summary>
		public string MimeType { get; }

		/// <summary>
		/// Image data
		/// </summary>
		public byte[] Data { get; }
	}
}
