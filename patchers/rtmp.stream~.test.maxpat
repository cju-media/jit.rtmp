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
        "rect": [ 1247.0, 361.0, 780.0, 620.0 ],
        "boxes": [
            {
                "box": {
                    "id": "obj-7",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 474.0, 238.0, 37.0, 22.0 ],
                    "text": "close"
                }
            },
            {
                "box": {
                    "data": {
                        "clips": [
                            {
                                "absolutepath": "dust.mp4",
                                "filename": "dust.mp4",
                                "filekind": "moviefile",
                                "id": "u013001343",
                                "loop": 1,
                                "content_state": {
                                    "loop": 1
                                }
                            }
                        ]
                    },
                    "drawto": "",
                    "id": "obj-5",
                    "loop": 1,
                    "maxclass": "jit.playlist",
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "jit_gl_texture", "", "dictionary" ],
                    "output_texture": 1,
                    "parameter_enable": 0,
                    "patching_rect": [ 315.0, 315.0, 150.0, 30.0 ],
                    "saved_attribute_attributes": {
                        "candicane2": {
                            "expression": ""
                        },
                        "candicane3": {
                            "expression": ""
                        },
                        "candicane4": {
                            "expression": ""
                        },
                        "candicane5": {
                            "expression": ""
                        },
                        "candicane6": {
                            "expression": ""
                        },
                        "candicane7": {
                            "expression": ""
                        },
                        "candicane8": {
                            "expression": ""
                        }
                    }
                }
            },
            {
                "box": {
                    "id": "obj-6",
                    "maxclass": "toggle",
                    "numinlets": 1,
                    "numoutlets": 1,
                    "outlettype": [ "int" ],
                    "parameter_enable": 0,
                    "patching_rect": [ 516.0, 270.0, 24.0, 24.0 ]
                }
            },
            {
                "box": {
                    "id": "obj-4",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 761.0, 330.0, 86.0, 22.0 ],
                    "text": "s renderbang1"
                }
            },
            {
                "box": {
                    "id": "obj-1",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 3,
                    "outlettype": [ "jit_gl_texture", "bang", "" ],
                    "patching_rect": [ 516.0, 300.0, 509.0, 22.0 ],
                    "text": "jit.world display1 @dim 1920 1080 @size 960 540 @erase_color 0. 0. 0. 1. @output_texture 1"
                }
            },
            {
                "box": {
                    "id": "obj-2",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 428.0, 246.0, 35.0, 22.0 ],
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
                    "patching_rect": [ 30.0, 20.0, 600.0, 22.0 ],
                    "text": "rtmp.stream~ test patch - set the url below, then click start"
                }
            },
            {
                "box": {
                    "id": "msg_url",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 60.0, 346.0, 22.0 ],
                    "text": "url rtmp://a.rtmp.youtube.com/live2/ram4-8z13-r8z5-1me0-9567"
                }
            },
            {
                "box": {
                    "id": "msg_res",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 95.0, 200.0, 22.0 ],
                    "text": "width 1280, height 720, fps 30"
                }
            },
            {
                "box": {
                    "id": "msg_start",
                    "maxclass": "message",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 70.0, 119.0, 60.0, 22.0 ],
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
                    "patching_rect": [ 110.0, 135.0, 60.0, 22.0 ],
                    "text": "stop"
                }
            },
            {
                "box": {
                    "id": "obj_rtmp",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "" ],
                    "patching_rect": [ 30.0, 300.0, 197.0, 22.0 ],
                    "text": "rtmp.stream~ @gl_context display1"
                }
            },
            {
                "box": {
                    "id": "obj_print",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 350.0, 150.0, 22.0 ],
                    "text": "print rtmp.stream~"
                }
            },
            {
                "box": {
                    "id": "obj_adc",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "signal", "signal" ],
                    "patching_rect": [ 50.0, 190.0, 80.0, 22.0 ],
                    "text": "adc~ 1 2"
                }
            },
            {
                "box": {
                    "id": "comment_audio",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 165.0, 361.0, 20.0 ],
                    "text": "audio in (use a duplex/aggregate device if capturing system audio)"
                }
            },
            {
                "box": {
                    "id": "obj_grab",
                    "maxclass": "newobj",
                    "numinlets": 1,
                    "numoutlets": 2,
                    "outlettype": [ "jit_matrix", "" ],
                    "patching_rect": [ 260.0, 190.0, 120.0, 22.0 ],
                    "text": "jit.grab 1280 720"
                }
            },
            {
                "box": {
                    "id": "comment_video",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 423.0, 165.0, 281.0, 20.0 ],
                    "text": "video in (swap jit.grab for jit.desktop / jit.matrix etc.)"
                }
            },
            {
                "box": {
                    "id": "obj_qmetro",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "bang" ],
                    "patching_rect": [ 260.0, 225.0, 80.0, 22.0 ],
                    "text": "qmetro 33"
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
                    "patching_rect": [ 397.0, 194.0, 24.0, 24.0 ]
                }
            },
            {
                "box": {
                    "id": "comment_toggle",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 295.0, 262.0, 200.0, 20.0 ],
                    "text": "<- enable video capture loop"
                }
            },
            {
                "box": {
                    "id": "obj_ezdac",
                    "maxclass": "ezdac~",
                    "numinlets": 2,
                    "numoutlets": 0,
                    "patching_rect": [ 140.0, 190.0, 45.0, 45.0 ]
                }
            },
            {
                "box": {
                    "id": "comment_dsp",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 140.0, 238.0, 220.0, 20.0 ],
                    "text": "<- click to turn DSP on! (required)"
                }
            },
            {
                "box": {
                    "id": "obj_mcpack",
                    "maxclass": "newobj",
                    "numinlets": 2,
                    "numoutlets": 1,
                    "outlettype": [ "multichannelsignal" ],
                    "patching_rect": [ 50.0, 262.0, 80.0, 22.0 ],
                    "text": "mc.pack~ 2"
                }
            },
            {
                "box": {
                    "id": "comment_mc",
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 135.0, 264.0, 300.0, 20.0 ],
                    "text": "packs adc~'s 2 mono channels into 1 MC cable for the demo"
                }
            },
            {
                "box": {
                    "id": "comment_note",
                    "linecount": 3,
                    "maxclass": "comment",
                    "numinlets": 1,
                    "numoutlets": 0,
                    "patching_rect": [ 30.0, 400.0, 620.0, 47.0 ],
                    "text": "Notes: left inlet is messages only (jit_matrix / jit_gl_texture / start / stop / url / attrs). Right inlet is a single MC audio inlet - any channel count, downmixed to stereo for the stream. The object accepts char ARGB or RGB jit_matrix data (jit.grab's default output is fine as-is). The status outlet reports \"status connecting\", \"status live\", \"status stopped\", and \"error ...\" messages - watch the Max console."
                }
            }
        ],
        "lines": [
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "msg_res", 0 ]
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
                    "destination": [ "obj_rtmp", 0 ],
                    "source": [ "msg_url", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-4", 0 ],
                    "source": [ "obj-1", 1 ]
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
                    "destination": [ "obj-1", 0 ],
                    "source": [ "obj-5", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj-1", 0 ],
                    "source": [ "obj-6", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_grab", 0 ],
                    "source": [ "obj-7", 0 ]
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
                    "destination": [ "obj_print", 0 ],
                    "source": [ "obj_rtmp", 0 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_qmetro", 0 ],
                    "source": [ "obj_toggle", 0 ]
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
                    "destination": [ "obj_mcpack", 1 ],
                    "source": [ "obj_adc", 1 ]
                }
            },
            {
                "patchline": {
                    "destination": [ "obj_rtmp", 1 ],
                    "source": [ "obj_mcpack", 0 ]
                }
            }
        ],
        "autosave": 0
    }
}