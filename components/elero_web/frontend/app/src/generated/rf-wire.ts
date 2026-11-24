// Generated from asyncapi.yaml.
export const msg_type = {
  "BUTTON": "0x44",
  "BUTTON_GROUP": "0x45",
  "COMMAND": "0x6a",
  "COMMAND_ALT": "0x69",
  "PROGRAM": "0x70",
  "STATUS": "0xca",
  "STATUS_ALT": "0xc9"
} as const
export const command = {
  "CHECK": "0x00",
  "RELEASE": "0x00",
  "STOP": "0x10",
  "UP": "0x20",
  "FULL_UP": "0x21",
  "FULL_DOWN": "0x41",
  "TILT": "0x24",
  "DOWN": "0x40",
  "INTERMEDIATE": "0x44",
  "PROGRAM": "0x80",
  "PROGRAM_GROUP": "0x81",
  "PROGRAM_TARGET": "0x84"
} as const
export const commandKeys = [{"name":"check","byte":"0x00","frames":["0x6a","0x69"]},{"name":"release","byte":"0x00","frames":["0x44"]},{"name":"stop","byte":"0x10"},{"name":"up","byte":"0x20"},{"name":"full_up","byte":"0x21"},{"name":"full_down","byte":"0x41"},{"name":"tilt","byte":"0x24"},{"name":"down","byte":"0x40"},{"name":"intermediate","byte":"0x44"},{"name":"program","byte":"0x80"},{"name":"program_group","byte":"0x81"},{"name":"program_target","byte":"0x84"}] as const
