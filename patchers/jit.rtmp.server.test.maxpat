{
    "patcher": {
        "fileversion": 1,
        "appversion": {
            "major": 9,
            "minor": 1,
            "revision": 2,
            "architecture": "x64",
            "modernui": 1
        },
        "classnamespace": "box",
        "rect": [ 1247.0, 361.0, 780.0, 520.0 ],
        "boxes": [
            {
                "box": {
                    "id": "obj-5",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 551.0, 310.0, 69.0, 22.0 ],
                    "text": "prepend url"
                }
            },
            {
                "box": {
                    "id": "obj-4",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 525.0, 274.0, 329.0, 22.0 ],
                    "text": "rtmp://a.rtmp.youtube.com/live2/ram4-8z13-r8z5-1me0-9567"
                }
            },
            {
                "box": {
                    "id": "obj-2",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 358.0, 284.0, 35.0, 22.0 ],
                    "text": "open"
                }
            },
            {
                "box": {
                    "fontsize": 14.0,
                    "id": "comment_title",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 20.0, 720.0, 22.0 ],
                    "text": "jit.rtmp.server test patch - one patch replacing OBS + a hand-run RTMP server"
                }
            },
            {
                "box": {
                    "id": "msg_server_start",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 60.0, 60.0, 22.0 ],
                    "text": "start"
                }
            },
            {
                "box": {
                    "id": "msg_server_stop",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 100.0, 60.0, 60.0, 22.0 ],
                    "text": "stop"
                }
            },
            {
                "box": {
                    "id": "obj_server",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 95.0, 100.0, 22.0 ],
                    "text": "jit.rtmp.server"
                }
            },
            {
                "box": {
                    "id": "obj_server_print",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 135.0, 170.0, 22.0 ],
                    "text": "print jit.rtmp.server"
                }
            },
            {
                "box": {
                    "id": "comment_step1",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 200.0, 60.0, 392.0, 20.0 ],
                    "text": "1. start the server first (watch the console for \"status mediamtx running\")"
                }
            },
            {
                "box": {
                    "id": "msg_url",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 195.0, 260.0, 22.0 ],
                    "text": "url rtmp://127.0.0.1:1935/live/STREAM-KEY"
                }
            },
            {
                "box": {
                    "id": "msg_send_start",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 45.0, 230.0, 60.0, 22.0 ],
                    "text": "start"
                }
            },
            {
                "box": {
                    "id": "msg_send_stop",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 125.0, 230.0, 60.0, 22.0 ],
                    "text": "stop"
                }
            },
            {
                "box": {
                    "id": "comment_step2",
                    "linecount": 2,
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 300.0, 195.0, 350.0, 33.0 ],
                    "text": "2. then point jit.rtmp.send~ at the server's own localhost URL and start it - this patch is now both the encoder AND the server"
                }
            },
            {
                "box": {
                    "id": "obj_rtmp",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 265.0, 81.0, 22.0 ],
                    "text": "jit.rtmp.send~"
                }
            },
            {
                "box": {
                    "id": "obj_send_print",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 305.0, 150.0, 22.0 ],
                    "text": "print jit.rtmp.send~"
                }
            },
            {
                "box": {
                    "id": "obj_adc",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "signal", "signal" ],
                    "patching_rect": [ 50.0, 340.0, 80.0, 22.0 ],
                    "text": "adc~ 1 2"
                }
            },
            {
                "box": {
                    "id": "obj_mcpack",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "multichannelsignal" ],
                    "patching_rect": [ 50.0, 375.0, 80.0, 22.0 ],
                    "text": "mc.pack~ 2"
                }
            },
            {
                "box": {
                    "id": "obj_grab",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "jit_matrix", "" ],
                    "patching_rect": [ 260.0, 340.0, 120.0, 22.0 ],
                    "text": "jit.grab 1280 720"
                }
            },
            {
                "box": {
                    "id": "obj_toggle",
                    "maxclass": "toggle",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "int" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 400.0, 340.0, 24.0, 24.0 ]
                }
            },
            {
                "box": {
                    "id": "obj_qmetro",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "bang" ],
                    "patching_rect": [ 260.0, 375.0, 80.0, 22.0 ],
                    "text": "qmetro 33"
                }
            },
            {
                "box": {
                    "id": "comment_toggle",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 435.0, 344.0, 200.0, 20.0 ],
                    "text": "<- enable video capture loop"
                }
            },
            {
                "box": {
                    "id": "comment_note",
                    "linecount": 6,
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 420.0, 1303.0, 87.0 ],
                    "text": "This patch alone is now the whole pipeline: it captures video/audio, encodes it, and serves it - no OBS, no separately-run server process. On another machine on the same network, replace 127.0.0.1 above with this machine's actual LAN IP and:\n  - VLC: File -> Open Network Stream -> rtmp://<this-machine-ip>:1935/live/STREAM-KEY\n  - a headless player (e.g. a Raspberry Pi running VLC via a launch script): same URL\n  - another Max patch: jit.rtmp.receive~ with @url rtmp://<this-machine-ip>:1935/live/STREAM-KEY\nAny number of players can connect to jit.rtmp.server at once - that's the difference from pointing jit.rtmp.send~ straight at a single jit.rtmp.receive~ (see that patch's own test file), which only supports one listener.\nTurn Max's DSP on (required by jit.rtmp.send~) before sending its 'start'."
                }
            }
        ],
        "lines": [
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "msg_send_start", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "msg_send_stop", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_server", 0 ],
                    "source": [ "msg_server_start", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_server", 0 ],
                    "source": [ "msg_server_stop", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "msg_url", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_grab", 0 ],
                    "source": [ "obj-2", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-5", 0 ],
                    "source": [ "obj-4", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "obj-5", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_mcpack", 1 ],
                    "source": [ "obj_adc", 1 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_mcpack", 0 ],
                    "source": [ "obj_adc", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "obj_grab", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 1 ],
                    "source": [ "obj_mcpack", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_grab", 0 ],
                    "source": [ "obj_qmetro", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_send_print", 0 ],
                    "source": [ "obj_rtmp", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_server_print", 0 ],
                    "source": [ "obj_server", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_qmetro", 0 ],
                    "source": [ "obj_toggle", 0 ]
                }
            }
        ],
        "autosave": 0
    }
}