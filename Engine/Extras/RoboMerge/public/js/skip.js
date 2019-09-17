// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
function skipVerify() {
    addOnBeforeUnload('Leaving this page will abort the skip action')
    let queryParams = processQueryParameters(['bot', 'branch', 'cl'], ["reason"])
    if (!queryParams) return;

    let requestedBotName = queryParams["bot"]
    let requestedBranchName = queryParams["branch"]
    let requestedBranchCl = parseInt(queryParams["cl"], 10)
    let skipReason = queryParams["reason"] // "notrelevant" | "willredo" | undefined

    if (isNaN(requestedBranchCl)) {
        skipFailure(`CL ${queryParams["cl"]} not a number.`)
        return
    }

    addOnBeforeUnload('Leaving this page will abort the skip action on CL ' + requestedBranchCl)

    getBranch(requestedBotName, requestedBranchName, function(data) {
        // Ensure we have data
        if (!data) {
            return
        }

        let requestedBranch = verify(requestedBotName, requestedBranchName, requestedBranchCl, data.branch)
        // If the branch failed verification, please quick immediately
        if (!requestedBranch) {
            return
        }

        const targetBranchName = requestedBranch.pause_info.targetBranchName

        // Print preview
        $('#preview').append($('<h3>').text("Preview:"))
        $('#preview').append(renderSingleBranchTable(requestedBranch))

        // Display skip reason, if passed in
        let skipText
        if (skipReason) {
            if (skipReason === "notrelevant") {
                $('#preview').append(
                    $('<h3 align="center">').html(`You've selected that this work is not relevant to <strong>${targetBranchName}</strong>.`))
                skipText = `change not relevant to ${targetBranchName}`
            } else if (skipReason === "willredo") {
                $('#preview').append(
                    $('<h3 align="center">').html(`You've selected that you will manually redo the work in <strong>${targetBranchName}</strong>.`))
                skipText = `will redo as new commit in ${targetBranchName}`
            } else {
                $('#preview').append(
                    $('<h3 style="color:red;" align="center">').html(`You haven't provided a valid reason to skip CL ${requestedBranchCl}.`))
                skipText = "invalid reason given"
            }
        } else {
            $('#preview').append(
                $('<h3 style="color:red;" align="center">').html(`You haven't provided a reason to skip CL ${requestedBranchCl}.`))
        }

        // Verify user wants to skip
        $('#preview').append($('<h3 align="center">').text("Click to confirm you wish to skip this CL:"))
        let buttonDiv = $('<div align="center">').appendTo($('#preview'))

        // Return to Robomerge homepage
        let cancelButton = $('<button type="button" class="btn btn-lg btn-default">').text(`Cancel`).appendTo(buttonDiv)
        cancelButton.click(function() {
            removeOnBeforeUnload()
            window.location.href='/'
        })

        // Perform skip
        let skipButton = $('<button type="button" class="btn btn-lg btn-primary">').text(`Skip ${requestedBranchCl}`).appendTo(buttonDiv)
        skipButton.click(function() {
            removeOnBeforeUnload()
            getBranch(requestedBotName, requestedBranchName, function(data) {
                $('#preview').hide()

                // Ensure we still have data since we last verifyed and displayed the data
                // (The issue might have been resolved during that time.)
                requestedBranch = verify(requestedBotName, requestedBranchName, requestedBranchCl, data.branch)
                if (!requestedBranch) {
                    // No longer valid.
                    return
                } else {
                    doSkip(data, requestedBranchCl, skipText)
                }

            })
        })
    })
}

// This function takes in requested bot/branch/cl combo (usually from query parameters processed earlier) and 
// ensures this is a valid request
function verifySkipRequest(requestedBotName, requestedBranchName, requestedBranchCl, branchDefData) {
    // Verify we got a branch
    if (!branchDefData.def) {
        let errText = `Could not find matching branch for ${requestedBotName}:${requestedBranchName}.`
        if (branchDefData.message) {
            errText += `\n${branchDefData.message}`
        }
        skipFailure(errText)
        return
    }
    let requestedBranch = branchDefData

     // Find conflict in question matching the requested CL
    // First, verify the request branch is paused
    if (!requestedBranch.pause_info) {
        skipFailure(`Branch ${requestedBotName}:${requestedBranchName} not currently paused, no need to skip.`)
        $('#result').append(renderSingleBranchTable(requestedBranch))
        return
    }
    
    // Ensure the current pause is applicable to the CL
    if (requestedBranch.pause_info.change != requestedBranchCl) {
        displayWarningMessage(`Branch ${requestedBotName}:${requestedBranchName} currently paused, but not at requested CL ${requestedBranchCl}. Performing no action.`, false)
        $('#result').append(renderSingleBranchTable(requestedBranch))
        $('#returnbutton').removeClass("invisible")
        return
    }

    return requestedBranch
}

function doSkip(fullData, branchCl, skipText) {
    // Data all verified, time to perform the branch operation.
    let queryData = {
        // If the forced CL is the same as the conflict CL, the conflict will be skipped
        cl: branchCl,
        reason: skipText
    }

    let branchData = fullData.branch

    let botname = branchData.bot
    let branchname = branchData.def.name

    // Increment CL
    branchOp(botname, branchname, '/set_last_cl?' + toQuery(queryData), function(success) {
        if (success) {
            console.log(`${fullData.user.displayName} successfully skipped CL ${branchCl} (${skipText}) for ${botname}:${branchname} `)

            // Unpause
            branchOp(botname, branchname, '/unpause', function(success) {
                if (success) {
                    skipSuccess(`${fullData.user.displayName} successfully skipped CL ${branchCl} (${skipText}), resuming Robomerge for ${botname}:${branchname}.`)
                } else {
                    skipFailure(`${fullData.user.displayName} successfully incremented CL for ${botname}:${branchname} (${skipText}), ` +
                        'but encountered an error unpausing the branch. Alert Robomerge devs.')
                }
                getBranch(botname, branchname, function(data) {
                    $('#result').append(renderSingleBranchTable(data.branch))
                })
            });
            
        } else {
            skipFailure(`${fullData.user.displayName} encountered an error skipping CL ${branchCl} for ${botname}:${branchname} (Reason was: ${skipText})`)
            getBranch(botname, branchname, function(data) {
                $('#result').append(renderSingleBranchTable(data.branch))
            })
        }
    });
    
}

function skipFailure(message) {
    removeOnBeforeUnload()
    $(`<div class="alert alert-danger show" role="alert">`).html(`<strong>ERROR:</strong> ${message}`).appendTo($('#result'))
    $('#returnbutton').removeClass("invisible")
}

function skipSuccess(message) {
    removeOnBeforeUnload()
    $(`<div class="alert alert-success show" role="alert">`).html(`<strong>SUCCESS!</strong> ${message}`).appendTo($('#result'))
    $('#returnbutton').removeClass("invisible")
}