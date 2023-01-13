// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using Horde.Build.Utilities;

namespace Horde.Build.Projects
{
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Interface for a collection of project documents
	/// </summary>
	public interface IProjectCollection
	{
		/// <summary>
		/// Gets the logo for a project
		/// </summary>
		/// <param name="projectId">The project id</param>
		/// <returns>The project logo document</returns>
		Task<IProjectLogo?> GetLogoAsync(ProjectId projectId);

		/// <summary>
		/// Sets the logo for a project
		/// </summary>
		/// <param name="projectId">The project id</param>
		/// <param name="path">Path to the source file</param>
		/// <param name="revision">Revision of the file</param>
		/// <param name="mimeType"></param>
		/// <param name="data"></param>
		/// <returns></returns>
		Task SetLogoAsync(ProjectId projectId, string path, string revision, string mimeType, byte[] data);
	}
}
