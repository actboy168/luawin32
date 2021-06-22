local win32 = require "win32"
local apis = win32.apis
local c = win32.constants

apis.MessageBoxA(0, "Hello!", "win32", c.MB_HELP | c.MB_ICONINFORMATION);
