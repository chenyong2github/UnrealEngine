// Copyright Epic Games, Inc. All Rights Reserved.
import { P4Client, P4Util } from '../framework'
import { SimpleMainAndReleaseTestBase, streams } from '../SimpleMainAndReleaseTestBase'

const TEXT_FILENAME = 'test.txt'
export class SyntaxErrorOnUnknownBranch extends SimpleMainAndReleaseTestBase {
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
		.then(() => P4Util.submit(releaseClient, 'Edit with command\n#robomerge somebranch'))
	}

// @todo add code to fix syntax error and retry, make sure unblocks and updates source node Slack message
// latter is probably only broken in case where there's also an edge blockage

	async verify() {
		return this.ensureBlocked('Release')
	}

	allowSyntaxErrors() {
		return true
	}

	private mainClient: P4Client
}
