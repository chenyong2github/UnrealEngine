// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.
// Rail error code

#ifndef RAIL_SDK_RAIL_RESULT_H
#define RAIL_SDK_RAIL_RESULT_H

namespace rail {

// error codes of result
enum RailResult {
    kSuccess = 0,                      // success
    kFailure = 1,                      // general failure error code
    kErrorInvalidParam = 2,            // input parameter is invalid
    kErrorImageNotFound = 3,           // image not found
    kErrorBufferTooSmall = 4,          // input buffer is too small
    kErrorNetworkError = 5,            // network is unavailable
    kErrorUnimplemented = 6,           // called interface is not implemented
    kErrorInvalidCustomKey = 7,        // custom key used in game can not start with "rail_" prefix
    kErrorClientInOfflineMode = 8,     // client is in off-line mode, all asynchronous interface
                                       // will return this error code

    kErrorParameterLengthTooLong = 9,        // parameter length is too much long
    kErrorWebApiKeyNoAccessOnThisGame = 10,  // Web API key has no access to this game
    kErrorOperationTimeout = 11,             // operations timeout, might caused by network issues
    kErrorServerResponseInvalid = 12,        // response from server is invalid
    kErrorServerInternalError = 13,          // server internal error

    // rail storage
    kErrorFileNotFound = 1000,                    // cant not find file
    kErrorAccessDenied = 1001,                    // cant access file
    kErrorOpenFileFailed = 1002,                  // cant open file
    kErrorCreateFileFailed = 1003,                // create file failed
    kErrorReadFailed = 1004,                      // read file failed
    kErrorWriteFailed = 1005,                     // write file failed
    kErrorFileDestroyed = 1006,
    kErrorFileDelete = 1007,                      // delete file failed
    kErrorFileQueryIndexOutofRange = 1008,        // param index of GetFileNameAndSize out of range
    kErrorFileAvaiableQuotaMoreThanTotal = 1009,  // cloud file size bigger than quota
    kErrorFileGetRemotePathError = 1010,          // get local cloud path failed when query quota
    kErrorFileIllegal = 1011,
    kErrorStreamFileWriteParamError = 1012,       // passing wrong param to AsyncWrite in StreamFile
    kErrorStreamFileReadParamError = 1013,        // passing wrong param to AsyncRead in StreamFile
    kErrorStreamCloseErrorIOWritting = 1014,      // close writing stream file failed
    kErrorStreamCloseErrorIOReading = 1015,       // close reading stream file failed
    kErrorStreamDeleteFileOpenFileError = 1016,   // open stream file failed when delete stream file
    kErrorStreamRenameFileOpenFileError = 1017,   // open stream file failed when Rename stream file
    kErrorStreamReadOnlyError = 1018,             // write to a stream file when the stream file is
                                                  // read only

    kErrorStreamCreateFileRemoveOldFileError = 1019,  // delete an old stream file when truncate a
                                                      // stream file

    kErrorStreamCreateFileNameNotAvailable = 1020,    // file name size bigger than 256 when open
                                                      // stream file

    kErrorStreamOpenFileErrorCloudStorageDisabledByPlatform = 1021,  // app cloud storage is
                                                                     // disabled

    kErrorStreamOpenFileErrorCloudStorageDisabledByPlayer = 1022,    // player's cloud storage is
                                                                     // disabled

    kErrorStoragePathNotFound = 1023,                 // file path is not available
    kErrorStorageFileCantOpen = 1024,                 // cant open file
    kErrorStorageFileRefuseAccess = 1025,             // cant open file because of access
    kErrorStorageFileInvalidHandle = 1026,            // read or write to a file that file
                                                      // handle is not available
    kErrorStorageFileInUsingByAnotherProcess = 1027,  // cant open file because it's using by
                                                      // another process
    kErrorStorageFileLockedByAnotherProcess = 1028,   // cant lock file because it's locked by
                                                      // another process
    kErrorStorageFileWriteDiskNotEnough = 1029,       // cant write to disk because it's not enough
    kErrorStorageFileCantCreateFileOrPath = 1030,     // path is not available when create file

    // net channel
    kErrorNetChannel = 2000,
    kErrorNetChannelInitializeFailed = 2001,       // net channel initialize is failed
    kErrorNetChannelMemberOnline = 2002,           // member in the same net channel is on-line
    kErrorNetChannelMemberOffline = 2003,          // member in the same net channel is off-line
    kErrorNetChannelServerTimout = 2004,           // server is timeout
    kErrorNetChannelServerException = 2005,        // server is exception
    kErrorNetChannelChannelIsAlreadyExist = 2006,  // net channel is already exist
    kErrorNetChannelChannelIsNotExist = 2007,      // net channel is not exist
    kErrorNetChannelNoAvailableDataToRead = 2008,  // there is not available data to be read

    // room
    kErrorRoomCreateFailed = 3000,        // create room failed
    kErrorKickOffFailed = 3001,           // kickoff failed
    kErrorKickOffPlayerNotInRoom = 3002,  // the player not in room
    kErrorKickOffNotRoomOwner = 3003,     // only the room owner can kick off others
    kErrorKickOffPlayingGame = 3004,      // same game can't kick off player who is playing game
    kErrorRoomServerRequestInvalidData = 3005,     // the request parameter is invalid
    kErrorRoomServerConnectTcaplusFail = 3006,     // the back end server connects db failed
    kErrorRoomServerConnectTcaplusTimeOut = 3007,  // the back end server connects db timeout
    kErrorRoomServerWrongDataInTcaplus = 3008,     // the data in back end db is wrong
    kErrorRoomServerNoDataInTcaplus = 3009,        // no related data found in back end db
    kErrorRoomServerAllocateRoomIdFail = 3010,     // allocate room id failed when creating room
    kErrorRoomServerCreateGroupInImCloudFail = 3011,  // allocate room resource failed when
                                                      // creating room

    kErrorRoomServerUserAlreadyInGame = 3012,       // player already join one room of the game
    kErrorRoomServerQueryResultEmpty = 3013,        // the query result is empty
    kErrorRoomServerRoomFull = 3014,                // the joined room is full
    kErrorRoomServerRoomNotExist = 3015,            // the room doesn't exist
    kErrorRoomServerUserAlreadyInRoom = 3016,       // player already join the room
    kErrorRoomServerZoneFull = 3017,                // the zone is full
    kErrorRoomServerQueryRailIdServiceFail = 3018,  // query rail id failed
    kErrorRoomServerImCloudFail = 3019,             // system error
    kErrorRoomServerPbSerializeFail = 3020,         // system error
    kErrorRoomServerDirtyWord = 3021,               // the input content includes dirty word
    kErrorRoomServerNoPermission = 3022,            // no permission
    kErrorRoomServerLeaveUserNotInRoom = 3023,      // the leaving player is not in the room
    kErrorRoomServerDestroiedRoomNotExist = 3024,   // the room to destroy doesn't exist
    kErrorRoomServerUserIsNotRoomMember = 3025,     // the player is not the room member
    kErrorRoomServerLockFailed = 3026,              // system error
    kErrorRoomServerRouteMiss = 3027,               // system error
    kErrorRoomServerRetry = 3028,                   // need retry
    kErrorRoomSendDataNotImplemented = 3029,        // this method is not supported
    kErrorRoomInvokeFailed = 3030,                  // network error
    kErrorRoomServerPasswordIncorrect = 3031,       // room password doesn't match
    kErrorRoomServerRoomIsNotJoinable = 3032,       // the room is not joinable

    // stats
    kErrorStats = 4000,
    kErrorStatsDontSetOtherPlayerStat = 4001,  // can't set other player's statistics

    // achievement
    kErrorAchievement = 5000,
    kErrorAchievementOutofRange = 5001,        // not any more achievement
    kErrorAchievementNotMyAchievement = 5002,  // can't set other player's achievement

    // leaderboard
    kErrorLeaderboard = 6000,
    kErrorLeaderboardNotExists = 6001,      // leaderboard not exists
    kErrorLeaderboardNotBePrepared = 6002,  // leaderboard not be prepared, call
                                            // IRailLeaderboard::AsyncGetLeaderboard first.
    kErrorLeaderboardCreattionNotSupport = 6003,  // not support AsyncCreateLeaderboard
                                                  // because your game doesn't configure leaderboard

    // assets
    kErrorAssets = 7000,
    kErrorAssetsPending = 7001,                    // assets still in pending
    kErrorAssetsOK = 7002,
    kErrorAssetsExpired = 7003,                    // assets expired
    kErrorAssetsInvalidParam = 7004,               // passing invalid param
    kErrorAssetsServiceUnavailable = 7005,         // service unavailable
    kErrorAssetsLimitExceeded = 7006,              // assets exceed limit
    kErrorAssetsFailed = 7007,
    kErrorAssetsRailIdInvalid = 7008,              // rail id invalid
    kErrorAssetsGameIdInvalid = 7009,              // game id invalid
    kErrorAssetsRequestInvokeFailed = 7010,        // request failed
    kErrorAssetsUpdateConsumeProgressNull = 7011,  // progress is null when update consume progress
    kErrorAssetsCanNotFindAssetId = 7012,     // cant file asset when do split or exchange or merge
    kErrorAssetInvalidRequest = 7013,         // invalid request
    kErrorAssetDBFail = 7014,                 // query db failed in server back end
    kErrorAssetDataTooOld = 7015,             // local asset data too old
    kErrorAssetInConsume = 7016,              // asset still in consuming
    kErrorAssetNotExist = 7017,               // asset not exist
    kErrorAssetExchangNotMatch = 7018,        // asset exchange not match
    kErrorAssetSystemError = 7019,            // asset system error back end
    kErrorAssetBadJasonData = 7020,
    kErrorAssetStateNotConsuing = 7021,       // asset state is not consuming
    kErrorAssetStateConsuing = 7022,          // asset state is consuming
    kErrorAssetDifferentProductId = 7023,     // exchange asset error with different product
    kErrorAssetConsumeQuantityTooBig = 7024,  // consume quantity bigger than exists
    kErrorAssetMissMatchRailId = 7025,        // rail id miss match the serialized buffer
    kErrorAssetProductInfoNotReady = 7026,    // IRailInGamePurchase::AsyncRequestAllProducts should
                                              // be called to cache product info to local memory

    // item purchase
    kErrorInGamePurchase = 8000,
    kErrorInGamePurchaseProductInfoExpired = 8001,  // product information in client is expired
    kErrorInGamePurchaseAcquireSessionTicketFailed = 8002,    // acquire session ticket failed
    kErrorInGamePurchaseParseWebContentFaild = 8003,          // parse product information from web
                                                              // content failed

    kErrorInGamePurchaseProductIsNotExist = 8004,             // product information is not exist
    kErrorInGamePurchaseOrderIDIsNotExist = 8005,             // order id is not exist
    kErrorInGamePurchasePreparePaymentRequestTimeout = 8006,  // prepare payment request timeout
    kErrorInGamePurchaseCreateOrderFailed = 8007,             // create order failed
    kErrorInGamePurchaseQueryOrderFailed = 8008,              // query order failed
    kErrorInGamePurchaseFinishOrderFailed = 8009,             // finish order failed
    kErrorInGamePurchasePaymentFailed = 8010,                 // payment is failed
    kErrorInGamePurchasePaymentCancle = 8011,                 // payment is canceled
    kErrorInGamePurchaseCreatePaymentBrowserFailed = 8012,    // create payment browser failed

    // in-game store
    kErrorInGameStorePurchase = 8500,
    kErrorInGameStorePurchasePaymentSuccess = 8501,  // payment is success
    kErrorInGameStorePurchasePaymentFailure = 8502,  // payment is failed
    kErrorInGameStorePurchasePaymentCancle = 8503,   // payment is canceled

    // player
    kErrorPlayer = 9000,
    kErrorPlayerUserFolderCreateFailed = 9001,     // create user data folder failed
    kErrorPlayerUserFolderCanntFind = 9002,        // can't find user data folder
    kErrorPlayerUserNotFriend = 9003,              // player is not friend
    kErrorPlayerGameNotSupportPurchaseKey = 9004,  // not support purchase key
    kErrorPlayerGetAuthenticateURLFailed = 9005,   // get authenticate url failed
    kErrorPlayerGetAuthenticateURLServerError = 9006,  // server error while get authenticate url
    kErrorPlayerGetAuthenticateURLInvalidURL = 9007,   // input url is not in the white list

    // friends
    kErrorFriends = 10000,
    kErrorFriendsKeyFrontUseRail = 10001,
    kErrorFriendsMetadataSizeInvalid = 10002,        // the size of key_values is more than 50
    kErrorFriendsMetadataKeyLenInvalid = 10003,      // the length of key is more than 256
    kErrorFriendsMetadataValueLenInvalid = 10004,    // the length of Value is more than 256
    kErrorFriendsMetadataKeyInvalid = 10005,         // user's key name can not start with "rail"
    kErrorFriendsGetMetadataFailed = 10006,          // CommonKeyValueNode's error_code is not 0
    kErrorFriendsSetPlayTogetherSizeZero = 10007,    // player_list count is 0
    kErrorFriendsSetPlayTogetherContentSizeInvalid = 10008,  // the size of user_rich_content is
                                                             // more than 100
    kErrorFriendsInviteResponseTypeInvalid = 10009,  // the invite result is invalid
    kErrorFriendsListUpdateFailed = 10010,           // the friend_list update failed
    kErrorFriendsAddFriendInvalidID = 10011,         // Request sent failed by invalid rail_id
    kErrorFriendsAddFriendNetworkError = 10012,      // Request sent failed by network error
    kErrorFriendsServerBusy = 10013,                 // server is busy. you could process the
                                                     // same handle later
    kErrorFriendsUpdateFriendsListTooFrequent = 10014,  // update friends list too frequent

    // session ticket
    kErrorSessionTicket = 11000,
    kErrorSessionTicketGetTicketFailed = 11001,      // get session ticket failed
    kErrorSessionTicketAuthFailed = 11002,           // authenticate the session ticket failed
    kErrorSessionTicketAuthTicketAbandoned = 11003,  // the session ticket is abandoned
    kErrorSessionTicketAuthTicketExpire = 11004,     // the session ticket expired
    kErrorSessionTicketAuthTicketInvalid = 11005,    // the session ticket is invalid

    // session ticket webAPI
    kErrorSessionTicketInvalidParameter = 11500,      // the request parameter is invalid
    kErrorSessionTicketInvalidTicket = 11501,         // invalid session ticket
    kErrorSessionTicketIncorrectTicketOwner = 11502,  // the session ticket owner is not correct
    kErrorSessionTicketHasBeenCanceledByTicketOwner = 11503,  // the session ticket has been
                                                              // canceled by owner

    kErrorSessionTicketExpired = 11504,               // the session ticket expired

    // float window
    kErrorFloatWindow = 12000,
    kErrorFloatWindowInitFailed = 12001,                    // initialize is failed
    kErrorFloatWindowShowStoreInvalidPara = 12002,          // input parameter is invalid
    kErrorFloatWindowShowStoreCreateBrowserFailed = 12003,  // create store browser window failed

    // user space
    kErrorUserSpace = 13000,
    kErrorUserSpaceGetWorkDetailFailed = 13001,      // unable to query spacework's data
    kErrorUserSpaceDownloadError = 13002,       // failed to download at least one file of spacework
    kErrorUserSpaceDescFileInvalid = 13003,     // spacework maybe broken, re-upload it to repair
    kErrorUserSpaceReplaceOldFileFailed = 13004,     // cannot update disk using download files
    kErrorUserSpaceUserCancelSync = 13005,           // user canceled the sync
    kErrorUserSpaceIDorUserdataPathInvalid = 13006,  // internal error
    kErrorUserSpaceNoData = 13007,                   // there is no data in such field
    kErrorUserSpaceSpaceWorkIDInvalid = 13008,       // use 0 as spacework id
    kErrorUserSpaceNoSyncingNow = 13009,             // there is no syncing to cancel
    kErrorUserSpaceSpaceWorkAlreadySyncing = 13010,  // only one syncing is allowed to the
                                                     // same spacework

    kErrorUserSpaceSubscribePartialSuccess = 13011,  // not all (un)subscribe operations success
    kErrorUserSpaceNoVersionField = 13012,  // missing version field when changing files
                                            // for spacework

    kErrorUserSpaceUpdateFailedWhenUploading = 13013,  // can not query spacework's data when
                                                       // uploading, the spacework may not exist

    kErrorUserSpaceGetTicketFailed = 13014,  // can not get ticket when uploading, usually
                                             // network issues

    kErrorUserSpaceVersionOccupied = 13015,  // new version value must not equal to the last version
    kErrorUserSpaceCallCreateMethodFailed = 13016,  // facing network issues when create a spacework
    kErrorUserSpaceCreateMethodRspFailed = 13017,   // server failed to create a spacework
    kErrorUserSpaceGenerateDescFileFailed = 13018,  // deprecated
    kErrorUserSpaceUploadFailed = 13019,            // deprecated
    kErrorUserSpaceNoEditablePermission = 13020,  // you have no permissions to
                                                  // change this spacework

    kErrorUserSpaceCallEditMethodFailed = 13021,  // facing network issues when committing
                                                  // changes of a spacework

    kErrorUserSpaceEditMethodRspFailed = 13022,    // server failed to commit changes of a spacework
    kErrorUserSpaceMetadataHasInvalidKey = 13023,  // the key of metadata should not start
                                                   // with Rail_(case insensitive)

    kErrorUserSpaceModifyFavoritePartialSuccess = 13024,  // not all (un)favorite operations success
    kErrorUserSpaceFilePathTooLong = 13025,  // the path of file is too long to upload or download
    kErrorUserSpaceInvalidContentFolder = 13026,  // the content folder provided is invalid,
                                                  // check the folder path is exist

    kErrorUserSpaceInvalidFilePath = 13027,  // internal error, the upload file path is invalid
    kErrorUserSpaceUploadFileNotMeetLimit = 13028,  // file to be uploaded don't meet requirements,
                                                    // such as size and format and so on

    kErrorUserSpaceCannotReadFileToBeUploaded = 13029,  // can not read the file need to be uploaded
                                                        // technically won't happen in updating a
                                                        // existing spacework, check whether the
                                                        // file is occupied by other programs

    kErrorUserSpaceUploadSpaceWorkHasNoVersionField = 13030,  // usually network issues, try again
    kErrorUserSpaceDownloadCurrentDescFileFailed = 13031,  // download current version's
                                                           // description file failed when
                                                           // no file changed in the new version,
                                                           // call StartSync again when
                                                           // facing it in creating spacework

    kErrorUserSpaceCannotGetSpaceWorkDownloadUrl = 13032,  // can not get the download url of
                                                           // spacework, this spacework maybe broken

    kErrorUserSpaceCannotGetSpaceWorkUploadUrl = 13033,  // can not get the upload url of spacework,
                                                         // spacework maybe broken

    kErrorUserSpaceCannotReadFileWhenUploading = 13034,  // can not read file when uploading,
                                                         // make sure you haven't changed the file
                                                         // when uploading

    kErrorUserSpaceUploadFileTooLarge = 13035,           // file uploaded should be smaller than
                                                         // 2^53 - 1 bytes

    kErrorUserSpaceUploadRequestTimeout = 13036,         // upload http request time out,
                                                         // check your network connections

    kErrorUserSpaceUploadRequestFailed = 13037,          // upload file failed
    kErrorUserSpaceUploadInternalError = 13038,          // internal error
    kErrorUserSpaceUploadCloudServerError = 13039,       // get error from cloud server or
                                                         // can not get needed data from response

    kErrorUserSpaceUploadCloudServerRspInvalid = 13040,  // cloud server response invalid data
    kErrorUserSpaceUploadCopyNoExistCloudFile = 13041,   // reuse the old version's files need copy
                                                         // them to new version location,
                                                         // the old version file is not exist,
                                                         // it may be cleaned by server
                                                         // if not used for a long time,
                                                         // please re-upload the full content folder

    kErrorUserSpaceShareLevelNotSatisfied = 13042,       // there are some limits of this type
    kErrorUserSpaceHasntBeenApproved = 13043,            // the spacework has been submit with
                                                         // public share level and hasn't been
                                                         // approved or rejected so you can't submit
                                                         // again until it is approved or rejected

    // game server
    kErrorGameServer = 14000,
    kErrorGameServerCreateFailed = 14001,              // create game server failed
    kErrorGameServerDisconnectedServerlist = 14002,    // the game server disconnects from
                                                       // game server list
    kErrorGameServerConnectServerlistFailure = 14003,  // report game server to game server
                                                       // list failed
    kErrorGameServerSetMetadataFailed = 14004,     // set game server meta data failed
    kErrorGameServerGetMetadataFailed = 14005,     // get game server meta data failed
    kErrorGameServerGetServerListFailed = 14006,   // query game server list failed
    kErrorGameServerGetPlayerListFailed = 14007,   // query game server player list failed
    kErrorGameServerPlayerNotJoinGameserver = 14008,
    kErrorGameServerNeedGetFovariteFirst = 14009,  // should get favorite list first
    kErrorGameServerAddFovariteFailed = 14010,     // add game server to favorite list failed
    kErrorGameServerRemoveFovariteFailed = 14011,  // remove game server from favorite list failed

    // network
    kErrorNetwork = 15000,
    kErrorNetworkInitializeFailed = 15001,       // initialize is failed
    kErrorNetworkSessionIsNotExist = 15002,      // session is not exist
    kErrorNetworkNoAvailableDataToRead = 15003,  // there is not available data to be read
    kErrorNetworkUnReachable = 15004,            // network is unreachable
    kErrorNetworkRemotePeerOffline = 15005,      // remote peer is offline
    kErrorNetworkServerUnavailabe = 15006,       // network server is unavailable
    kErrorNetworkConnectionDenied = 15007,       // connect request is denied
    kErrorNetworkConnectionClosed = 15008,       // connected session has been closed by remote peer
    kErrorNetworkConnectionReset = 15009,        // connected session has been reset
    kErrorNetworkSendDataSizeTooLarge = 15010,   // send data size is too big
    kErrorNetworkSessioNotRegistered = 15011,    // remote peer does not register to server
    kErrorNetworkSessionTimeout = 15012,         // remote register but no response

    // Dlc error code
    kErrorDlc = 16000,
    kErrorDlcInstallFailed = 16001,         // install dlc failed
    kErrorDlcUninstallFailed = 16002,       // uninstall dlc failed
    kErrorDlcGetDlcListTimeout = 16003,     // deprecated
    kErrorDlcRequestInvokeFailed = 16004,   // request failed when query dlc authority
    kErrorDlcRequestToofrequently = 16005,  // request too frequently when query dlc authority

    // utils
    kErrorUtils = 17000,
    kErrorUtilsImagePathNull = 17001,                 // the image path is null
    kErrorUtilsImagePathInvalid = 17002,              // the image path is invalid
    kErrorUtilsImageDownloadFail = 17003,             // failed to download the image
    kErrorUtilsImageOpenLocalFail = 17004,            // failed to open local image file
    kErrorUtilsImageBufferAllocateFail = 17005,       // failed to allocate image buffer
    kErrorUtilsImageReadLocalFail = 17006,            // failed to read local image
    kErrorUtilsImageParseFail = 17007,                // failed parse the image
    kErrorUtilsImageScaleFail = 17008,                // failed to scale the image
    kErrorUtilsImageUnknownFormat = 17009,            // image image format is unknown
    kErrorUtilsImageNotNeedResize = 17010,            // the image is not need to resize
    kErrorUtilsImageResizeParameterInvalid = 17011,   // the parameter used to resize image
                                                      // is invalid
    kErrorUtilsImageSaveFileFail = 17012,             // could not save image
    kErrorUtilsDirtyWordsFilterTooManyInput = 17013,  // there are too many inputs for dirty
                                                      // words filter
    kErrorUtilsDirtyWordsHasInvalidString = 17014,    // there are invalid strings in the
                                                      // dirty words
    kErrorUtilsDirtyWordsNotReady = 17015,      // dirty words utility is not ready
    kErrorUtilsDirtyWordsDllUnloaded = 17016,   // dirty words library is not loaded
    kErrorUtilsCrashAllocateFailed = 17017,     // crash report buffer can not be allocated
    kErrorUtilsCrashCallbackSwitchOff = 17018,  // crash report callback switch is currently off

    // users
    kErrorUsers = 18000,
    kErrorUsersInvalidInviteCommandLine = 18001,  // the invite command line provided is invalid
    kErrorUsersSetCommandLineFailed = 18002,      // failed to set command line
    kErrorUsersInviteListEmpty = 18003,           // the invite user list is empty
    kErrorUsersGenerateRequestFail = 18004,       // failed to generate invite request
    kErrorUsersUnknownInviteType = 18005,         // the invite type provided is unknown
    kErrorUsersInvalidInviteUsersSize = 18006,    // the user count to invite is invalid

    // screenshot
    kErrorScreenshot = 19000,
    kErrorScreenshotWorkNotExist = 19001,    // create space work for the screenshot failed
    kErrorScreenshotCantConvertPng = 19002,  // convert the screenshot image to png format failed
    kErrorScreenshotCopyFileFailed = 19003,  // copy the screenshot image to publish folder failed
    kErrorScreenshotCantCreateThumbnail = 19004,  // create a thumbnail image for screenshot failed

    // voice capture
    kErrorVoiceCapture = 20000,
    kErrorVoiceCaptureInitializeFailed = 20001,   // initialized failed
    kErrorVoiceCaptureDeviceLost = 20002,         // voice device lost
    kErrorVoiceCaptureIsRecording = 20003,        // is already recording
    kErrorVoiceCaptureNotRecording = 20004,       // is not recording
    kErrorVoiceCaptureNoData = 20005,             // currently no voice data to get
    kErrorVoiceCaptureMoreData = 20006,           // there is more data to get
    kErrorVoiceCaptureDataCorrupted = 20007,      // illegal data captured
    kErrorVoiceCapturekUnsupportedCodec = 20008,  // illegal data to decode
    kErrorVoiceChannelHelperNotReady = 20009,     // voice module is not ready now, try again later
    kErrorVoiceChannelIsBusy = 20010,             // voice channel is too busy to handle operation,
                                                  // try again later or slow down the operations

    kErrorVoiceChannelNotJoinedChannel = 20011,   // player haven't joined this channel, you can not
                                                  // do some operations on this channel like leave

    kErrorVoiceChannelLostConnection = 20012,     // lost connection to server now,
                                                  // sdk will automatically reconnect later

    kErrorVoiceChannelAlreadyJoinedAnotherChannel = 20013,  // player could only join one channel
                                                            // at the same time

    kErrorVoiceChannelPartialSuccess = 20014,      // operation is not fully success
    kErrorVoiceChannelNotTheChannelOwner = 20015,  // only the owner could remove users

    // text input
    kErrorTextInputTextInputSendMessageToPlatformFailed = 21000,
    kErrorTextInputTextInputSendMessageToOverlayFailed = 21001,
    kErrorTextInputTextInputUserCanceled = 21002,
    kErrorTextInputTextInputEnableChineseFailed = 21003,
    kErrorTextInputTextInputShowFailed = 21004,
    kErrorTextInputEnableIMEHelperTextInputWindowFailed = 21005,

    // app error
    kErrorApps = 22000,
    kErrorAppsCountingKeyExists = 22001,
    kErrorAppsCountingKeyDoesNotExist = 22002,

    // http session errro
    kErrorHttpSession = 23000,
    kErrorHttpSessionPostBodyContentConflictWithPostParameter = 23001,
    kErrorHttpSessionRequestMehotdNotPost = 23002,

    // small object service
    kErrorSmallObjectService = 24000,
    kErrorSmallObjectServiceObjectNotExist = 24001,
    kErrorSmallObjectServiceFailedToRequestDownload = 24002,
    kErrorSmallObjectServiceDownloadFailed = 24003,
    kErrorSmallObjectServiceFailedToWriteDisk = 24004,
    kErrorSmallObjectServiceFailedToUpdateObject = 24005,
    kErrorSmallObjectServicePartialDownloadSuccess = 24006,
    kErrorSmallObjectServiceObjectNetworkIssue = 24007,
    kErrorSmallObjectServiceObjectServerError = 24008,
    kErrorSmallObjectServiceInvalidBranch = 24009,

    // zone server
    kErrorZoneServer = 25000,
    kErrorZoneServerValueDataIsNotExist = 25001,

    // --------------sdk error code end----------------

    // --------------rail server error code begin----------------
    // RAIL_ERROR_SERVER_BEGIN = 2000000;
    // RAIL_ERROR_SERVER_END = 2999999;
    kRailErrorServerBegin = 2000000,

    // Payment Web-API error code
    kErrorPaymentSystem = 2080001,
    kErrorPaymentParameterIlleage = 2080008,
    kErrorPaymentOrderIlleage = 2080011,

    // Assets Web-API error code
    kErrorAssetsInvalidParameter = 2230001,
    kErrorAssetsSystemError = 2230007,

    // Dirty words filter Web-API error code
    kErrorDirtyWordsFilterNoPermission = 2290028,
    kErrorDirtyWordsFilterCheckFailed = 2290029,
    kErrorDirtyWordsFilterSystemBusy = 2290030,

    // Web-API error code for inner server
    kRailErrorInnerServerBegin = 2500000,

    // GameGray Web-API error code
    kErrorGameGrayCheckSnowError = 2500001,
    kErrorGameGrayParameterIlleage = 2500002,
    kErrorGameGraySystemError = 2500003,
    kErrorGameGrayQQToWegameidError = 2500004,

    kRailErrorInnerServerEnd = 2699999,

    // last rail server error code
    kRailErrorServerEnd = 2999999,
    // --------------rail server error code end----------------

    kErrorUnknown = 0xFFFFFFFF,  // unknown error
};

}  // namespace rail

#endif  // RAIL_SDK_RAIL_RESULT_H

