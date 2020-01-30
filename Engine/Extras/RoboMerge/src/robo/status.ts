// Copyright Epic Games, Inc. All Rights Reserved.

import * as util from 'util'
import {Bot, Branch, BranchStatus} from './branch-interfaces'
import {AuthData} from './session'

export class Status {
	constructor(private startTime: Date, private version: string) {
	}

	addBranch(branch: Branch, autoUpdater: Bot | null) {
		const status: BranchStatus = {def: Status.makeBranchDef(branch), bot: branch.parent.botname, branch_spec_cl: -1}
		this.allBranches.push(status)

		if (autoUpdater) {
			status.branch_spec_cl = autoUpdater.lastCl
		}

		branch.bot!.applyStatus(status)
	}

	getForUser(user: AuthData) {
		const result: any = {
			started: this.startTime,
			version: this.version,
			user: {
				userName: user.user,
				displayName: user.displayName
			}
		}

		if (user.tags.size !== 0) {
			result.user.privileges = [...user.tags]
		}

		if (user.tags.has('fte')) {
			result.branches = this.allBranches
		}
		else {
			const branches: BranchStatus[] = []
			result.branches = branches
			for (const status of this.allBranches) {
				// @todo: check for list of tags
				if (this.includeBranch(status.def, user.tags)) {
					branches.push(status)
				}
			}

			if (this.allBranches.length > 0 && branches.length === 0) {
				result.insufficientPrivelege = true
			}
		}
		return result
	}

	static fromIPC(obj: any) {
		if (!('allBranches' in obj && 'startTime' in obj && 'version' in obj)) {
			throw new Error('Invalid status from IPC')
		}

		const result = new Status(obj.startTime, obj.version)
		result.allBranches = obj.allBranches
		return result
	}

	private includeBranch(branch: Branch, userPrivileges: Set<string>) {
		if (branch.visibility === 'all') {
			return true
		}

		if (!Array.isArray(branch.visibility)) {
			// for now, only keyword 'all' supported
			util.log('Warning! Unknown visibility keyword: ' + branch.visibility)
			return false
		}

		for (const vis of branch.visibility) {
			if (userPrivileges.has(vis)) {
				// @todo filter flow lists if individual branch bot filtered
				return true
			}
		}
		return false
	}

	static makeBranchDef(branch: Branch) {
		const def: any = {}
		for (const key in branch) {
			let val = (branch as any)[key]
			if (val) {	
				const isIterable = typeof val !== 'string' && typeof val[Symbol.iterator] === 'function'
				// expand any iterable to an array
				def[key] = isIterable ? [...val] : val
			}
		}
		return def
	}

	private allBranches: BranchStatus[] = []
}
