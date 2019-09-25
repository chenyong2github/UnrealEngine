// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

import * as util from 'util'
import {Change, Perforce, OpenedFileRecord, RoboWorkspace} from './perforce'

const VERBOSE_LOGGING = true


///////////////////
// Error reporting
/*function _reportFilesStillToResolve(changeNum: number, needsResolve: Object[]) {
	// @todo list files in error
	throw new Error(`changelist ${changeNum} has files still to resolve`)
}*/

function _reportUnknownActions(changeNum: number, unknownActions: OpenedFileRecord[]) {
	util.log('ERROR: These files have unknown actions so cannot be reliably re-opened:')
	for (const openedRec of unknownActions) {
		util.log(`    ${openedRec.action},${openedRec.clientFile}`)
	}
	throw new Error(`changelist ${changeNum} has files with unknown action type (see log)`)
}

/*function _warnNotSyncedOpenFiles(notSyncedOpenFiles: OpenedFileRecord[]) {
	util.log('WARNING: the following files were not synced to the revision they were resolved to before. syncing them to the indicated rev first:')
	for (const file of notSyncedOpenFiles) {
		util.log(`    ${file.clientFile}#${file.rev} (had #${file.haveRev})`)
	}
}*/
///////////////////

export async function convertIntegrateToEdit(p4: Perforce, roboWorkspace: RoboWorkspace, changeNum: number) {
	const workspace = Perforce.coerceWorkspace(roboWorkspace)!
	const files = await p4.opened(changeNum) as OpenedFileRecord[]
	if (files.length === 0) {
		throw new Error('nothing to integrate')
	}

	// @todo check all files have all OpenedFileRecord fields

	if (VERBOSE_LOGGING) {
		util.log(`Converting ${files.length} file${files.length === 1 ? '' : 's'} in CL ${changeNum}.`)
	}

	// create a mapping of client to local files so we can re-open using local paths (needed for some actions)
	const openedClientToLocalMap = new Map(files.map(file => [file.clientFile.toLowerCase(), file.depotFile] as [string, string]))
	const needResolve = await p4.listFilesToResolve(workspace, changeNum)
	if (needResolve.length !== 0) {
		// @todo list files in error
		throw new Error(`changelist ${changeNum} has files still to resolve`)
	}

	// We'll just assume all the files in the changelist are integrated. We'll revert and re-open
	// with the same action/filetype. This should remove the integration records.

	// partition our files into ones we know the actions for, and the ones we don't
	const basicActions = new Map<string, OpenedFileRecord[]>([
		['delete', []],
		['add', []],
		['edit', []],
	])
	const moveAddActions: OpenedFileRecord[] = []
	const moveDeleteActions: OpenedFileRecord[] = []
	const unknownActions: OpenedFileRecord[] = []

	// multiple action map to the same reopen mapping, so this handles that.
	const actionMappings = new Map<string, OpenedFileRecord[]>([
		['delete', basicActions.get('delete')!],
		['branch', basicActions.get('add')!],
		['add', basicActions.get('add')!],
		['edit', basicActions.get('edit')!],
		['integrate', basicActions.get('edit')!],
		['move/add', moveAddActions],
		['move/delete', moveDeleteActions],
	])

	// this maps all open records to a reopen action.
	for (const openedRec of files) {
		(actionMappings.get(openedRec.action) || unknownActions).push(openedRec)
	}

	// if we have an unknown action, abort with error.
	if (unknownActions.length !== 0) {
		_reportUnknownActions(changeNum, unknownActions)
	}

	// we are good to go, so actually revert the files.
	if (VERBOSE_LOGGING) {
		util.log(`Reverting files that will be reopened`)
	}

	await p4.revert(workspace, changeNum, ['-k'])

	// P4 lets you resolve/submit integrations without syncing them to the revision you are resolving against because it's a server operation.
	// if the haveRev doesn't match the rev of the originally opened file, this was the case.
	// so we sync -k because the local file matches the one we want, we just have to tell P4 that so it will let us check it out at that revision.
	// If we simply sync -k to #head we might miss a legitimate submit that happened since our last resolve, and that would stomp that submit.
	const notSyncedOpenFiles: OpenedFileRecord[] = files.filter(file => 
		file.haveRev !== file.rev && file.action !== 'branch' && file.action !== 'add')

	if (notSyncedOpenFiles.length !== 0) {
// don't warn - this will almost always be the case for RoboMerge
//		_warnNotSyncedOpenFiles(notSyncedOpenFiles);
		await Promise.all(notSyncedOpenFiles.map(file => p4.sync(workspace, `${file.clientFile}#${file.rev}`, ['-k'])))
	}

	// Perform basic actions
	for (const [action, bucket] of basicActions.entries()) {
		if (bucket.length !== 0) {
			const localFiles = bucket.map(file => openedClientToLocalMap.get(file.clientFile.toLowerCase())!)
			if (VERBOSE_LOGGING) {
				util.log(`Re-opening the following files for ${action}:`)
				for (const file of localFiles) {
					util.log(`    ${file}`)
				}
			}
			await p4.run(workspace, action, changeNum, localFiles)
		}
	}

	// Perform move actions
	if (moveAddActions.length !== 0) {
		const localFiles = moveAddActions.map(file => [
			file.movedFile!,
			openedClientToLocalMap.get(file.clientFile.toLowerCase())!
		])

		// todo: check that movedFile actually exists

		if (VERBOSE_LOGGING) {
			util.log(`Re-opening the following files for move:`)

			for (const [src, target] of localFiles) {
				if (!src) {
					throw new Error(`No source file in move (CL${changeNum})`)
				}

				util.log(`    ${src} to ${target}`)
			}
		}

		// We have to first open the source file for edit (have to use -k because the file has already been moved locally!)
		await p4.run(workspace, 'edit', changeNum, localFiles.map(([src, _target]) => src), ['-k'])

		// then we can open the file for move in the new location (have to use -k because the file has already been moved locally!)
		for (const [src, target] of localFiles) {
			await p4.move(workspace, changeNum, src, target, ['-k'])
		}
	}

	// Get the list of reopened files in the CL to check their filetype
	if (VERBOSE_LOGGING) {
		util.log(`Getting the list of files reopened in changelist ${changeNum}...`)
	}

	const reopened = await p4.opened(changeNum) as OpenedFileRecord[]

	if (reopened.length === 0) {
		throw new Error('change has no reopened files. This is an error in the conversion code')
	}

	if (reopened.length !== files.length) {
		// should block stream and get human to resolve?
		throw new Error(`change doesn't have the same number of reopened files (${reopened.length}) as originally (${files.length}). ` +
			'This probably signifies an error in the conversion code, and the actions should be reverted!')
	}

	const reopenedRecordsMap = new Map(reopened.map(x => [x.clientFile, x] as [string, OpenedFileRecord]))
	for (const file of files) {
		const reopenedRecord = reopenedRecordsMap.get(file.clientFile)
		if (!reopenedRecord) {
			throw new Error(`ERROR: Could not find original file ${file.clientFile} in re-opened records. This signifies an error in the conversion code! Aborting...`)
		}
		if (file.type !== reopenedRecord.type) {
			if (VERBOSE_LOGGING) {
				util.log(`Changing filetype of ${file.clientFile} from ${reopenedRecord.type} to ${file.type}.`)
			}
			await p4.run(workspace, 'reopen', changeNum, [reopenedRecord.clientFile], ['t', file.type])
		}
	}
}

export async function cleanWorkspaces(p4: Perforce, workspaceNames: Set<string>) {
	const changes = await p4.get_pending_changes() as Change[]

	const toRevert = <Change[]>[];
	const ignoredChangeCounts = new Map<string, number>();
	for (const change of changes) {
		if (workspaceNames.has(change.client)) {
			toRevert.push(change);
		}
		else {
			ignoredChangeCounts.set(change.client, (ignoredChangeCounts.get(change.client) || 0) + 1);
		}
	}

	for (const [ws, count] of ignoredChangeCounts.entries()) {
		util.log(`Ignoring ${count} changelist${count > 1 ? 's' : ''} in ${ws} which is not monitored by this bot`);
	}

	// revert any pending changes left over from previous runs
	for (const change of toRevert) {
		const changeStr = `CL ${change.change}: ${change.desc}`
		const workspace = change.client
		if (change.shelved) {
			util.log(`Attempting to delete shelved files in ${changeStr}`)
			try {
				await p4.delete_shelved(workspace, change.change)
			}
			catch (err) {
				// ignore delete errors on startup (as long as delete works, we're good)
				util.log(`Delete shelved failed. Will try revert anyway: ${err}`)
			}
		}
		util.log(`Attempting to revert ${changeStr}`)
		try {
			await p4.revert(workspace, change.change)
		}
		catch (err) {
			// ignore revert errors on startup (As long as delete works, we're good)
			util.log(`Revert failed. Will try delete anyway: ${err}`)
		}

		await p4.deleteCl(workspace, change.change)
	}
}
