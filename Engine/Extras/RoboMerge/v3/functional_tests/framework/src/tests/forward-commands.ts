// Copyright Epic Games, Inc. All Rights Reserved.
import { P4Client, P4Util } from '../framework'
import { SimpleMainAndReleaseTestBase, streams } from '../SimpleMainAndReleaseTestBase'
import { Change } from '../test-perforce'

const TEXT_FILENAME = 'test.txt'
export class ForwardCommands extends SimpleMainAndReleaseTestBase {
	async setup() {
		await this.p4.depot('stream', this.depotSpec())
		await this.createStreamsAndWorkspaces(streams)

		this.mainClient = this.getClient('Main')

		await P4Util.addFileAndSubmit(this.mainClient, TEXT_FILENAME, 'Initial content')

		await this.initialPopulate()
	}

	run() {
		const releaseClient = this.getClient('Release')
		return releaseClient.sync()
		.then(() => P4Util.editFile(releaseClient, TEXT_FILENAME, 'Initial content\n\nMergeable'))
		.then(() => P4Util.submit(releaseClient, 'Edit with command\n#robomerge[DOOBIE] somebranch'))
	}

	async verify() {
		let description: string | null = null
		await Promise.all([
			this.checkHeadRevision('Main', TEXT_FILENAME, 2),
			this.mainClient.changes(1)
				.then((changes: Change[]) => description = changes[0]!.description)
		])

		if (description!.indexOf('[DOOBIE]') < 0) {
			throw new Error('forwarded command got snipped')
		}
	}

	private mainClient: P4Client
}
