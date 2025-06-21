#ifndef MC_COMMANDS_HEADER
#define MC_COMMANDS_HEADER

// copied from https://www.jriver.com/DevZone/MCCommands.h

/***********************************************************************************
Media Core Commands (for Media Center 9.x and later)
Copyright (c) 2003-2018 JRiver, Inc. -- All Rights Reserved.

Each command has a "what command" (i.e. MCC_PLAY_PAUSE) and also an optional parameter,
which is explained by the comment following the command.  If you don't use the parameter,
set it to '0'.

Both parts are numbers. To determine what number a command is, count up from the command above
it with a number.

Example (1): MCC_PLAY_PAUSE = 10000; MCC_PLAY = 10001; MCC_STOP = 10002; etc.
Example (2): MCC_OPEN_FILE = 20000; MCC_OPEN_URL = 20001; etc.

Note: Some commands may only work with the latest version of Media Center.
***********************************************************************************/

enum MC_COMMANDS
{
    MCC_FIRST = 10000,

    ///////////////////////////////////////////////////////////////////////////////
    // Playback (range 10,000 to 20,000)
    // 
    // To issue playback commands to a specific zone, mask these values with the parameter:
    // Current Zone: 0
    // Zone Index 0: 16777216 (or 0x1000000 hex)
    // Zone Index 1: 33554432 (or 0x2000000 hex)
    // Zone Index 2: 50331648 (or 0x3000000 hex)
    // Zone Index 3: 67108864 (or 0x4000000 hex)
    // Zone Index 4: 83886080 (or 0x5000000 hex)
    // Zone Index 5: 100663296 (or 0x6000000 hex)
    // etc... (keep adding 16777216 (or 2^24)) (up to Zone Index 31)
    //
    // for the geeks, this is the top 6 bits of the 32-bit parameter
    // the lower 24 bits are used for the rest of the parameter (see the C++ macros below if you like)
    // if bit 32 is set, we assume someone passed in a simple negative number, so discard the zone portion
    // 
    // for parameters >= 0: zone number + parameter
    // for parameters < 0: zone number + (16777216 + parameter)
    // example: parameter -1 to zone 3: 67108864 + (16777216 + -1) = 83886079
    ///////////////////////////////////////////////////////////////////////////////
    MCC_PLAYBACK_SECTION = 10000,
    MCC_PLAY_PAUSE = 10000,                        // [ignore]
    MCC_PLAY,                                      // [ignore]
    MCC_STOP,                                      // [bool bDisplayError]
    MCC_NEXT,                                      // [int nFlags (1: bNotActualNext, 2: bNoChapters)]
    MCC_PREVIOUS,                                  // [int nFlags (1: reserved, 2: bNoChapters, 4: no seek to beginning)]
    MCC_SHUFFLE,                                   // [0: toggle shuffle; 1: shuffle, jump to PN; 2: shuffle, no jump; 3..5: set mode]
    MCC_CONTINUOUS,                                // [0: toggle continuous; 1: off; 2: playlist; 3: song; 4: stop after each]
    MCC_OBSOLETE_10007,                            // [ignore]
    MCC_FAST_FORWARD,                              // [int nRate]
    MCC_REWIND,                                    // [int nRate]
    MCC_STOP_CONDITIONAL,                          // [ignore]
    MCC_SET_ZONE,                                  // [int nZoneIndex (-1 toggles forward, -2 toggles backwards)]
    MCC_TOGGLE_DISPLAY,                            // [bool bExcludeTheaterView]
    MCC_SHOW_WINDOW,                               // [bool bJumpToPlayingNow]
    MCC_MINIMIZE_WINDOW,                           // [ignore]
    MCC_PLAY_CPLDB_INDEX,                          // [int nIndex]
    MCC_SHOW_DSP_STUDIO,                           // [ignore]
    MCC_VOLUME_MUTE,                               // [0: toggle; 1: mute; 2: unmute]
    MCC_VOLUME_UP,                                 // [int nDeltaPercent]
    MCC_VOLUME_DOWN,                               // [int nDeltaPercent]
    MCC_VOLUME_SET,                                // [int nPercent]
    MCC_SHOW_PLAYBACK_OPTIONS,                     // [ignore]
    MCC_SET_PAUSE,                                 // [bool bPause (-1 toggles)]
    MCC_SET_CURRENTLY_PLAYING_RATING,              // [int nRating (0 means ?)]
    MCC_SHOW_PLAYBACK_ENGINE_MENU,                 // [screen point (loword: x, hiword: y) -- must send directly]
    MCC_PLAY_NEXT_PLAYLIST,                        // [ignore]
    MCC_PLAY_PREVIOUS_PLAYLIST,                    // [ignore]
    MCC_MAXIMIZE_WINDOW,                           // [ignore]
    MCC_RESTORE_WINDOW,                            // [ignore]
    MCC_SET_PLAYERSTATUS,                          // [PLAYER_STATUS_CODES Code]
    MCC_SET_ALTERNATE_PLAYBACK_SETTINGS,           // [bool bAlternateSettings (-1 toggles)]
    MCC_SET_PREVIEW_MODE_SETTINGS,                 // [low 12 bits: int nDurationSeconds, high 12 bits: int nStartSeconds]
    MCC_SHOW_PLAYBACK_ENGINE_DISPLAY_PLUGIN_MENU,  // [screen point (loword: x, hiword: y) -- must send directly]
    MCC_DVD_MENU,                                  // [ignore]
    MCC_SEEK_FORWARD,                              // [int nMilliseconds (0 means default -- varies depending on playback type)]
    MCC_SEEK_BACK,                                 // [int nMilliseconds (0 means default -- varies depending on playback type)]
    MCC_STOP_AFTER_CURRENT_FILE,                   // [bool bStopAfterCurrentFile (-1 toggles)]
    MCC_DETACH_DISPLAY,                            // [bool bDetach (-1 toggles)]
    MCC_SET_MODE_ZONE_SPECIFIC,                    // [UI_MODES Mode]
    MCC_STOP_INTERNAL,                             // [ignore]
    MCC_PLAYING_NOW_REMOVE_DUPLICATES,             // [ignore]
    MCC_SHUFFLE_REMAINING,                         // [ignore]
    MCC_PLAY_FIRST_FILE,                           // [ignore]
    MCC_PLAY_LAST_FILE,                            // [ignore]
    MCC_PLAY_FILE_BY_STRING,                       // [BSTR bstrFile (deleted by receiver)]
    MCC_PLAY_FILE_AGAIN,                           // [ignore]
    MCC_HANDLE_PLAYBACK_ERROR,                     // [ignore]
    MCC_PLAY_AUTOMATIC_PLAYLIST,                   // [BSTR bstrSeed (deleted by receiver)]
    MCC_SEEK,                                      // [int nPositionMilliseconds]
    MCC_CLEAR_PLAYING_NOW_ZONE_SPECIFIC,           // [0: all files; 1: leave playing file]
    MCC_PLAY_RADIO_LAST_FM,                        // [ignore]
    MCC_SHOW_ON_SCREEN_DISPLAY,                    // [0: position bar]
    MCC_SET_SUBTITLES,                             // [int nIndex (-1 toggles forward, -2 toggles backwards, -3 browses)]
    MCC_SET_AUDIO_STREAM,                          // [int nIndex (-1 toggles forward, -2 toggles backwards)]
    MCC_SET_VIDEO_STREAM,                          // [int nIndex (-1 toggles forward, -2 toggles backwards)]
    MCC_VIDEO_SCREEN_GRAB,                         // [0: use as thumbnail; 1: save as external file]
    MCC_SET_VOLUME_MODE,                           // [int nMode (internal type EPlaybackVolumeModes) (0: application, 1: internal; 2: system; 3: disabled)]
    MCC_RESTART_PLAYBACK,                          // [ignore]
    MCC_ZONE_SWITCH,                               // [ignore]
    MCC_SKIP_TO,                                   // [SKIP_TO_MODES Mode]
    MCC_LINK_ZONE,                                 // [int nZoneID]
    MCC_UNLINK_ZONE,                               // [ignore]
    MCC_PLAY_RADIO_MEDIANET,                       // [ignore]
    MCC_CLEAR_PLAYED_ZONE_SPECIFIC,                // [ignore]
    MCC_PLAY_LAST_TV_CHANNEL,                      // [ignore]
    MCC_BLURAY_POPUP_MENU,                         // [ignore]
    MCC_PLAY_SYNCED_CPLDB_INDEX,                   // [int nIndex]
    MCC_STOP_AFTER_DELAY,                          // [int nMinutes (negative value for hard stop) (-1000 to reset)]  
    MCC_STOP_AFTER_TRACKS,                         // [int nNumberTracks (negative one to reset)]
    MCC_PLAY_SELECTED,                             // [0: play replace, 1: append, 2: play next]
    MCC_PLAY_AUTO_PLAYLIST_CLOUD,                  // [BSTR bstrSeed (deleted by receiver)]
    MCC_CLEAR_REMAINING_ZONE_SPECIFIC,             // [ignore]
    MCC_SEEK_PERCENT,                              // [int nPercent]
    MCC_SHUFFLE_ALBUMS,                            // [ignore]
    MCC_JRVR_PROFILE_OUTPUT,                       // [int ProfileID]
    MCC_JRVR_PROFILE_SCALING,                      // [int ProfileID]
    MCC_JRVR_PROFILE_FILTERING,                    // [int ProfileID]
    MCC_JRVR_PROFILE_ADVANCED,                     // [int ProfileID]
    MCC_CLOSE_PROGRAM_AFTER_STOP,                  // [0: no, 1: yes, -1: toggle]
    MCC_ZONE_SWITCH_ENABLE,                        // [0: no, 1: yes, -1: toggle]
    MCC_JRVR_DEFAULT_PROFILE_OUTPUT,               // [int ProfileID]
    MCC_JRVR_DEFAULT_PROFILE_SCALING,              // [int ProfileID]
    MCC_JRVR_DEFAULT_PROFILE_FILTERING,            // [int ProfileID]
    MCC_JRVR_DEFAULT_PROFILE_ADVANCED,             // [int ProfileID]
    MCC_HIDE_DSP_STUDIO,                           // [ignore]
    MCC_SET_DSP_STUDIO_PLAYBACK_RATE,              // [Non-zero integers for percent of normal speed, negative values mute audio]
    MCC_RESTART_PLAYBACK_WITH_RATE,                // [new playback rate]

    ///////////////////////////////////////////////////////////////////////////////
    // File (range 20,000 to 21,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_FILE_SECTION = 20000,
    MCC_OPEN_FILE = 20000,                         // [ignore]
    MCC_OPEN_URL,                                  // [ignore]
    MCC_PRINT_LIST,                                // [ignore]
    MCC_EXPORT_PLAYLIST,                           // [int nPlaylistID (-1 for active view)]
    MCC_EXPORT_ALL_PLAYLISTS,                      // [1: silent M3U, 2: silent MPL]
    MCC_UPLOAD_FILES,                              // [ignore]
    MCC_EMAIL_FILES,                               // [ignore]
    MCC_EXIT,                                      // [int nMode (0: normal, 1: force close (close media server), 2: force close (allow media server))]
    MCC_UPDATE_LIBRARY,                            // [ignore]
    MCC_CLEAR_LIBRARY,                             // [ignore]
    MCC_EXPORT_LIBRARY,                            // [ignore]
    MCC_BACKUP_LIBRARY,                            // [int nMode (0: normal, 1: silent automatic backup)]
    MCC_RESTORE_LIBRARY,                           // [ignore]
    MCC_LIBRARY_MANAGER,                           // [ignore]
    MCC_IMAGE_ACQUIRE,                             // [ignore]
    MCC_PRINT_IMAGES,                              // [MFKEY nKey (-1 for selected files)]
    MCC_PRINT,                                     // [ignore]
    MCC_OBSOLETE_20017,                            // [ignore]
    MCC_OBSOLETE_20018,                            // [ignore]
    MCC_OBSOLETE_20019,                            // [ignore]
    MCC_OBSOLETE_20020,                            // [ignore]
    MCC_OBSOLETE_20021,                            // [ignore]
    MCC_OBSOLETE_20022,                            // [ignore]
    MCC_OBSOLETE_20023,                            // [ignore]
    MCC_IMPORT_PLAYLIST,                           // [ignore]
    MCC_LOAD_LIBRARY,                              // [int nLibraryIndex]
    MCC_SYNC_LIBRARY,                              // [ignore]
    MCC_EMAIL_PODCAST_FEED,                        // [ignore]
    MCC_LOAD_LIBRARY_READ_ONLY,                    // [int nLibraryIndex]
    MCC_ADD_LIBRARY,                               // [ignore]
    MCC_EXPORT_ITUNES,                             // [ignore]
    MCC_DISCONNECT_LIBRARY,                        // [ignore]
    MCC_SYNC_WITH_LIBRARY_SERVER,                  // [bool bSilent]
    MCC_STOP_ALL_ZONES,                            // [bool bStopRemoteZones]
    MCC_CLONE_LIBRARY,                             // [int nLibraryIndex]
    MCC_OPEN_LIVE,                                 // [ignore]
    MCC_PLAY_RADIO_PARADISE,                       // [ignore]
    MCC_IMPORT_ITUNES,                             // [ignore]
    MCC_PLAY_RADIO_JRIVER,                         // [int nStationNumber]
    MCC_EXPORT_ALL_TO_ITUNES,                      // [ignore]
    MCC_IMPORT_ITUNES_DATABASE,                    // [ignore]
    MCC_SET_CROSS_PLATFORM_RULES,                  // [ignore]
    MCC_DOWNLOAD_FROM_LIBRARY_SERVER,              // [ignore]
    MCC_DOWNLOAD_LIBRARY_FROM_LIBRARY_SERVER,      // [ignore]
    MCC_MIGRATE_LIBRARY_WIZARD,                    // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // Edit (range 21,000 to 22,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_EDIT_SECTION = 21000,
    MCC_COPY = 21000,                              // [ignore]
    MCC_PASTE,                                     // [ignore]
    MCC_SELECT_ALL,                                // [ignore]
    MCC_SELECT_INVERT,                             // [ignore]
    MCC_DELETE,                                    // [bool bAggressive]
    MCC_RENAME,                                    // [ignore]
    MCC_UNDO,                                      // [ignore]
    MCC_REDO,                                      // [ignore]
    MCC_QUICK_SEARCH,                              // [bool bRepeatLastSearch]
    MCC_ADD_PLAYLIST,                              // [MEDIAFILE_INFO_ARRAY * paryFiles = NULL]
    MCC_ADD_SMARTLIST,                             // [ignore]
    MCC_ADD_PLAYLIST_GROUP,                        // [ignore]
    MCC_PROPERTIES,                                // [MEDIAFILE_INFO_ARRAY * paryFiles = NULL (-1 toggles) (note: never PostMessage(...) a pointer)]
    MCC_TOGGLE_TAGGING_MODE,                       // [ignore]
    MCC_CUT,                                       // [ignore]
    MCC_DESELECT_ALL,                              // [ignore]
    MCC_DELETE_ALL,                                // [bool bAggressive]
    MCC_ADD_PODCAST_FEED,                          // [ignore]
    MCC_EDIT_PODCAST_FEED,                         // [ignore]
    MCC_ADD_PODCAST_DEFAULTS,                      // [ignore]
    MCC_CREATE_STOCK_SMARTLISTS,                   // [ignore]
    MCC_ENABLE_PODCAST_DOWNLOAD,                   // [ignore]
    MCC_DISABLE_PODCAST_DOWNLOAD,                  // [ignore]
    MCC_EDIT_PLAYLIST,                             // [ignore]
    MCC_EDIT_PLAYING_NOW,                          // [int nZoneID]
    MCC_EDIT_DISC_INFORMATION,                     // [ignore]
    MCC_EDIT_SMARTLIST,                            // [int nPlaylistID]
    MCC_REFRESH_PODCAST_FEED,                      // [ignore]
    MCC_LOOKUP_MOVIE_INFORMATION,                  // [ignore]
    MCC_ADD_ZONE,                                  // [ignore]
    MCC_ADD_AUTOMATIC_PLAYLIST,                    // [ignore]
    MCC_SET_WRITE_TAGS,                            // [bool bWriteTags (-1 toggles)]
    MCC_PASTE_TAGS,                                // [ignore]
    MCC_SHUFFLE_SELECTION,                         // [ignore]
    MCC_CLOSE_QUICK_SEARCH,                        // [ignore]
    MCC_ADD_ZONE_GROUP,                            // [ignore]
    MCC_COMBINE,                                   // [int nPlaylistID]
    MCC_ADD_DYNAMIC_ZONE,                          // [ignore]
    MCC_PLAYLIST_SEARCH,                           // [ignore]
    MCC_DUPLICATE,                                 // [ignore]
    MCC_PLAYING_NOW_VERTICAL_SPLIT,                // [bool bVerticalSplit (-1 toggles)]
    MCC_COMBINE_PLAYLISTS,                         // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // View (range 22,000 to 23,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_VIEW_SECTION = 22000,
    MCC_TOGGLE_MODE = 22000,                       // [-1: Next Mode (else UI_MODES Mode)]
    MCC_THEATER_VIEW,                              // [SHOW_THEATER_VIEW_MODES Mode]
    MCC_PARTY_MODE,                                // [ignore]
    MCC_SHOW_TREE_ROOT,                            // [int nTreeRootIndex]
    MCC_FIND_MEDIA,                                // [wchar * pstrSearch (note: memory will be deleted by receiver)]
    MCC_BACK,                                      // [int nLevels (0 does 1 level)]
    MCC_FORWARD,                                   // [int nLevels (0 does 1 level)]
    MCC_REFRESH,                                   // [int nFlags (1: no webpage refresh)]
    MCC_SET_LIST_STYLE,                            // [int nListStyle (-1 toggles)]
    MCC_SET_MODE,                                  // [UI_MODES Mode]
    MCC_OBSOLETE_22010,                            // [ignore]
    MCC_OBSOLETE_22011,                            // [ignore]
    MCC_SHOW_RECENTLYIMPORTED,                     // [ignore]
    MCC_SHOW_TOPHITS,                              // [ignore]
    MCC_SHOW_RECENTLYPLAYED,                       // [ignore]
    MCC_SET_MEDIA_MODE,                            // [int nMediaMode]
    MCC_OBSOLETE_22016,                            // [ignore]
    MCC_SET_SERVER_MODE,                           // [bool bServerMode]
    MCC_SET_MODE_FOR_EXTERNAL_PROGRAM_LAUNCH,      // [int nType (0: starting external app, 1: ending external app)]
    MCC_SET_MODE_FOR_SECOND_INSTANCE_LAUNCH,       // [UI_MODES Mode]
    MCC_HOME,                                      // [ignore]
    MCC_ROLLUP_VIEW_HEADER,                        // [bool bRollup (-1: toggle)]
    MCC_FOCUS_SEARCH_CONTROL,                      // [ignore]
    MCC_SET_ACTIVE_VIEW_KEY,                       // [int nViewKey (-1: toggle, -2: toggle backwards, -3: new view)]
    MCC_CLOSE_VIEW_KEY,                            // [int nViewKey (-1: current view)]
    MCC_VIEW_ZOOM_SET,                             // [int nZoomPercentage]
    MCC_VIEW_ZOOM_INCREMENT,                       // [int nZoomDeltaPercentage]
    MCC_FIND_MEDIA_WITH_WIZARD,                    // [ignore]
    MCC_SET_USER,                                  // [int nUserId]
    MCC_SHOW_TREE,                                 // [bool bShowTree (-1 toggles)]
    MCC_SET_TOOLTIPS,                              // [bool bTooltips (-1 toggles)]
    MCC_AUDIO_ONLY_MODE,                           // [bool bAudioOnlyMode (-1 toggles)]
    MCC_SHOW_SHARED_PLAYLISTS,                     // [ignore]
    MCC_SET_ZONE_VISIBLE,                          // [ignore]
    MCC_SHOW_PLAYING_FILE,                         // [int nFlags (1: no force; 2: only do Playing Now)]
    MCC_F11,                                       // [ignore]
    MCC_SHOW_PLAYLIST,                             // [int nPlaylistID]
    MCC_SPLIT_VIEW_TOGGLE,                         // [ignore]
    MCC_SHOW_PLAYERBAR,                            // [ignore]
    MCC_SET_PLAYERBAR_ALTERNATE_TEXT,              // [bool bValue (-1 toggles)]
    MCC_LOCK_TAB,                                  // [bool bValue (-1 toggles) (or in 2 and all tabs are done)]
    MCC_SHOW_SPOTLIGHT,                            // [int nType (0: current file, 1: current selection)
    MCC_CLEAR_SEARCH_CONTROL,                      // [ignore]
    MCC_THEATER_VIEW_PATH,                         // [BSTR bstrPath (deleted by receiver)]
    MCC_ERROR_FREE_MODE,                           // [bool bErrorFree (-1 toggles)]
    MCC_SHOW_VIEW_HEADER_MENU,                     // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // Tools (range 23,000 to 24,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_TOOLS_SECTION = 23000,
    MCC_IMPORT = 23000,                            // [int nFlags (1: bDisableAlreadyRunningWarning, 2: bFirstImportMode)]
    MCC_RIP_CD,                                    // [ignore]
    MCC_BURN,                                      // [ignore]
    MCC_RECORD_AUDIO,                              // [ignore]
    MCC_CONVERT,                                   // [ignore]
    MCC_ANALYZE_AUDIO,                             // [bool bAutoStart]
    MCC_MEDIA_EDITOR,                              // [ignore]
    MCC_CD_LABELER,                                // [ignore]
    MCC_OBSOLETE_23008,                            // [ignore]
    MCC_OBSOLETE_23009,                            // [ignore]
    MCC_SKIN_MANAGER,                              // [ignore]
    MCC_OPTIONS,                                   // [int nPageID]
    MCC_RENAME_CD_FILES,                           // [ignore]
    MCC_OBSOLETE_23013,                            // [ignore]
    MCC_OBSOLETE_23014,                            // [ignore]
    MCC_HANDHELD_UPLOAD,                           // [loword: nDeviceSessionID (0 gets default), hiword: flags (1: sync only; 2: show warnings)]
    MCC_HANDHELD_UPDATE_UPLOAD_WORKER_FINISHED,    // [int nDeviceSessionID]
    MCC_HANDHELD_CLOSE_DEVICE,                     // [int nDeviceSessionID]
    MCC_HANDHELD_SHOW_OPTIONS,                     // [int nDeviceSessionID]
    MCC_HANDHELD_INFO_DUMP,                        // [bool bShowInfo]
    MCC_IMPORT_AUTO_RUN_NOW,                       // [int nFlags (1: bRunSilent, 2: bRunFindFoldersThread)]
    MCC_IMPORT_AUTO_CONFIGURE,                     // [ignore]
    MCC_HANDHELD_EJECT,                            // [int nDeviceSessionID]
    MCC_RECORD_TV,                                 // [ignore]
    MCC_FIND_AND_REPLACE,                          // [ignore]
    MCC_CLEAN_PROPERTIES,                          // [ignore]
    MCC_FILL_TRACK_ORDER,                          // [ignore]
    MCC_MOVE_COPY_FIELDS,                          // [ignore]
    MCC_REMOVE_TAGS,                               // [ignore]
    MCC_UPDATE_TAGS_FROM_DB,                       // [ignore]
    MCC_UPDATE_DB_FROM_TAGS,                       // [ignore]
    MCC_LOOKUP_TRACK_INFO_FROM_INTERNET,           // [ignore]
    MCC_SUBMIT_TRACK_INFO_TO_INTERNET,             // [ignore]
    MCC_OBSOLETE_23033,                            // [ignore]
    MCC_FILL_PROPERTIES_FROM_FILENAME,             // [ignore]
    MCC_RENAME_FILES_FROM_PROPERTIES,              // [ignore]
    MCC_COVER_ART_ADD_FROM_FILE,                   // [ignore]
    MCC_COVER_ART_QUICK_ADD_FROM_FILE,             // [ignore]
    MCC_COVER_ART_GET_FROM_INTERNET,               // [ignore]
    MCC_COVER_ART_SUBMIT_TO_INTERNET,              // [ignore]
    MCC_COVER_ART_GET_FROM_SCANNER,                // [ignore]
    MCC_COVER_ART_SELECT_SCANNER,                  // [ignore]
    MCC_COVER_ART_GET_FROM_CLIPBOARD,              // [ignore]
    MCC_COVER_ART_COPY_TO_CLIPBOARD,               // [ignore]
    MCC_COVER_ART_REMOVE,                          // [ignore]
    MCC_COVER_ART_PLAY,                            // [ignore]
    MCC_COVER_ART_SAVE_TO_EXTERNAL_FILE,           // [ignore]
    MCC_COVER_ART_REBUILD_THUMBNAIL,               // [ignore]
    MCC_RINGTONE,                                  // [ignore]
    MCC_AUDIO_CALIBRATION,                         // [ignore]
    MCC_MARK_PLAYED,                               // [ignore]
    MCC_MARK_NOT_PLAYED,                           // [ignore]
    MCC_LINK_TRACKS,                               // [ignore]
    MCC_BREAK_TRACK_LINKS,                         // [ignore]
    MCC_AB_COMPARISON,                             // [ignore]
    MCC_COVER_ART_EDIT,                            // [ignore]
    MCC_BUILD_MISSING_THUMBNAILS,                  // [ignore]
    MCC_UPLOAD_TO_CLOUD,                           // [ignore]
    MCC_LOOKUP_LYRICS,                             // [ignore]
    MCC_COVER_ART_GET_ARTIST_IMAGES_FROM_LAST_FM,  // [ignore]
    MCC_COVER_ART_GET_COMPOSER_IMAGES_FROM_GOOGLE, // [ignore]
    MCC_LOOKUP_DATE,                               // [ignore]
    MCC_LOCK_PLAYLIST,                             // [int nPlaylistID]
    MCC_UNLOCK_PLAYLIST,                           // [int nPlaylistID]
    MCC_GET_SHARING_URL,                           // [ignore]
    MCC_UNSHARE,                                   // [ignore]
    MCC_MAKEMKV_RIP,                               // [ignore]
    MCC_BACKUP_DISC,                               // [ignore]
    MCC_ADJUST_DATETIME,                           // [ignore]
    MCC_VIDEO_ANALYSIS,                            // [ignore]
    MCC_VIDEO_ANALYSIS_BLACK_BARS,                 // [ignore]
    MCC_SEARCH_DJ,                                 // [ignore]
    MCC_CONVERTUTF8FILENAMES,                       // [ignore]
    MCC_COVER_ART_ACTION_WINDOW,                   // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // Help (range 24,000 to 25,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_HELP_SECTION = 24000,
    MCC_HELP_CONTENTS = 24000,                     // [ignore]
    MCC_HELP_HOWTO_IMPORT_FILES,                   // [ignore]
    MCC_HELP_HOWTO_PLAY_FILES,                     // [ignore]
    MCC_HELP_HOWTO_RIP,                            // [ignore]
    MCC_HELP_HOWTO_BURN,                           // [ignore]
    MCC_HELP_HOWTO_ORGANIZE_FILES,                 // [ignore]
    MCC_HELP_HOWTO_VIEW_SCHEMES,                   // [ignore]
    MCC_HELP_HOWTO_MANAGE_PLAYLISTS,               // [ignore]
    MCC_HELP_HOWTO_EDIT_PROPERTIES,                // [ignore]
    MCC_HELP_HOWTO_FIND,                           // [ignore]
    MCC_HELP_HOWTO_CONFIGURE,                      // [ignore]
    MCC_CHECK_FOR_UPDATES,                         // [ignore]
    MCC_BUY,                                       // [ignore]
    MCC_INSTALL_LICENSE,                           // [ignore]
    MCC_REGISTRATION_INFO,                         // [ignore]
    MCC_PLUS_FEATURES,                             // [ignore]
    MCC_INTERACT,                                  // [ignore]
    MCC_SYSTEM_INFO,                               // [ignore]
    MCC_ABOUT,                                     // [ignore]
    MCC_CONFIGURE_DEBUG_LOGGING,                   // [ignore]
    MCC_WIKI,                                      // [ignore]
    MCC_TEST,                                      // [ignore]
    MCC_SHOW_EULA,                                 // [ignore]
    MCC_BENCHMARK,                                 // [ignore]
    MCC_UPGRADE_TO_MASTER_LICENSE,                 // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // Tree (range 25,000 to 26,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_TREE_SECTION = 25000,
    MCC_ADD_VIEW_SCHEME = 25000,                   // [ignore]
    MCC_EDIT_VIEW_SCHEME,                          // [ignore]
    MCC_OBSOLETE_25002,                            // [ignore]
    MCC_OBSOLETE_25003,                            // [ignore]
    MCC_OBSOLETE_25004,                            // [ignore]
    MCC_OBSOLETE_25005,                            // [ignore]
    MCC_OBSOLETE_25006,                            // [ignore]
    MCC_OBSOLETE_25007,                            // [ignore]
    MCC_TREE_ADD_DIRECTORY,                        // [ignore]
    MCC_TREE_IMPORT,                               // [ignore]
    MCC_TREE_ADD_CD_FOLDER,                        // [ignore]
    MCC_UPDATE_FROM_CD_DATABASE,                   // [ignore]
    MCC_SUBMIT_TO_CD_DATABASE,                     // [ignore]
    MCC_TREE_RIP,                                  // [ignore]
    MCC_CLEAR_PLAYING_NOW,                         // [0: all files; 1: leave playing file]
    MCC_COPY_LISTENING_TO,                         // [bool bPaste]
    MCC_TREE_SET_EXPANDED,                         // [0: collapsed; 1: expanded; -1: save / restore]
    MCC_RESET_VIEW_SCHEMES,                        // [ignore]
    MCC_TREE_ERASE_CD_DVD,                         // [ignore]
    MCC_UPDATE_FROM_CDPLAYER_INI,                  // [ignore]
    MCC_TREE_EJECT,                                // [ignore]
    MCC_TREE_ADD_VIRTUAL_DEVICE,                   // [ignore]
    MCC_TREE_RENAME_PLAYLIST,                      // [int nPlaylistID]
    MCC_TWITTER_LISTENING_TO,                      // [ignore]
    MCC_SCROBBLE_LISTENING_TO,                     // [ignore]
    MCC_TREE_OPEN_DIRECTORY_IN_FILE_MANAGER,       // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // List (range 26,000 to 27,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_LIST_SECTION = 26000,
    MCC_LIST_UPDATE_ORDER = 26000,                 // [ignore]
    MCC_LIST_SHUFFLE_ORDER,                        // [ignore]
    MCC_LIST_IMPORT,                               // [ignore]
    MCC_LIST_REMOVE_ORDER,                         // [ignore]
    MCC_LOCATE_FILE,                               // [int nLocation (-1: on disk (internal); -2: on disk (external); 0-n: library field index)]
    MCC_LIST_OBSOLETE_26005,                       // [ignore]
    MCC_LIST_INCREMENT_SELECTION,                  // [int nDelta]
    MCC_LIST_REMOVE_DUPLICATES,                    // [ignore]
    MCC_LIST_AUTO_SIZE_COLUMN,                     // [int nColumn, zero-based column index (-1: all)]
    MCC_LIST_CUSTOMIZE_VIEW,                       // [ignore]
    MCC_LIST_COPY_DISK_FILES,                      // [ignore]
    MCC_LIST_SET_RIP_CHECK,                        // [0: uncheck, 1: check, -1: toggle]
    MCC_LIST_DOWNLOAD,                             // [ignore]
    MCC_LIST_GET_LIST_POINTER,                     // [ignore]
    MCC_LOCATE_STACK,                              // [ignore]
    MCC_SET_AS_STACK_TOP,                          // [ignore]
    MCC_EXPAND_STACK,                              // [ignore]
    MCC_COLLAPSE_STACK,                            // [ignore]
    MCC_AUTOSTACK,                                 // [0: by name, 1: artist, album, name, 2: Artist, Album, Track # and Name]
    MCC_CHECK_STACKS,                              // [ignore]
    MCC_STACK,                                     // [int nZeroBasedSelection]
    MCC_UNSTACK,                                   // [ignore]
    MCC_ADD_TO_STACK,                              // [ignore]
    MCC_PANE_RESET_SELECTION,                      // [int nPaneIndex (-1 resets all)]
    MCC_LIST_REMOVE_ALL,                           // [ignore]
    MCC_LIST_LOCK,                                 // [bool bLock (-1 toggles)]
    MCC_PANE_SET_EXPANDED,                         // [loword: nPaneIndex, hiword: 0: collapsed; 1: expanded]
    MCC_STACK_CREATE_PARTICLE,                     // [ignore]
    MCC_STACK_AUTO_CREATE_TITLE_PARTICLES,         // [ignore]
    MCC_LIST_MOVE_UP,                              // [ignore]
    MCC_LIST_MOVE_DOWN,                            // [ignore]
    MCC_LIST_SEND_TO_PLAYING_NOW,                  // [SENDTO_PLAYING_NOW_TYPES Type]
    MCC_LIST_SELECT_RANDOM,                        // [ignore]
    MCC_LIST_TOGGLE_FILES,                         // [ignore]
    MCC_STACK_AUTO_CREATE_CHAPTER_PARTICLES,       // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // System (range 27,000 to 28,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_SYSTEM_SECTION = 27000,
    MCC_KEYSTROKE = 27000,                         // [int nKeyCode]
    MCC_SHUTDOWN,                                  // [int nMode (0: shutdown; 1: sleep; 2: hibernate; 3: restart) (based on CSystemShutdown::EShutdownModes)]

    ///////////////////////////////////////////////////////////////////////////////
    // Playback engine (range 28,000 to 29,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_PLAYBACK_ENGINE_SECTION = 28000,
    MCC_PLAYBACK_ENGINE_ZOOM_IN = 28000,           // [ignore]
    MCC_PLAYBACK_ENGINE_ZOOM_OUT,                  // [ignore]
    MCC_PLAYBACK_ENGINE_UP,                        // [ignore]
    MCC_PLAYBACK_ENGINE_DOWN,                      // [ignore]
    MCC_PLAYBACK_ENGINE_LEFT,                      // [ignore]
    MCC_PLAYBACK_ENGINE_RIGHT,                     // [ignore]
    MCC_PLAYBACK_ENGINE_ENTER,                     // [ignore]
    MCC_PLAYBACK_ENGINE_FIRST,                     // [ignore]
    MCC_PLAYBACK_ENGINE_LAST,                      // [ignore]
    MCC_PLAYBACK_ENGINE_NEXT,                      // [ignore]
    MCC_PLAYBACK_ENGINE_PREVIOUS,                  // [ignore]
    MCC_PLAYBACK_ENGINE_PAUSE,                     // [bool bPause (-1 toggles)]
    MCC_IMAGE_PAN_AND_ZOOM,                        // [bool bPanAndZoom (-1 toggles)]
    MCC_IMAGE_TOGGLE_EFFECT,                       // [int nDelta]
    MCC_IMAGE_RAPID_ZOOM,                          // [int nRapidZoom]
    MCC_OBSOLETE_28015,                            // [ignore]
    MCC_DVD_SHOW_MENU,                             // [ignore]
    MCC_TV_RECORD,                                 // [ignore]
    MCC_TV_SNAPSHOT,                               // [ignore]
    MCC_TV_CHANGE_STANDARD,                        // [ignore]
    MCC_PLAYBACK_ENGINE_OSD_VIDEO_PROC_AMP,        // [int nIndex (0 for brightness, 1 for contrast, etc. -1 cycles)]
    MCC_PLAYBACK_ENGINE_SET_CUR_VIDEO_PROC_AMP,    // [int nStep (... -2, -1, 1, 2, etc. 0 is invalid and will default to 1)]
    MCC_PLAYBACK_ENGINE_SET_ASPECT_RATIO,          // [int nIndex of available ratios (-1 toggles)]
    MCC_PLAYBACK_ENGINE_SCROLL_UP,                 // [ignore]
    MCC_PLAYBACK_ENGINE_SCROLL_DOWN,               // [ignore]
    MCC_PLAYBACK_ENGINE_SCROLL_LEFT,               // [ignore]
    MCC_PLAYBACK_ENGINE_SCROLL_RIGHT,              // [ignore]
    MCC_TV_SET_SAVE_TIME_SHIFTING,                 // [int nSaveMode (0 - 6, -1 cycles by incrementing, -2 cycles by decrementing)]
    MCC_PLAYBACK_ENGINE_ZOOM_TO_PRESET,            // [int 0 to fit window, 1 for 100%, 2 for 200%]
    MCC_TV_SCAN_PROGRAMMING_EVENTS,                // [ignored]
    MCC_TV_CHANGE_CHANNEL_KEY,                     // [int nKey]
    MCC_TV_PLAY_CHANNEL_POSITION,                  // [int Playlist position]
    MCC_PLAYBACK_ENGINE_SET_SUBTITLES,             // [int nIndex (-1 toggles forward, -2 toggles backwards)]
    MCC_PLAYBACK_ENGINE_SET_AUDIO_STREAM,          // [int nIndex (-1 toggles forward, -2 toggles backwards)]
    MCC_PLAYBACK_ENGINE_SET_VIDEO_STREAM,          // [int nIndex (-1 toggles forward, -2 toggles backwards)]
    MCC_PLAYBACK_ENGINE_VIDEO_SCREEN_GRAB,         // [0: use as thumbnail; 1: save as external file]
    MCC_PLAYBACK_ENGINE_VIDEO_LIPSYNC,             // [int nShiftMS, positive or negative increment, zero to reset to 0.  Value is saved in file tag]
    MCC_PLAYBACK_ENGINE_SET_SUBTITLE_TIMING,       // [int nChangeMS]
    MCC_PLAYBACK_ENGINE_VIDEO_ZOOM,                // [int nZoomAmount (100 is 1.0)]
    MCC_IMAGE_SET_FOCUS,                           // [ignore]
    MCC_STEP_FORWARD_FRAMES,                       // [int nNumOfFrames (0 is special and default when no parameter is passed - to the next Keyframe)]
    MCC_STEP_BACK_FRAMES,                          // [int nNumOfFrames (0 is special and default when no parameter is passed - to the previous Keyframe)]
    MCC_PLAYBACK_ENGINE_TOGGLE_COMSKIP,            // [ignore]
    MCC_PLAYBACK_ENGINE_HIDE_OSD,                  // [ignore]
    MCC_PLAYBACK_ENGINE_VIDEO_LIPSYNC_ZONE,        // [int nShiftMS, positive or negative increment, zero to reset to 0.  Value is saved in zone setting]
    MCC_BLURAY_SHOW_POPUP_MENU,                    // [ignore]
    MCC_PLAYBACK_ENGINE_SETTINGS_CHANGED,          // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // Flavor specific (range 29,000 to 30,000) defined as offsets within the flavor
    ///////////////////////////////////////////////////////////////////////////////
    MCC_FLAVOR_SPECIFIC_SECTION = 29000,

    ///////////////////////////////////////////////////////////////////////////////
    // Other (range 30,000 to 31,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_OTHER_SECTION = 30000,
    MCC_RELOAD_MC_VIEW = 30000,                    // [ignore]
    MCC_CUSTOMIZE_TOOLBAR,                         // [ignore]
    MCC_PLAY_TV,                                   // [int nChannelNumber, user assigned channel number]
    MCC_UPDATE_WEBPAGES,                           // [ignore]
    MCC_SHOW_RUNNING_MC,                           // [bool bToggleVisibility]
    MCC_SHOW_MENU,                                 // [int nMenuID]
    MCC_TUNE_TV,                                   // [ignore]
    MCC_PLAY_PLAYLIST,                             // [int nPlaylistID]
    MCC_SENDTO_TOOL,                               // [0: labeler; 1: media editor; 2: default editor; 3: ftp upload; 4: email; 5 Menalto Gallery; 6 Web Gallery]
    MCC_SHOW_VIEW_INFO,                            // [new CMCViewInfo * (for internal use only)]
    MCC_OBSOLETE_30010,                            // [ignore]
    MCC_DEVICE_CHANGED,                            // [new DEVICE_CHANGE_INFO * (for internal use only)]
    MCC_CONFIGURE_THEATER_VIEW,                    // [ignore]
    MCC_SET_STATUSTEXT,                            // [BSTR bstrText (deleted by receiver)]
    MCC_UPDATE_UI_AFTER_ACTIVE_WINDOW_CHANGE,      // [ignore]
    MCC_REENUM_PORTABLE_DEVICES,                   // [bool bDeviceConnected]
    MCC_PLAY_ADVANCED,                             // [PLAY_COMMAND * pCommand (deleted by receiver)]
    MCC_UPDATE_STATUS_BAR,                         // [ignore]
    MCC_REQUEST_PODCAST_UPDATE,                    // [ignore]
    MCC_REQUEST_PODCAST_PURGE,                     // [ignore]
    MCC_OBSOLETE_30020,                            // [ignore]
    MCC_SHOW_INVALID_CD_VOLUME_WARNING,            // [TCHAR cDriveLetter]
    MCC_PLAY_TV_CHANNEL_FOR_CLIENT,                // [the MFKEY key of the TV channel to be played]
    MCC_STOP_SERVING_TV_FILE,                      // [CTVPlayer *: pointer to TVPlayer object serving the file]
    MCC_SHOW_DEVICE_PRESENTATION_WEBPAGE,          // [int nDeviceSessionID]
    MCC_SET_OSD_ENABLED,                           // [bool bEnabled (-1 toggles)]
    MCC_TOGGLE_THEATER_VIEW_GRIDS_CHANNEL_NAME,    // [ignore]
    MCC_PASTE_PLAYING_NOW_IMAGE_FROM_CLIPBOARD,    // [int nZoneID]
    MCC_SHOW_SERVER_PRESENTATION_WEBPAGE,          // [int nDeviceSessionID]
    MCC_PLAY_TRAILER,                              // [ignore]
    MCC_MOVE_TAB,                                  // [int nDelta]
    MCC_CHECK_LOADED,                              // [ignore]
    MCC_RELAUNCH_PROGRAM,                          // [ignore]
    MCC_SHOW_PLAYINGNOW_POPUP,                     // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // Image tools (range 31,000 to 32,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_IMAGE_SECTION = 31000,
    MCC_IMAGE_SET_DESKTOP_BACK = 31000,            // [ignore]
    MCC_IMAGE_ROTATE_LEFT,                         // [ignore]
    MCC_IMAGE_ROTATE_RIGHT,                        // [ignore]
    MCC_IMAGE_ROTATE_UPSIDEDOWN,                   // [ignore]
    MCC_IMAGE_RESIZE,                              // [ignore]
    MCC_IMAGE_EDIT,                                // [int nFileKey]
    MCC_IMAGE_DELETE,                              // [int nFileKey]
    MCC_IMAGE_PREVIEW_SHOW,                        // [ignore]
    MCC_IMAGE_PREVIEW_HIDE,                        // [ignore]
    MCC_IMAGE_LOCATE_ON_MAP,                       // [ignore]
    MCC_IMAGE_FACE_TAG,                            // [int nFileKey]
    MCC_IMAGE_REVERSE_GEOCODE,                     // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // Query (range 32,000 to 33,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_QUERY_SECTION = 32000,
    MCC_QUERY_UI_MODE = 32000,                     // [bool bInternalMode] / returns UI_MODES enumeration
    MCC_QUERY_WEBPAGE_VIEW,

    ///////////////////////////////////////////////////////////////////////////////
    // Commands (used internally -- get routed standard way)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_COMMANDS_SECTION = 33000,
    MCC_GET_SELECTED_FILES = 33000,                // [loword: GET_SELECTION_MODES Mode, hiword: short nFlags (1: for playback, 2: disable when in a grouping view)]
    MCC_PRINTVIEW,                                 // [ignore]
    MCC_OUTPUT,                                    // [int nPlaylistID (-1 for active view)]
    MCC_SETFOCUS,                                  // [ignore]
    MCC_SELECT_FILES,                              // [CMediaArray *]
    MCC_DOUBLE_CLICK,                              // [ignore]
    MCC_PLAY_OR_SHOW,                              // [bool bAppend]
    MCC_SHOW_CURRENT_FILE,                         // [int nFlags (1: force, 2: select)]
    MCC_BUY_SELECTED_TRACKS,                       // [int nPurchaseFlags]
    MCC_BUY_ALL_TRACKS,                            // [int nPurchaseFlags]
    MCC_BUY_ALBUM,                                 // [int nPurchaseFlags]
    MCC_UPDATE_AFTER_PLUGIN_INSTALLED,             // [ignore]
    MCC_UPDATE_AFTER_SKIN_INSTALLED,               // [bool bMiniView]

    ///////////////////////////////////////////////////////////////////////////////
    // Notifications (used internally -- go to all view windows)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_NOTIFICATIONS_SECTION = 34000,
    MCC_NOTIFY_UI_CHANGED = 34000,                 // [int nFlags]
    MCC_NOTIFY_VIEW_CHANGED,                       // [ignore]
    MCC_NOTIFY_BEFORE_ACTIVE_VIEW_CHANGED,         // [ignore]
    MCC_NOTIFY_ACTIVE_VIEW_CHANGED,                // [ignore]
    MCC_NOTIFY_PLAYER_INFO_CHANGED,                // [PLAYER_INFO_CHANGES nChange]
    MCC_NOTIFY_TOOLTIPS_CHANGED,                   // [bool bEnabled]
    MCC_NOTIFY_OPTIONS_CHANGED,                    // [ignore]
    MCC_UPDATE,                                    // [int nFlags]
    MCC_NOTIFY_FOCUS_CHANGED,                      // [ignore]
    MCC_SAVE_PROPERTIES,                           // [ignore]
    MCC_NOTIFY_UI_MODE_CHANGED,                    // [UI_MODES NewMode]
    MCC_NOTIFY_SELECTION_CHANGED,                  // [int nViewKey]
    MCC_NOTIFY_FILE_CHANGED,                       // [int nFileKey (-1: invalidates all files)]
    MCC_NOTIFY_FILE_STATUS_CHANGED,                // [int nFileKey (-1: invalidates all files)]
    MCC_NOTIFY_FILE_ENSURE_VISIBLE,                // [int nFileKey]
    MCC_NOTIFY_GET_TAB_HWNDS,                      // [ignore]
    MCC_NOTIFY_BURNER_QUEUE_CHANGED,               // [int nFlags (1: folder change)]
    MCC_NOTIFY_BURNER_PROGRESS_CHANGED,            // [int nPercentage]
    MCC_NOTIFY_BURNER_STATUS_CHANGED,              // [ignore]
    MCC_NOTIFY_BURNER_STARTED,                     // [ignore]
    MCC_NOTIFY_BURNER_FINISHED_INTERNAL,           // [ignore]
    MCC_NOTIFY_BURNER_FINISHED,                    // [ignore]
    MCC_NOTIFY_BURNER_FAILED_INTERNAL,             // [ignore]
    MCC_NOTIFY_BURNER_FAILED,                      // [ignore]
    MCC_NOTIFY_BURNER_CLOSE_UI,                    // [ignore]
    MCC_NOTIFY_BURNER_PREPARE_FOR_NEXT_COPY,       // [LPCTSTR pStatus]
    MCC_NOTIFY_RIP_STARTED,                        // [ignore]
    MCC_NOTIFY_RIP_FINISHED,                       // [ignore]
    MCC_NOTIFY_RIP_FAILED,                         // [LPCTSTR pError]
    MCC_NOTIFY_RIP_PROGRESS_CHANGED,               // [ignore]
    MCC_NOTIFY_RIP_QUEUE_CHANGED,                  // [ignore]
    MCC_NOTIFY_DVD_RIP_STARTED,                    // [ignore]
    MCC_NOTIFY_DVD_RIP_FINISHED,                   // [ignore]
    MCC_NOTIFY_DVD_RIP_FAILED,                     // [int nErrorCode]
    MCC_NOTIFY_DVD_RIP_PROGRESS_CHANGED,           // [int nPercent]
    MCC_NOTIFY_DOWNLOAD_FINISHED,                  // [int nFileKey (-1: unknown)]
    MCC_NOTIFY_DOWNLOAD_FAILED,                    // [int nFileKey (-1: unknown)]
    MCC_NOTIFY_DOWNLOAD_STATUS_CHANGED,            // [ignore]
    MCC_NOTIFY_STATUS_CHECKER_COMPLETE,            // [ignore]
    MCC_NOTIFY_CURRENT_ZONE_CHANGED,               // [ignore]
    MCC_NOTIFY_DISPLAY_OWNER_CHANGED,              // [JRWnd * pwndOwner]
    MCC_NOTIFY_AFTER_FIRST_UPDATE_LAYOUT_WINDOW,   // [ignore]
    MCC_NOTIFY_AFTER_FIRST_UPDATE_APPLY_VIEW_STATE,// [ignore]
    MCC_NOTIFY_PROCESS_TIME_REMAINING,             // [int nSecondsRemaining]
    MCC_NOTIFY_UI_UPDATE_ENABLE_DISABLE_STATES,    // [ignore]
    MCC_OBSOLETE_34045,                            // [ignore]
    MCC_UPDATE_WINDOW_LAYOUT,                      // [ignore]
    MCC_NOTIFY_SAVE_UI_BEFORE_SHUTDOWN,            // [ignore]
    MCC_OBSOLETE_34048,                            // [ignore]
    MCC_NOTIFY_PLAYLIST_FILES_CHANGED,             // [int nPlaylistID]
    MCC_NOTIFY_PLAYLIST_INFO_CHANGED,              // [int nPlaylistID]
    MCC_NOTIFY_PLAYLIST_ADDED_INTERNAL,            // [int nPlaylistID]
    MCC_NOTIFY_PLAYLIST_ADDED_BY_USER,             // [int nPlaylistID]
    MCC_NOTIFY_PLAYLIST_REMOVED,                   // [int nPlaylistID]
    MCC_NOTIFY_PLAYLIST_COLLECTION_CHANGED,        // [ignore]
    MCC_NOTIFY_PLAYLIST_PROPERTIES_CHANGED,        // [int nPlaylistID]
    MCC_NOTIFY_HANDHELD_UPLOAD_STARTED,            // [int nDeviceSessionID (0 gets default)]
    MCC_NOTIFY_HANDHELD_AFTER_DEVICE_CHANGED,      // [ignore]
    MCC_NOTIFY_HANDHELD_QUEUE_CHANGED,             // [ignore]
    MCC_NOTIFY_HANDHELD_INFO_COMPLETE,             // [ignore]
    MCC_NOTIFY_HANDHELD_AFTER_UPLOAD_FINISHED,     // [ignore]
    MCC_NOTIFY_COMPACT_MEMORY,                     // [ignore]
    MCC_NOTIFY_SEARCH_CHANGED,                     // [ignore]
    MCC_NOTIFY_SEARCH_CONTEXT_CHANGED,             // [ignore]
    MCC_NOTIFY_UPDATE_SHOPPING_CART,               // [JRStoreBase * pStore]
    MCC_NOTIFY_UPDATE_NAVIGATION_TRAIL,            // [ignore]
    MCC_NOTIFY_IMPORT_STARTED,                     // [bool bSilent]
    MCC_NOTIFY_IMPORT_FINISHED,                    // [bool bSilent]
    MCC_NOTIFY_ROTATED_IMAGES,                     // [MFKEY nKey]
    MCC_NOTIFY_LOGIN_STATE_CHANGE,                 // [bool bLoggedIn]
    MCC_NOTIFY_MYGAL_PROGRESS,                     // [ignore]
    MCC_NOTIFY_MYGAL_DONE,                         // [ignore]
    MCC_NOTIFY_PODCAST_CHANGED,                    // [ignore]
    MCC_NOTIFY_PODCAST_SETTINGS_CHANGED,           // [ignore]
    MCC_NOTIFY_CONVERT_PROGRESS,                   // [ignore]
    MCC_NOTIFY_CONVERT_UPDATE,                     // [ignore]
    MCC_NOTIFY_BREADCRUMBS_CHANGED,                // [ignore]
    MCC_OBSOLETE_34078,                            // [ignore]
    MCC_NOTIFY_INSTALLED_PLUGINS_CHANGED,          // [ignore]
    MCC_NOTIFY_SUGGESTED_MUSIC_CHANGED,            // [ignore]
    MCC_NOTIFY_VIEW_SETTINGS_CHANGED,              // [int nFlags]
    MCC_NOTIFY_BEFORE_CONFIGURE_VIEW_SETTINGS,     // [ignore]
    MCC_NOTIFY_TV_RECORDING_CHANGED,               // [ignore]
    MCC_NOTIFY_TV_PROGRAMMING_GUIDE_CHANGED,       // [ignore]
    MCC_NOTIFY_TV_CHANNELS_CHANGED,                // [ignore]
    MCC_NOTIFY_TV_RECORDING_STARTED,               // [ignore]
    MCC_NOTIFY_TV_RECORDING_FINISHED,              // [ignore]
    MCC_NOTIFY_IMPORT_FILES_ADDED,                 // [ignore]
    MCC_NOTIFY_PLAYBACK_OPTIONS_CHANGED,           // [ignore]
    MCC_NOTIFY_BEFORE_LAYOUT_USER_INTERFACE,       // [ignore]
    MCC_NOTIFY_AFTER_LAYOUT_USER_INTERFACE,        // [ignore]
    MCC_NOTIFY_ZONE_ADDED_OR_REMOVED,              // [int nZoneID (PLAYER_ZONE_ID_UNDEFINED means multiple changes)]
    MCC_NOTIFY_ZONE_LINKED_OR_UNLINKED,            // [ignore]
    MCC_NOTIFY_LIBRARY_LOCATIONS_CHANGED,          // [ignore]
    MCC_NOTIFY_DSP_SETTINGS_CHANGED_IN_CODE,       // [int nZoneID]
    MCC_NOTIFY_OPTICAL_DISC_CHANGED,               // [ignore]
    MCC_NOTIFY_STORE_DOWNLOAD_STATUS_CHANGED,      // [int nStoreNumber]
    MCC_NOTIFY_CURRENT_PLAYLIST_NEXT_ITEM_TO_PLAY_CHANGED, // [int nZone]
    MCC_NOTIFY_CONTENT_UPLOAD_PROGRESS,            // [ignore]
    MCC_NOTIFY_CONTENT_UPLOAD_UPDATE,              // [int nEnsureVisibleColumn]  if nEnsureVisibleColumn > 0, call EnsureVisible() on the list control
    MCC_NOTIFY_TV_PROPERTIES_CHANGED,              // [ignore]
    MCC_NOTIFY_STATUSBAR_TEXT_CHANGED,             // [ignore]
    MCC_NOTIFY_TV_EPG_STATUS_UPDATE,               // [ignore]    

    ///////////////////////////////////////////////////////////////////////////////
    // Store  (range 35,000 to 36,000)
    ///////////////////////////////////////////////////////////////////////////////
    MCC_STORE_SECTION = 35000,
    MCC_STORE_DOWNLOAD = 35000,                    // [bool bAllowPurchaseType]
    MCC_STORE_PURCHASE,                            // [MFKEY nKey]
    MCC_STORE_SEARCH_AMAZON,                       // [MFKEY nKey]
    MCC_STORE_SEARCH_AMAZON_MP3_STORE,             // [MFKEY nKey]
    MCC_OBSOLETE_35004,                            //
    MCC_STORE_CHANGE_USER,                         // [ignore]
    MCC_STORE_CHECK_FOR_DOWNLOADS,                 // [ignore]

    ///////////////////////////////////////////////////////////////////////////////
    // Last
    ///////////////////////////////////////////////////////////////////////////////
    MCC_LAST = 40000
};

///////////////////////////////////////////////////////////////////////////////
// Customization specific (used internally)
///////////////////////////////////////////////////////////////////////////////
#define MCC_CUSTOMIZATION_OFFSET 100000


/***********************************************************************************
How to issue Media Core commands

a) Post a WM_MC_COMMAND based message to the MC frame

Example (C++ source code):
HWND hwndMC = FindWindow(_T("MJFrame"), NULL);
PostMessage(hwndMC, WM_MC_COMMAND, MCC_PLAY_PAUSE, 0);

b) Fire the same command through the launcher (i.e. 'MC13.exe') in the system directory

Example (command-line program):
MC13.exe /MCC 10000, 0
***********************************************************************************/

// the WM_APP based message (WM_APP = 32768, so WM_MC_COMMAND = 33768)
#define WM_MC_COMMAND (WM_APP + 1000)

// extended MC_COMMAND message that takes a structure with extra information (internal use only)
#define WM_MC_COMMAND_EX (WM_APP + 1001)


/***********************************************************************************
System wide notification mechanism

Notification messages (commands in the 34xxx block) get broadcast system wide so
that external applications can monitor and react to events in the program.  This
is done by using the registered Windows message UM_MC_COMMAND and the function
BroadcastSystemMessage(...).

It is _critical_ that you do not try to use any parameters that are pointers like
a LPCTSTR of an error since the pointer would be garbage from an external process
(and also because the notification is posted to you so the memory could be gone).
***********************************************************************************/

#define UM_MC_COMMAND_NAME _T("JRiver Media Core Command (v1.0)")
#define UM_MC_COMMAND RegisterWindowMessage(UM_MC_COMMAND_NAME)

/***************************************************************************************************************
Helper macros
***************************************************************************************************************/

#define IS_MCC_COMMAND_IN_RANGE(INDEX, FIRST, LAST) (((abs((int)INDEX)) >= FIRST && ((abs((int)INDEX)) < LAST)) || (((abs((int)INDEX)) >= FIRST + MCC_CUSTOMIZATION_OFFSET) && ((abs((int)INDEX)) < LAST + MCC_CUSTOMIZATION_OFFSET)))
#define IS_MCC_COMMAND_IN_SECTION(INDEX, FIRST) IS_MCC_COMMAND_IN_RANGE(INDEX, FIRST, FIRST + 1000)
#define IS_VALID_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_RANGE(INDEX, MCC_FIRST, MCC_LAST)
#define IS_PLAYBACK_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_PLAYBACK_SECTION)
#define IS_FILE_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_FILE_SECTION)
#define IS_EDIT_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_EDIT_SECTION)
#define IS_VIEW_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_VIEW_SECTION)
#define IS_TOOL_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_TOOLS_SECTION)
#define IS_HELP_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_HELP_SECTION)
#define IS_TREE_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_TREE_SECTION)
#define IS_LIST_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_LIST_SECTION)
#define IS_SYSTEM_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_SYSTEM_SECTION)
#define IS_PLAYBACK_ENGINE_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_PLAYBACK_ENGINE_SECTION)
#define IS_IMAGE_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_IMAGE_SECTION)
#define IS_QUERY_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_QUERY_SECTION)
#define IS_INTERNAL_COMMAND_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_COMMANDS_SECTION)
#define IS_NOTIFY_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_SECTION(INDEX, MCC_NOTIFICATIONS_SECTION)
#define IS_CUSTOMIZATION_MCC_COMMAND(INDEX) IS_MCC_COMMAND_IN_RANGE(INDEX, MCC_FIRST + MCC_CUSTOMIZATION_OFFSET, MCC_LAST + MCC_CUSTOMIZATION_OFFSET)

#define MAKE_MCC_PLAYBACK_PARAM(PARAM, ZONE_INDEX) (((ZONE_INDEX) == -1) ? ((PARAM) & 0x00FFFFFF) : ((((ZONE_INDEX) + 1) << 24) & 0xFF000000) | ((PARAM) & 0x00FFFFFF))
#define GET_MCC_PLAYBACK_PARAM(PARAM) (((PARAM) & 0x400000) ? ((PARAM) & 0xFFFFFF) - 0x1000000 : ((PARAM) & 0xFFFFFF))
#define GET_MCC_PLAYBACK_ZONE_INDEX(PARAM) (((PARAM) & 0x80000000) ? -1 : (((PARAM) >> 24) - 1))


/***************************************************************************************************************
Defines
***************************************************************************************************************/

// return value for unhandled MCC commands
#define MCC_UNHANDLED 0

// flags for command enable, disable, and check
enum MCC_UPDATEUI_FLAGS
{
    MCC_UPDATEUI_ENABLE = 1,
    MCC_UPDATEUI_DISABLE = 2,
    MCC_UPDATEUI_PRESSED = 4,
};

// update flags
#define MCC_UPDATE_FLAG_THUMBNAILS                           (1 << 0)
#define MCC_UPDATE_FLAG_FILE_PROPERTIES                      (1 << 1)
#define MCC_UPDATE_FLAG_FILE_ADDED_OR_REMOVED                (1 << 2)
#define MCC_UPDATE_FLAG_TREE_STRUCTURE                       (1 << 3)
#define MCC_UPDATE_FLAG_REFILL_LIST                          (1 << 4)
#define MCC_UPDATE_FLAG_ITEM_DELETED                         (1 << 5)
#define MCC_UPDATE_FLAG_NO_PRESERVE_VIEW_STATE               (1 << 6)
#define MCC_UPDATE_FLAG_WEB_VIEW                             (1 << 7)

// update all
#define MCC_UPDATE_FLAG_ALL                                  (0x7FFFFFFF & ~(MCC_UPDATE_FLAG_NO_PRESERVE_VIEW_STATE))

// settings changed flags
#define MCC_SETTING_CHANGED_FLAG_UNKNOWN                    (1 << 0)
#define MCC_SETTING_CHANGED_FLAG_COLUMNS                    (1 << 1)
#define MCC_SETTING_CHANGED_FLAG_SORTING                    (1 << 2)
#define MCC_SETTING_CHANGED_FLAG_VIEW_SCHEME                (1 << 3)
#define MCC_SETTING_CHANGED_FLAG_GROUPING                   (1 << 5)
#define MCC_SETTING_CHANGED_FLAG_LIBRARY_VIEW_SETTINGS      (1 << 6)

// UI changed flags
#define MCC_UI_CHANGED_FLAG_FONT                            (1 << 0)
#define MCC_UI_CHANGED_FLAG_SKIN                            (1 << 1)
#define MCC_UI_CHANGED_FLAG_SCALE                           (1 << 2)
#define MCC_UI_CHANGED_FLAG_LANGUAGE                        (1 << 3)
#define MCC_UI_CHANGED_FLAG_USER_CHANGE_SKIN                (1 << 4)

#define MCC_FILENAME_USE_MCSTRING_IN_TEMP_FOLDER            1001

// UI modes
enum UI_MODES
{
    // unknown
    UI_MODE_UNKNOWN = -2000,

    // internal modes
    UI_MODE_INTERNAL_NO_UI = -1000,
    UI_MODE_INTERNAL_MIN = UI_MODE_INTERNAL_NO_UI,
    UI_MODE_INTERNAL_STANDARD,
    UI_MODE_INTERNAL_MINI_FREEFORM,
    UI_MODE_INTERNAL_MINI_SLIM,
    UI_MODE_INTERNAL_DISPLAY_WINDOWED,
    UI_MODE_INTERNAL_DISPLAY_FULLSCREEN,
    UI_MODE_INTERNAL_THEATER,
    UI_MODE_INTERNAL_COVER,
    UI_MODE_INTERNAL_WINDOWLESS,  // Android
    UI_MODE_INTERNAL_MAX = UI_MODE_INTERNAL_WINDOWLESS,

    // toggles, shortcuts, etc.
    UI_MODE_SHORTCUT_TEMPORARY_DISPLAY_WINDOWED = -8,
    UI_MODE_SHORTCUT_TOGGLE_DISPLAY_AND_LAST_USER_INPUT_MODE = -7,
    UI_MODE_SHORTCUT_TOGGLE_DISPLAY_EXCLUDE_THEATER_VIEW = -6,
    UI_MODE_SHORTCUT_TOGGLE_DISPLAY = -5,
    UI_MODE_SHORTCUT_LAST_SHUTDOWN = -4,
    UI_MODE_SHORTCUT_CURRENT = -3,
    UI_MODE_SHORTCUT_CLOSE_DISPLAY = -2,
    UI_MODE_SHORTCUT_NEXT = -1,

    // modes presented to the user
    UI_MODE_STANDARD = 0,
    UI_MODE_MINI,
    UI_MODE_DISPLAY,
    UI_MODE_THEATER,
    UI_MODE_COVER,
    // UI_MODE_WINDOWLESS,
    UI_MODE_COUNT,
};

// player changes
#define PLAYER_INFO_CHANGE_ALL                               0xFFFF
#define PLAYER_INFO_CHANGE_PLAYER_STATE                      (1 << 0)
#define PLAYER_INFO_CHANGE_VOLUME                            (1 << 1)
#define PLAYER_INFO_CHANGE_FILE_INFO                         (1 << 2)
#define PLAYER_INFO_CHANGE_PLAYLIST                          (1 << 3)
#define PLAYER_INFO_CHANGE_DSP                               (1 << 4)
#define PLAYER_INFO_CHANGE_IMAGE                             (1 << 5)
#define PLAYER_INFO_CHANGE_PLAYING_FILE                      (1 << 6)
#define PLAYER_INFO_CHANGE_OUTPUT_INFO                       (1 << 7)

// player status codes
enum PLAYER_STATUS_CODES
{
    PLAYER_STATUS_CODE_BUFFERING,
    PLAYER_STATUS_CODE_LOCATING,
    PLAYER_STATUS_CODE_CONNECTING,
    PLAYER_STATUS_CODE_DOWNLOADING_CODEC,
    PLAYER_STATUS_CODE_ACQUIRING_LICENSE,
    PLAYER_STATUS_CODE_INDIVIDUALIZE_STARTING,
    PLAYER_STATUS_CODE_INDIVIDUALIZE_CONNECTING,
    PLAYER_STATUS_CODE_INDIVIDUALIZE_REQUESTING,
    PLAYER_STATUS_CODE_INDIVIDUALIZE_RECEIVING,
    PLAYER_STATUS_CODE_INDIVIDUALIZE_COMPLETED,
};

// theater view modes
enum SHOW_THEATER_VIEW_MODES
{
    SHOW_THEATER_VIEW_MODE_INVALID = -1,
    SHOW_THEATER_VIEW_MODE_TOGGLE_THEATER_VIEW,
    SHOW_THEATER_VIEW_MODE_HOME,
    SHOW_THEATER_VIEW_MODE_PLAYING_NOW,
    SHOW_THEATER_VIEW_MODE_AUDIO,
    SHOW_THEATER_VIEW_MODE_IMAGES,
    SHOW_THEATER_VIEW_MODE_VIDEOS,
    SHOW_THEATER_VIEW_MODE_PLAYLISTS,
    SHOW_THEATER_VIEW_MODE_CD_DVD,
    SHOW_THEATER_VIEW_MODE_TELEVISION,
    SHOW_THEATER_VIEW_MODE_TELEVISION_GUIDE,
    SHOW_THEATER_VIEW_MODE_TELEVISION_RECORDINGS,
    SHOW_THEATER_VIEW_MODE_LAST_VIEWED,
    SHOW_THEATER_VIEW_MODE_NETFLIX,
    SHOW_THEATER_VIEW_MODE_SPOTLIGHT,
};

// get selection modes
enum GET_SELECTION_MODES
{
    GET_SELECTION_EXACT,
    GET_SELECTION_ALL_ON_NONE,
    GET_SELECTION_ALL_ON_ONE_OR_NONE,
    GET_SELECTION_ALL,
    GET_SELECTION_EXACT_WITH_POSITION,
    GET_SELECTION_ALL_AFTER_SELECTION,
};

// skip to modes
enum SKIP_TO_MODES
{
    SKIP_TO_UNDEFINED = 0,
    SKIP_TO_PREVIOUS_ALBUM,
    SKIP_TO_NEXT_ALBUM,
    SKIP_TO_PREVIOUS_ARTIST,
    SKIP_TO_NEXT_ARTIST,
};

// send to playing now types (commented out so it doesn't multiply define in Media Center)
/*
enum SENDTO_PLAYING_NOW_TYPES
{
    SENDTO_PLAYING_NOW_TYPE_PLAY_ALL_VISIBLE,
    SENDTO_PLAYING_NOW_TYPE_PLAY,
    SENDTO_PLAYING_NOW_TYPE_PLAY_SHUFFLED,
    SENDTO_PLAYING_NOW_TYPE_PLAY_AUTOMATIC_PLAYLIST,
    SENDTO_PLAYING_NOW_TYPE_PLAY_ALBUM_SHUFFLED,
    SENDTO_PLAYING_NOW_TYPE_PLAY_SHUFFLED_ALBUMS,
    SENDTO_PLAYING_NOW_TYPE_ADD_TO_END,
    SENDTO_PLAYING_NOW_TYPE_ADD_AS_NEXT_TO_PLAY,
    SENDTO_PLAYING_NOW_TYPE_ADD_PLAY_NOW,
    SENDTO_PLAYING_NOW_TYPE_ADD_SHUFFLED,
    SENDTO_PLAYING_NOW_TYPE_ADD_SHUFFLE_ALL,
    SENDTO_PLAYING_NOW_TYPE_ADD_SHUFFLE_REMAINING,
    SENDTO_PLAYING_NOW_TYPE_ADD_TO_BEGINNING,
    SENDTO_PLAYING_NOW_TYPE_ADD_AFTER_CURRENT_ALBUM,
    SENDTO_PLAYING_NOW_TYPE_ADD_AFTER_CURRENT_ARTIST,
    SENDTO_PLAYING_NOW_TYPE_ADD_ALBUM,
    SENDTO_PLAYING_NOW_TYPE_ADD_ARTIST,
    SENDTO_PLAYING_NOW_TYPE_REMOVE,
    SENDTO_PLAYING_NOW_STOP_AFTER_TRACK,
    SENDTO_PLAYING_NOW_TYPE_COUNT,
};
*/

#endif