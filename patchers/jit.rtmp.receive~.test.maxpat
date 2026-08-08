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
        "rect": [ 1247.0, 361.0, 780.0, 610.0 ],
        "boxes": [
            {
                "box": {
                    "fontsize": 14.0,
                    "id": "comment_title",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 20.0, 700.0, 22.0 ],
                    "text": "jit.rtmp.receive~ test patch - set the url below, click start, then publish to that URL from an encoder"
                }
            },
            {
                "box": {
                    "id": "msg_url",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 60.0, 260.0, 22.0 ],
                    "text": "url rtmp://0.0.0.0:1935/live/STREAM-KEY"
                }
            },
            {
                "box": {
                    "id": "msg_start",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 95.0, 60.0, 22.0 ],
                    "text": "start"
                }
            },
            {
                "box": {
                    "id": "msg_stop",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 100.0, 95.0, 60.0, 22.0 ],
                    "text": "stop"
                }
            },
            {
                "box": {
                    "id": "obj_rtmp",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "jit_matrix", "multichannelsignal", "" ],
                    "patching_rect": [ 30.0, 140.0, 130.0, 22.0 ],
                    "text": "jit.rtmp.receive~"
                }
            },
            {
                "box": {
                    "id": "comment_ports",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 170.0, 140.0, 330.0, 20.0 ],
                    "text": "outlets: jit_matrix, audio (MC), status"
                }
            },
            {
                "box": {
                    "id": "obj_pwindow",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 190.0, 480.0, 270.0 ],
                    "text": "jit.pwindow"
                }
            },
            {
                "box": {
                    "id": "obj_mcdac",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 540.0, 190.0, 100.0, 22.0 ],
                    "text": "mc.dac~"
                }
            },
            {
                "box": {
                    "id": "comment_dsp",
                    "maxclass": "comment",
                    "linecount": 3,
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 540.0, 220.0, 220.0, 48.0 ],
                    "text": "click the toggle below to turn DSP on (audio playback). mc.dac~ takes the outlet's MC cable directly and spreads its channels across your hardware outputs."
                }
            },
            {
                "box": {
                    "id": "obj_dsp_toggle",
                    "maxclass": "ezdac~",
                    "numinlets": 2,
                    "numoutlets": 0,
                    "patching_rect": [ 690.0, 190.0, 45.0, 45.0 ]
                }
            },
            {
                "box": {
                    "id": "obj_print",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 480.0, 180.0, 22.0 ],
                    "text": "print jit.rtmp.receive~"
                }
            },
            {
                "box": {
                    "id": "comment_note",
                    "linecount": 5,
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 510.0, 719.0, 78.0 ],
                    "text": "This object *is* the RTMP server - it listens on the configured @url and accepts an incoming publish (e.g. from OBS, ffmpeg, or another Max patch running jit.rtmp.send~). Video comes out the left (jit_matrix) outlet; audio is decoded to a single multichannel (MC) signal outlet - stereo by default, or set @channels for more (e.g. 6 for 5.1); the rightmost outlet reports \"status listening on ...\", \"status client connected\", \"status live\", \"status client disconnected\", \"status stopped\", and \"error ...\" messages - watch the Max console. Turn DSP on to hear audio; video updates as frames decode regardless of DSP state. After a publisher disconnects, the server keeps listening for the next one until you send 'stop'."
                }
            }
        ],
        "lines": [
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "msg_url", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "msg_start", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "msg_stop", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_pwindow", 0 ],
                    "source": [ "obj_rtmp", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_mcdac", 0 ],
                    "source": [ "obj_rtmp", 1 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_print", 0 ],
                    "source": [ "obj_rtmp", 2 ]
                }
            }
        ],
        "autosave": 0
    }
}
