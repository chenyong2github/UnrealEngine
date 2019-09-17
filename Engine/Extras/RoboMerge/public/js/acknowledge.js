// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
function acknowledge() {
    let queryParams = processQueryParameters(['bot', 'branch', 'cl'])
    if (!queryParams) return;

    let requestedBotName = queryParams["bot"]
    let requestedBranchName = queryParams["branch"]
    let requestedBranchCl = parseInt(queryParams["cl"], 10)

    if (isNaN(requestedBranchCl)) {
        acknowledgeFailure(`CL ${queryParams["cl"]} not a number.`)
        return
    }

    getBranch(requestedBotName, requestedBranchName, function(data) {
        // Ensure we have data
        if (!data) {
            return
        }

        // Verify we got a branch
        if (!data.branch) {
            let errText = `Could not find matching branch for ${requestedBotName}:${requestedBranchName}.`
            if (data.message) {
                errText += `\n${data.message}`
            }
            acknowledgeFailure(errText)
            return
        }
        let requestedBranch = data.branch

         // Find conflict in question matching the requested CL
        // First, verify the request branch is paused
        if (!requestedBranch.pause_info) {
            acknowledgeFailure(`Branch ${requestedBotName}:${requestedBranchName} not currently paused, no need to acknowledge.`)
            $('#result').append(renderSingleBranchTable(requestedBranch))
            return
        }
        
        // Ensure the current pause is applicable to the CL
        if (requestedBranch.pause_info.change !== requestedBranchCl) {
            displayWarningMessage(`Branch ${requestedBotName}:${requestedBranchName} currently paused, but not at requested CL ${requestedBranchCl}. Performing no action.`)
            $('#result').append(renderSingleBranchTable(requestedBranch))
            return
        }

        // Data all verified, time to perform the branch operation.
        branchOp(requestedBranch.bot, requestedBranch.def.name, "/acknowledge", function(success) {
            if (success) {
                acknowledgeSuccess(`${data.user.displayName} successfully acknowledged CL ${requestedBranchCl} for ${requestedBotName}:${requestedBranchName}`)
            } else {
                acknowledgeFailure(`${data.user.displayName} encountered an error acknowledging CL ${requestedBranchCl} for ${requestedBotName}:${requestedBranchName}`)
            }

            // Call getBranchList a second time to display most up-to-date data
            getBranch(requestedBranch.bot, requestedBranch.def.name, function(data) {
                $('#result').append(renderSingleBranchTable(data.branch))
            })
        });
    }) 

}

function acknowledgeFailure(message) {
    $(`<div class="alert alert-danger show" role="alert">`).html(`<strong>ERROR:</strong> ${message}`).appendTo($('#result'))
}

function acknowledgeSuccess(message) {
    $(`<div class="alert alert-success show" role="alert">`).html(`<strong>SUCCESS!</strong> ${message}`).appendTo($('#result'))
}